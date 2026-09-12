# NTP 比对测试记录（GPS vs 本机 / 阿里云）

## 摘要

2026-09-11 对固件设备做了 **10 分钟、每 10 秒一轮** 的 NTP 比对。结论：

- 相对本机，**GPS 设备 offset 很稳**（stdev ≈ **2.6 ms**，相邻步长 median ≈ **1.1 ms**）。
- 相对阿里云，大 diff 主要来自 **云侧路径跳变**，不是 GPS 抖动。
- **`|GPS−阿里云| > 400 ms`：0/60**；全程 `clock=LCK`、stratum 1。
- **勿把「相对本机的 2.6 ms stdev」当成对 UTC 的绝对精度**；WiFi + PC 系统钟测法底噪通常已在数毫秒。

原始数据：`ntp_cmp_20260911_234802.csv`（仓库根目录，已被 `.gitignore` 忽略时可本地留存）。

---

## 测试条件

| 项 | 值 |
|----|-----|
| 日期 | 2026-09-11 23:48–23:58（本地时区） |
| 时长 / 间隔 | 600 s / 10 s → **60** 轮配对成功 |
| 设备 | ESP32-C3 GNSS NTP（`192.168.124.6`） |
| STA | `H3C_LuxYang`，HTTP `/status` 显示 `sta=true` |
| 时钟状态 | 全程 **LCK**，`residualMs=0`，`ppsFresh=true` |
| GPS NTP | UDP/123，stratum **1**，refid **GPSS** |
| 参考 | `ntp.aliyun.com`（公网） |
| 本机 | Windows PC，经 WiFi 查询双方 NTP offset |
| 指标定义 | `offset_ms`：经典 NTP 客户端估算的「服务器相对本机」；`diff = gps_offset − ali_offset` |

---

## 结果 1：GPS NTP ↔ 本机

`gps_offset_ms` 序列（n=60）：

| 指标 | 值 (ms) |
|------|---------|
| median | −243.28 |
| mean | −243.55 |
| **stdev** | **2.63** |
| min / max | −256.81 / −239.83 |
| 相邻 \|Δ\| median | **1.11** |
| 相邻 \|Δ\| mean | 1.86 |
| 相邻 Δ stdev | 3.40 |
| max \|Δ\| | 13.19 |
| RTT delay median | 5.26 |

约 **−243 ms** 的绝对偏差主要是 **本机系统钟相对 UTC/GPS 的慢/快差**，不是模块抖动。  
稳定性应看 **stdev / 步长**，不看绝对 offset 大小。

滑动窗（对 offset 均值再求 stdev，越平滑越小）：

| 窗宽 | 约时长 | stdev (ms) |
|------|--------|------------|
| 3 | 30 s | ≈ 1.7 |
| 6 | 60 s | ≈ 1.4 |
| 12 | 120 s | ≈ 1.1 |

---

## 结果 2：GPS vs 阿里云（谁更稳）

相对本机的两条 offset 曲线：

| 来源 | offset stdev (ms) | 相邻 \|Δ\| median (ms) | max \|Δ\| (ms) |
|------|-------------------|------------------------|----------------|
| **GPS 设备** | **2.63** | **1.11** | 13.2 |
| 阿里云 NTP | 55.77 | 5.53 | ≈ 250 |

`diff = GPS − 阿里云`（n=60）：

| 指标 | 值 (ms) |
|------|---------|
| median | −3.34 |
| mean | 8.46 |
| stdev | 56.07 |
| min / max | −20.11 / +252.93 |
| \|diff\| > 400 | **0 / 60** |

最大几次 \|diff\|（例如 +253 ms）对应阿里云 `offset` 落到约 **−490 ms**，GPS 仍停在约 **−242 ms** → **云路径偶发尖峰**。

**结论：本测法下 GPS 设备明显比阿里云更稳，宜作局域网主时钟；阿里云只适合粗参考。**

---

## 精度等级说明（产品定位）

| 说法 | 说明 |
|------|------|
| Stratum 1 / `GPSS` | 角色是「GNSS 主参考」，**不是**「误差 = 1 ms」 |
| 本测 GPS offset stdev ~2.6 ms | **测量起伏**（PC + WiFi NTP），易触碰测法底噪 |
| 商用机架 GPS NTP | 常宣传对 UTC **µs～百 µs**（有线 + 好客户端） |
| 本产品合理定位 | **局域网 Stratum-1 GNSS NTP**，客户端体验约 **1～10 ms** 量级；**非**电信/PTP/金融同步级 |

要宣称亚毫秒/微秒级，需 **PPS 对参考 PPS（示波器）** 或 **硬件时间戳有线 NTP**，不能单靠「笔记本 vs 阿里云」。

---

## 复现

```text
# 设备已 STA + LCK 后，对本机执行（示例）：
# 每 10s 查 GPS_IP 与 ntp.aliyun.com，写 CSV，持续 600s
# 字段含：gps_offset_ms, ali_offset_ms, diff_gps_minus_ali_ms, clock, residualMs, ppsFresh
```

分析脚本可参考 `tools/analyze_monitor_csv.py`（旧版 monitor 格式）或对 `ntp_cmp_*.csv` 用同等统计。

---

## 相关文档

- [local_clock_gps_check.md](local_clock_gps_check.md) — LocalClock / residual / Holdover  
- [wifi_event_fsm.md](wifi_event_fsm.md) — WiFi 事件与重连  
