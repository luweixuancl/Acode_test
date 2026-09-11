#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <vector>
#include "settings.h"

struct WifiNetwork {
  String ssid;
  int32_t rssi = 0;
  wifi_auth_mode_t enc = WIFI_AUTH_OPEN;
};

enum class WifiConnectState : uint8_t {
  Idle = 0,
  Connecting = 1,
  Connected = 2,
  Failed = 3,
};

enum class WifiScanState : uint8_t {
  Idle = 0,
  Running = 1,
  Done = 2,
  Failed = 3,
};

enum class WifiProbeState : uint8_t {
  Idle = 0,
  Running = 1,
  Conflict = 2,
  Clear = 3,
  Failed = 4,
};

enum class WifiEvtBits : uint32_t {
  GotIp = 1u << 0,
  Disc = 1u << 1,
  ScanDone = 1u << 2,
};

class WifiManager {
 public:
  // Init radio + WiFi.onEvent (callback only sets flags / logs).
  void begin();

  // Non-blocking STA connect.
  bool beginConnect(const AppSettings& settings);
  WifiConnectState pollConnect();
  bool isConnecting() const { return connectState_ == WifiConnectState::Connecting; }
  bool isConnectedState() const { return connectState_ == WifiConnectState::Connected; }

  void startSetupAp();
  void stopAp();
  bool isStaConnected() const;
  IPAddress localIp() const;
  String macAddress() const;

  // Non-blocking scan (completion driven by SCAN_DONE when possible).
  bool startScan();
  bool isScanRunning() const { return scanState_ == WifiScanState::Running; }
  WifiScanState scanState() const { return scanState_; }
  const std::vector<WifiNetwork>& lastScan() const { return lastScan_; }
  WifiScanState pollScan(std::vector<WifiNetwork>* out);

  // Non-blocking ARP conflict probe (requires STA up).
  bool beginConflictProbe(const IPAddress& ip);
  bool isProbeRunning() const { return probeState_ == WifiProbeState::Running; }
  WifiProbeState pollConflictProbe();

  bool isBusy() const {
    return isConnecting() || isScanRunning() || isProbeRunning();
  }

  // Auto-reconnect (credentials remembered from last beginConnect).
  void setAutoReconnect(bool enabled) { autoReconnect_ = enabled; }
  bool autoReconnectEnabled() const { return autoReconnect_; }
  void cancelAutoReconnect();
  // Call from task-net when Idle: may start a reconnect beginConnect.
  bool pollAutoReconnect(AppSettings* outSettings);
  // True once if STA dropped while we considered ourselves connected.
  bool consumeDisconnect(uint16_t* reasonOut = nullptr);
  // True once after auto-reconnect gives up (max attempts / window).
  bool consumeReconnectGiveUp();

 private:
  static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  static WifiManager* instance_;

  void setEventBit(WifiEvtBits bit);
  bool takeEventBit(WifiEvtBits bit);
  void harvestScanResults();
  static uint32_t backoffMsForAttempt(uint8_t attempt);

  portMUX_TYPE evtMux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile uint32_t evtFlags_ = 0;
  volatile uint16_t lastDiscReason_ = 0;

  WifiConnectState connectState_ = WifiConnectState::Idle;
  uint32_t connectDeadlineMs_ = 0;
  bool expectLink_ = false;

  WifiScanState scanState_ = WifiScanState::Idle;
  std::vector<WifiNetwork> lastScan_;

  WifiProbeState probeState_ = WifiProbeState::Idle;
  IPAddress probeIp_;
  uint32_t probeDeadlineMs_ = 0;
  struct netif* probeNetif_ = nullptr;

  bool autoReconnect_ = true;
  AppSettings reconnectSettings_;
  uint8_t reconnectAttempt_ = 0;
  uint32_t reconnectNextMs_ = 0;
  uint32_t reconnectWindowStartMs_ = 0;
  bool reconnectArmed_ = false;
  bool reconnectGaveUp_ = false;
};
