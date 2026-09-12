#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

WifiManager* WifiManager::instance_ = nullptr;

void WifiManager::setEventBit(WifiEvtBits bit) {
  portENTER_CRITICAL(&evtMux_);
  evtFlags_ |= static_cast<uint32_t>(bit);
  portEXIT_CRITICAL(&evtMux_);
}

bool WifiManager::takeEventBit(WifiEvtBits bit) {
  const uint32_t mask = static_cast<uint32_t>(bit);
  portENTER_CRITICAL(&evtMux_);
  const bool had = (evtFlags_ & mask) != 0;
  evtFlags_ &= ~mask;
  portEXIT_CRITICAL(&evtMux_);
  return had;
}

void WifiManager::onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  WifiManager* self = instance_;
  if (self == nullptr) {
    return;
  }

  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[wifi-evt] STA_CONNECTED");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      const uint32_t ip = info.got_ip.ip_info.ip.addr;
      Serial.printf("[wifi-evt] GOT_IP %u.%u.%u.%u\n", ip & 0xffu, (ip >> 8) & 0xffu,
                    (ip >> 16) & 0xffu, (ip >> 24) & 0xffu);
      self->setEventBit(WifiEvtBits::GotIp);
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      const uint16_t reason = info.wifi_sta_disconnected.reason;
      self->lastDiscReason_ = reason;
      Serial.printf("[wifi-evt] DISCONNECTED reason=%u\n", reason);
      self->setEventBit(WifiEvtBits::Disc);
      break;
    }
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      Serial.printf("[wifi-evt] SCAN_DONE status=%u num=%u\n", info.wifi_scan_done.status,
                    info.wifi_scan_done.number);
      self->setEventBit(WifiEvtBits::ScanDone);
      break;
    default:
      break;
  }
}

void WifiManager::begin() {
  instance_ = this;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32c3-ntp");
  WiFi.onEvent(onWifiEvent);
  Serial.println("[wifi] event handler registered");
}

uint32_t WifiManager::backoffMsForAttempt(uint8_t attempt) {
  switch (attempt) {
    case 0:
      return WIFI_RECONNECT_BACKOFF_0_MS;
    case 1:
      return WIFI_RECONNECT_BACKOFF_1_MS;
    case 2:
      return WIFI_RECONNECT_BACKOFF_2_MS;
    case 3:
      return WIFI_RECONNECT_BACKOFF_3_MS;
    default:
      return WIFI_RECONNECT_BACKOFF_4_MS;
  }
}

void WifiManager::cancelAutoReconnect() {
  reconnectArmed_ = false;
  reconnectAttempt_ = 0;
  reconnectNextMs_ = 0;
  reconnectWindowStartMs_ = 0;
}

void WifiManager::armReconnect(const AppSettings& settings, uint32_t firstDelayMs) {
  if (settings.wifiSsid.isEmpty()) {
    return;
  }
  reconnectSettings_ = settings;
  reconnectArmed_ = true;
  reconnectAttempt_ = 0;
  reconnectWindowStartMs_ = millis();
  reconnectNextMs_ = millis() + firstDelayMs;
  Serial.printf("[wifi] reconnect armed ssid=\"%s\" delay=%ums\n", settings.wifiSsid.c_str(),
                static_cast<unsigned>(firstDelayMs));
}

bool WifiManager::consumeReconnectGiveUp() {
  if (!reconnectGaveUp_) {
    return false;
  }
  reconnectGaveUp_ = false;
  return true;
}

bool WifiManager::beginConnect(const AppSettings& settings) {
  if (settings.wifiSsid.isEmpty()) {
    return false;
  }
  if (isScanRunning() || isProbeRunning()) {
    return false;
  }
  if (isConnecting()) {
    return false;
  }

  // Manual connect cancels pending reconnect schedule (fresh attempt).
  cancelAutoReconnect();
  reconnectSettings_ = settings;

  takeEventBit(WifiEvtBits::GotIp);
  takeEventBit(WifiEvtBits::Disc);

  // SoftAP-only setup: bring STA up alongside AP when user joins a network.
  // Reconnect / normal join without portal: STA only.
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.disconnect(false);
    delay(50);
    WiFi.mode(WIFI_STA);
    delay(50);
  }
  WiFi.setSleep(false);
#if defined(WIFI_ALL_CHANNEL_SCAN)
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#endif
  if (settings.useStaticIp) {
    if (!WiFi.config(settings.staticIp, settings.gateway, settings.subnet, settings.dns)) {
      Serial.println("WiFi.config failed");
    }
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  Serial.printf("WiFi connecting ssid=\"%s\"\n", settings.wifiSsid.c_str());
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  connectDeadlineMs_ = millis() + WIFI_CONNECT_TIMEOUT_MS;
  lastBeginMs_ = millis();
  connectState_ = WifiConnectState::Connecting;
  expectLink_ = true;
  return true;
}

WifiConnectState WifiManager::pollConnect() {
  if (connectState_ != WifiConnectState::Connecting) {
    return connectState_;
  }

  if (takeEventBit(WifiEvtBits::GotIp)) {
    connectState_ = WifiConnectState::Connected;
    cancelAutoReconnect();
    Serial.println();
    Serial.printf("STA GOT_IP: %s\n", WiFi.localIP().toString().c_str());
    return connectState_;
  }

  if (takeEventBit(WifiEvtBits::Disc)) {
    // Cold boot often gets NO_AP_FOUND (201) before the first full scan finishes.
    // Keep trying until the connect deadline instead of aborting into SoftAP.
    if (lastDiscReason_ == 201 || lastDiscReason_ == 200) {
      if (static_cast<int32_t>(millis() - connectDeadlineMs_) >= 0) {
        connectState_ = WifiConnectState::Failed;
        expectLink_ = false;
        Serial.println();
        Serial.printf("WiFi connect failed (no AP, reason=%u)\n", lastDiscReason_);
        return connectState_;
      }
      if (static_cast<int32_t>(millis() - lastBeginMs_) >= static_cast<int32_t>(WIFI_NO_AP_REBEGIN_MS)) {
        Serial.printf("\n[wifi] no AP yet (reason=%u) — re-begin\n", lastDiscReason_);
        WiFi.disconnect(false);
        delay(20);
        WiFi.begin(reconnectSettings_.wifiSsid.c_str(), reconnectSettings_.wifiPass.c_str());
        lastBeginMs_ = millis();
      }
      return WifiConnectState::Connecting;
    }

    connectState_ = WifiConnectState::Failed;
    expectLink_ = false;
    Serial.println();
    Serial.printf("WiFi connect aborted (disc reason=%u)\n", lastDiscReason_);
    return connectState_;
  }

  // Fallback: associated + has IP (covers static IP if GOT_IP was missed).
  if (WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0) {
    connectState_ = WifiConnectState::Connected;
    cancelAutoReconnect();
    Serial.println();
    Serial.printf("STA connected (poll): %s\n", WiFi.localIP().toString().c_str());
    return connectState_;
  }

  if (static_cast<int32_t>(millis() - connectDeadlineMs_) >= 0) {
    connectState_ = WifiConnectState::Failed;
    expectLink_ = false;
    Serial.println();
    Serial.println("WiFi connect timeout");
    return connectState_;
  }

  // Periodic re-begin even if Disc bit was coalesced.
  if (!reconnectSettings_.wifiSsid.isEmpty() &&
      static_cast<int32_t>(millis() - lastBeginMs_) >= static_cast<int32_t>(WIFI_NO_AP_REBEGIN_MS) &&
      WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[wifi] still joining — re-begin");
    WiFi.disconnect(false);
    delay(20);
    WiFi.begin(reconnectSettings_.wifiSsid.c_str(), reconnectSettings_.wifiPass.c_str());
    lastBeginMs_ = millis();
  }

  return WifiConnectState::Connecting;
}

bool WifiManager::consumeDisconnect(uint16_t* reasonOut) {
  if (!takeEventBit(WifiEvtBits::Disc)) {
    return false;
  }
  if (reasonOut != nullptr) {
    *reasonOut = lastDiscReason_;
  }
  // Connecting / Failed disc is consumed in pollConnect; ignore here.
  if (connectState_ == WifiConnectState::Connecting) {
    return false;
  }
  // Only arm after a real STA link loss — not after failed join (avoids killing SoftAP).
  if (connectState_ != WifiConnectState::Connected) {
    expectLink_ = false;
    return false;
  }

  connectState_ = WifiConnectState::Idle;
  expectLink_ = false;
  if (autoReconnect_ && !reconnectSettings_.wifiSsid.isEmpty()) {
    reconnectArmed_ = true;
    reconnectAttempt_ = 0;
    reconnectWindowStartMs_ = millis();
    reconnectNextMs_ = millis();  // attempt 0 immediate
    Serial.println("[wifi] disconnect → auto-reconnect armed");
  }
  return true;
}

bool WifiManager::pollAutoReconnect(AppSettings* outSettings) {
  if (!autoReconnect_ || !reconnectArmed_) {
    return false;
  }
  if (isBusy()) {
    return false;
  }
  if (reconnectSettings_.wifiSsid.isEmpty()) {
    cancelAutoReconnect();
    return false;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - reconnectWindowStartMs_) >= static_cast<int32_t>(WIFI_RECONNECT_GIVEUP_MS) ||
      reconnectAttempt_ >= WIFI_RECONNECT_MAX_ATTEMPTS) {
    Serial.println("[wifi] auto-reconnect give up");
    cancelAutoReconnect();
    reconnectGaveUp_ = true;
    return false;
  }

  if (static_cast<int32_t>(now - reconnectNextMs_) < 0) {
    return false;
  }

  if (outSettings != nullptr) {
    *outSettings = reconnectSettings_;
  }

  // beginConnect clears reconnectArmed_ via cancelAutoReconnect — re-arm after.
  const uint8_t attempt = reconnectAttempt_;
  const AppSettings creds = reconnectSettings_;
  const uint32_t windowStart = reconnectWindowStartMs_;
  if (!beginConnect(creds)) {
    return false;
  }
  reconnectArmed_ = true;
  reconnectAttempt_ = static_cast<uint8_t>(attempt + 1);
  reconnectWindowStartMs_ = windowStart;
  reconnectNextMs_ = now + backoffMsForAttempt(reconnectAttempt_);
  Serial.printf("[wifi] auto-reconnect attempt %u\n", attempt + 1);
  return true;
}

void WifiManager::startSetupAp() {
  // SoftAP escape hatch: pure AP mode. AP_STA + leftover STA scans often makes
  // the beacon invisible to phones/PCs even though softAP() returns success.
  cancelAutoReconnect();
  connectState_ = WifiConnectState::Idle;
  expectLink_ = false;
  takeEventBit(WifiEvtBits::GotIp);
  takeEventBit(WifiEvtBits::Disc);

  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(100);

  String ssid = String(AP_SSID_PREFIX) + "-" + String((uint32_t)ESP.getEfuseMac() & 0xFFFF, HEX);
  ssid.toUpperCase();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  // Fixed channel so beacon is stable (STA scanning would otherwise hop).
  const bool ok = WiFi.softAP(ssid.c_str(), AP_PASSWORD, /*channel=*/6, /*ssid_hidden=*/0, /*max_connection=*/4);
  Serial.printf("Setup AP: %s / %s  IP=%s ch=6 ok=%d mode=%d\n", ssid.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str(), ok ? 1 : 0, static_cast<int>(WiFi.getMode()));
}

void WifiManager::stopAp() {
  if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_OFF) {
    return;
  }
  WiFi.softAPdisconnect(true);
  delay(50);
  WiFi.mode(WIFI_STA);
  Serial.println("[wifi] SoftAP stopped (STA only)");
}

bool WifiManager::isStaConnected() const {
  return WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0;
}

IPAddress WifiManager::localIp() const {
  if (isStaConnected()) {
    return WiFi.localIP();
  }
  return WiFi.softAPIP();
}

String WifiManager::macAddress() const {
  return WiFi.macAddress();
}

bool WifiManager::startScan() {
  if (isConnecting() || isProbeRunning()) {
    return false;
  }
  if (scanState_ == WifiScanState::Running) {
    return true;
  }

  // Do not cancelAutoReconnect — scan must not abort STA recovery.
  takeEventBit(WifiEvtBits::ScanDone);

  const int16_t r = WiFi.scanNetworks(/*async=*/true, /*hidden=*/false);
  if (r == WIFI_SCAN_FAILED) {
    scanState_ = WifiScanState::Failed;
    return false;
  }
  scanState_ = WifiScanState::Running;
  scanStartedMs_ = millis();
  return true;
}

void WifiManager::harvestScanResults() {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  if (n < 0) {
    lastScan_.clear();
    scanState_ = WifiScanState::Failed;
    WiFi.scanDelete();
    return;
  }

  lastScan_.clear();
  lastScan_.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    WifiNetwork net;
    net.ssid = WiFi.SSID(i);
    net.rssi = WiFi.RSSI(i);
    net.enc = WiFi.encryptionType(i);
    if (net.ssid.length() > 0) {
      lastScan_.push_back(net);
    }
  }
  WiFi.scanDelete();
  scanState_ = WifiScanState::Done;
}

WifiScanState WifiManager::pollScan(std::vector<WifiNetwork>* out) {
  if (scanState_ == WifiScanState::Done) {
    if (out != nullptr) {
      *out = lastScan_;
    }
    return scanState_;
  }
  if (scanState_ != WifiScanState::Running) {
    return scanState_;
  }

  // Prefer SCAN_DONE; also poll complete as dual insurance.
  const bool doneEvt = takeEventBit(WifiEvtBits::ScanDone);
  const int16_t n = WiFi.scanComplete();
  if (!doneEvt && n == WIFI_SCAN_RUNNING) {
    if (static_cast<int32_t>(millis() - scanStartedMs_) >= static_cast<int32_t>(WIFI_SCAN_TIMEOUT_MS)) {
      Serial.println("[wifi] scan timeout — abort");
      WiFi.scanDelete();
      lastScan_.clear();
      scanState_ = WifiScanState::Failed;
      return scanState_;
    }
    return WifiScanState::Running;
  }

  harvestScanResults();
  if (out != nullptr && scanState_ == WifiScanState::Done) {
    *out = lastScan_;
  }
  return scanState_;
}

bool WifiManager::beginConflictProbe(const IPAddress& ip) {
  if (isConnecting() || isScanRunning() || isProbeRunning()) {
    return false;
  }
  if (!isStaConnected()) {
    probeState_ = WifiProbeState::Failed;
    return false;
  }
  if (ip == WiFi.localIP()) {
    probeState_ = WifiProbeState::Clear;
    return true;
  }

  ip4_addr_t addr;
  IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
  probeNetif_ = netif_default;
  if (probeNetif_ == nullptr) {
    probeState_ = WifiProbeState::Failed;
    return false;
  }

  etharp_cleanup_netif(probeNetif_);
  if (etharp_request(probeNetif_, &addr) != ERR_OK) {
    probeState_ = WifiProbeState::Failed;
    return false;
  }

  probeIp_ = ip;
  probeDeadlineMs_ = millis() + IP_CONFLICT_TIMEOUT_MS;
  probeState_ = WifiProbeState::Running;
  return true;
}

WifiProbeState WifiManager::pollConflictProbe() {
  if (probeState_ != WifiProbeState::Running) {
    return probeState_;
  }

  ip4_addr_t addr;
  IP4_ADDR(&addr, probeIp_[0], probeIp_[1], probeIp_[2], probeIp_[3]);
  struct eth_addr* eth_ret = nullptr;
  const ip4_addr_t* ip_ret = nullptr;
  if (probeNetif_ != nullptr && etharp_find_addr(probeNetif_, &addr, &eth_ret, &ip_ret) >= 0) {
    probeState_ = WifiProbeState::Conflict;
    return probeState_;
  }

  if (static_cast<int32_t>(millis() - probeDeadlineMs_) >= 0) {
    probeState_ = WifiProbeState::Clear;
    return probeState_;
  }

  return WifiProbeState::Running;
}
