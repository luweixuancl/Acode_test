#include "web_portal.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

void WebPortal::begin(WifiManager* wifi, SettingsStore* store, AppSettings* settings) {
  wifi_ = wifi;
  store_ = store;
  settings_ = settings;
  if (started_) {
    return;
  }

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/scan", HTTP_GET, [this]() { handleScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/status", HTTP_GET, [this]() { handleStatus(); });
  server_.onNotFound([this]() {
    server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server_.send(302, "text/plain", "");
  });
  server_.begin();
  started_ = true;
}

void WebPortal::loop() {
  server_.handleClient();
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

String WebPortal::buildPage(const String& body) const {
  String html;
  html.reserve(body.length() + 512);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ESP32-C3 NTP Setup</title>"
            "<style>"
            "body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 12px;background:#0f172a;color:#e2e8f0}"
            "h1{font-size:1.25rem}button,input,select{font-size:1rem;padding:8px;margin:6px 0;width:100%;box-sizing:border-box}"
            "button{background:#38bdf8;border:0;border-radius:8px;color:#0f172a;font-weight:700}"
            ".card{background:#1e293b;padding:16px;border-radius:12px}"
            "</style></head><body>");
  html += body;
  html += F("</body></html>");
  return html;
}

void WebPortal::handleRoot() {
  String body = F("<h1>ESP32-C3 NTP WiFi Setup</h1>"
                  "<div class='card'>"
                  "<p>Scan networks, choose SSID, enter password, then Connect.</p>"
                  "<button onclick='scan()'>Scan WiFi</button>"
                  "<label>SSID</label><select id='ssid'></select>"
                  "<label>Password</label><input id='pass' type='password'>"
                  "<button onclick='save()'>Connect</button>"
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
  server_.send(200, "text/html", buildPage(body));
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
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}
