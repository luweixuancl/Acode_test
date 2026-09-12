#!/usr/bin/env python3
import csv
import statistics
from collections import defaultdict
from pathlib import Path

p = Path(r"D:\编程目录\Acode_test_leds\ntp_monitor_20260911_104143.csv")
rows = list(csv.DictReader(p.open(encoding="utf-8")))


def fnum(x):
    try:
        return float(x)
    except Exception:
        return None


by = defaultdict(list)
for r in rows:
    by[r["server"]].append(r)

print("=== Overview ===")
print(f"file: {p.name}")
print(f"rows: {len(rows)}  rounds: {max(int(r['idx']) for r in rows)}")
print(f"time: {rows[0]['utc_time']} -> {rows[-1]['utc_time']}")

for name in ("gps", "aliyun"):
    rs = by[name]
    ok = [r for r in rs if r.get("ok") == "1"]
    offs = [fnum(r["offset_ms"]) for r in ok if fnum(r["offset_ms"]) is not None]
    dlys = [fnum(r["delay_ms"]) for r in ok if fnum(r["delay_ms"]) is not None]
    print(f"\n=== {name} ===")
    print(f"success {len(ok)}/{len(rs)} ({100 * len(ok) / len(rs):.1f}%)")
    if offs:
        print(
            f"offset median={statistics.median(offs):.1f} "
            f"std={statistics.pstdev(offs):.1f} "
            f"range=[{min(offs):.1f},{max(offs):.1f}]"
        )
        print(f"delay  median={statistics.median(dlys):.1f} max={max(dlys):.1f}")
        strata = set(r["stratum"] for r in ok)
        lis = set(r["li"] for r in ok)
        refs = set(r["refid"] for r in ok)
        print(f"stratum={strata} LI={lis} refid={refs}")

print("\n=== Paired GPS - aliyun ===")
gps_map = {r["idx"]: r for r in by["gps"] if r.get("ok") == "1"}
ali_map = {r["idx"]: r for r in by["aliyun"] if r.get("ok") == "1"}
common = sorted(set(gps_map) & set(ali_map), key=int)
diffs = []
for i in common:
    go = fnum(gps_map[i]["offset_ms"])
    ao = fnum(ali_map[i]["offset_ms"])
    if go is None or ao is None:
        continue
    d = go - ao
    diffs.append(
        (
            int(i),
            d,
            go,
            ao,
            gps_map[i]["utc_time"],
            fnum(gps_map[i]["delay_ms"]),
            fnum(ali_map[i]["delay_ms"]),
        )
    )

ds = [d for _, d, _, _, _, _, _ in diffs]
print(f"paired rounds: {len(diffs)}")
print(
    f"diff median={statistics.median(ds):.1f} mean={statistics.mean(ds):.1f} "
    f"std={statistics.pstdev(ds):.1f}"
)
print(f"diff range=[{min(ds):.1f},{max(ds):.1f}]")
bad = sum(1 for d in ds if abs(d) > 400)
print(f"|diff|>400ms (anomaly): {bad}/{len(ds)} ({100 * bad / len(ds):.1f}%)")
for thr in (50, 100, 200, 400, 980):
    n = sum(1 for d in ds if abs(d) > thr)
    print(f"  |diff|>{thr}ms: {n} ({100 * n / len(ds):.1f}%)")

norm = [d for d in ds if abs(d) <= 400]
if norm:
    print(
        f"normal (|diff|<=400): n={len(norm)} median={statistics.median(norm):.1f} "
        f"std={statistics.pstdev(norm):.1f}"
    )

print("\n=== GPS consecutive jumps ===")
gofs = []
for i in sorted(gps_map, key=int):
    go = fnum(gps_map[i]["offset_ms"])
    if go is not None:
        gofs.append((int(i), go, gps_map[i]["utc_time"]))
jumps = [
    (gofs[k][0], gofs[k - 1][1], gofs[k][1], gofs[k][1] - gofs[k - 1][1], gofs[k][2])
    for k in range(1, len(gofs))
    if abs(gofs[k][1] - gofs[k - 1][1]) > 400
]
print(f"consecutive |delta offset|>400ms: {len(jumps)}")
for j in jumps[:10]:
    print(f"  idx={j[0]} {j[4]} {j[1]:.1f} -> {j[2]:.1f} (d={j[3]:+.1f})")

print("\n=== Extreme paired diffs ===")
diffs_sorted = sorted(diffs, key=lambda x: x[1])
print("most negative:")
for t in diffs_sorted[:5]:
    print(
        f"  idx={t[0]} {t[4]} diff={t[1]:+.1f} gps={t[2]:.1f} ali={t[3]:.1f} gd={t[5]:.1f}"
    )
print("most positive:")
for t in diffs_sorted[-5:]:
    print(
        f"  idx={t[0]} {t[4]} diff={t[1]:+.1f} gps={t[2]:.1f} ali={t[3]:.1f} gd={t[5]:.1f}"
    )

print("\n=== vs acceptance / baseline ===")
print("baseline 2026-09-10: anomaly 51.3%, stale -1s then -3~-4s")
print("target: anomaly<2%, |median|<50ms, std<30ms")
med = statistics.median(ds)
std = statistics.pstdev(ds)
print(
    f"current: anomaly={100 * bad / len(ds):.1f}% |median|={abs(med):.1f} std={std:.1f}"
)
ok_med = abs(med) < 50
ok_std = std < 30
ok_anom = (100 * bad / len(ds)) < 2
print(f"pass median: {ok_med}  pass std: {ok_std}  pass anomaly: {ok_anom}")
