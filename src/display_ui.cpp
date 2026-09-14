#include "display_ui.h"
#include "app_ipc.h"
#include "ntp_server.h"
#include <Wire.h>
#include <WiFi.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// FreeMono 9pt. Menu / large single-line values only.
// Dense 6-line pages stay on the built-in 6×8 so they do not pack.
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

static void menuFontBegin(Adafruit_SH1107& d) {
  d.setFont(&FreeMono9pt7b);
  d.setTextSize(1);
  d.setTextWrap(false);
}

static void menuFontEnd(Adafruit_SH1107& d) {
  d.setFont(nullptr);
  d.setTextSize(1);
  d.setTextColor(SH110X_WHITE);
}

static uint16_t monoWidth(Adafruit_SH1107& d, const char* text) {
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  d.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void monoLine(Adafruit_SH1107& d, int16_t top, const char* text, bool center) {
  menuFontBegin(d);
  d.setTextColor(SH110X_WHITE);
  const uint16_t w = monoWidth(d, text);
  const int16_t x = center ? static_cast<int16_t>((128 - w) / 2) : 0;
  d.setCursor(x < 0 ? 0 : x, top + OLED_MENU_BASELINE);
  d.print(text);
  menuFontEnd(d);
}

// Home clock: FreeMono Bold 12pt (digit 10×15, advance 14 → HH:MM:SS ≈ 112 px).
static constexpr int16_t kClockBaseline = 16;

static void clockLine(Adafruit_SH1107& d, int16_t top, const char* text) {
  d.setFont(&FreeMonoBold12pt7b);
  d.setTextSize(1);
  d.setTextWrap(false);
  d.setTextColor(SH110X_WHITE);
  const uint16_t w = monoWidth(d, text);
  const int16_t x = static_cast<int16_t>((128 - w) / 2);
  d.setCursor(x < 0 ? 0 : x, top + kClockBaseline);
  d.print(text);
  d.setFont(nullptr);
  d.setTextSize(1);
}

static constexpr int16_t kWifiIconW = 13;

static void drawWifiGlyph(Adafruit_SH1107& d, int16_t x, int16_t y) {
  static const char* kRows[8] = {
      "..#######....",
      ".#.......#...",
      "...#####.....",
      "..#.....#....",
      "....###......",
      "...#...#.....",
      ".....#.......",
      "....###......",
  };
  for (int16_t r = 0; r < 8; ++r) {
    for (int16_t c = 0; kRows[r][c]; ++c) {
      if (kRows[r][c] == '#') {
        d.drawPixel(x + c, y + r, SH110X_WHITE);
      }
    }
  }
}

static void menuDrawRow(Adafruit_SH1107& d, int16_t y, bool sel, const char* text) {
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
  display_.setTextColor(SH110X_WHITE);
  display_.setTextSize(1);
  display_.setCursor(0, 0);
  display_.println("ESP32-C3 NTP");
  display_.println("Booting...");
  display_.display();
  Serial.printf("[ui] OLED menu FreeMono9pt rows=%u rowH=%u mark=%s\n",
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
      handleMenu(rot, click, longPress, wifi);
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
  display_.setFont(nullptr);
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
  // Scheme A (128×64): top status chips · large time · SSID · IP+SYNC
  // Built-in font: size1 = 6×8, size2 = 12×16.

  static const uint8_t kIconSat[] PROGMEM = {
      0b00011000, 0b00111100, 0b01100110, 0b11011011,
      0b01100110, 0b00111100, 0b00011000, 0b00100100,
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
  const int16_t gap = 2;
  const int16_t iconX = 128 - rightW - gap - kWifiIconW;
  drawWifiGlyph(display_, iconX, 0);
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
  clockLine(display_, 12, timeBuf);

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
    display_.setCursor(4, 56);
    display_.print("long=back");
    return;
  }
  if (networks_.empty()) {
    menuDrawRow(display_, 16, false, "No APs");
    menuFontEnd(display_);
    display_.setCursor(4, 56);
    display_.print("click=retry");
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
  display_.println("Timezone");
  char off[12];
  snprintf(off, sizeof(off), "UTC%s%d", settings.timezoneHours >= 0 ? "+" : "",
           static_cast<int>(settings.timezoneHours));
  monoLine(display_, 20, off, true);
  display_.setCursor(0, 48);
  display_.print("rot=chg click=save");
}

void DisplayUi::drawAnomaly(const AppSettings& settings) {
  (void)settings;
  display_.setCursor(0, 0);
  display_.println("Anomaly");
  char line[20];
  snprintf(line, sizeof(line), ">%s", anomalyPolicyMenuLabel(editPolicy_));
  monoLine(display_, 16, line, false);
  display_.setCursor(0, 40);
  display_.println("rot=chg click=save");
  display_.setCursor(0, 52);
  display_.println("long=back");
}

void DisplayUi::drawTempComp(const AppSettings& settings) {
  display_.setCursor(0, 0);
  display_.println("Temp Comp");
  monoLine(display_, 14, editTempComp_ ? ">On" : ">Off", false);
  display_.setCursor(0, 32);
  display_.print("k=");
  display_.print(tempCoeffPpmPerC(settings.tempCoeffCenti), 2);
  display_.println(" ppm/C");
  display_.setCursor(0, 44);
  display_.println("coeff via Web");
  display_.setCursor(0, 56);
  display_.println("rot=on/off save");
}

void DisplayUi::drawAcl(const AppSettings& settings) {
  (void)settings;
  display_.setCursor(0, 0);
  display_.println("NTP ACL");
  char mode[20];
  snprintf(mode, sizeof(mode), ">%s", ntpAclModeMenuLabel(editAclMode_));
  monoLine(display_, 14, mode, false);
  display_.setCursor(0, 32);
  display_.print("IPs: ");
  display_.print(editAclCount_);
  display_.print("/");
  display_.println(NTP_ACL_MAX_ENTRIES);
  display_.setCursor(0, 44);
  display_.println("edit list via Web");
  display_.setCursor(0, 56);
  display_.println("rot=mode click=save");
}

void DisplayUi::drawNtpStats(const NtpServer& ntp) {
  display_.setCursor(0, 0);
  display_.println("NTP Stats");
  display_.setCursor(0, 12);
  display_.print("served ");
  display_.println(ntp.servedCount());
  display_.setCursor(0, 22);
  display_.print("RATE   ");
  display_.println(ntp.rateLimitedCount());
  display_.setCursor(0, 32);
  display_.print("DENY   ");
  display_.print(ntp.deniedCount());
  display_.print(" ACL ");
  display_.println(ntp.aclDeniedCount());
  display_.setCursor(0, 42);
  display_.print("drop   ");
  display_.print(ntp.droppedCount());
  display_.print(" c=");
  display_.println(ntp.activeClientCount());
  display_.setCursor(0, 56);
  display_.print("click=back");
}

void DisplayUi::drawMessage() {
  // "OK <ip>" — keep the full address (9pt "OK" + 6×8 IP).
  if (message_.startsWith("OK ") && message_.length() > 3) {
    monoLine(display_, 8, "OK", true);
    const char* ip = message_.c_str() + 3;
    const int16_t ipW = static_cast<int16_t>(strlen(ip) * 6);
    display_.setCursor(ipW >= 128 ? 0 : (128 - ipW) / 2, 40);
    display_.print(ip);
    return;
  }

  menuFontBegin(display_);
  const uint16_t w = monoWidth(display_, message_.c_str());
  menuFontEnd(display_);
  if (w <= 124) {
    monoLine(display_, 22, message_.c_str(), true);
    return;
  }

  // Long toast: wrap 6×8 instead of chopping characters.
  const uint8_t cpl = 21;
  uint8_t row = 0;
  const char* p = message_.c_str();
  while (*p && row < 6) {
    char line[22];
    uint8_t n = 0;
    while (p[n] && n < cpl) {
      ++n;
    }
    memcpy(line, p, n);
    line[n] = '\0';
    display_.setCursor(0, 8 + row * 10);
    display_.print(line);
    p += n;
    ++row;
  }
}

void DisplayUi::drawWebHint() {
  display_.setCursor(0, 0);
  display_.println("Web WiFi Setup");
  display_.setCursor(0, 16);
  display_.println("Join AP NTP-Setup-*");
  display_.setCursor(0, 28);
  display_.print("Pass:");
  String apPass;
  if (settingsLock(pdMS_TO_TICKS(20))) {
    apPass = effectiveSoftApPassword(gSettings);
    settingsUnlock();
  } else {
    apPass = derivedSoftApPassword();
  }
  display_.println(apPass);
  display_.setCursor(0, 40);
  display_.println("then /setup login");
  display_.setCursor(0, 52);
  display_.print(WiFi.softAPIP());
}

void DisplayUi::handleHome(int8_t rot, bool click) {
  (void)rot;
  if (click) {
    mode_ = UiMode::Menu;
    menuIndex_ = 0;
  }
}

void DisplayUi::handleMenu(int8_t rot, bool click, bool longPress, WifiManager& wifi) {
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
      // Prefer the live STA address so the user edits the LAN IP they already have,
      // not SoftAP 192.168.4.1 or a stale NVS placeholder.
      if (wifi.isStaConnected()) {
        editIp_ = wifi.localIp();
      } else if (settingsLock(pdMS_TO_TICKS(50))) {
        editIp_ = gSettings.staticIp;
        settingsUnlock();
        // SoftAP subnet is never a useful static-IP seed.
        if (editIp_[0] == 192 && editIp_[1] == 168 && editIp_[2] == 4) {
          editIp_ = IPAddress(192, 168, 1, 50);
        }
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
      // Prefer live STA gateway when editing on the same /24; else keep NVS or .1.
      IPAddress gw = gSettings.gateway;
      if (WiFi.status() == WL_CONNECTED) {
        const IPAddress curGw = WiFi.gatewayIP();
        if (curGw[0] == editIp_[0] && curGw[1] == editIp_[1] && curGw[2] == editIp_[2] &&
            static_cast<uint32_t>(curGw) != 0) {
          gw = curGw;
        }
      }
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
