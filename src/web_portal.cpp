#include "web_portal.h"
#include "config.h"
#include "gps_service.h"
#include "ntp_server.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

void WebPortal::begin(WifiManager* wifi, SettingsStore* store, AppSettings* settings,
                      GpsService* gps, NtpServer* ntp) {
  wifi_ = wifi;
  store_ = store;
  settings_ = settings;
  gps_ = gps;
  ntp_ = ntp;
  if (started_) {
    return;
  }

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/setup", HTTP_GET, [this]() { handleSetup(); });
  server_.on("/scan", HTTP_GET, [this]() { handleScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
  server_.onNotFound([this]() {
    const bool apUp = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    if (apUp && wifi_ && !wifi_->isStaConnected()) {
      server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
      server_.send(302, "text/plain", "");
      return;
    }
    server_.sendHeader("Location", "/", true);
    server_.send(302, "text/plain", "");
  });
  server_.begin();
  started_ = true;
  Serial.println("HTTP status on port 80  (/ and /status)");
}

void WebPortal::loop() {
  if (started_) {
    server_.handleClient();
  }
}

bool WebPortal::consumeConnectRequest(String& ssid, String& pass) {
  if (!pendingConnect_) {
    return false;
  }
  pendingConnect_ = false;
  ssid = pendingSsid_;
  pass = pendingPass_;
  return true;
}

String WebPortal::buildPage(const String& title, const String& body, bool refresh) const {
  String html;
  html.reserve(body.length() + 700);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  if (refresh) {
    html += F("<meta http-equiv='refresh' content='2'>");
  }
  html += F("<title>");
  html += title;
  html += F("</title><style>"
            "body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 12px;background:#0f172a;color:#e2e8f0}"
            "h1{font-size:1.25rem}button,input,select{font-size:1rem;padding:8px;margin:6px 0;width:100%;box-sizing:border-box}"
            "button{background:#38bdf8;border:0;border-radius:8px;color:#0f172a;font-weight:700}"
            "a{color:#38bdf8}.card{background:#1e293b;padding:16px;border-radius:12px;margin:12px 0}"
            ".row{display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-bottom:1px solid #334155}"
            ".row:last-child{border-bottom:0}.k{color:#94a3b8}.v{font-weight:700;text-align:right}"
            ".ok{color:#4ade80}.warn{color:#fbbf24}.bad{color:#f87171}"
            "</style></head><body>");
  html += body;
  html += F("</body></html>");
  return html;
}

String WebPortal::formatUtc(uint32_t epoch, int8_t tzHours) const {
  if (epoch == 0) {
    return "--";
  }
  time_t t = static_cast<time_t>(epoch) + static_cast<time_t>(tzHours) * 3600;
  struct tm tmv = {};
  gmtime_r(&t, &tmv);
  char buf[36];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
           tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(buf);
}

void WebPortal::handleRoot() {
  if (!wifi_->isStaConnected()) {
    handleSetup();
    return;
  }

  const GpsStatus st = gps_ ? gps_->status() : GpsStatus{};
  uint32_t utcSec = 0;
  uint32_t utcFrac = 0;
  const bool haveTime = gps_ && gps_->nowUtc(utcSec, utcFrac);
  (void)utcFrac;
  const bool ppsOk = gps_ && gps_->ppsFresh();
  const bool stratum1 = haveTime && ppsOk;
  const int8_t tz = settings_ ? settings_->timezoneHours : 8;
  const uint32_t ntpReqs = ntp_ ? ntp_->requestCount() : 0;

  String cls = stratum1 ? "ok" : (haveTime ? "warn" : "bad");
  String ntpState = stratum1 ? "Stratum 1 就绪" : (haveTime ? "已有 UTC，等待 PPS" : "未同步");

  String body;
  body.reserve(1600);
  body += F("<h1>GNSS NTP 状态</h1><p><a href='/setup'>WiFi 配网</a></p>");
  body += F("<div class='card'><div class='row'><span class='k'>NTP</span><span class='v ");
  body += cls;
  body += F("'>");
  body += ntpState;
  body += F("</span></div><div class='row'><span class='k'>Stratum</span><span class='v'>");
  body += haveTime ? "1" : "16";
  body += F("</span></div><div class='row'><span class='k'>RefID</span><span class='v'>GPSS</span></div>");
  body += F("<div class='row'><span class='k'>查询次数</span><span class='v'>");
  body += String(ntpReqs);
  body += F("</span></div><div class='row'><span class='k'>UTC</span><span class='v'>");
  body += formatUtc(haveTime ? utcSec : st.utcEpoch, 0);
  body += F("</span></div><div class='row'><span class='k'>本地 (UTC");
  body += tz >= 0 ? "+" : "";
  body += String(tz);
  body += F(")</span><span class='v'>");
  body += formatUtc(haveTime ? utcSec : st.utcEpoch, tz);
  body += F("</span></div></div>");

  body += F("<div class='card'><div class='row'><span class='k'>GPS 锁定</span><span class='v ");
  body += st.validFix ? "ok" : "bad";
  body += F("'>");
  body += st.validFix ? "是" : "否";
  body += F("</span></div><div class='row'><span class='k'>搜星</span><span class='v'>");
  body += String(st.satellites);
  body += F("</span></div><div class='row'><span class='k'>HDOP</span><span class='v'>");
  body += String(st.hdop, 1);
  body += F("</span></div><div class='row'><span class='k'>PPS</span><span class='v ");
  body += ppsOk ? "ok" : "bad";
  body += F("'>");
  body += ppsOk ? "正常" : "无";
  body += F(" (");
  body += String(st.ppsCount);
  body += F(")</span></div><div class='row'><span class='k'>经纬度</span><span class='v'>");
  if (st.validFix) {
    body += String(st.lat, 5);
    body += F(", ");
    body += String(st.lon, 5);
  } else {
    body += F("--");
  }
  body += F("</span></div></div>");

  body += F("<div class='card'><div class='row'><span class='k'>IP</span><span class='v'>");
  body += wifi_->localIp().toString();
  body += F("</span></div><div class='row'><span class='k'>SSID</span><span class='v'>");
  body += WiFi.SSID();
  body += F("</span></div><div class='row'><span class='k'>RSSI</span><span class='v'>");
  body += String(WiFi.RSSI());
  body += F(" dBm</span></div><div class='row'><span class='k'>MAC</span><span class='v'>");
  body += wifi_->macAddress();
  body += F("</span></div><div class='row'><span class='k'>运行</span><span class='v'>");
  body += String(millis() / 1000);
  body += F(" s</span></div></div>");
  body += F("<p style='color:#64748b'>每 2 秒刷新 · JSON: <a href='/status'>/status</a></p>");

  server_.send(200, "text/html", buildPage("NTP 状态", body, true));
}

void WebPortal::handleSetup() {
  String body = F("<h1>ESP32-C3 NTP WiFi 配网</h1>"
                  "<p><a href='/'>返回状态</a></p>"
                  "<div class='card'>"
                  "<p>扫描热点，选择 SSID，输入密码后连接。</p>"
                  "<button onclick='scan()'>扫描 WiFi</button>"
                  "<label>SSID</label><select id='ssid'></select>"
                  "<label>Password</label><input id='pass' type='password'>"
                  "<button onclick='save()'>连接</button>"
                  "<p id='msg'></p>"
                  "</div>"
                  "<script>"
                  "async function scan(){"
                  " document.getElementById('msg').textContent='Scanning...';"
                  " const r=await fetch('/scan'); const j=await r.json();"
                  " const s=document.getElementById('ssid'); s.innerHTML='';"
                  " j.forEach(n=>{const o=document.createElement('option');"
                  " o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+'dBm)'; s.appendChild(o);});"
                  " document.getElementById('msg').textContent='Found '+j.length+' networks';"
                  "}"
                  "async function save(){"
                  " const ssid=document.getElementById('ssid').value;"
                  " const pass=document.getElementById('pass').value;"
                  " const body=JSON.stringify({ssid,pass});"
                  " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body});"
                  " const t=await r.text(); document.getElementById('msg').textContent=t;"
                  "}"
                  "scan();"
                  "</script>");
  server_.send(200, "text/html", buildPage("NTP 配网", body));
}

void WebPortal::handleScan() {
  auto nets = wifi_->scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& n : nets) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = n.ssid;
    o["rssi"] = n.rssi;
  }
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebPortal::handleSave() {
  String body = server_.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    server_.send(400, "text/plain", "Bad JSON");
    return;
  }
  pendingSsid_ = doc["ssid"].as<String>();
  pendingPass_ = doc["pass"].as<String>();
  if (pendingSsid_.isEmpty()) {
    server_.send(400, "text/plain", "SSID required");
    return;
  }
  settings_->wifiSsid = pendingSsid_;
  settings_->wifiPass = pendingPass_;
  store_->save(*settings_);
  pendingConnect_ = true;
  server_.send(200, "text/plain", "Saved. Connecting...");
}

void WebPortal::handleStatus() {
  JsonDocument doc;
  doc["sta"] = wifi_->isStaConnected();
  doc["ip"] = wifi_->localIp().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["mac"] = wifi_->macAddress();
  doc["uptimeSec"] = millis() / 1000;
  doc["tzHours"] = settings_ ? settings_->timezoneHours : 8;

  const GpsStatus st = gps_ ? gps_->status() : GpsStatus{};
  uint32_t utcSec = 0;
  uint32_t utcFrac = 0;
  const bool haveTime = gps_ && gps_->nowUtc(utcSec, utcFrac);
  const bool ppsOk = gps_ && gps_->ppsFresh();
  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = st.validFix;
  gps["satellites"] = st.satellites;
  gps["hdop"] = st.hdop;
  gps["lat"] = st.lat;
  gps["lon"] = st.lon;
  gps["ppsFresh"] = ppsOk;
  gps["ppsCount"] = st.ppsCount;
  gps["utcEpoch"] = haveTime ? utcSec : st.utcEpoch;
  gps["ageMs"] = st.ageMs;

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["synced"] = haveTime;
  ntp["stratum"] = haveTime ? 1 : 16;
  ntp["stratum1Ready"] = haveTime && ppsOk;
  ntp["refId"] = "GPSS";
  ntp["requests"] = ntp_ ? ntp_->requestCount() : 0;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}
