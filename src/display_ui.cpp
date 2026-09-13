#include "display_ui.h"
#include "app_ipc.h"
#include <Wire.h>
#include <WiFi.h>

static const char* MENU_LABELS[] = {
    "WiFi Scan",
    "Web Setup",
    "Set Static IP",
    "Use DHCP",
    "Timezone",
    "Anomaly Mode",
    "Restart",
};

static const char PWD_CHARS[] =
    "<ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%&*-_.";

void DisplayUi::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  delay(250);  // SH1107 power-up settle
  if (!display_.begin(OLED_I2C_ADDR, true)) {
    Serial.println("SH1107 init failed");
  }
  display_.setRotation(OLED_ROTATION);
  display_.clearDisplay();
  display_.setTextColor(SH110X_WHITE);
  display_.setTextSize(1);
  display_.setCursor(0, 0);
  display_.println("ESP32-C3 NTP");
  display_.println("Booting...");
  display_.display();
}

void DisplayUi::showMessage(const String& msg) {
  message_ = msg;
  messageUntil_ = millis() + 2500;
  mode_ = UiMode::Message;
}

void DisplayUi::onScanResults(const std::vector<WifiNetwork>& nets) {
  networks_ = nets;
  wifiIndex_ = 0;
  scanPending_ = false;
  mode_ = UiMode::WifiScan;
  messageUntil_ = 0;
}

void DisplayUi::drainUiMessages() {
  if (!gIpc.uiMsg) {
    return;
  }
  UiMsg msg;
  while (xQueueReceive(gIpc.uiMsg, &msg, 0) == pdTRUE) {
    if (msg.type == UiMsgType::Text) {
      showMessage(String(msg.text));
    } else if (msg.type == UiMsgType::ScanResult) {
      if (xSemaphoreTake(gIpc.scanMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        onScanResults(gIpc.scanResults);
        gIpc.scanReady = false;
        xSemaphoreGive(gIpc.scanMutex);
      }
    } else if (msg.type == UiMsgType::ScanFailed) {
      scanPending_ = false;
      showMessage("Scan failed");
    }
  }
}

void DisplayUi::loop(EncoderInput& enc, GpsService& gps, WifiManager& wifi) {
  drainUiMessages();

  int8_t rot = enc.consumeRotate();
  bool click = enc.consumeClick();
  bool longPress = enc.consumeLongPress();

  switch (mode_) {
    case UiMode::Home:
      handleHome(rot, click);
      break;
    case UiMode::Menu:
      handleMenu(rot, click, longPress);
      break;
    case UiMode::WifiScan:
      handleWifiScan(rot, click);
      break;
    case UiMode::WifiPassword:
      handlePassword(rot, click, longPress);
      break;
    case UiMode::SetIp:
      handleSetIp(rot, click, longPress);
      break;
    case UiMode::SetTimezone:
      handleTimezone(rot, click);
      break;
    case UiMode::SetAnomaly:
      handleAnomaly(rot, click, longPress);
      break;
    case UiMode::WebSetupHint:
      if (click || longPress) {
        mode_ = UiMode::Home;
      }
      break;
    case UiMode::Message:
      if (millis() > messageUntil_ || click) {
        mode_ = UiMode::Home;
      }
      break;
  }

  if (millis() - lastDrawMs_ < DISPLAY_REFRESH_MS && rot == 0 && !click && !longPress) {
    return;
  }
  lastDrawMs_ = millis();

  AppSettings settings;
  if (settingsLock(pdMS_TO_TICKS(20))) {
    settings = gSettings;
    settingsUnlock();
  }

  const GpsStatus st = gps.snapshot();

  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextColor(SH110X_WHITE);
  switch (mode_) {
    case UiMode::Home:
      drawHome(st, wifi, settings);
      break;
    case UiMode::Menu:
      drawMenu();
      break;
    case UiMode::WifiScan:
      drawWifiScan();
      break;
    case UiMode::WifiPassword:
      drawPassword();
      break;
    case UiMode::SetIp:
      drawSetIp();
      break;
    case UiMode::SetTimezone:
      drawTimezone(settings);
      break;
    case UiMode::SetAnomaly:
      drawAnomaly(settings);
      break;
    case UiMode::WebSetupHint:
      drawWebHint();
      break;
    case UiMode::Message:
      drawMessage();
      break;
  }
  display_.display();
}

void DisplayUi::drawHome(const GpsStatus& st, const WifiManager& wifi, const AppSettings& settings) {
  // Scheme A (128×64): top status chips · large time · SSID · IP+SYNC
  // Built-in font: size1 = 6×8, size2 = 12×16.

  static const uint8_t kIconSat[] PROGMEM = {
      0b00011000, 0b00111100, 0b01100110, 0b11011011,
      0b01100110, 0b00111100, 0b00011000, 0b00100100,
  };
  static const uint8_t kIconWifi[] PROGMEM = {
      0b00000000, 0b00111000, 0b01000100, 0b10000010,
      0b00111000, 0b01000100, 0b00010000, 0b00010000,
  };

  const bool sta = wifi.isStaConnected();
  const bool ap = gIpc.setupAp;

  // --- Top bar (y=0..8) ---
  display_.drawBitmap(0, 0, kIconSat, 8, 8, SH110X_WHITE);
  display_.setTextSize(1);
  display_.setCursor(10, 0);
  char left[20];
  snprintf(left, sizeof(left), "%u %s", static_cast<unsigned>(st.satellites),
           clockStateLabel(st.clockState));
  display_.print(left);

  display_.drawBitmap(80, 0, kIconWifi, 8, 8, SH110X_WHITE);
  char right[14];
  if (sta) {
    snprintf(right, sizeof(right), "%d", static_cast<int>(WiFi.RSSI()));
  } else if (ap) {
    snprintf(right, sizeof(right), "AP");
  } else if (!settings.wifiSsid.isEmpty()) {
    snprintf(right, sizeof(right), "JOIN");
  } else {
    snprintf(right, sizeof(right), "--");
  }
  const int16_t rightW = static_cast<int16_t>(strlen(right) * 6);
  display_.setCursor(128 - rightW, 0);
  display_.print(right);

  // Thin separator under status chips
  display_.drawFastHLine(0, 10, 128, SH110X_WHITE);

  // --- Large local time (primary) ---
  char timeBuf[9];
  if (st.utcEpoch > 0) {
    time_t local = static_cast<time_t>(st.utcEpoch) + settings.timezoneHours * 3600L;
    struct tm tmv = {};
    gmtime_r(&local, &tmv);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "--:--:--");
  }
  display_.setTextSize(2);
  // 8 glyphs × 12 px = 96; center on 128
  display_.setCursor(16, 16);
  display_.print(timeBuf);

  // --- SSID (secondary) ---
  display_.setTextSize(1);
  String ssid;
  if (sta) {
    ssid = WiFi.SSID();
    if (ssid.isEmpty()) {
      ssid = settings.wifiSsid;
    }
  } else if (ap) {
    ssid = WiFi.softAPSSID();
    if (ssid.isEmpty()) {
      ssid = String(AP_SSID_PREFIX) + "-****";
    }
  } else if (!settings.wifiSsid.isEmpty()) {
    ssid = settings.wifiSsid;
  } else {
    ssid = "(no WiFi)";
  }
  // Max ~21 chars at size1; keep 20 + NUL
  char ssidLine[21];
  const size_t n = ssid.length();
  if (n <= 20) {
    memcpy(ssidLine, ssid.c_str(), n);
    ssidLine[n] = '\0';
  } else {
    memcpy(ssidLine, ssid.c_str(), 17);
    ssidLine[17] = '.';
    ssidLine[18] = '.';
    ssidLine[19] = '.';
    ssidLine[20] = '\0';
  }
  display_.setCursor(0, 38);
  display_.print(ssidLine);

  // --- IP + SYNC ---
  display_.setCursor(0, 52);
  if (sta) {
    display_.print(wifi.localIp());
  } else if (ap) {
    display_.print(WiFi.softAPIP());
  } else {
    display_.print("--.--.--.--");
  }

  const char* sync = st.timeValid ? "SYNC" : "WAIT";
  display_.setCursor(128 - static_cast<int16_t>(strlen(sync) * 6), 52);
  display_.print(sync);
}

void DisplayUi::drawMenu() {
  display_.setCursor(0, 0);
  display_.println("Menu  long=Back");
  const uint8_t count = static_cast<uint8_t>(MenuItem::Count);
  const uint8_t visible = 5;
  uint8_t start = 0;
  if (menuIndex_ >= visible) {
    start = menuIndex_ - visible + 1;
  }
  for (uint8_t row = 0; row < visible; ++row) {
    const uint8_t i = static_cast<uint8_t>(start + row);
    if (i >= count) {
      break;
    }
    display_.setCursor(0, 12 + row * 10);
    display_.print(i == menuIndex_ ? ">" : " ");
    display_.print(MENU_LABELS[i]);
  }
}

void DisplayUi::drawWifiScan() {
  display_.setCursor(0, 0);
  display_.println("WiFi list click=OK");
  if (scanPending_) {
    display_.setCursor(0, 20);
    display_.println("Scanning...");
    return;
  }
  if (networks_.empty()) {
    display_.setCursor(0, 20);
    display_.println("Empty. Click scan");
    return;
  }
  const int visible = 4;
  int start = max(0, static_cast<int>(wifiIndex_) - visible + 1);
  for (int row = 0; row < visible; ++row) {
    int idx = start + row;
    if (idx >= static_cast<int>(networks_.size())) {
      break;
    }
    display_.setCursor(0, 12 + row * 12);
    display_.print(idx == wifiIndex_ ? ">" : " ");
    String line = networks_[idx].ssid;
    if (line.length() > 16) {
      line = line.substring(0, 16);
    }
    display_.print(line);
  }
}

void DisplayUi::drawPassword() {
  display_.setCursor(0, 0);
  display_.println("Password");
  display_.setCursor(0, 12);
  display_.print("SSID:");
  String s = pendingSsid_;
  if (s.length() > 14) {
    s = s.substring(0, 14);
  }
  display_.println(s);

  display_.setCursor(0, 28);
  display_.print("PWD:");
  display_.println(password_);

  display_.setCursor(0, 44);
  display_.print("Char:[");
  display_.print(PWD_CHARS[pwdCursor_]);
  display_.print("] rot=chg");
  display_.setCursor(0, 56);
  display_.print("click=add long=OK");
}

void DisplayUi::drawSetIp() {
  display_.setCursor(0, 0);
  display_.println("Static IP edit");
  for (uint8_t i = 0; i < 4; ++i) {
    display_.setCursor(i * 32, 24);
    if (i == ipOctet_) {
      display_.print('[');
    }
    display_.print(editIp_[i]);
    if (i == ipOctet_) {
      display_.print(']');
    }
    if (i < 3) {
      display_.setCursor(i * 32 + 26, 24);
      display_.print('.');
    }
  }
  display_.setCursor(0, 48);
  display_.print("rot=val click=next");
  display_.setCursor(0, 56);
  display_.print("long=save+check");
}

void DisplayUi::drawTimezone(const AppSettings& settings) {
  display_.setCursor(0, 0);
  display_.println("Timezone UTC offset");
  display_.setCursor(0, 24);
  display_.print("UTC");
  if (settings.timezoneHours >= 0) {
    display_.print("+");
  }
  display_.print(settings.timezoneHours);
  display_.setCursor(0, 48);
  display_.print("rot=chg click=save");
}

void DisplayUi::drawAnomaly(const AppSettings& settings) {
  (void)settings;
  display_.setCursor(0, 0);
  display_.println("Anomaly Mode");
  display_.setCursor(0, 16);
  display_.print(">");
  display_.println(anomalyPolicyMenuLabel(editPolicy_));
  display_.setCursor(0, 40);
  display_.println("rot=chg click=save");
  display_.setCursor(0, 52);
  display_.println("long=back");
}

void DisplayUi::drawMessage() {
  display_.setCursor(0, 20);
  display_.println(message_);
}

void DisplayUi::drawWebHint() {
  display_.setCursor(0, 0);
  display_.println("Web WiFi Setup");
  display_.setCursor(0, 16);
  display_.println("Join AP NTP-Setup-*");
  display_.setCursor(0, 28);
  display_.print("Pass: ");
  display_.println(AP_PASSWORD);
  display_.setCursor(0, 40);
  display_.print("Open http://");
  display_.println(WiFi.softAPIP());
  display_.setCursor(0, 56);
  display_.print("click=back");
}

void DisplayUi::handleHome(int8_t rot, bool click) {
  (void)rot;
  if (click) {
    mode_ = UiMode::Menu;
    menuIndex_ = 0;
  }
}

void DisplayUi::handleMenu(int8_t rot, bool click, bool longPress) {
  if (longPress) {
    mode_ = UiMode::Home;
    return;
  }
  if (rot > 0) {
    menuIndex_ = (menuIndex_ + 1) % static_cast<uint8_t>(MenuItem::Count);
  } else if (rot < 0) {
    menuIndex_ = (menuIndex_ + static_cast<uint8_t>(MenuItem::Count) - 1) %
                 static_cast<uint8_t>(MenuItem::Count);
  }
  if (!click) {
    return;
  }
  switch (static_cast<MenuItem>(menuIndex_)) {
    case MenuItem::WifiScan:
      networks_.clear();
      wifiIndex_ = 0;
      scanPending_ = false;
      mode_ = UiMode::WifiScan;
      break;
    case MenuItem::WebSetup: {
      NetRequest req;
      req.type = NetReqType::StartWebSetup;
      postNetRequest(req);
      mode_ = UiMode::WebSetupHint;
      break;
    }
    case MenuItem::SetStaticIp:
      if (settingsLock(pdMS_TO_TICKS(50))) {
        editIp_ = gSettings.staticIp;
        settingsUnlock();
      }
      ipOctet_ = 0;
      mode_ = UiMode::SetIp;
      break;
    case MenuItem::UseDhcp: {
      NetRequest req;
      req.type = NetReqType::UseDhcp;
      postNetRequest(req);
      showMessage("DHCP enabled");
      break;
    }
    case MenuItem::Timezone:
      mode_ = UiMode::SetTimezone;
      break;
    case MenuItem::AnomalyMode:
      if (settingsLock(pdMS_TO_TICKS(50))) {
        editPolicy_ = gSettings.anomalyPolicy;
        settingsUnlock();
      }
      mode_ = UiMode::SetAnomaly;
      break;
    case MenuItem::Restart:
      ESP.restart();
      break;
    default:
      break;
  }
}

void DisplayUi::handleWifiScan(int8_t rot, bool click) {
  if (networks_.empty() && !scanPending_) {
    if (click) {
      NetRequest req;
      req.type = NetReqType::ScanWifi;
      if (postNetRequest(req)) {
        scanPending_ = true;
        showMessage("Scanning...");
      } else {
        showMessage("Scan busy");
      }
    }
    return;
  }
  if (scanPending_) {
    return;
  }
  if (rot > 0 && wifiIndex_ + 1 < networks_.size()) {
    wifiIndex_++;
  } else if (rot < 0 && wifiIndex_ > 0) {
    wifiIndex_--;
  }
  if (click && !networks_.empty()) {
    pendingSsid_ = networks_[wifiIndex_].ssid;
    password_ = "";
    pwdCursor_ = 0;
    mode_ = UiMode::WifiPassword;
  }
}

void DisplayUi::handlePassword(int8_t rot, bool click, bool longPress) {
  size_t n = sizeof(PWD_CHARS) - 1;
  if (rot > 0) {
    pwdCursor_ = (pwdCursor_ + 1) % n;
  } else if (rot < 0) {
    pwdCursor_ = (pwdCursor_ + n - 1) % n;
  }
  if (click) {
    char c = PWD_CHARS[pwdCursor_];
    if (c == '<') {
      if (!password_.isEmpty()) {
        password_.remove(password_.length() - 1);
      }
    } else if (password_.length() < 63) {
      password_ += c;
    }
  }
  if (longPress) {
    NetRequest req;
    req.type = NetReqType::ConnectWifi;
    strncpy(req.ssid, pendingSsid_.c_str(), sizeof(req.ssid) - 1);
    strncpy(req.pass, password_.c_str(), sizeof(req.pass) - 1);
    postNetRequest(req);
    showMessage("Connecting...");
  }
}

void DisplayUi::handleSetIp(int8_t rot, bool click, bool longPress) {
  if (rot != 0) {
    int v = editIp_[ipOctet_] + rot;
    if (v < 0) {
      v = 255;
    }
    if (v > 255) {
      v = 0;
    }
    editIp_[ipOctet_] = static_cast<uint8_t>(v);
  }
  if (click) {
    ipOctet_ = (ipOctet_ + 1) % 4;
  }
  if (longPress) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.staticIp = editIp_;
      gSettings.useStaticIp = true;
      // Keep existing gateway when still on the same /24; otherwise default to .1.
      IPAddress gw = gSettings.gateway;
      if (gw[0] != editIp_[0] || gw[1] != editIp_[1] || gw[2] != editIp_[2]) {
        gw = IPAddress(editIp_[0], editIp_[1], editIp_[2], 1);
      }
      gSettings.gateway = gw;
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
    }
    NetRequest req;
    req.type = NetReqType::ApplyStaticIp;
    req.staticIp = editIp_;
    postNetRequest(req);
    showMessage("Checking IP...");
  }
}

void DisplayUi::handleTimezone(int8_t rot, bool click) {
  if (!settingsLock(pdMS_TO_TICKS(50))) {
    return;
  }
  if (rot != 0) {
    int v = gSettings.timezoneHours + rot;
    if (v < -12) {
      v = 14;
    }
    if (v > 14) {
      v = -12;
    }
    gSettings.timezoneHours = static_cast<int8_t>(v);
  }
  if (click) {
    AppSettings copy = gSettings;
    settingsUnlock();
    gStore.save(copy);
    showMessage("TZ saved");
    return;
  }
  settingsUnlock();
}

void DisplayUi::handleAnomaly(int8_t rot, bool click, bool longPress) {
  if (longPress) {
    mode_ = UiMode::Home;
    return;
  }
  if (rot != 0) {
    int v = static_cast<int>(editPolicy_) + (rot > 0 ? 1 : -1);
    if (v < 0) {
      v = static_cast<int>(AnomalyPolicy::HoldoverLong);
    }
    if (v > static_cast<int>(AnomalyPolicy::HoldoverLong)) {
      v = 0;
    }
    editPolicy_ = static_cast<AnomalyPolicy>(v);
  }
  if (click) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.anomalyPolicy = editPolicy_;
      const uint16_t defHold = anomalyPolicyDefaultHoldoverSec(editPolicy_);
      if (defHold > 0) {
        gSettings.holdoverSec = defHold;
      }
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
    }
    showMessage(String("A:") + anomalyPolicyShortLabel(editPolicy_));
  }
}
