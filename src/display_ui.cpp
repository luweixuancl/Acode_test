#include "display_ui.h"
#include "app_ipc.h"
#include "ntp_server.h"
#include <Wire.h>
#include <WiFi.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>

// FreeSans 9pt = former size-1; 12pt = former size-2. Never setTextSize(2)
// on these fonts (that would be ~24px).
static const char* MENU_LABELS[] = {
    "WiFi Scan",
    "Web Setup",
    "Static IP",
    "Use DHCP",
    "Timezone",
    "Anomaly",
    "NTP ACL",
    "Temp Comp",
    "NTP Stats",
    "Restart",
};

static const char PWD_CHARS[] =
    "<ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%&*-_.";

static void uiFont(Adafruit_SH1107& d, bool large) {
  d.setFont(large ? &FreeSans12pt7b : &FreeSans9pt7b);
  d.setTextSize(1);
  d.setTextWrap(false);
  d.setTextColor(SH110X_WHITE);
}

static int16_t uiBaseline(int16_t top, bool large) {
  return static_cast<int16_t>(top + (large ? 16 : OLED_MENU_BASELINE));
}

static void uiAt(Adafruit_SH1107& d, int16_t x, int16_t top, const char* text, bool large = false) {
  uiFont(d, large);
  d.setCursor(x, uiBaseline(top, large));
  d.print(text);
}

static uint16_t uiTextWidth(Adafruit_SH1107& d, const char* text) {
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  d.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void menuFontBegin(Adafruit_SH1107& d) { uiFont(d, false); }

static void menuFontEnd(Adafruit_SH1107& d) { uiFont(d, false); }

static void menuDrawRow(Adafruit_SH1107& d, int16_t y, bool sel, const char* text) {
  uiFont(d, false);
  if (sel) {
    d.fillRect(0, y, 128, OLED_MENU_BAR_H, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
  } else {
    d.setTextColor(SH110X_WHITE);
  }
  d.setCursor(2, y + OLED_MENU_BASELINE);
  d.print(text);
}

void DisplayUi::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  delay(250);  // SH1107 power-up settle
  if (!display_.begin(OLED_I2C_ADDR, true)) {
    Serial.println("SH1107 init failed");
  }
  display_.setRotation(OLED_ROTATION);
  display_.setTextWrap(false);
  display_.clearDisplay();
  uiAt(display_, 0, 4, "ESP32-C3 NTP");
  uiAt(display_, 0, 24, "Booting...");
  display_.display();
  Serial.printf("[ui] OLED FreeSans 9/12pt rows=%u rowH=%u mark=%s\n",
                static_cast<unsigned>(OLED_MENU_ROWS),
                static_cast<unsigned>(OLED_MENU_ROW_H), OLED_UI_MARK);
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
  scanError_ = nets.empty() ? String("No APs") : String();
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
      // Stay on the scan/password screens; STA scan often emits "WiFi lost"/"OK IP".
      if (mode_ != UiMode::WifiScan && mode_ != UiMode::WifiPassword && !scanPending_) {
        showMessage(String(msg.text));
      }
    } else if (msg.type == UiMsgType::ScanResult) {
      if (msg.seq != 0 && msg.seq != scanSeq_) {
        continue;
      }
      if (xSemaphoreTake(gIpc.scanMutex, pdMS_TO_TICKS(80)) == pdTRUE) {
        onScanResults(gIpc.scanResults);
        gIpc.scanReady = false;
        xSemaphoreGive(gIpc.scanMutex);
      } else if (gIpc.scanReady) {
        scanPending_ = false;
        scanError_ = "list busy";
        mode_ = UiMode::WifiScan;
      }
    } else if (msg.type == UiMsgType::ScanFailed) {
      // Ignore leftover "start fail" from a previous request, or a fail
      // that raced ahead of SCAN_DONE for this generation.
      if (msg.seq != 0 && msg.seq != scanSeq_) {
        continue;
      }
      scanPending_ = false;
      scanError_ = msg.text[0] ? String(msg.text) : String("Scan failed");
      mode_ = UiMode::WifiScan;
    }
  }
}

void DisplayUi::loop(EncoderInput& enc, GpsService& gps, WifiManager& wifi, NtpServer& ntp) {
  drainUiMessages();
  if (scanPending_ && scanStartedMs_ != 0 &&
      static_cast<int32_t>(millis() - scanStartedMs_) >=
          static_cast<int32_t>(WIFI_SCAN_UI_TIMEOUT_MS)) {
    scanPending_ = false;
    if (networks_.empty() && !scanError_.length()) {
      scanError_ = "timeout";
    }
  }
  if (scanPending_ && gIpc.scanReady) {
    if (xSemaphoreTake(gIpc.scanMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      onScanResults(gIpc.scanResults);
      gIpc.scanReady = false;
      xSemaphoreGive(gIpc.scanMutex);
    }
  }

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
      handleWifiScan(rot, click, longPress);
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
    case UiMode::SetAcl:
      handleAcl(rot, click, longPress);
      break;
    case UiMode::SetTempComp:
      handleTempComp(rot, click, longPress);
      break;
    case UiMode::NtpStats:
      handleNtpStats(rot, click, longPress);
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
  display_.setTextWrap(false);
  uiFont(display_, false);
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
    case UiMode::SetAcl:
      drawAcl(settings);
      break;
    case UiMode::SetTempComp:
      drawTempComp(settings);
      break;
    case UiMode::NtpStats:
      drawNtpStats(ntp);
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

  uiFont(display_, false);
  display_.drawBitmap(0, 2, kIconSat, 8, 8, SH110X_WHITE);
  char left[20];
  snprintf(left, sizeof(left), "%u %s", static_cast<unsigned>(st.satellites),
           clockStateLabel(st.clockState));
  display_.setCursor(10, uiBaseline(2, false));
  display_.print(left);

  display_.drawBitmap(80, 2, kIconWifi, 8, 8, SH110X_WHITE);
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
  display_.setCursor(128 - 2 - static_cast<int16_t>(uiTextWidth(display_, right)),
                     uiBaseline(2, false));
  display_.print(right);

  display_.drawFastHLine(0, 16, 128, SH110X_WHITE);

  char timeBuf[9];
  if (st.utcEpoch > 0) {
    time_t local = static_cast<time_t>(st.utcEpoch) + settings.timezoneHours * 3600L;
    struct tm tmv = {};
    gmtime_r(&local, &tmv);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "--:--:--");
  }
  uiFont(display_, true);
  const uint16_t timeW = uiTextWidth(display_, timeBuf);
  display_.setCursor(static_cast<int16_t>((128 - timeW) / 2), uiBaseline(18, true));
  display_.print(timeBuf);

  uiFont(display_, false);
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
  while (ssid.length() > 1 && uiTextWidth(display_, ssid.c_str()) > 128) {
    ssid.remove(ssid.length() - 1);
  }
  display_.setCursor(0, uiBaseline(40, false));
  display_.print(ssid);

  String ip;
  if (sta) {
    ip = wifi.localIp().toString();
  } else if (ap) {
    ip = WiFi.softAPIP().toString();
  } else {
    ip = "--.--.--.--";
  }
  display_.setCursor(0, uiBaseline(52, false));
  display_.print(ip);

  const char* sync = st.timeValid ? "SYNC" : "WAIT";
  display_.setCursor(128 - 2 - static_cast<int16_t>(uiTextWidth(display_, sync)),
                     uiBaseline(52, false));
  display_.print(sync);
}

void DisplayUi::drawMenu() {
  const uint8_t count = static_cast<uint8_t>(MenuItem::Count);
  const uint8_t visible = OLED_MENU_ROWS;
  uint8_t start = 0;
  if (menuIndex_ >= visible) {
    start = menuIndex_ - visible + 1;
  }
  menuFontBegin(display_);
  for (uint8_t row = 0; row < visible; ++row) {
    const uint8_t i = static_cast<uint8_t>(start + row);
    if (i >= count) {
      break;
    }
    const int16_t y = static_cast<int16_t>(OLED_MENU_Y0 + row * OLED_MENU_ROW_H);
    menuDrawRow(display_, y, i == menuIndex_, MENU_LABELS[i]);
  }
  menuFontEnd(display_);
}

void DisplayUi::drawWifiScan() {
  menuFontBegin(display_);
  if (scanPending_) {
    menuDrawRow(display_, 16, false, "Scanning");
    menuFontEnd(display_);
    uiAt(display_, 4, 52, "long=back");
    return;
  }
  if (networks_.empty()) {
    menuDrawRow(display_, 16, false, "No APs");
    menuFontEnd(display_);
    uiAt(display_, 4, 52, "click=retry");
    return;
  }
  const int visible = OLED_MENU_ROWS;
  int start = max(0, static_cast<int>(wifiIndex_) - visible + 1);
  for (int row = 0; row < visible; ++row) {
    int idx = start + row;
    if (idx >= static_cast<int>(networks_.size())) {
      break;
    }
    const int16_t y = static_cast<int16_t>(OLED_MENU_Y0 + row * OLED_MENU_ROW_H);
    String line = networks_[idx].ssid;
    bool ascii = true;
    for (size_t k = 0; k < line.length(); ++k) {
      const uint8_t c = static_cast<uint8_t>(line[k]);
      if (c < 32 || c > 126) {
        ascii = false;
        break;
      }
    }
    if (!ascii) {
      char hex[12];
      snprintf(hex, sizeof(hex), "AP %ddBm", static_cast<int>(networks_[idx].rssi));
      line = hex;
    }
    int16_t x1 = 0, y1 = 0;
    uint16_t tw = 0, th = 0;
    while (line.length() > 1) {
      display_.getTextBounds(line.c_str(), 0, 0, &x1, &y1, &tw, &th);
      if (tw <= 124) {
        break;
      }
      line.remove(line.length() - 1);
    }
    menuDrawRow(display_, y, idx == static_cast<int>(wifiIndex_), line.c_str());
  }
  menuFontEnd(display_);
}

void DisplayUi::drawPassword() {
  uiAt(display_, 0, 0, "Password");
  String s = pendingSsid_;
  while (s.length() > 1 && uiTextWidth(display_, (String("SSID:") + s).c_str()) > 128) {
    s.remove(s.length() - 1);
  }
  uiAt(display_, 0, 14, (String("SSID:") + s).c_str());
  String pwd = password_;
  while (pwd.length() > 1 && uiTextWidth(display_, (String("PWD:") + pwd).c_str()) > 128) {
    pwd.remove(0, 1);
  }
  uiAt(display_, 0, 28, (String("PWD:") + pwd).c_str());
  char chLine[20];
  snprintf(chLine, sizeof(chLine), "Char:[%c] rot", PWD_CHARS[pwdCursor_]);
  uiAt(display_, 0, 42, chLine);
  uiAt(display_, 0, 54, "click=add long=OK");
}

void DisplayUi::drawSetIp() {
  uiAt(display_, 0, 0, "Static IP");
  uiFont(display_, false);
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t x = static_cast<int16_t>(i * 32);
    const bool sel = (i == ipOctet_);
    if (sel) {
      display_.fillRect(x, 20, 31, 16, SH110X_WHITE);
      display_.setTextColor(SH110X_BLACK);
    } else {
      display_.setTextColor(SH110X_WHITE);
    }
    char oct[5];
    snprintf(oct, sizeof(oct), "%u", static_cast<unsigned>(editIp_[i]));
    display_.setCursor(x + 2, uiBaseline(22, false));
    display_.print(oct);
  }
  uiAt(display_, 0, 40, "rot=val click=next");
  uiAt(display_, 0, 52, "long=save");
}

void DisplayUi::drawTimezone(const AppSettings& settings) {
  uiAt(display_, 0, 0, "Timezone");
  char off[12];
  snprintf(off, sizeof(off), "UTC%s%d", settings.timezoneHours >= 0 ? "+" : "",
           static_cast<int>(settings.timezoneHours));
  uiFont(display_, true);
  const uint16_t w = uiTextWidth(display_, off);
  display_.setCursor(static_cast<int16_t>((128 - w) / 2), uiBaseline(20, true));
  display_.print(off);
  uiAt(display_, 0, 50, "rot=chg click=save");
}

void DisplayUi::drawAnomaly(const AppSettings& settings) {
  (void)settings;
  uiAt(display_, 0, 0, "Anomaly");
  char line[20];
  snprintf(line, sizeof(line), ">%s", anomalyPolicyMenuLabel(editPolicy_));
  uiAt(display_, 0, 18, line, true);
  uiAt(display_, 0, 40, "rot=chg click=save");
  uiAt(display_, 0, 52, "long=back");
}

void DisplayUi::drawTempComp(const AppSettings& settings) {
  uiAt(display_, 0, 0, "Temp Comp");
  uiAt(display_, 0, 16, editTempComp_ ? ">On" : ">Off", true);
  char kline[24];
  snprintf(kline, sizeof(kline), "k=%.2f ppm/C",
           static_cast<double>(tempCoeffPpmPerC(settings.tempCoeffCenti)));
  uiAt(display_, 0, 36, kline);
  uiAt(display_, 0, 52, "rot=on/off save");
}

void DisplayUi::drawAcl(const AppSettings& settings) {
  (void)settings;
  uiAt(display_, 0, 0, "NTP ACL");
  char mode[20];
  snprintf(mode, sizeof(mode), ">%s", ntpAclModeMenuLabel(editAclMode_));
  uiAt(display_, 0, 16, mode, true);
  char ips[16];
  snprintf(ips, sizeof(ips), "IPs: %u/%u", static_cast<unsigned>(editAclCount_),
           static_cast<unsigned>(NTP_ACL_MAX_ENTRIES));
  uiAt(display_, 0, 36, ips);
  uiAt(display_, 0, 52, "rot=mode save");
}

void DisplayUi::drawNtpStats(const NtpServer& ntp) {
  char line[28];
  snprintf(line, sizeof(line), "served %lu", static_cast<unsigned long>(ntp.servedCount()));
  uiAt(display_, 0, 0, line);
  snprintf(line, sizeof(line), "RATE %lu", static_cast<unsigned long>(ntp.rateLimitedCount()));
  uiAt(display_, 0, 12, line);
  snprintf(line, sizeof(line), "DENY %lu ACL %lu",
           static_cast<unsigned long>(ntp.deniedCount()),
           static_cast<unsigned long>(ntp.aclDeniedCount()));
  uiAt(display_, 0, 24, line);
  snprintf(line, sizeof(line), "drop %lu c=%u", static_cast<unsigned long>(ntp.droppedCount()),
           static_cast<unsigned>(ntp.activeClientCount()));
  uiAt(display_, 0, 36, line);
  uiAt(display_, 0, 52, "click=back");
}

void DisplayUi::drawMessage() {
  String line = message_;
  uiFont(display_, true);
  while (line.length() > 1 && uiTextWidth(display_, line.c_str()) > 124) {
    line.remove(line.length() - 1);
  }
  const uint16_t w = uiTextWidth(display_, line.c_str());
  display_.setCursor(static_cast<int16_t>((128 - w) / 2), uiBaseline(22, true));
  display_.print(line);
}

void DisplayUi::drawWebHint() {
  uiAt(display_, 0, 0, "Web Setup");
  uiAt(display_, 0, 14, "Join NTP-Setup-*");
  String apPass;
  if (settingsLock(pdMS_TO_TICKS(20))) {
    apPass = effectiveSoftApPassword(gSettings);
    settingsUnlock();
  } else {
    apPass = derivedSoftApPassword();
  }
  uiAt(display_, 0, 28, (String("Pass:") + apPass).c_str());
  uiAt(display_, 0, 42, "/setup login");
  uiAt(display_, 0, 54, WiFi.softAPIP().toString().c_str());
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
      scanError_ = "";
      mode_ = UiMode::WifiScan;
      requestWifiScan();
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
    case MenuItem::NtpAcl:
      if (settingsLock(pdMS_TO_TICKS(50))) {
        editAclMode_ = gSettings.ntpAclMode;
        editAclCount_ = gSettings.ntpAclCount;
        settingsUnlock();
      }
      mode_ = UiMode::SetAcl;
      break;
    case MenuItem::TempComp:
      if (settingsLock(pdMS_TO_TICKS(50))) {
        editTempComp_ = gSettings.tempComp;
        settingsUnlock();
      }
      mode_ = UiMode::SetTempComp;
      break;
    case MenuItem::NtpStats:
      mode_ = UiMode::NtpStats;
      break;
    case MenuItem::Restart:
      ESP.restart();
      break;
    default:
      break;
  }
}

void DisplayUi::requestWifiScan() {
  NetRequest req;
  req.type = NetReqType::ScanWifi;
  scanSeq_++;
  if (scanSeq_ == 0) {
    scanSeq_ = 1;
  }
  req.seq = scanSeq_;
  if (postNetRequest(req)) {
    scanPending_ = true;
    scanError_ = "";
    scanStartedMs_ = millis();
    networks_.clear();
  } else if (!scanPending_) {
    scanError_ = "Scan busy";
  }
}

void DisplayUi::handleWifiScan(int8_t rot, bool click, bool longPress) {
  if (longPress) {
    scanPending_ = false;
    mode_ = UiMode::Menu;
    return;
  }
  if (scanPending_) {
    return;
  }
  if (networks_.empty()) {
    if (click) {
      requestWifiScan();
    }
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

void DisplayUi::handleAcl(int8_t rot, bool click, bool longPress) {
  if (longPress) {
    mode_ = UiMode::Menu;
    return;
  }
  if (rot != 0) {
    editAclMode_ = (editAclMode_ == NtpAclMode::Off) ? NtpAclMode::AllowList : NtpAclMode::Off;
  }
  if (click) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.ntpAclMode = editAclMode_;
      editAclCount_ = gSettings.ntpAclCount;
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
    }
    showMessage(String("ACL:") + ntpAclModeMenuLabel(editAclMode_));
  }
}

void DisplayUi::handleTempComp(int8_t rot, bool click, bool longPress) {
  if (longPress) {
    mode_ = UiMode::Menu;
    return;
  }
  if (rot != 0) {
    editTempComp_ = !editTempComp_;
  }
  if (click) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.tempComp = editTempComp_;
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
    }
    showMessage(editTempComp_ ? "Tcomp On" : "Tcomp Off");
  }
}

void DisplayUi::handleNtpStats(int8_t /*rot*/, bool click, bool longPress) {
  if (click || longPress) {
    mode_ = UiMode::Menu;
  }
}
