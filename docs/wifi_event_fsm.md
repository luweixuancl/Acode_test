# WiFi 事件 FSM（ESP32-C3）

本板为 **ESP32-C3 单核 RISC-V**，不能做 APP/PRO 双核拆分。WiFi/lwIP 已在高优先级内部任务中运行；应用仍用三任务（`task-time` / `task-net` / `task-ui`），全部钉在 core 0。

双核拆分仅适用于未来换 **ESP32-S3 / 经典双核 ESP32** 时另立项；C3 路线是 **事件驱动加固 + 自动重连**，不是迁核。

## 原则

- `WiFi.onEvent` 回调只在 WiFi 任务上下文中运行：**只置位 / 打日志**，禁止 `WiFi.begin` / `disconnect` / `scanNetworks` / NVS / 大 `String`。
- 所有 WiFi API 仅在 `task-net` 调用。
- `task-time` 零 WiFi 调用，授时路径不依赖事件。
- 连接仍保留 deadline；事件丢失时靠超时失败（双保险）。

## 事件 → 标志

| Arduino 事件 | 标志 | `task-net` 处理 |
|--------------|------|-----------------|
| `STA_CONNECTED` | （仅日志） | 仍等 `GOT_IP` |
| `STA_GOT_IP` | `GotIp` | `Connecting → Connected`；停 SoftAP（若需要） |
| `STA_DISCONNECTED` | `Disc` + reason | 连接中 → `Failed`；已连接 → 掉线提示 + 武装自动重连 |
| `SCAN_DONE` | `ScanDone` | `harvestScanResults()` → `lastScan_`；OLED/Web 共用 |

标志在 `WifiManager` 内用 `portMUX` 保护的 `evtFlags_`。

## 连接状态

```
Idle ──beginConnect──► Connecting ──GOT_IP / (WL_CONNECTED+IP)──► Connected
                           │
                           ├──DISC / timeout──► Failed
                           │
Connected ──DISC──► Idle（武装 Reconnect）
```

`pollConnect` 优先消费 `GotIp` / `Disc`，否则回退 `WiFi.status()`+IP，再否则超时。

## 自动重连（NVS `arec`，默认开）

```
Connected --DISC--> armed
  attempt 1: 立即 beginConnect（同一凭据）
  attempt 2..N: 退避 2s / 5s / 10s / 30s（上限 30s）
  连续失败 ≥5 或累计 >2min → SoftAP 逃生（`NTP-Setup-XXXX`）
```

手动 Connect / Scan 会 `cancelAutoReconnect()`，以用户操作为准。

常量见 `config.h`：`WIFI_RECONNECT_*`。

## 扫描

- `startScan` 异步 `scanNetworks`；完成以 `SCAN_DONE` 为主，`scanComplete` 为双保险。
- 结果写入 `lastScan_`；`NetWork::Scanning`（OLED）与 Web `/scan` 都读同一缓存。
- 连接 / ARP probe 期间 `startScan` 失败 → Web 返回 `busy`。

## 常见 DISC reason（排障）

| reason | 含义（常见） |
|--------|----------------|
| 2 | `AUTH_EXPIRE` |
| 3 | `AUTH_LEAVE` |
| 4 | `ASSOC_EXPIRE` |
| 8 | `ASSOC_LEAVE` / 主动断开 |
| 15 | `4WAY_HANDSHAKE_TIMEOUT`（密码错误常见） |
| 200+ | 无 AP / 信号丢失等（依 IDF 版本） |

串口日志前缀：`[wifi-evt]` / `[wifi]`。

## SoftAP 逃生

- STA 失败 / 重连放弃后进入 **纯 `WIFI_AP`**（不是 `AP_STA`）。
- 原因：`AP_STA` 下残留 STA 扫描常导致 SoftAP **不发 beacon**，手机/电脑扫不到 SSID，但串口仍打印 `Setup AP`。
- SoftAP 仅 **2.4 GHz**（ESP32-C3 无 5 GHz）。手机若只看 5G 列表会漏掉；请在 2.4G WiFi 列表中找 `NTP-Setup-XXXX`。
- 配网成功后再切回 STA（或 `AP_STA` 至停 AP）。
