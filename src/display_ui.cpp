#include "display_ui.h"
#include <Wire.h>
#include <WiFi.h>

static const char* MENU_LABELS[] = {
    "WiFi Scan",
    "Web Setup",
    "Set Static IP",
    "Use DHCP",
    "Timezone",
    "Restart",
};

static const char PWD_CHARS[] =
    "<ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%&*-_.";
// '<' means backspace when appended via click

void DisplayUi::begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
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

bool DisplayUi::takePendingWifi(String& ssid, String& pass) {
  if (!pendingWifi_) {
    return false;
  }
  pendingWifi_ = false;
  ssid = pendingSsid_;
  pass = pendingPass_;
  return true;
}

bool DisplayUi::takeApplyStaticIp() {
  if (!pendingStatic_) {
    return false;
  }
  pendingStatic_ = false;
  return true;
}

bool DisplayUi::takeStartWebSetup() {
  if (!pendingWeb_) {
    return false;
  }
  pendingWeb_ = false;
  return true;
}

bool DisplayUi::takeUseDhcp() {
  if (!pendingDhcp_) {
    return false;
  }
  pendingDhcp_ = false;
  return true;
}

void DisplayUi::loop(EncoderInput& enc,
                     GpsService& gps,
                     WifiManager& wifi,
                     AppSettings& settings,
                     SettingsStore& store) {
  int8_t rot = enc.consumeRotate();
  bool click = enc.consumeClick();
  bool longPress = enc.consumeLongPress();

  switch (mode_) {
    case UiMode::Home:
      handleHome(rot, click);
      break;
    case UiMode::Menu:
      handleMenu(rot, click, longPress, settings, store);
      break;
    case UiMode::WifiScan:
      handleWifiScan(rot, click, wifi);
      break;
    case UiMode::WifiPassword:
      handlePassword(rot, click, longPress);
      break;
    case UiMode::SetIp:
      handleSetIp(rot, click, longPress, settings, store, wifi);
      break;
    case UiMode::SetTimezone:
      handleTimezone(rot, click, settings, store);
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

  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextColor(SSD1306_WHITE);
  switch (mode_) {
    case UiMode::Home:
      drawHome(gps, wifi, settings);
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
    case UiMode::WebSetupHint:
      drawWebHint();
      break;
    case UiMode::Message:
      drawMessage();
      break;
  }
  display_.display();
}

void DisplayUi::drawHome(const GpsService& gps, const WifiManager& wifi, const AppSettings& settings) {
  const auto& st = gps.status();
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
  display_.print("PPS ");
  display_.print(st.ppsSeen ? "OK" : "--");
  display_.print("  TZ");
  if (settings.timezoneHours >= 0) {
    display_.print("+");
  }
  display_.print(settings.timezoneHours);

  display_.setCursor(0, 48);
  if (st.validFix && st.utcEpoch > 0) {
    time_t local = static_cast<time_t>(st.utcEpoch) + settings.timezoneHours * 3600L;
    struct tm* tm = gmtime(&local);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    display_.print(buf);
  } else {
    display_.print("--:--:--");
  }
  display_.setCursor(72, 48);
  display_.print("Click=Menu");
}

void DisplayUi::drawMenu() {
  display_.setCursor(0, 0);
  display_.println("Menu  long=Back");
  for (uint8_t i = 0; i < static_cast<uint8_t>(MenuItem::Count); ++i) {
    display_.setCursor(0, 12 + i * 10);
    display_.print(i == menuIndex_ ? ">" : " ");
    display_.print(MENU_LABELS[i]);
  }
}

void DisplayUi::drawWifiScan() {
  display_.setCursor(0, 0);
  display_.println("WiFi list click=OK");
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

void DisplayUi::handleMenu(int8_t rot, bool click, bool longPress, AppSettings& settings, SettingsStore& store) {
  (void)settings;
  (void)store;
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
      mode_ = UiMode::WifiScan;
      break;
    case MenuItem::WebSetup:
      pendingWeb_ = true;
      mode_ = UiMode::WebSetupHint;
      break;
    case MenuItem::SetStaticIp:
      editIp_ = settings.staticIp;
      ipOctet_ = 0;
      mode_ = UiMode::SetIp;
      break;
    case MenuItem::UseDhcp:
      pendingDhcp_ = true;
      showMessage("DHCP enabled");
      break;
    case MenuItem::Timezone:
      mode_ = UiMode::SetTimezone;
      break;
    case MenuItem::Restart:
      ESP.restart();
      break;
    default:
      break;
  }
}

void DisplayUi::handleWifiScan(int8_t rot, bool click, WifiManager& wifi) {
  if (networks_.empty()) {
    if (click) {
      showMessage("Scanning...");
      display_.display();
      networks_ = wifi.scanNetworks();
      wifiIndex_ = 0;
      mode_ = UiMode::WifiScan;
      messageUntil_ = 0;
    }
    return;
  }
  if (rot > 0 && wifiIndex_ + 1 < networks_.size()) {
    wifiIndex_++;
  } else if (rot < 0 && wifiIndex_ > 0) {
    wifiIndex_--;
  }
  if (click) {
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
    pendingPass_ = password_;
    pendingWifi_ = true;
    showMessage("Connecting...");
  }
}

void DisplayUi::handleSetIp(int8_t rot, bool click, bool longPress, AppSettings& settings,
                            SettingsStore& store, WifiManager& wifi) {
  (void)wifi;
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
    settings.staticIp = editIp_;
    settings.useStaticIp = true;
    // Default gateway to x.x.x.1 on same subnet if unset/mismatched
    settings.gateway = IPAddress(editIp_[0], editIp_[1], editIp_[2], 1);
    store.save(settings);
    pendingStatic_ = true;
    showMessage("Checking IP...");
  }
}

void DisplayUi::handleTimezone(int8_t rot, bool click, AppSettings& settings, SettingsStore& store) {
  if (rot != 0) {
    int v = settings.timezoneHours + rot;
    if (v < -12) {
      v = 14;
    }
    if (v > 14) {
      v = -12;
    }
    settings.timezoneHours = static_cast<int8_t>(v);
  }
  if (click) {
    store.save(settings);
    showMessage("TZ saved");
  }
}
