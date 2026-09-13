#!/usr/bin/env python3
# Termux / 任意 Python3：GPS NTP 设备 vs 阿里云 NTP 比对（仅标准库）
#
# Termux:
#   pkg install python
#   curl -L -o ntp_cmp_termux.py \
#     https://ghproxy.net/https://raw.githubusercontent.com/luweixuancl/Acode_test/cursor/ntp-phase-a-metadata-c502/tools/ntp_cmp_termux.py
#   python ntp_cmp_termux.py
#   python ntp_cmp_termux.py --quick          # 约 1 分钟冒烟
#   python ntp_cmp_termux.py --gps 10.81.127.143 --minutes 10
#
# 手机请连与设备同一 2.4 GHz WiFi。CSV 写在当前目录。

from __future__ import annotations

import argparse
import csv
import json
import socket
import statistics
import struct
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

EPOCH_DELTA = 2208988800
DEFAULT_GPS = "10.81.127.143"
DEFAULT_REF = "ntp.aliyun.com"


def ntp_query(host: str, timeout: float = 2.5) -> dict:
    ip = socket.gethostbyname(host)
    pkt = bytearray(48)
    pkt[0] = 0x1B  # client, VN=3
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    t1 = time.time()
    try:
        sock.sendto(pkt, (ip, 123))
        data, _ = sock.recvfrom(48)
    finally:
        sock.close()
    t4 = time.time()
    if len(data) < 48:
        raise OSError("short NTP packet")

    def ts(off: int) -> float:
        sec, frac = struct.unpack("!II", data[off : off + 8])
        return (sec - EPOCH_DELTA) + frac / (2**32)

    t2, t3 = ts(32), ts(40)
    offset = ((t2 - t1) + (t3 - t4)) / 2
    delay = (t4 - t1) - (t3 - t2)
    refid = data[12:16]
    try:
        refid_s = refid.decode("ascii")
        if not all(32 <= ord(c) < 127 for c in refid_s):
            refid_s = refid.hex()
    except Exception:
        refid_s = refid.hex()
    root_disp = struct.unpack("!I", data[8:12])[0] / 65536.0
    return {
        "ok": 1,
        "ip": ip,
        "offset_ms": offset * 1000.0,
        "delay_ms": delay * 1000.0,
        "li": data[0] >> 6,
        "stratum": data[1],
        "refid": refid_s,
        "root_disp_ms": root_disp * 1000.0,
        "t1": t1,
        "t2": t2,
        "t3": t3,
        "t4": t4,
        "error": "",
    }


def query_safe(host: str, timeout: float) -> dict:
    try:
        return ntp_query(host, timeout=timeout)
    except Exception as e:
        return {
            "ok": 0,
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


def http_status(ip: str, timeout: float = 2.5) -> dict:
    url = "http://%s/status" % ip
    try:
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", "replace")
        doc = json.loads(raw)
        clock = doc.get("clock") or {}
        ntp = doc.get("ntp") or {}
        gps = doc.get("gps") or {}
        return {
            "clock": clock.get("state", ""),
            "residual_ms": clock.get("residualMs", ""),
            "pps_fresh": gps.get("ppsFresh", ""),
            "stratum1": ntp.get("stratum1Ready", ""),
            "ntp_stratum": ntp.get("stratum", ""),
            "error": "",
        }
    except Exception as e:
        return {
            "clock": "",
            "residual_ms": "",
            "pps_fresh": "",
            "stratum1": "",
            "ntp_stratum": "",
            "error": str(e),
        }


def fnum(x):
    try:
        if x is None or x == "":
            return None
        return float(x)
    except (TypeError, ValueError):
        return None


def summarize(name: str, rows: list) -> list:
    ok = [r for r in rows if str(r.get("ok")) == "1"]
    offs = [fnum(r["offset_ms"]) for r in ok]
    offs = [v for v in offs if v is not None]
    dlys = [fnum(r["delay_ms"]) for r in ok]
    dlys = [v for v in dlys if v is not None]
    print("=== %s ===" % name)
    print("  成功 %d/%d (%.1f%%)" % (len(ok), len(rows), 100.0 * len(ok) / max(1, len(rows))))
    if offs:
        step = [abs(offs[i] - offs[i - 1]) for i in range(1, len(offs))]
        print(
            "  offset median=%.2f mean=%.2f stdev=%.2f range=[%.2f, %.2f] ms"
            % (
                statistics.median(offs),
                statistics.mean(offs),
                statistics.pstdev(offs) if len(offs) > 1 else 0.0,
                min(offs),
                max(offs),
            )
        )
        if step:
            print(
                "  相邻|Δ| median=%.2f mean=%.2f max=%.2f ms"
                % (statistics.median(step), statistics.mean(step), max(step))
            )
        print("  delay median=%.2f max=%.2f ms" % (statistics.median(dlys), max(dlys)))
        print(
            "  stratum=%s LI=%s refid=%s"
            % (
                sorted({str(r["stratum"]) for r in ok}),
                sorted({str(r["li"]) for r in ok}),
                sorted({str(r["refid"]) for r in ok}),
            )
        )
    return offs


def main() -> int:
    ap = argparse.ArgumentParser(description="GPS NTP vs 阿里云比对（Termux）")
    ap.add_argument("--gps", default=DEFAULT_GPS, help="设备 IP（默认 %s）" % DEFAULT_GPS)
    ap.add_argument("--ref", default=DEFAULT_REF, help="参考 NTP（默认 %s）" % DEFAULT_REF)
    ap.add_argument("--minutes", type=float, default=10.0, help="时长分钟（默认 10）")
    ap.add_argument("--interval", type=float, default=10.0, help="轮间隔秒（默认 10）")
    ap.add_argument("--timeout", type=float, default=2.5, help="单次超时秒")
    ap.add_argument("--quick", action="store_true", help="冒烟：1 分钟 / 间隔 5 秒")
    ap.add_argument("--no-status", action="store_true", help="不拉设备 /status")
    ap.add_argument("-o", "--out", default="", help="CSV 路径（默认当前目录自动命名）")
    args = ap.parse_args()

    duration_s = 60.0 if args.quick else args.minutes * 60.0
    interval_s = 5.0 if args.quick else args.interval

    out = Path(args.out) if args.out else Path(
        "ntp_cmp_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
    )
    out = out.resolve()

    print("GPS  %s" % args.gps)
    print("REF  %s" % args.ref)
    print("时长 %.0fs  间隔 %.1fs  超时 %.1fs" % (duration_s, interval_s, args.timeout))
    print("CSV  %s" % out)

    print("\n-- 连通冒烟 --")
    g0 = query_safe(args.gps, args.timeout)
    a0 = query_safe(args.ref, args.timeout)
    if g0["ok"]:
        print(
            "GPS  OK  stratum=%s refid=%s offset=%+.2f ms delay=%.2f ms"
            % (g0["stratum"], g0["refid"], g0["offset_ms"], g0["delay_ms"])
        )
    else:
        print("GPS  FAIL  %s" % g0["error"])
        print("请确认手机与设备同一 WiFi，且 %s:123 可达。" % args.gps)
    if a0["ok"]:
        print(
            "ALI  OK  stratum=%s refid=%s offset=%+.2f ms delay=%.2f ms"
            % (a0["stratum"], a0["refid"], a0["offset_ms"], a0["delay_ms"])
        )
    else:
        print("ALI  FAIL  %s" % a0["error"])
        print("请确认蜂窝或 WiFi 能访问公网 UDP/123（%s）。" % args.ref)
    if not args.no_status:
        st = http_status(args.gps, args.timeout)
        if st["error"]:
            print("/status FAIL  %s" % st["error"])
        else:
            print(
                "/status OK  clock=%s residual=%s ppsFresh=%s stratum1=%s"
                % (st["clock"], st["residual_ms"], st["pps_fresh"], st["stratum1"])
            )
    if not g0["ok"]:
        return 2

    fields = [
        "idx",
        "local_time",
        "epoch",
        "gps_ok",
        "gps_ip",
        "gps_stratum",
        "gps_li",
        "gps_refid",
        "gps_offset_ms",
        "gps_delay_ms",
        "gps_disp_ms",
        "gps_error",
        "ali_ok",
        "ali_ip",
        "ali_stratum",
        "ali_li",
        "ali_refid",
        "ali_offset_ms",
        "ali_delay_ms",
        "ali_disp_ms",
        "ali_error",
        "diff_gps_minus_ali_ms",
        "clock",
        "residual_ms",
        "pps_fresh",
        "stratum1",
    ]

    gps_rows = []
    ali_rows = []
    start = time.time()
    end = start + duration_s
    idx = 0

    with out.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        while time.time() < end:
            idx += 1
            now = time.time()
            local = datetime.now().astimezone().isoformat(timespec="seconds")
            g = query_safe(args.gps, args.timeout)
            a = query_safe(args.ref, args.timeout)
            st = (
                {"clock": "", "residual_ms": "", "pps_fresh": "", "stratum1": "", "error": ""}
                if args.no_status
                else http_status(args.gps, args.timeout)
            )
            g["server"] = "gps"
            a["server"] = "aliyun"
            gps_rows.append(g)
            ali_rows.append(a)

            go = fnum(g.get("offset_ms"))
            ao = fnum(a.get("offset_ms"))
            diff = "" if go is None or ao is None else (go - ao)

            w.writerow(
                {
                    "idx": idx,
                    "local_time": local,
                    "epoch": "%.6f" % now,
                    "gps_ok": g["ok"],
                    "gps_ip": g.get("ip", ""),
                    "gps_stratum": g.get("stratum", ""),
                    "gps_li": g.get("li", ""),
                    "gps_refid": g.get("refid", ""),
                    "gps_offset_ms": g.get("offset_ms", ""),
                    "gps_delay_ms": g.get("delay_ms", ""),
                    "gps_disp_ms": g.get("root_disp_ms", ""),
                    "gps_error": g.get("error", ""),
                    "ali_ok": a["ok"],
                    "ali_ip": a.get("ip", ""),
                    "ali_stratum": a.get("stratum", ""),
                    "ali_li": a.get("li", ""),
                    "ali_refid": a.get("refid", ""),
                    "ali_offset_ms": a.get("offset_ms", ""),
                    "ali_delay_ms": a.get("delay_ms", ""),
                    "ali_disp_ms": a.get("root_disp_ms", ""),
                    "ali_error": a.get("error", ""),
                    "diff_gps_minus_ali_ms": diff,
                    "clock": st.get("clock", ""),
                    "residual_ms": st.get("residual_ms", ""),
                    "pps_fresh": st.get("pps_fresh", ""),
                    "stratum1": st.get("stratum1", ""),
                }
            )
            f.flush()

            gtxt = (
                "gps %+8.2f ms s%s %s" % (go, g["stratum"], g["refid"])
                if g["ok"]
                else "gps FAIL"
            )
            atxt = (
                "ali %+8.2f ms s%s" % (ao, a["stratum"]) if a["ok"] else "ali FAIL"
            )
            dtxt = "diff=%+.2f" % diff if diff != "" else "diff=n/a"
            clk = st.get("clock") or "-"
            print("[%3d] %s | %s | %s | clk=%s" % (idx, gtxt, atxt, dtxt, clk))

            sleep_for = interval_s - (time.time() - now)
            if sleep_for > 0 and time.time() + sleep_for < end + 0.5:
                time.sleep(sleep_for)

    print("\n======= 汇总 =======")
    print("轮次 %d  文件 %s" % (idx, out))
    summarize("GPS %s" % args.gps, gps_rows)
    summarize("阿里云 %s" % args.ref, ali_rows)

    diffs = []
    for g, a in zip(gps_rows, ali_rows):
        go, ao = fnum(g.get("offset_ms")), fnum(a.get("offset_ms"))
        if go is not None and ao is not None:
            diffs.append(go - ao)
    print("=== 配对 GPS − 阿里云 ===")
    if not diffs:
        print("  无成功配对")
        return 3
    bad = sum(1 for d in diffs if abs(d) > 400)
    med = statistics.median(diffs)
    std = statistics.pstdev(diffs) if len(diffs) > 1 else 0.0
    print("  配对 %d" % len(diffs))
    print(
        "  diff median=%.2f mean=%.2f stdev=%.2f range=[%.2f, %.2f] ms"
        % (med, statistics.mean(diffs), std, min(diffs), max(diffs))
    )
    for thr in (50, 100, 200, 400, 980):
        n = sum(1 for d in diffs if abs(d) > thr)
        print("  |diff|>%dms: %d (%.1f%%)" % (thr, n, 100.0 * n / len(diffs)))
    clocks = [r for r in (http_status(args.gps, args.timeout),) if not args.no_status]
    if clocks and not clocks[0]["error"]:
        print("  结束时 /status clock=%s ppsFresh=%s" % (clocks[0]["clock"], clocks[0]["pps_fresh"]))

    print("\n判读：")
    print("  绝对 offset 含本机钟差，看 stdev / 相邻步长，不看绝对大小。")
    print("  |GPS−阿里云|>400ms 视为异常尖峰（旧测曾出现 1s 回退）。")
    print(
        "  本轮异常 %d/%d (%.1f%%)  median=%+.2f stdev=%.2f"
        % (bad, len(diffs), 100.0 * bad / len(diffs), med, std)
    )
    ok_anom = (100.0 * bad / len(diffs)) < 2
    ok_med = abs(med) < 50
    print(
        "  参考门槛：异常<2%% %s  |median|<50ms %s"
        % ("通过" if ok_anom else "未过", "通过" if ok_med else "未过（可为本机/路径）")
    )
    print("CSV 已保存，可拷出分享。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
