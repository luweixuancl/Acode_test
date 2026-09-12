# ESP32-C3 GNSS NTP Server

基于 **合宙 CORE ESP32-C3** + **大夏龙雀 DX-GP22** 的一级（Stratum-1）NTP 时间服务器。

## 功能

1. **OLED 状态页**：显示 IP、GPS 锁定/搜星数、PPS、本地时间
2. **板载双状态 LED**（合宙 CORE 表4-1）：D4（IO12）显示运行/WiFi，D5（IO13）显示 GPS/PPS，高电平有效
3. **旋转编码器菜单**：扫描 WiFi、网页配网、静态 IP、DHCP、时区、重启
4. **手动静态 IP**：编码器逐字节编辑；保存时通过 **ARP 探测**检测局域网是否已有相同 IP
5. **编码器配网**：扫描附近 WiFi → 选择 SSID → 编码器输入密码 → 连接
6. **网页配网**：开启 SoftAP（`NTP-Setup-XXXX` / 密码 `12345678`，**仅 2.4 GHz**），浏览器选择 WiFi 并输入密码。手机/电脑若连在 5 GHz 上可能扫不到该热点，请在 WLAN 列表中看 2.4 GHz 网络，或先断开当前 WiFi 再扫。
7. **网页状态**：STA 连上后访问 `http://<设备IP>/` 查看 NTP/GPS/PPS（JS 按 1 Hz 轮询 `/status`，无需整页刷新）；`/setup` 仍可改 WiFi
8. **FreeRTOS 三任务**：`task-time`(5) 独占 GNSS/NTP，`task-net`(2) 管 WiFi/网页，`task-ui`(1) 管 OLED/编码器/LED；PPS 计数对齐避免 NMEA 迟到导致的整秒跳变
9. **WiFi 事件 + 自动重连**：`GOT_IP`/`DISC`/`SCAN_DONE` 驱动状态机；掉线后退避重连（默认开，NVS `arec`）；多次失败后开 SoftAP 逃生。本板为 **C3 单核**，不做双核拆分（详见 `docs/wifi_event_fsm.md`）

## 硬件连接

| 模块 | 信号 | ESP32-C3 GPIO |
|------|------|---------------|
| DX-GP22 | TXD | 1 (UART1 RX) |
| DX-GP22 | RXD | 0 (UART1 TX) |
| DX-GP22 | 1PPS | 4 |
| DX-GP22 | VCC | 3.3V 或 5V（按模块说明） |
| DX-GP22 | GND | GND |
| SH1107/SSD1107 OLED 64×128 | SDA | 8 |
| SH1107/SSD1107 OLED 64×128 | SCL | 10 |
| OLED | VCC/GND | 3.3V / GND |
| KY-040 编码器 | CLK(A) | 2 |
| KY-040 | DT(B) | 3 |
| KY-040 | SW | 5 |
| KY-040 | + / GND | 3.3V / GND |
| 合宙 CORE 板载 D4 | IO12 | 12（高电平有效） |
| 合宙 CORE 板载 D5 | IO13 | 13（高电平有效） |

引脚可在 `include/config.h` 中修改。

**串口分工**：GPIO20/21 是 UART0（板载 CH343 下载/调试，115200）。GNSS 走 UART1（GPIO1 RX / GPIO0 TX，9600）。`pio device monitor` 看的是调试口，不是 GPS。

### 状态 LED

| LED | 现象 | 含义 |
|-----|------|------|
| D4 | 快闪（约 4 Hz） | 配网 AP 模式 |
| D4 | 慢闪（约 1 Hz） | 固件运行中，STA 未连接 |
| D4 | 心跳（约 900 ms 亮 / 100 ms 灭） | WiFi STA 已连接且固件存活 |
| D5 | 灭 | 无 GNSS 信号 |
| D5 | 慢闪 | 已搜星或 ACQ，等待稳定锁定 |
| D5 | 快闪 | Holdover 守时 |
| D5 | 心跳（与 D4 反相短灭） | GPS 锁定（LCK/DEG）且可授时 |
| D4+D5 | 交替狂闪（约 5 Hz） | 某 FreeRTOS 任务超过约 3 s 未响应 |

健康态用「短灭心跳」而非常亮：若灯僵死在常亮/常灭且不再闪断，多半已死机（UI 也停了）。

### DX-GP22 说明

- 默认串口：**9600 8N1**，输出 NMEA 0183
- 定位成功后 **1PPS** 约 1Hz
- 支持 GPS / 北斗 / GLONASS 等多模

## 操作说明

### 主界面

短按编码器进入菜单。

### 菜单项

- **WiFi Scan**：扫描 → 选择热点 → 旋转选字符、短按追加、长按确认连接
- **Web Setup**：打开配网热点，手机连上后访问 `http://192.168.4.1`
- **Set Static IP**：编辑四个字节；长按保存并做 IP 冲突检测
- **Use DHCP**：改回自动获取 IP
- **Timezone**：设置 UTC 偏移（默认 +8）
- **Anomaly Mode**：GPS 异常策略 — Refuse（拒授时）/ Holdover 30s / Holdover 300s（写入 NVS）
- **Restart**：重启

主界面显示时钟状态缩写（ACQ/LCK/DEG/HLD/UNS）与 residual；Web `/` 与 `/status` 同步展示。`/setup` 也可改异常策略。

长按编码器：多数界面返回上一级。

D5（GNSS）：LCK/DEG 心跳；Holdover 快闪；ACQ 慢闪；UNS/无星灭。D4 STA 为心跳。双灯交替狂闪表示任务卡死告警。

## 编译与烧录

```bash
pio run -t upload
pio device monitor
```

PlatformIO 环境：`esp32-c3-devkitm-1`（Arduino）。

## 客户端测试

```bash
ntpdate -q <设备IP>
# 或
chronyc sources
```

GPS 锁定且 PPS 正常时，应答为 **stratum 1**，Reference ID 为 `GPSS`。未同步时 LI=3 / stratum 16 / RefID `INIT`；Holdover 时 LI=1。

Windows 下若工程路径含非 ASCII 字符导致链接失败，可用 ASCII junction（如 `C:\acode_leds`）再 `pio run`。

## 目录结构

```
include/     配置与头文件
src/         固件源码
docs/        设计方案（如本地时钟与 GPS 检核）
platformio.ini
```

本地时钟 / GPS 交叉检核与异常策略已落地，见 [docs/local_clock_gps_check.md](docs/local_clock_gps_check.md)。

WiFi 事件 FSM、自动重连与 C3 无双核说明见 [docs/wifi_event_fsm.md](docs/wifi_event_fsm.md)。

NTP 比对测试（GPS vs 本机/阿里云，2026-09-11）见 [docs/ntp_cmp_test_20260911.md](docs/ntp_cmp_test_20260911.md)。
