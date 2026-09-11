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
  if (!display_.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("SSD1306 init failed");
  }
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
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
  display_.setTextColor(SSD1306_WHITE);
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
  display_.setCursor(0, 0);
  display_.println("ESP32-C3 NTP Srv");

  display_.setCursor(0, 12);
  if (wifi.isStaConnected()) {
    display_.print("IP ");
    display_.println(wifi.localIp());
  } else {
    display_.println("IP (no STA)");
  }

  display_.setCursor(0, 24);
  display_.print("GPS ");
  if (st.validFix) {
    display_.print("LOCK ");
    display_.print(st.satellites);
    display_.print("s");
  } else {
    display_.print("SEARCH ");
    display_.print(st.satellites);
  }

  display_.setCursor(0, 36);
  display_.print(clockStateLabel(st.clockState));
  display_.print(" R");
  display_.print(st.residualMs);
  display_.print(" A:");
  display_.print(anomalyPolicyShortLabel(settings.anomalyPolicy));

  display_.setCursor(0, 48);
  if (st.utcEpoch > 0) {
    time_t local = static_cast<time_t>(st.utcEpoch) + settings.timezoneHours * 3600L;
    struct tm tmv = {};
    gmtime_r(&local, &tmv);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    display_.print(buf);
  } else {
    display_.print("--:--:--");
  }
  display_.setCursor(72, 48);
  display_.print(st.timeValid ? "SYNC" : "WAIT");
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
