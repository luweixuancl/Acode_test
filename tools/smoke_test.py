#!/usr/bin/env python3
"""Partial automated smoke / regression against a live GNSS NTP device."""
from __future__ import annotations

import argparse
import json
import socket
import struct
import statistics
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Any, Callable, List, Optional, Tuple

EPOCH_DELTA = 2208988800


@dataclass
class CheckResult:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class SuiteResult:
    checks: List[CheckResult] = field(default_factory=list)

    def add(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append(CheckResult(name, ok, detail))
        mark = "PASS" if ok else "FAIL"
        print(f"[{mark}] {name}: {detail}")

    @property
    def failed(self) -> int:
        return sum(1 for c in self.checks if not c.ok)

    @property
    def passed(self) -> int:
        return sum(1 for c in self.checks if c.ok)


def http_json(url: str, timeout: float = 4.0) -> Tuple[int, Any]:
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read()
        code = r.status
    return code, json.loads(body.decode("utf-8", errors="replace"))


def http_raw(url: str, timeout: float = 8.0) -> Tuple[int, bytes]:
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def ntp_query(host: str, timeout: float = 2.0) -> dict:
    pkt = bytearray(48)
    pkt[0] = 0x1B
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    t1 = time.time()
    s.sendto(pkt, (host, 123))
    data, _ = s.recvfrom(48)
    t4 = time.time()
    s.close()

    def ts(off: int) -> float:
        sec, frac = struct.unpack("!II", data[off : off + 8])
        return (sec - EPOCH_DELTA) + frac / (2**32)

    t2, t3 = ts(32), ts(40)
    offset = ((t2 - t1) + (t3 - t4)) / 2
    delay = (t4 - t1) - (t3 - t2)
    refid = data[12:16]
    try:
        refid_s = refid.decode("ascii")
    except Exception:
        refid_s = refid.hex()
    return {
        "li": data[0] >> 6,
        "stratum": data[1],
        "refid": refid_s,
        "offset_ms": offset * 1000,
        "delay_ms": delay * 1000,
        "rtt_ms": (t4 - t1) * 1000,
    }


def run_suite(host: str, ntp_samples: int, interval_s: float) -> SuiteResult:
    s = SuiteResult()
    base = f"http://{host}"

    # --- HTTP /status ---
    try:
        code, st = http_json(f"{base}/status")
        s.add("HTTP /status", code == 200, f"code={code}")
    except Exception as e:
        s.add("HTTP /status", False, str(e))
        return s

    s.add("STA connected", bool(st.get("sta")), f"ssid={st.get('ssid')!r} ip={st.get('ip')}")
    clock = st.get("clock") or {}
    gps = st.get("gps") or {}
    ntp = st.get("ntp") or {}
    s.add("clock LCK/DEG", clock.get("state") in ("LCK", "DEG"), f"state={clock.get('state')}")
    s.add("PPS fresh", bool(gps.get("ppsFresh")), f"ppsCount={gps.get('ppsCount')}")
    s.add("GPS fix", bool(gps.get("fix")), f"sats={gps.get('satellites')}")
    s.add(
        "NTP meta honest",
        bool(ntp.get("synced")) and ntp.get("stratum") == 1 and ntp.get("refId") == "GPSS",
        f"synced={ntp.get('synced')} stratum={ntp.get('stratum')} ref={ntp.get('refId')} li={ntp.get('li')}",
    )
    free_heap = st.get("freeHeap")
    s.add(
        "heap telemetry",
        isinstance(free_heap, int) and free_heap > 20000,
        f"freeHeap={free_heap} minFreeHeap={st.get('minFreeHeap')}",
    )

    # --- NTP samples ---
    offsets: List[float] = []
    metas: List[dict] = []
    for i in range(ntp_samples):
        try:
            r = ntp_query(host)
            metas.append(r)
            offsets.append(r["offset_ms"])
            print(
                f"  ntp[{i+1}/{ntp_samples}] li={r['li']} stratum={r['stratum']} "
                f"ref={r['refid']!r} offset={r['offset_ms']:+.2f}ms delay={r['delay_ms']:.2f}ms"
            )
        except Exception as e:
            s.add(f"NTP sample {i+1}", False, str(e))
            return s
        if i + 1 < ntp_samples:
            time.sleep(interval_s)

    if metas:
        ok_meta = all(m["stratum"] == 1 and m["refid"] == "GPSS" and m["li"] == 0 for m in metas)
        s.add("NTP wire stratum1/GPSS/LI0", ok_meta, f"n={len(metas)}")
        stdev = statistics.pstdev(offsets) if len(offsets) > 1 else 0.0
        # Soft gate: WiFi+PC measurement floor; flag only extreme instability.
        s.add(
            "NTP offset stability",
            stdev < 50.0,
            f"n={len(offsets)} median={statistics.median(offsets):.2f}ms stdev={stdev:.2f}ms "
            f"range=[{min(offsets):.1f},{max(offsets):.1f}]",
        )

    # --- /scan (may return 202 then 200, or busy during connect) ---
    scan_ok = False
    scan_detail = ""
    try:
        nets = -1
        for _ in range(40):
            code, body = http_raw(f"{base}/scan", timeout=10)
            if code == 202:
                time.sleep(0.3)
                continue
            if code == 503 and b"busy" in body:
                scan_detail = "busy (acceptable if connecting)"
                scan_ok = True
                break
            if code == 200:
                arr = json.loads(body.decode("utf-8", errors="replace"))
                nets = len(arr) if isinstance(arr, list) else -1
                scan_ok = nets >= 0
                scan_detail = f"code=200 nets={nets}"
                break
            scan_detail = f"code={code} body={body[:80]!r}"
            break
        else:
            scan_detail = "timeout waiting for scan"
        s.add("HTTP /scan", scan_ok, scan_detail)
    except Exception as e:
        s.add("HTTP /scan", False, str(e))

    # --- /status still alive after scan ---
    try:
        code2, st2 = http_json(f"{base}/status")
        s.add(
            "status after scan",
            code2 == 200 and bool(st2.get("sta")),
            f"sta={st2.get('sta')} clock={(st2.get('clock') or {}).get('state')} heap={st2.get('freeHeap')}",
        )
    except Exception as e:
        s.add("status after scan", False, str(e))

    return s


def main() -> int:
    ap = argparse.ArgumentParser(description="Partial automated smoke test for ESP32 GNSS NTP")
    ap.add_argument("--host", default="192.168.124.6")
    ap.add_argument("--ntp-samples", type=int, default=6)
    ap.add_argument("--interval", type=float, default=2.0, help="seconds between NTP samples")
    args = ap.parse_args()

    print(f"smoke host={args.host} ntp_samples={args.ntp_samples} interval={args.interval}s")
    print("---")
    suite = run_suite(args.host, args.ntp_samples, args.interval)
    print("---")
    print(f"RESULT passed={suite.passed} failed={suite.failed} total={len(suite.checks)}")
    return 1 if suite.failed else 0


if __name__ == "__main__":
    sys.exit(main())
