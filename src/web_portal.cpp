#include "web_portal.h"
#include "app_ipc.h"
#include "config.h"
#include "gps_service.h"
#include "ntp_server.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

void WebPortal::begin(WifiManager* wifi, GpsService* gps, NtpServer* ntp) {
  wifi_ = wifi;
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

  // Static shell; live values filled by JS polling /status (no full-page refresh).
  String body = F(
      "<h1>GNSS NTP 状态</h1><p><a href='/setup'>WiFi 配网</a></p>"
      "<div class='card'>"
      "<div class='row'><span class='k'>NTP</span><span class='v' id='ntpState'>--</span></div>"
      "<div class='row'><span class='k'>Stratum</span><span class='v' id='stratum'>--</span></div>"
      "<div class='row'><span class='k'>RefID</span><span class='v' id='refId'>GPSS</span></div>"
      "<div class='row'><span class='k'>查询次数</span><span class='v' id='ntpReq'>--</span></div>"
      "<div class='row'><span class='k'>UTC</span><span class='v' id='utc'>--</span></div>"
      "<div class='row'><span class='k'>本地</span><span class='v' id='local'>--</span></div>"
      "</div>"
      "<div class='card'>"
      "<div class='row'><span class='k'>GPS 锁定</span><span class='v' id='fix'>--</span></div>"
      "<div class='row'><span class='k'>搜星</span><span class='v' id='sats'>--</span></div>"
      "<div class='row'><span class='k'>HDOP</span><span class='v' id='hdop'>--</span></div>"
      "<div class='row'><span class='k'>PPS</span><span class='v' id='pps'>--</span></div>"
      "<div class='row'><span class='k'>经纬度</span><span class='v' id='ll'>--</span></div>"
      "</div>"
      "<div class='card'>"
      "<div class='row'><span class='k'>IP</span><span class='v' id='ip'>--</span></div>"
      "<div class='row'><span class='k'>SSID</span><span class='v' id='ssid'>--</span></div>"
      "<div class='row'><span class='k'>RSSI</span><span class='v' id='rssi'>--</span></div>"
      "<div class='row'><span class='k'>MAC</span><span class='v' id='mac'>--</span></div>"
      "<div class='row'><span class='k'>运行</span><span class='v' id='up'>--</span></div>"
      "</div>"
      "<p style='color:#64748b'>自动更新 · JSON: <a href='/status'>/status</a></p>"
      "<script>"
      "function pad(n){return n<10?'0'+n:''+n}"
      "function fmt(epoch,tz){"
      " if(!epoch)return '--';"
      " const d=new Date((epoch+tz*3600)*1000);"
      " return d.getUTCFullYear()+'-'+pad(d.getUTCMonth()+1)+'-'+pad(d.getUTCDate())+' '"
      "  +pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+':'+pad(d.getUTCSeconds())}"
      "function setCls(el,c){el.className='v '+(c||'')}"
      "async function tick(){"
      " try{"
      "  const r=await fetch('/status'); const j=await r.json();"
      "  const g=j.gps||{}, n=j.ntp||{};"
      "  const have=!!n.synced, pps=!!g.ppsFresh, s1=!!n.stratum1Ready;"
      "  const st=document.getElementById('ntpState');"
      "  st.textContent=s1?'Stratum 1 就绪':(have?'已有 UTC，等待 PPS':'未同步');"
      "  setCls(st,s1?'ok':(have?'warn':'bad'));"
      "  document.getElementById('stratum').textContent=(n.stratum!=null)?n.stratum:'--';"
      "  document.getElementById('refId').textContent=n.refId||'GPSS';"
      "  document.getElementById('ntpReq').textContent=(n.requests!=null)?n.requests:'--';"
      "  const tz=j.tzHours||0;"
      "  document.getElementById('utc').textContent=fmt(g.utcEpoch,0);"
      "  document.getElementById('local').textContent=fmt(g.utcEpoch,tz)"
      "    +' (UTC'+(tz>=0?'+':'')+tz+')';"
      "  const fx=document.getElementById('fix');"
      "  fx.textContent=g.fix?'是':'否'; setCls(fx,g.fix?'ok':'bad');"
      "  document.getElementById('sats').textContent=(g.satellites!=null)?g.satellites:'--';"
      "  document.getElementById('hdop').textContent=(g.hdop!=null)?Number(g.hdop).toFixed(1):'--';"
      "  const pp=document.getElementById('pps');"
      "  pp.textContent=(pps?'正常':'无')+' ('+(g.ppsCount||0)+')'; setCls(pp,pps?'ok':'bad');"
      "  document.getElementById('ll').textContent=g.fix"
      "    ?(Number(g.lat).toFixed(5)+', '+Number(g.lon).toFixed(5)):'--';"
      "  document.getElementById('ip').textContent=j.ip||'--';"
      "  document.getElementById('ssid').textContent=j.ssid||'--';"
      "  document.getElementById('rssi').textContent=(j.rssi!=null)?(j.rssi+' dBm'):'--';"
      "  document.getElementById('mac').textContent=j.mac||'--';"
      "  document.getElementById('up').textContent=(j.uptimeSec!=null)?(j.uptimeSec+' s'):'--';"
      " }catch(e){}"
      "}"
      "tick(); setInterval(tick,2000);"
      "</script>");

  server_.send(200, "text/html", buildPage("NTP 状态", body, false));
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
  if (settingsLock(pdMS_TO_TICKS(200))) {
    gSettings.wifiSsid = pendingSsid_;
    gSettings.wifiPass = pendingPass_;
    gStore.save(gSettings);
    settingsUnlock();
  }
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

  if (settingsLock(pdMS_TO_TICKS(20))) {
    doc["tzHours"] = gSettings.timezoneHours;
    settingsUnlock();
  } else {
    doc["tzHours"] = 8;
  }

  const GpsStatus st = gps_ ? gps_->snapshot() : GpsStatus{};
  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = st.validFix;
  gps["satellites"] = st.satellites;
  gps["hdop"] = st.hdop;
  gps["lat"] = st.lat;
  gps["lon"] = st.lon;
  gps["ppsFresh"] = st.ppsFresh;
  gps["ppsCount"] = st.ppsCount;
  gps["utcEpoch"] = st.utcEpoch;
  gps["ageMs"] = st.ageMs;
  gps["timeValid"] = st.timeValid;
  gps["qualityMs"] = st.qualityMs;

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["synced"] = st.timeValid;
  ntp["stratum"] = st.timeValid ? 1 : 16;
  ntp["stratum1Ready"] = st.timeValid && st.ppsFresh;
  ntp["refId"] = "GPSS";
  ntp["requests"] = ntp_ ? ntp_->requestCount() : 0;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}
