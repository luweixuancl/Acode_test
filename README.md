# ESP32-C3 GNSS NTP Server

基于 **合宙 CORE ESP32-C3** + **大夏龙雀 DX-GP22** 的一级（Stratum-1）NTP 时间服务器。

## 功能

1. **OLED 状态页**：大字体本地时间；显示当前 WiFi SSID / IP；顶栏角标为星数·时钟态 / RSSI·AP；SYNC/WAIT
2. **OLED 防烧屏**：无操作（默认 10 分钟）自动关断面板，旋钮任意动作唤醒（首个动作仅唤醒不导航）；息屏时长可在 **Screen Off** 菜单或网页 `/cfg` 调档（常亮/1/5/10/30 分钟，NVS `ooff`）
3. **板载双状态 LED**（合宙 CORE 表4-1）：D4（IO12）显示运行/WiFi，D5（IO13）显示 GPS/PPS，高电平有效
4. **旋转编码器菜单**：扫描 WiFi、网页配网、静态 IP、DHCP、时区、异常策略、NTP ACL、温度补偿、息屏、NTP 统计、重启
5. **手动静态 IP**：编码器逐字节编辑；保存时通过 **ARP 探测**检测局域网是否已有相同 IP；当前 IP 与目标一致时提示 `IP unchanged` 并保持在线（不断网重连）
6. **编码器配网**：扫描附近 WiFi → 选择 SSID → 编码器输入密码 → 连接
7. **网页配网**：开启 SoftAP（`NTP-Setup-XXXX` / 密码默认 **`NTP-`+MAC 后 4 位十六进制**，与串口打印的 MAC 对应；**仅 2.4 GHz**）。`/` 始终是只读状态页。打开 `/setup` 或 `/login` 先登录，通过后才进入 `/cfg` 设置；设置页不再出现写口令输入框。`/status` `/metrics` 只读开放。若手机仍显示旧设置页，请强制刷新（缓存了刷固件前的 `/`）。
8. **网页状态**：访问 `http://<设备IP>/` 查看 NTP/GPS/PPS（JS 按 1 Hz 轮询 `/status`，无需整页刷新）；点「设置」进入登录后再到 `/cfg`
9. **FreeRTOS 三任务**：`task-time`(5) 独占 GNSS/NTP，`task-net`(2) 管 WiFi/网页，`task-ui`(1) 管 OLED/编码器/LED；PPS 计数对齐避免 NMEA 迟到导致的整秒跳变
10. **WiFi 事件 + 自动重连**：`GOT_IP`/`DISC`/`SCAN_DONE` 驱动状态机；掉线后无限退避重连（默认开，NVS `arec`；0s/2s/5s/10s/30s 后每 60s 一轮持续搜寻）。**信号消失绝不自动切 SoftAP 配网**——SoftAP 仅在开机无已存 SSID 或菜单 Web Setup 手动触发。本板为 **C3 单核**，不做双核拆分（详见 `docs/wifi_event_fsm.md`）

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
- **Set Static IP**：编辑四个字节；长按保存并做 IP 冲突检测（IP 未变则不重连）
- **Use DHCP**：改回自动获取 IP
- **Timezone**：设置 UTC 偏移（默认 +8）
- **Anomaly Mode**：GPS 异常策略 — Refuse（拒授时）/ Holdover 30s / Holdover 300s（写入 NVS）
- **NTP ACL**：Off / AllowList（默认 Off；名单在网页 `/cfg` 编辑）
- **Temp Comp**：片上温度一阶 ppm 补偿（默认 Off；系数在 `/cfg` 改）
- **Screen Off**：息屏时长选档 — Always / 1 / 5 / 10 / 30 分钟（默认 10 分钟，NVS `ooff`）
- **NTP Stats**：served / RATE / DENY / ACL / drop / clients
- **Restart**：重启

主界面显示时钟状态缩写（ACQ/LCK/DEG/HLD/UNS）与 residual；Web `/` 与 `/status` 同步展示。`/cfg` 也可改异常策略、ACL 与息屏时长。

长按编码器：多数界面返回上一级。

### 恢复出厂

冷开机（上电/按复位）时**按住编码器按钮不松**：OLED 显示 `Hold 3s = reset`，持续 3 秒后擦除全部配置（WiFi、口令、静态 IP、时区、异常策略、ACL、温度补偿、息屏时长等）并自动重启；重启后因无已存 SSID 会自动打开 `NTP-Setup-XXXX` 配网热点。3 秒内松开即取消，正常启动，期间串口输出 `[reset]` 日志。

每次**上电周期只复位一次**：复位后的自动重启带 RTC 闩锁，若按钮仍按着不会再次擦写配置；断电重上电后功能恢复可用。

D5（GNSS）：LCK/DEG 心跳；Holdover 快闪；ACQ 慢闪；UNS/无星灭。D4 STA 为心跳。双灯交替狂闪表示任务卡死告警。

## 内网部署（阶段 B）

面向局域网 Stratum-1，**不要**把 UDP/123 直接暴露到公网。

1. **仅 2.4 GHz**：SoftAP 与 STA 都只支持 2.4G；手机若只看 5G 会扫不到 `NTP-Setup-XXXX`。
2. **限流（B1）**：每 IP 约 4 req/s，超限 KoD `RATE`；持续超限 → `DENY` 冷静丢弃；全局约 32 pkt/s 静默丢弃，保护 `task-time`/PPS。
3. **ACL（B3）**：默认 Off。需要更严时在 `/cfg` 开 AllowList，只放行可信客户端 IP（最多 8 条）；空名单=拒绝全部。
4. **管理面**：`/` `/status` `/metrics` 只读开放。`/cfg` `/save` `/scan` 仅接受登录后的会话 Cookie（约 30 分钟）；`/setup` 只显示登录页。口令默认 SoftAP `NTP-`+MAC 后 4 位。设置页不要求再次填写写口令。
5. **升级保配置**：只刷 `firmware.bin` @ `0x10000`，关闭全片擦除，以免清掉 NVS 里的 WiFi/口令/ACL。
6. **观测**：OLED **NTP Stats**、串口约每 60s 一行 `[ntp] …` 摘要、或抓 `http://<ip>/metrics`。

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

安卓 Termux 与阿里云比对（仅 Python 标准库）：

```bash
pkg install python
curl -L -o ntp_cmp_termux.py \
  https://ghproxy.net/https://raw.githubusercontent.com/luweixuancl/esp32c3-gnss-ntp/main/tools/ntp_cmp_termux.py
python ntp_cmp_termux.py --gps 10.81.127.143
# 冒烟约 1 分钟：
python ntp_cmp_termux.py --quick
```

默认 10 分钟、每 10 秒一轮，同时打设备 UDP/123 与 `ntp.aliyun.com`，并读 `/status` 时钟态。CSV 写在当前目录。手机须与设备同一 2.4 GHz WiFi。

GPS 锁定且 PPS 正常时，应答为 **stratum 1**，Reference ID 为 `GPSS`。未同步时 LI=3 / stratum 16 / RefID `INIT`；Holdover 时仍 **LI=0**（闰秒告警位不复用），靠抬高 root dispersion 与 `/status` 的 `clock.state=HLD` 标明守时。

限流（阶段 B1）：每 IP 默认 4 req/s，超限回 KoD `RATE`；持续超限约 10s 后 KoD `DENY` 并冷静丢弃约 60s；全局约 32 req/s 静默丢弃。`/status` 字段 `served` / `rateLimited` / `denied` / `dropped` / `clients`。OLED 菜单 **NTP Stats**；`http://<ip>/metrics` 文本指标（只读，无口令）。

ACL 白名单（阶段 B3）：默认 **Off**。开启 AllowList 后仅列出的 IPv4（最多 8 条）可取时，未命中静默丢弃；空列表=拒绝全部。OLED **NTP ACL** 切换模式；IP 列表在 `/cfg` 编辑。`/status` 含 `ntpAclMode` / `ntpAcl` / `ntp.aclDenied`。

**不做阶段 C**：NTS、HTTPS、PTP、硬件时间戳、TCXO 等机架级能力对本板（C3 + 无线）成本过高或得不偿失，产品范围止于阶段 A+B 的内网轻量 Stratum-1。

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

NTP 比对测试见 [docs/ntp_cmp_test_20260911.md](docs/ntp_cmp_test_20260911.md)（2026-09-11，PC）与 [docs/ntp_cmp_test_20260914.md](docs/ntp_cmp_test_20260914.md)（2026-09-14，Termux；原始 CSV [`docs/cmp_20260914_102829.csv`](docs/cmp_20260914_102829.csv)）。两次结果结合时钟算法的评价见 [docs/clock_eval_two_ntp_cmp.md](docs/clock_eval_two_ntp_cmp.md)。
