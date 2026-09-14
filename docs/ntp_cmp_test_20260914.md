# NTP 比对测试记录（2026-09-14，Termux vs 阿里云）

## 摘要

2026-09-14 用手机 Termux 对固件设备做了 **10 分钟、每 10 秒一轮** 的 NTP 比对（`tools/ntp_cmp_termux.py`）。结论：

- 脚本在 **搜星未锁定** 时就开跑：前 **5** 轮 `clock=ACQ`、stratum **16**、refid **`INIT`**、LI=3。此时 offset 约 −4×10¹² ms，是诚实的未同步时间，**不能计入精度**。
- 第 6 轮起进入 **`LCK`**（10:29:19 +08），之后 **55/55** 轮 stratum **1**、refid **`GPSS`**、`ppsFresh=true`、`residualMs=0`。
- 锁定后相对手机，offset 中位 **+24.00 ms**（手机系统钟慢约 24 ms）。含 Wi‑Fi 高 RTT 尖峰时 stdev ≈ **9.2 ms**；**delay < 20 ms** 的 46 轮 stdev ≈ **1.34 ms**、相邻步长中位 ≈ **0.98 ms**。
- 相对阿里云：成功配对 **52** 轮，`|GPS−阿里云|` 中位 **−6.6 ms**，范围 **−20.1～+28.8 ms**；**`|diff| > 50 / 100 / 400 ms`：0/52**。阿里云超时 3 次。
- **勿把「相对手机的 1.3～9 ms stdev」当成对 UTC 的绝对精度**；测法是手机 Wi‑Fi NTP，底噪已在数毫秒。

原始 CSV：[`docs/cmp_20260914_102829.csv`](cmp_20260914_102829.csv)（从 `ntp_cmp_20260914_102829_4253.csv` 归档）。

对比 2026-09-11（全程已锁定、Windows PC）：见 [ntp_cmp_test_20260911.md](ntp_cmp_test_20260911.md)。

---

## 测试条件

| 项 | 值 |
|----|-----|
| 日期 | 2026-09-14 10:28:29–10:38:19（**+08**） |
| 时长 / 间隔 | 600 s / 10 s → **60** 轮（含 5 轮 ACQ） |
| 设备 | ESP32-C3 GNSS NTP（`10.81.127.143`） |
| 客户端 | Termux 手机，与设备同一局域网 Wi‑Fi |
| 脚本 | `tools/ntp_cmp_termux.py`（标准库 UDP/123 + 读 `/status`） |
| 时钟状态 | 轮 1–5 **ACQ**；轮 6–60 **LCK**，`residualMs=0`，`ppsFresh=true` |
| GPS NTP（锁定后） | UDP/123，stratum **1**，LI=0，refid **GPSS** |
| 参考 | `ntp.aliyun.com` → `203.107.6.88`（stratum 2 或 3） |
| 指标定义 | `offset_ms`：经典 NTP「服务器相对本机」；`diff = gps_offset − ali_offset` |

统计锁定后精度时：**只使用 `clock=LCK` 且 `gps_stratum=1` 且 `gps_refid=GPSS` 的行。**

---

## 结果 0：搜星阶段（轮 1–5）

| 轮 | 本地时间 | clock | stratum | LI | refid | `/status` |
|----|----------|-------|---------|----|-------|-----------|
| 1–5 | 10:28:29–10:29:09 | ACQ | 16 | 3 | `INIT` | `ppsFresh=false`，`stratum1=false` |

未锁定时设备故意不报 GPS 时：offset 约 **−3.998×10¹² ms**（量级上等于「NTP 纪元/未同步」相对 2026 年的差），root dispersion **65535 s**。这是 Phase A 诚实元数据（unsync → LI=3 / stratum 16 / `INIT`），不是坏钟。

**复现建议：** 等 OLED / `ntpdate -q` 已是 stratum 1 再开脚本，曲线会干净很多。

---

## 结果 1：锁定后 GPS NTP ↔ 手机

`gps_offset_ms`（n=55，10:29:19–10:38:19）：

| 指标 | 全部 LCK (ms) | delay < 20 ms (ms) |
|------|---------------|------------------------|
| n | 55 | **46** |
| median | **+24.00** | **+23.99** |
| mean | +27.18 | +24.21 |
| **stdev** | 9.16 | **1.34** |
| min / max | +4.33 / +61.12 | +21.71 / +29.83 |
| 相邻 \|Δ\| median | 1.52 | **0.98** |
| 相邻 \|Δ\| mean | 7.85 | 1.32 |
| max \|Δ\| | 37.34 | 7.58 |
| RTT delay median | 7.46 | ~6–8 |
| RTT delay max | 81.29 | < 20 |

约 **+24 ms** 的绝对偏差是 **手机系统钟相对 GPS/UTC 慢了约 24 ms**，不是模块误差。稳定性看 **stdev / 步长**。

9 次 offset 明显偏离 +24 ms 的行，delay 也同时升到 38–81 ms（见下表）。这是 **手机↔设备 Wi‑Fi 排队/重传**，不是 GNSS 秒跳。滤掉后 1.34 ms stdev 略好于 2026-09-11 PC 测的 2.63 ms。

锁定后 GPS offset 滑动窗（窗内 pstdev 再取平均）：

| 窗宽 | 约时长 | 全部 LCK (ms) | delay < 20 ms (ms) |
|------|--------|---------------|------------------------|
| 3 | 30 s | 5.40 | **0.83** |
| 6 | 60 s | 7.52 | **1.02** |
| 12 | 120 s | 8.28 | **1.26** |

全部 LCK 的滑动窗被高 RTT 尖峰抬高；干净样本才反映设备侧起伏。

锁定后 root dispersion 几乎恒为 **6.00 ms**（前 2 轮 LCK 为 4.99 ms）。

### 高 RTT / offset 尖峰（LCK）

| idx | 时间 | gps_offset (ms) | gps_delay (ms) | ali_offset (ms) | diff (ms) |
|-----|------|-----------------|----------------|-----------------|-----------|
| 8 | 10:29:39 | 49.68 | 61.94 | 36.91 | +12.77 |
| 14 | 10:30:39 | 48.90 | 63.65 | 30.35 | +18.55 |
| 15 | 10:30:49 | 41.49 | 41.68 | （超时） | — |
| 17 | 10:31:09 | 46.28 | 51.79 | 30.08 | +16.20 |
| 27 | 10:32:49 | 36.19 | 38.51 | 26.17 | +10.02 |
| 34 | 10:33:59 | 61.12 | 81.29 | 32.37 | +28.75 |
| 39 | 10:34:49 | 40.53 | 38.86 | 40.75 | −0.22 |
| 51 | 10:36:49 | 4.33 | 46.41 | （超时） | — |
| 55 | 10:37:29 | 52.63 | 68.98 | 35.01 | +17.62 |

#51 的 offset 落到 +4.3 ms 而 delay 仍高，是典型的 **不对称时延**（去程/回程不等），不能读成 GPS 回退了 20 ms。

---

## 结果 2：GPS vs 阿里云

锁定后阿里云成功 **52/55**；超时 3 次（#15、#37、#51，`timed out`）。云侧 stratum 在 **2**（32 次）与 **3**（20 次）之间切换。

相对手机的两条 offset 曲线（LCK 且阿里云成功）：

| 来源 | offset stdev (ms) | 相邻 \|Δ\| median (ms) | delay median (ms) | delay 范围 (ms) |
|------|-------------------|------------------------|-------------------|-----------------|
| **GPS 设备** | 8.63（含 Wi‑Fi 尖峰） | **1.27** | **7.5** | 5.3–81.3 |
| 阿里云 NTP | 6.33 | 6.64 | **65.1** | 45.0–103.5 |

`diff = GPS − 阿里云`（n=52）：

| 指标 | 值 (ms) |
|------|---------|
| median | **−6.63** |
| mean | −3.65 |
| stdev | 10.03 |
| min / max | −20.08 / +28.75 |
| \|diff\| > 50 | **0 / 52** |
| \|diff\| > 100 | **0 / 52** |
| \|diff\| > 400 | **0 / 52** |

与 2026-09-11 不同：那次阿里云出现约 **+253 ms** 的路径尖峰（GPS 仍贴着本机基线）；本次云侧没有几百毫秒跳变，两边对 UTC 的粗一致性很好（约 ±20 ms）。

**结论：** 锁定后设备宜作 **局域网主时钟**。阿里云 delay 约 65 ms、stratum 2/3 会切，只适合粗参考。GPS 在干净 RTT 下比云更稳；含手机 Wi‑Fi 尖峰时 stdev 会被测法抬高，不要据此说「GPS 比阿里云抖」。

脚本参考门槛（`tools/ntp_cmp_termux.py`）：异常 `|diff|>400` < 2%、`|median|<50 ms` —— **本轮均通过**（异常 0/52，median −6.6 ms）。

---

## 与 2026-09-11 对照

| 项 | 2026-09-11 | 2026-09-14 |
|----|------------|------------|
| 客户端 | Windows PC + Wi‑Fi | Termux 手机 + Wi‑Fi |
| 设备 IP | `192.168.124.6` | `10.81.127.143` |
| 开跑时时钟 | 全程 LCK | 先 ACQ 约 50 s，再 LCK |
| GPS offset median | −243 ms（PC 钟差） | +24 ms（手机钟差） |
| GPS stdev（宜引用） | 2.63 ms（全程 delay 都小） | **1.34 ms**（delay < 20 ms） |
| \|GPS−阿里云\| 最大 | +253 ms（云尖峰） | +29 ms |
| \|diff\| > 400 | 0/60 | 0/52 |

两次绝对 offset 差一个数量级，只说明 **两台客户端系统钟不一样**，不能横向比「谁更准」。

---

## 精度等级说明（产品定位）

| 说法 | 说明 |
|------|------|
| Stratum 1 / `GPSS` | 角色是「GNSS 主参考」，**不是**「误差 = 1 ms」 |
| 本测干净样本 stdev ~1.3 ms | **测量起伏**（手机 + Wi‑Fi NTP），易触碰测法底噪 |
| 商用机架 GPS NTP | 常宣传对 UTC **µs～百 µs**（有线 + 好客户端） |
| 本产品合理定位 | **局域网 Stratum-1 GNSS NTP**，客户端体验约 **1～10 ms** 量级；**非**电信/PTP/金融同步级 |

要宣称亚毫秒/微秒级，需 **PPS 对参考 PPS（示波器）** 或 **硬件时间戳有线 NTP**，不能单靠「手机 vs 阿里云」。

---

## 复现

设备已 STA，**建议已 LCK** 后再跑：

```bash
# Termux
pkg install python
curl -L -o ntp_cmp_termux.py \
  https://ghproxy.net/https://raw.githubusercontent.com/luweixuancl/Acode_test/cursor/ntp-phase-a-metadata-c502/tools/ntp_cmp_termux.py
python ntp_cmp_termux.py --gps 10.81.127.143 --minutes 10
```

手机请连与设备同一 2.4 GHz Wi‑Fi。CSV 写在当前目录（`ntp_cmp_YYYYMMDD_HHMMSS.csv`）。

分析本记录：

```bash
# 只统计 clock=LCK 且 stratum=1 的 gps_offset_ms / diff_gps_minus_ali_ms
# 原始表：docs/cmp_20260914_102829.csv
```

---

## 相关文档

- [clock_eval_two_ntp_cmp.md](clock_eval_two_ntp_cmp.md) — 两次比对 + 时钟算法评价  
- [ntp_cmp_test_20260911.md](ntp_cmp_test_20260911.md) — 2026-09-11 PC 比对  
- [local_clock_gps_check.md](local_clock_gps_check.md) — LocalClock / residual / Holdover  
- [wifi_event_fsm.md](wifi_event_fsm.md) — WiFi 事件与重连  
