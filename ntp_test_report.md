# NTP 服务器测试报告

> 测试对象：`10.81.127.143`（GPS 时间源 NTP 服务器）
> 测试日期：2026-09-10
> 测试环境：安卓设备（`/mnt/sdcard`），Python3 内联脚本测量（无 ntpdate/chrony 工具）
> 状态：已完成方案 E 长时监测，待服务器管理员修复后复测

---

## 0. 结论摘要（TL;DR）

| 问题 | 答案 |
|------|------|
| 10.81.127.143 能用作校时源吗？ | **❌ 不能。** 30 分钟监测异常率 **51.3%**（27 段 episode），时间落后幅度从 ~1s **恶化至 3~4s**，疑似 GPS 失锁+守时漂移（见 2.4/2.5） |
| 本机时间真实偏差？ | 比标准时间**慢约 596 ms**（GPS/aliyun 双源交叉验证，正常时两源一致性好于 25 ms） |
| 推荐校时方案？ | 安卓设备改用 `ntp.aliyun.com`（实测精度 std ~5ms，极稳）；`time.android.com` 国内被墙不可用（见 2.3） |
| 服务器还能救吗？ | 大概率可以：正常时与 aliyun 一致性 ~20ms，属授时质量良好的服务器。需管理员按 2.5 节清单排查（GPS 锁星、chrony/ntpd、holdover、PPS），修复后用 `monitor_ntp.py` 复测，异常率 <2% 即可恢复使用 |

**文档结构**：1 服务器信息 · 2 测试记录（2.1 连通性 → 2.4 配对发现异常 → 2.5 长时监测实锤） · 3 Android 同步机制 · 4 待办 · 5 更新日志

---

## 1. 服务器基本信息

| 项目 | 结果 |
|------|------|
| IP | 10.81.127.143 |
| NTP 版本 | v4（server 模式） |
| Stratum | 1（一级时钟源） |
| 时间源 | GPS（Reference ID: `GPSS`） |
| 可用性 | ❌ **不可靠：长时监测异常率 51.3%，且持续恶化（见 2.5），暂不可作为校时源** |

---

## 2. 测试记录

### 2.1 单次连通性测试

- 响应正常，网络往返 21.7 ms
- 本机时间比服务器慢约 0.6 秒（单次采样）

### 2.2 30 次采样统计（间隔 1s，标准 NTP 偏移公式）

| 指标 | 数值 |
|------|------|
| 成功率 | 30/30 |
| 偏移中位数 | **-604 ms** |
| 偏移标准差 | 485 ms（受网络抖动影响偏大） |
| 偏移范围 | -1842 ~ -504 ms |
| 延迟中位数 | 27 ms |
| 延迟最大值 | 472 ms |

**关键发现：偏移呈三个簇**
- 主簇：-500 ~ -610 ms（20 次，正常网络时）
- 次簇：-1500 ~ -1600 ms（9 次）
- 离群：-1842 ms（1 次）

偏移跳变与网络延迟尖峰（27ms → 472ms）强相关，最初归因于非对称网络抖动（Wi-Fi）。依据 NTP 理论，最小延迟样本偏移最准确，即本机比 GPS 服务器**慢约 600 ms**。

> **修正（2026-09-10，依据 2.4 配对分析）**：延迟尖峰最多造成 ±472 ms 误差，无法解释 -1000 ms 级跳变。真实原因是 **GPS 服务器偶发返回整体慢 ~1s 的时间戳**（详见 2.4 第 2 点），属服务器侧问题。

### 2.3 GPS 服务器 vs 公共 NTP 对比（各 20 次交替采样）

| NTP 服务器 | 可达性 | Stratum | 偏移 | 延迟 |
|------------|--------|---------|------|------|
| GPS 服务器 10.81.127.143 | ✅ | 1 | -547 ms | 128 ms |
| time.android.com（安卓自带） | ❌ 全部超时（被墙） | - | - | - |
| ntp.aliyun.com | ✅ | 2 | -605 ms | 36 ms |
| ntp.tencent.com | ✅ | 2 | -603 ms | 47 ms |
| cn.pool.ntp.org | ✅ | 2 | -609 ms | 55 ms |
| time.windows.com | ❌ 超时 | - | - | - |

**结论：**
1. 安卓自带的 `time.android.com`（谷歌）在此网络完全不可达（UDP 123 被墙），这是国内手机"自动确定时间"失效的根源。国内 ROM 通常改用厂商 NTP 或运营商 NITZ（基站时间）。
2. 可达服务器一致性好：GPS（stratum 1）与阿里/腾讯/pool（stratum 2）测得偏移仅差 ~60 ms，都确认本机慢 0.55~0.61 秒。两者授时质量对手机场景等价。
3. ~~精度瓶颈在本机测量链路（Wi-Fi 抖动 + 系统调度延迟），不在服务器。~~ **已被 2.4 修正：跳变主因是 GPS 服务器侧时间戳异常，本机链路对 aliyun 采样全程稳定（std < 6 ms）。**

### 2.4 方案 C：GPS vs ntp.aliyun.com 严格精度对比（30 轮交替采样，间隔 1s）

方法要点：两服务器逐轮交替采样，可用**逐轮配对差值**完全消除本机时钟漂移和整体网络状况变化的影响；最小延迟 20% 样本过滤（NTP 标准做法：最小延迟样本最准确）。

| 指标 | GPS 服务器 10.81.127.143 | ntp.aliyun.com |
|------|--------------------------|----------------|
| 成功率 | 30/30 | 30/30 |
| 偏移中位数 | -594.77 ms | -596.58 ms |
| 偏移标准差 | **434.48 ms** ⚠️ | **5.94 ms** ✅ |
| 偏移范围 | -1608 ~ -522 ms | -608 ~ -586 ms |
| 延迟中位数 / 最大 | 94.8 / 191.6 ms | 60.4 / 76.1 ms |
| 最小延迟 20% 样本标准差 | 516.62 ms | **3.91 ms** ✅ |

**逐轮配对差值（GPS − 阿里）：中位数 +1.44 ms，均值 -206.63 ms，标准差 435.37 ms，范围 [-1011.4, +84.1] ms**

**关键分析：**
1. **正常时两源几乎完全一致**：配对差中位数仅 +1.44 ms，偏移中位数差 1.8 ms → 本机真实偏差约 **-595.7 ms**。
2. **GPS 服务器存在 ~1 秒"时间回退"异常**：GPS 读数偶发跳到约 -1600 ms（恰好慢 ~1000 ms），而此时往返延迟仍正常（≤192 ms）。网络延迟最多造成 ±192 ms 误差，无法解释 -1000 ms 跳变；唯一自洽的解释是**服务器偶尔返回整体慢约 1 秒的时间戳（t2/t3 同时变旧时往返延迟不变），即其 GPS 授时/守时机制偶发失步**。aliyun 全程稳定（std < 6 ms）反证本机链路正常。
3. **精度结论**：ntp.aliyun.com 精度 ≈ 4~6 ms（极稳）；GPS 服务器中位精度与阿里同级（差 ~1.4 ms），但有约 20~30% 概率返回偏差 ~1s 的陈旧时间。**作为校时源暂不如公共 NTP 可靠，建议排查该服务器 GPS 授时模块/守时时钟。**

### 2.5 方案 E：30 分钟长时监测（2026-09-10 15:44:02 ~ 16:14 UTC+8，间隔 5s，GPS vs ntp.aliyun.com 交替）

原始数据：`ntp_monitor_20260910_154402.csv`（275 轮 / 550 样本，含完整 t1~t4 原始时间戳）；分析输出：`analysis_result.md`；工具：`monitor_ntp.py` / `analyze_ntp.py`。

**总体结果：**

| 指标 | GPS 服务器 | ntp.aliyun.com（参照） |
|------|-----------|------------------------|
| 成功率 | 275/275 (100%) | 269/275 (97.8%) |
| 偏移中位数 | **-1531 ms** | -597 ms |
| 偏移标准差 | 1439 ms | 5.77 ms |
| 延迟中位数/最大 | 66.8 / 317 ms | 53.0 / 242 ms |

**配对差分析（GPS − aliyun，消除本机因素）：**
- 正常样本（\|diff\|≤400ms）：131 轮（48.7%），中位数 +22.5 ms，标准差 20.7 ms ← **健康基线：与 aliyun 一致性良好**
- 异常样本：138 轮（**51.3%**），全部为 stale（服务端时间整体变旧）
- 异常 episode：27 段

**异常演化过程（关键发现——服务器在恶化）：**

| 时段 (UTC) | 特征 |
|------------|------|
| 15:44 ~ 16:01 | 异常呈周期性：每 ~30~45s 出现一段 1~3 轮（5~10s）的 stale，幅度稳定在 **-980 ms（约慢 1 秒）**，类似"落后 1 秒又追上"的循环 |
| 16:00 ~ 16:01 | episode 变长至 15 轮（~72s），幅度仍 ~-1s |
| 16:01 ~ 16:07 | （过渡） |
| 16:07:50 ~ 16:13:34 | **连续 68 轮（~344 秒）异常，幅度 -2.9 ~ -4.0 秒** ← 服务器时间额外落后了 2~3 秒 |
| 16:13:47 ~ 16:14 | 回落到 -2.9 ~ -3.0 秒 |

**证据链（排除法）：**
1. 异常期间网络往返正常（GPS 延迟中位 67ms、最大 317ms），延迟最多解释 ±0.3s 误差，无法解释 1~4s 偏差 → **排除网络**
2. 参照源 aliyun 全程稳定（std 5.77ms），本机采样机制无异常 → **排除本机测量问题**
3. 异常幅度呈整秒级（-0.98s → -3~-4s），t2/t3 同时变旧（延迟不变）→ **服务器侧时间戳整体变旧**
4. 30 分钟内幅度从 ~1s 恶化到 ~4s → **服务器守时在持续失步，疑似 GPS 失锁后振荡器自由漂移**

**结论：❌ 10.81.127.143 当前不可作为校时源。** 正常时授时质量良好（与 aliyun 差 ~20ms），但过半时间返回落后 1~4 秒的时间，且偏差在扩大。需服务器管理员立即排查。

**给服务器管理员的排查建议（按优先级）：**
1. **GPS 接收机锁星状态**：锁星数、信噪比、天线/馈线/防雷器是否受损（失步最常见原因）；异常循环周期（~30-45s）疑似与 GPS 接收机输出更新周期相关
2. **授时守护进程状态**：`chronyc tracking`（关注 leap status、Last offset、RMS offset、源状态）或 `ntpq -c rv`（关注 leap、sync 状态）；检查 `makestep`/`tinker panic` 配置是否允许了异常步进
3. **守时振荡器（holdover）**：GPS 失锁后晶振自由漂移与"落后逐秒扩大"的特征吻合；检查振荡器锁定/健康状态
4. **PPS 信号**：是否接入、是否被 daemon 使用（`chronyc sourcestats`）
5. **重点检查时段日志**：2026-09-10 16:01~16:14（UTC+8）前后应有 step/fallback/失锁记录，可定位恶化事件
6. **修复后验证**：可复用本目录 `monitor_ntp.py`（`python3 monitor_ntp.py 1800 5`）+ `analyze_ntp.py` 做修复前后对比

### 2.6 固件代码分析与修复（2026-09-10）

**服务器固件已开源**：`github.com/luweixuancl/Acode_test`，分支 `cursor/esp32c3-status-leds-5075`。硬件：合宙 CORE ESP32-C3 + DX-GP22 GNSS（1PPS + NMEA@9600）+ SSD1306 + KY-040。本地已 clone 并新建修复分支 `fix/time-alignment`（3 个提交），补丁导出于 `patches/`。

**实测现象 ↔ 代码缺陷精确对应：**

| 实测现象 | 代码缺陷 | 位置 |
|----------|----------|------|
| 周期性 **-980ms**（每30~45s，1~3轮） | `nowUtc()` 假设"NMEA 时间=当前 PPS 秒"直接使用，但 RMC(T) 语句在 9600 波特率下比 PPS(T) 边沿晚 ~100~800ms 到达并解析。窗口内 `utcEpoch` 还是上一秒 → 返回时间恰好旧 ~1s | `gps_service.cpp nowUtc()` |
| **-3~-4s** 持续失步（最长344s） | 主循环阻塞叠加：WiFi 同步连接（≤20s delay 循环）、同步扫描（2~5s）、OLED I2C 默认 100kHz 全帧 ~100ms/250ms、`delay(500)`；UART RX 缓冲仅 256B（~260ms 就溢出）→ NMEA 丢帧，时间停更 | `wifi_manager.cpp / main.cpp / display_ui.cpp` |
| 客户端无法识别坏样本 | 时间过期照样应答：无新鲜度检查、LI 恒 0、root dispersion 恒 0 | `gps_service.cpp / ntp_server.cpp` |

**修复内容（fix/time-alignment 分支）：**

| 提交 | 内容 |
|------|------|
| `a4c186e` fix(gps) | **PPS 计数对齐算法**：NMEA 提交时快照单调 `ppsCount_`，查询时 `lag = 当前计数 − 提交计数` = 该语句落后的 PPS 秒数，`baseSec = utcEpoch + lag` —— 对任意解析延迟（1s 竞态 / 多秒积压）都精确；仅解析到新秒值才提交（防旧值刷新失效新鲜度）；`commitMs` 超 3s 视为流停滞拒绝服务；RX 缓冲 256B→2048B（可吸收 ~2.1s 阻塞） |
| `dbf9d23` fix(ntp) | 元数据诚实化：时间不可用 → LI=3 + stratum 16；root dispersion 由 `qualityMs` 动态计算（客户端可据此丢弃坏样本）；precision 分级（PPS 锚定 -10 / 插值 -6）；Reference 时间戳 = PPS 锚定整秒 |
| `ea7d315` refactor | 主循环去阻塞：WiFi 连接改非阻塞状态机（`beginConnect`/`pollConnect`）、扫描异步化（UI 可取消）、删除 `delay(500)`、OLED I2C 提速 400kHz（帧耗时 100ms→25ms）；Web 配置页保留阻塞封装（仅 AP 模式使用） |

**遗留与验收：**
- 本机无 PlatformIO 交叉编译环境，固件**未经编译验证**，烧录前建议本地 `pio run` 检查
- **验收流程**：烧录后运行 `python3 monitor_ntp.py 1800 5 && python3 analyze_ntp.py`，目标：配对差中位数 \|diff\| < 50ms、std < 30ms、异常率 < 2%（修复前基线：51.3%，数据见 2.5 节 CSV）
- 风险提示：若 -3~-4s 失步主因是 DX-GP22 模块自身失锁（而非固件），固件修复后 LI=3 会如实暴露该状态 → OLED/LED 可直接观察（D5 灯：常亮=Stratum-1 ready，闪烁=有定位无 PPS）

### 2.7 RTOS 三任务化架构（2026-09-10，分支 feat/rtos-tasks）

**背景**：ESP32-C3 Arduino 本身运行在 FreeRTOS 上（`loop()` 只是优先级 1 的 loopTask），2.6 靠"每个调用点都不阻塞"的纪律保证时间链路——新增功能可能破坏该纪律。任务化提供**抢占式隔离**，是授时设备的正统架构，作为第二步演进叠加在 fix/time-alignment 之上。

**任务架构：**

| 任务 | 优先级 | 栈 | 职责（独占资源） |
|------|--------|-----|------------------|
| `task-time` | 5 | 6KB | NMEA 解析（独占 UART）、PPS 处理、NTP 应答（独占 UDP 123）、订阅 `esp_task_wdt` |
| `task-net` | 2 | 8KB | WiFi 连接/扫描状态机、Web Portal（其阻塞式扫描不再波及授时） |
| `task-ui` | 1 | 4KB | OLED、旋钮、LED（原 loopTask 删除，三任务接管） |

**同步设计（无应用层互斥锁竞争，纯所有权隔离）：**
- **TimeSnapshot**：`GpsStatus` 由 task-time 独占写入，其他任务经 `snapshot()` 在 portMUX 临界区取副本（<1µs）——消灭跨任务共享竞争
- **PPS 唤醒**：ISR `vTaskNotifyGiveFromISR` 直接唤醒 task-time（微秒级、抖动最小），1ms 兜底 tick 覆盖 UDP 轮询
- **跨任务请求**：UI→net 走 `NetRequest` 队列（WiFi 连接/扫描/静态 IP/启动 Portal），替代原 pending 标志（消除 String 竞态）；net→UI 消息走 `gUiMsgQueue`
- **gSettings 统一互斥锁**：所有读改写（时区/静态 IP/Portal 保存）持锁进行
- **优先级红线**：应用任务 ≤5，低于 WiFi/lwIP 内部任务（~18-23），防协议栈饿死

**边界与验收：**
- RTOS 解决"软件阻塞/优先级"问题，**不解决 GPS 模块自身失锁**（2.6 风险提示仍有效）
- 验收指标与 2.6 相同（异常率 <2%、\|配对差中位数\|<50ms、std<30ms），另可观察 PPS 唤醒延迟抖动作为附加指标
- A/B 归因：fix/time-alignment（算法修复）与 feat/rtos-tasks（架构修复）可分别烧录对比
- 本机无编译环境，烧录前需 `pio run` 检查；补丁 `patches/0004-feat-rtos-time-net-ui.patch`

### 2.8 分支交付与远程推送状态（2026-09-10）

**本地仓库状态**（`/tmp/opencode/Acode_test`）：
- `fix/time-alignment` = 原分支 + 3 个修复提交（`a4c186e` → `ea7d315`）
- `feat/rtos-tasks` = fix/time-alignment + RTOS 任务化（`7d32adf`）

**本机直推 GitHub 不可行，双重阻塞：**
1. **网络**：`github.com` 直连完全超时（000/8s），而全局 gitconfig 的 `insteadOf` 规则把所有 github.com URL 劫持到 gh-proxy 只读镜像（fetch 可用，**push 不支持**）
2. **认证**：本机无 PAT / SSH 密钥 / credential helper

**交付物（已生成，位于本项目目录）：**
- `Acode_test_branches.bundle`（5MB）：两分支完整历史，可完整重建精确提交
- `patches/0001~0004`：四个补丁，可 `git am` 到上游分支重建（内容一致，哈希略有差异）

**在有权限的电脑上推送（二选一）：**

方案一 · bundle（推荐，保留精确提交）：
```bash
git clone -b fix/time-alignment Acode_test_branches.bundle repo && cd repo
git remote set-url origin https://github.com/luweixuancl/Acode_test.git
git push origin fix/time-alignment feat/rtos-tasks
```

方案二 · patches：
```bash
git clone -b cursor/esp32c3-status-leds-5075 https://github.com/luweixuancl/Acode_test.git && cd Acode_test
git checkout -b fix/time-alignment
git am 0001-fix-gps-PPS-NMEA-1s.patch 0002-fix-ntp.patch 0003-refactor-main-wifi-ui.patch
git push origin fix/time-alignment
git checkout -b feat/rtos-tasks
git am 0004-feat-rtos-time-net-ui.patch
git push origin feat/rtos-tasks
```

**状态**：⏳ 待推送。推送成功后本报告记录远程分支链接。

---

## 3. 背景知识：Android 时间同步机制

由系统服务 `NetworkTimeUpdateService` 实现：

| 项目 | 默认值 |
|------|--------|
| NTP 服务器 | `time.android.com` |
| 周期性同步 | 约 24 小时一次 |
| 网络切换/重连/开机 | 触发尝试同步（网络可用后 ~3 秒） |
| 校时阈值 | 偏差 > 2 秒才实际调整 |
| 失败重试 | 约 5 分钟一次 |

**本机 600 ms 的偏差 < 2 秒阈值，系统不会校准**，会长期保持。设备晶振每天漂移几~几十 ms，积累到 2 秒才触发校正。

强制触发一次同步（Android 10+，不改服务器）：
```bash
adb shell cmd network_time_update_service force-refresh
```

---

## 4. 待办 / 下一步方案（未实施）

- [ ] **方案 A**：让安卓设备改用 `10.81.127.143` 校时 → **取消：2.5 长时监测证明该服务器异常率 51.3% 且恶化中，不可用**
- [ ] **方案 B**：改用 `ntp.aliyun.com` 等公共 NTP 校时（精度 ~5ms，实测极稳）
- [x] **方案 C**：GPS vs ntp.aliyun.com 30 次交替采样，输出更严格的精度统计对比（结果见 2.4）
- [ ] **方案 D**：向服务器管理员提交 2.5/2.6 节证据链与修复代码（`patches/`），跟踪烧录验证
- [x] **方案 E**：30 分钟长时监测，量化异常频率/持续时间/演化（结果见 2.5）
- [x] **方案 F（代码侧）**：固件病因定位 + fix/time-alignment 分支修复（见 2.6），待烧录
- [ ] **后续 G**：固件烧录后复测（`monitor_ntp.py` 30 分钟 + `analyze_ntp.py`），异常率 <2% 且 \|配对差中位数\|<50ms 方可恢复方案 A
- [x] **方案 H（代码侧）**：RTOS 三任务化 feat/rtos-tasks 分支（见 2.7），可与 2.6 版本 A/B 对比烧录
- [ ] **后续 I**：远程推送 fix/time-alignment 与 feat/rtos-tasks 分支（见 2.8：本机网络/认证双重受限，需在有权限电脑上经 bundle 或 patches 推送）

---

## 5. 更新日志

| 日期 | 内容 |
|------|------|
| 2026-09-10 | 初版：连通性测试、30 次采样统计、GPS vs 公共 NTP 对比、Android 同步机制整理 |
| 2026-09-10 | 方案 C 完成（新增 2.4）：发现 GPS 服务器偶发 ~1s 时间戳异常；修正 2.2/2.3 跳变归因；暂缓方案 A；新增待办 D/E |
| 2026-09-10 | 方案 E 完成（新增 2.5）：30 分钟监测，异常率 51.3%（27 段 episode），幅度从 ~1s 恶化至 3~4s，判定服务器不可用；新增管理员排查建议；方案 A 取消，新增公共 NTP 校时建议与修复后复测流程 |
| 2026-09-10 | 新增 2.6 节：固件开源代码（Acode_test@esp32c3-status-leds-5075）病因定位——NMEA/PPS 秒对齐竞态（-1s）+ 主循环阻塞致 NMEA 丢帧（多秒失步）；本地分支 fix/time-alignment 完成 3 项修复（PPS 计数对齐 / 元数据诚实化 / 主循环去阻塞），补丁见 patches/，待烧录复测 |
| 2026-09-10 | 新增 2.7 节：RTOS 三任务化（feat/rtos-tasks 分支）——time(5)/net(2)/ui(1) 抢占式隔离 + portMUX 快照 + 队列请求 + 统一设置锁；补丁 0004；与 2.6 可 A/B 对比烧录 |
| 2026-09-10 | 新增 2.8 节：分支交付与推送状态——本机直推不可行（github.com 超时 + 无凭证 + 代理只读），生成 bundle 与 4 个 patch 供外部推送，待办 I 跟踪 |
