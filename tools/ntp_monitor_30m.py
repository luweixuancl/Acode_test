#!/usr/bin/env python3
"""30-minute GPS vs aliyun NTP monitor (background-friendly)."""
from __future__ import annotations

import csv
import socket
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

GPS_HOST = "10.81.127.143"
ALIYUN_HOST = "ntp.aliyun.com"
DURATION_S = 1800
INTERVAL_S = 5
EPOCH_DELTA = 2208988800


def ntp_query(host: str, timeout: float = 2.0) -> dict:
    pkt = bytearray(48)
    pkt[0] = 0x1B
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    t1 = time.time()
    sock.sendto(pkt, (host, 123))
    data, _ = sock.recvfrom(48)
    t4 = time.time()
    sock.close()

    def ts(off: int) -> float:
        sec, frac = struct.unpack("!II", data[off : off + 8])
        return (sec - EPOCH_DELTA) + frac / (2**32)

    t2, t3 = ts(32), ts(40)
    offset = ((t2 - t1) + (t3 - t4)) / 2
    delay = (t4 - t1) - (t3 - t2)
    li = data[0] >> 6
    stratum = data[1]
    refid = data[12:16]
    try:
        refid_s = refid.decode("ascii")
    except Exception:
        refid_s = refid.hex()
    root_disp = struct.unpack("!I", data[8:12])[0] / 65536.0
    return {
        "ok": 1,
        "offset_ms": offset * 1000,
        "delay_ms": delay * 1000,
        "li": li,
        "stratum": stratum,
        "refid": refid_s,
        "root_disp_ms": root_disp * 1000,
        "t1": t1,
        "t2": t2,
        "t3": t3,
        "t4": t4,
        "ip": socket.gethostbyname(host),
    }


def query_safe(name: str, host: str) -> dict:
    try:
        r = ntp_query(host)
        r["server"] = name
        return r
    except Exception as e:
        return {
            "ok": 0,
            "server": name,
            "ip": host,
            "offset_ms": "",
            "delay_ms": "",
            "li": "",
            "stratum": "",
            "refid": "",
            "root_disp_ms": "",
            "t1": "",
            "t2": "",
            "t3": "",
            "t4": "",
            "error": str(e),
        }


def main() -> int:
    out = Path(__file__).resolve().parent.parent / (
        "ntp_monitor_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
    )
    log = out.with_suffix(".log")
    fields = [
        "idx",
        "utc_time",
        "epoch",
        "server",
        "ip",
        "stratum",
        "li",
        "refid",
        "t1",
        "t2",
        "t3",
        "t4",
        "offset_ms",
        "delay_ms",
        "root_disp_ms",
        "ok",
        "error",
    ]

    start = time.time()
    end = start + DURATION_S
    idx = 0
    gps_ok = ali_ok = 0
    with out.open("w", newline="", encoding="utf-8") as f, log.open(
        "w", encoding="utf-8"
    ) as lf:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        msg = (
            f"start {datetime.now().isoformat(timespec='seconds')} "
            f"gps={GPS_HOST} aliyun={ALIYUN_HOST} duration={DURATION_S}s interval={INTERVAL_S}s\n"
            f"csv={out}\n"
        )
        print(msg, end="")
        lf.write(msg)
        lf.flush()

        while time.time() < end:
            idx += 1
            now = time.time()
            utc = datetime.now().astimezone().isoformat(timespec="seconds")
            for name, host in (("gps", GPS_HOST), ("aliyun", ALIYUN_HOST)):
                r = query_safe(name, host)
                if r.get("ok"):
                    if name == "gps":
                        gps_ok += 1
                    else:
                        ali_ok += 1
                row = {
                    "idx": idx,
                    "utc_time": utc,
                    "epoch": f"{now:.6f}",
                    "server": r.get("server", name),
                    "ip": r.get("ip", host),
                    "stratum": r.get("stratum", ""),
                    "li": r.get("li", ""),
                    "refid": r.get("refid", ""),
                    "t1": r.get("t1", ""),
                    "t2": r.get("t2", ""),
                    "t3": r.get("t3", ""),
                    "t4": r.get("t4", ""),
                    "offset_ms": r.get("offset_ms", ""),
                    "delay_ms": r.get("delay_ms", ""),
                    "root_disp_ms": r.get("root_disp_ms", ""),
                    "ok": r.get("ok", 0),
                    "error": r.get("error", ""),
                }
                w.writerow(row)
            f.flush()

            if idx % 12 == 0:  # ~1 min
                elapsed = time.time() - start
                line = (
                    f"[{elapsed:6.0f}s] rounds={idx} gps_ok={gps_ok} ali_ok={ali_ok}\n"
                )
                print(line, end="")
                lf.write(line)
                lf.flush()

            # Keep roughly INTERVAL_S between round starts.
            sleep_for = INTERVAL_S - (time.time() - now)
            if sleep_for > 0:
                time.sleep(sleep_for)

        summary = (
            f"done {datetime.now().isoformat(timespec='seconds')} "
            f"rounds={idx} gps_ok={gps_ok} ali_ok={ali_ok} csv={out}\n"
        )
        print(summary, end="")
        lf.write(summary)

    # Write path for the parent to discover.
    Path(__file__).resolve().parent.parent.joinpath(
        "ntp_monitor_latest.txt"
    ).write_text(str(out), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
