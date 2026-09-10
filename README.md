# ESP32-C3 GNSS NTP Server

基于 **合宙 CORE ESP32-C3** + **大夏龙雀 DX-GP22** 的一级（Stratum-1）NTP 时间服务器。

## 功能

1. **OLED 状态页**：显示 IP、GPS 锁定/搜星数、PPS、本地时间
2. **板载双状态 LED**（合宙 CORE 表4-1）：D4（IO12）显示运行/WiFi，D5（IO13）显示 GPS/PPS，高电平有效
3. **旋转编码器菜单**：扫描 WiFi、网页配网、静态 IP、DHCP、时区、重启
4. **手动静态 IP**：编码器逐字节编辑；保存时通过 **ARP 探测**检测局域网是否已有相同 IP
5. **编码器配网**：扫描附近 WiFi → 选择 SSID → 编码器输入密码 → 连接
6. **网页配网**：开启 SoftAP（`NTP-Setup-XXXX` / 密码 `12345678`），浏览器选择 WiFi 并输入密码

## 硬件连接

| 模块 | 信号 | ESP32-C3 GPIO |
|------|------|---------------|
| DX-GP22 | TXD | 1 (UART1 RX) |
| DX-GP22 | RXD | 0 (UART1 TX) |
| DX-GP22 | 1PPS | 4 |
| DX-GP22 | VCC | 3.3V 或 5V（按模块说明） |
| DX-GP22 | GND | GND |
| SSD1306 OLED | SDA | 8 |
| SSD1306 OLED | SCL | 10 |
| SSD1306 | VCC/GND | 3.3V / GND |
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
| D4 | 常亮 | WiFi STA 已连接 |
| D5 | 灭 | 无 GNSS 信号 |
| D5 | 闪烁 | 已搜星或已定位，等待稳定 PPS |
| D5 | 常亮 | GPS 锁定且 PPS 正常（可作为 Stratum-1 NTP） |

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
- **Restart**：重启

长按编码器：多数界面返回上一级。

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

GPS 锁定且 PPS 正常时，应答为 **stratum 1**，Reference ID 为 `GPSS`。

## 目录结构

```
include/     配置与头文件
src/         固件源码
platformio.ini
```
