#include "web_portal.h"
#include "app_ipc.h"
#include "config.h"
#include "gps_service.h"
#include "ntp_server.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

namespace {

String jsonSafeSsid(const String& ssid) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(ssid.c_str());
  const size_t n = ssid.length();
  bool utf8 = true;
  for (size_t i = 0; i < n;) {
    const uint8_t c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t need = 0;
    if ((c & 0xE0) == 0xC0) {
      need = 2;
    } else if ((c & 0xF0) == 0xE0) {
      need = 3;
    } else if ((c & 0xF8) == 0xF0) {
      need = 4;
    } else {
      utf8 = false;
      break;
    }
    if (i + need > n) {
      utf8 = false;
      break;
    }
    for (size_t k = 1; k < need; ++k) {
      if ((p[i + k] & 0xC0) != 0x80) {
        utf8 = false;
        break;
      }
    }
    if (!utf8) {
      break;
    }
    i += need;
  }
  if (utf8) {
    return ssid;
  }
  String hex;
  hex.reserve(n * 2 + 4);
  hex = "hex:";
  for (size_t i = 0; i < n; ++i) {
    char b[3];
    snprintf(b, sizeof(b), "%02X", p[i]);
    hex += b;
  }
  return hex;
}

}  // namespace

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
  server_.on("/metrics", HTTP_GET, [this]() { handleMetrics(); });
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
  Serial.println("HTTP on :80  (/ /status /metrics /setup)");
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

void WebPortal::handleRoot() {
  if (!wifi_->isStaConnected()) {
    handleSetup();
    return;
  }

  // Static shell; live values filled by JS polling /status (no full-page refresh).
  String body = F(
      "<h1>GNSS NTP 状态</h1><p><a href='/setup'>设置</a></p>"
      "<div class='card'>"
      "<div class='row'><span class='k'>NTP</span><span class='v' id='ntpState'>--</span></div>"
      "<div class='row'><span class='k'>时钟</span><span class='v' id='clk'>--</span></div>"
      "<div class='row'><span class='k'>Residual</span><span class='v' id='res'>--</span></div>"
      "<div class='row'><span class='k'>策略</span><span class='v' id='apol'>--</span></div>"
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
      "<p style='color:#64748b'>自动更新 1 Hz · JSON: <a href='/status'>/status</a></p>"
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
      "  const g=j.gps||{}, n=j.ntp||{}, c=j.clock||{};"
      "  const have=!!n.synced, pps=!!g.ppsFresh, s1=!!n.stratum1Ready;"
      "  const st=document.getElementById('ntpState');"
      "  st.textContent=s1?'Stratum 1 就绪':(have?'降级/守时':'未同步');"
      "  setCls(st,s1?'ok':(have?'warn':'bad'));"
      "  document.getElementById('clk').textContent=c.state||'--';"
      "  document.getElementById('res').textContent=(c.residualMs!=null)?(c.residualMs+' ms'):'--';"
      "  document.getElementById('apol').textContent=j.anomalyLabel||'--';"
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
      "tick(); setInterval(tick,1000);"
      "</script>");

  server_.send(200, "text/html", buildPage("NTP 状态", body, false));
}

void WebPortal::handleSetup() {
  uint8_t apol = 0;
  uint8_t aclm = 0;
  bool tcmp = false;
  int16_t tcpc = CLK_TEMP_COEFF_CENTI;
  String aclLines;
  String savedSsid;
  bool haveSaved = false;
  if (settingsLock(pdMS_TO_TICKS(50))) {
    apol = static_cast<uint8_t>(gSettings.anomalyPolicy);
    aclm = static_cast<uint8_t>(gSettings.ntpAclMode);
    tcmp = gSettings.tempComp;
    tcpc = gSettings.tempCoeffCenti;
    for (uint8_t i = 0; i < gSettings.ntpAclCount && i < NTP_ACL_MAX_ENTRIES; ++i) {
      if (i) {
        aclLines += '\n';
      }
      aclLines += gSettings.ntpAcl[i].toString();
    }
    savedSsid = gSettings.wifiSsid;
    haveSaved = !savedSsid.isEmpty();
    settingsUnlock();
  }
  String body;
  body.reserve(5600);
  body += F("<h1>NTP 设置</h1><p><a href='/'>返回状态</a></p>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>管理口令</h2>"
            "<p style='color:#64748b;font-size:.85rem'>写操作需要口令（默认 SoftAP："
            "<code>NTP-</code>+MAC 后 4 位）。可在下方覆盖 SoftAP / Web 口令。</p>"
            "<label>Auth</label><input id='auth' type='password' autocomplete='current-password'>"
            "<label>SoftAP 口令覆盖（可选，≥8）</label>"
            "<input id='appw' type='password' placeholder='留空=默认 NTP-XXXX'>"
            "<label>Web 写口令覆盖（可选）</label>"
            "<input id='webpw' type='password' placeholder='留空=跟 SoftAP'>"
            "</div>");
  if (haveSaved) {
    body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>已保存的 WiFi</h2><p>SSID: <b>");
    body += savedSsid;
    body += F("</b></p>"
              "<p style='color:#64748b;font-size:.85rem'>固件更新后会自动重连；无需重新输入 WiFi 密码。"
              "仅当路由器改密或换热点时才需要下方重新配网。</p>"
              "<button onclick='reconnectSaved()'>使用已保存网络重连</button>"
              "<p id='rmsg'></p></div>");
  }
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>GPS 异常策略</h2>"
            "<select id='apol'>"
            "<option value='0'>立即拒绝授时 (Refuse)</option>"
            "<option value='1'>短时守时 Holdover 30s</option>"
            "<option value='2'>长时守时 Holdover 5min</option>"
            "</select>"
            "<button onclick='savePolicy()'>保存策略</button>"
            "<p id='pmsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>NTP ACL 白名单</h2>"
            "<p style='color:#64748b;font-size:.85rem'>默认 Off。开启 AllowList 后仅列出的 IPv4 可取时"
            "（最多 8 条；未命中静默丢弃；空列表=拒绝全部）。</p>"
            "<label>模式</label><select id='aclm'>"
            "<option value='0'>Off（不限制）</option>"
            "<option value='1'>AllowList</option>"
            "</select>"
            "<label>允许的 IP（每行一个）</label>"
            "<textarea id='acllist' rows='5' style='width:100%;font-family:monospace'></textarea>"
            "<button onclick='saveAcl()'>保存 ACL</button>"
            "<p id='amsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>晶振温度补偿</h2>"
            "<p style='color:#64748b;font-size:.85rem'>默认关。用片上温度对 Holdover 外推做一阶 "
            "ppm/°C 修正（相对最近 PPS 估频时的温度）。系数可改，默认 -0.50。</p>"
            "<label>模式</label><select id='tcmp'>"
            "<option value='0'>Off</option>"
            "<option value='1'>On</option>"
            "</select>"
            "<label>系数 ppm/°C</label>"
            "<input id='tcpc' type='number' step='0.01'>"
            "<button onclick='saveTemp()'>保存温度补偿</button>"
            "<p id='tmsg'></p></div>");
  body += F("<div class='card'><h2 style='font-size:1rem;margin:0 0 8px'>WiFi 配网</h2>"
            "<p>扫描热点，选择 SSID，输入密码后连接。</p>"
            "<button onclick='scan()'>扫描 WiFi</button>"
            "<label>SSID</label><select id='ssid'></select>"
            "<label>Password</label><input id='pass' type='password'>"
            "<button onclick='saveWifi()'>连接</button>"
            "<p id='msg'></p></div>");
  body += F("<script>"
            "function authBody(extra){"
            " const o=Object.assign({auth:document.getElementById('auth').value||''},extra||{});"
            " const ap=document.getElementById('appw').value;"
            " const wp=document.getElementById('webpw').value;"
            " if(ap)o.apPassword=ap;if(wp)o.webPassword=wp;return o;}"
            "async function sleep(ms){return new Promise(r=>setTimeout(r,ms));}"
            "async function scan(){"
            " document.getElementById('msg').textContent='Scanning...';"
            " let j=null;"
            " for(let i=0;i<50;i++){"
            "  const r=await fetch('/scan');"
            "  if(r.status===202){await sleep(250);continue;}"
            "  if(!r.ok){document.getElementById('msg').textContent='Scan failed';return;}"
            "  j=await r.json(); break;"
            " }"
            " if(!j){document.getElementById('msg').textContent='Scan timeout';return;}"
            " const s=document.getElementById('ssid'); s.innerHTML='';"
            " j.forEach(n=>{const o=document.createElement('option');"
            " o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+'dBm)'; s.appendChild(o);});"
            " document.getElementById('msg').textContent='Found '+j.length+' networks';"
            "}"
            "async function saveWifi(){"
            " const ssid=document.getElementById('ssid').value;"
            " const pass=document.getElementById('pass').value;"
            " const body=JSON.stringify(authBody({ssid,pass}));"
            " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body});"
            " document.getElementById('msg').textContent=await r.text();"
            "}"
            "async function reconnectSaved(){"
            " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},"
            "  body:JSON.stringify(authBody({reconnectSaved:true})});"
            " document.getElementById('rmsg').textContent=await r.text();"
            "}"
            "async function savePolicy(){"
            " const anomalyPolicy=parseInt(document.getElementById('apol').value,10);"
            " const body=JSON.stringify(authBody({anomalyPolicy}));"
            " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body});"
            " document.getElementById('pmsg').textContent=await r.text();"
            "}"
            "async function saveAcl(){"
            " const ntpAclMode=parseInt(document.getElementById('aclm').value,10);"
            " const ntpAcl=document.getElementById('acllist').value.split(/\\r?\\n/)"
            "  .map(s=>s.trim()).filter(s=>s.length>0);"
            " const body=JSON.stringify(authBody({ntpAclMode,ntpAcl}));"
            " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body});"
            " document.getElementById('amsg').textContent=await r.text();"
            "}"
            "async function saveTemp(){"
            " const tempComp=parseInt(document.getElementById('tcmp').value,10)===1;"
            " const tempCoeff=parseFloat(document.getElementById('tcpc').value);"
            " const body=JSON.stringify(authBody({tempComp,tempCoeff}));"
            " const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body});"
            " document.getElementById('tmsg').textContent=await r.text();"
            "}"
            "document.getElementById('apol').value='");
  body += String(apol);
  body += F("';"
            "document.getElementById('aclm').value='");
  body += String(aclm);
  body += F("';"
            "document.getElementById('tcmp').value='");
  body += String(tcmp ? 1 : 0);
  body += F("';"
            "document.getElementById('tcpc').value='");
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(tempCoeffPpmPerC(tcpc)));
    body += buf;
  }
  body += F("';"
            "document.getElementById('acllist').value=");
  // JSON-encode the ACL lines for safe JS string.
  {
    JsonDocument tmp;
    tmp.set(aclLines);
    String enc;
    serializeJson(tmp, enc);
    body += enc;
  }
  body += F(";");
  if (!haveSaved) {
    body += F("scan();");
  }
  body += F("</script>");
  server_.send(200, "text/html", buildPage("NTP 设置", body));
}

void WebPortal::handleScan() {
  if (wifi_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"no wifi\"}");
    return;
  }

  // Start a new async scan when idle; Running shares SCAN_DONE → lastScan_ with OLED.
  if (!wifi_->isScanRunning()) {
    if (!wifi_->startScan()) {
      server_.send(503, "application/json", "{\"status\":\"busy\"}");
      return;
    }
  }

  std::vector<WifiNetwork> nets;
  const WifiScanState st = wifi_->pollScan(&nets);
  if (st == WifiScanState::Running) {
    server_.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (st == WifiScanState::Failed) {
    server_.send(500, "application/json", "{\"status\":\"failed\"}");
    return;
  }

  const std::vector<WifiNetwork>& src = nets.empty() ? wifi_->lastScan() : nets;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& n : src) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = jsonSafeSsid(n.ssid);
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

  // B2: write endpoints require auth (default SoftAP derived password).
  String expect;
  if (settingsLock(pdMS_TO_TICKS(100))) {
    expect = effectiveWebWritePassword(gSettings);
    settingsUnlock();
  } else {
    expect = derivedSoftApPassword();
  }
  const char* got = doc["auth"].is<const char*>() ? doc["auth"].as<const char*>() : "";
  if (expect.isEmpty() || got == nullptr || expect != String(got)) {
    server_.send(401, "text/plain", "Unauthorized");
    return;
  }

  // Optional password overrides (still require auth above).
  bool touchMgmt = false;
  if (doc["apPassword"].is<const char*>()) {
    String ap = doc["apPassword"].as<const char*>();
    if (ap == "null") {
      ap = "";
    }
    if (!ap.isEmpty() && ap.length() < 8) {
      server_.send(400, "text/plain", "apPassword must be >=8 chars");
      return;
    }
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.apPassword = ap;
      settingsUnlock();
      touchMgmt = true;
    }
  }
  if (doc["webPassword"].is<const char*>()) {
    String wp = doc["webPassword"].as<const char*>();
    if (wp == "null") {
      wp = "";
    }
    if (settingsLock(pdMS_TO_TICKS(100))) {
      gSettings.webPassword = wp;
      settingsUnlock();
      touchMgmt = true;
    }
  }
  if (touchMgmt && settingsLock(pdMS_TO_TICKS(100))) {
    AppSettings copy = gSettings;
    settingsUnlock();
    gStore.save(copy);
  }

  // Reuse NVS WiFi without retyping password (SoftAP escape / post-OTA).
  if (doc["reconnectSaved"] == true) {
    String ssid;
    String pass;
    if (settingsLock(pdMS_TO_TICKS(200))) {
      ssid = gSettings.wifiSsid;
      pass = gSettings.wifiPass;
      settingsUnlock();
    }
    if (ssid.isEmpty()) {
      server_.send(400, "text/plain", "No saved WiFi");
      return;
    }
    pendingSsid_ = ssid;
    pendingPass_ = pass;
    pendingConnect_ = true;
    server_.send(200, "text/plain", "Reconnecting with saved WiFi...");
    return;
  }

  bool savedPolicy = false;
  if (!doc["anomalyPolicy"].isNull()) {
    const int v = doc["anomalyPolicy"].as<int>();
    if (v >= 0 && v <= static_cast<int>(AnomalyPolicy::HoldoverLong)) {
      AppSettings copy;
      bool locked = false;
      if (settingsLock(pdMS_TO_TICKS(200))) {
        gSettings.anomalyPolicy = static_cast<AnomalyPolicy>(v);
        const uint16_t defHold = anomalyPolicyDefaultHoldoverSec(gSettings.anomalyPolicy);
        if (defHold > 0) {
          gSettings.holdoverSec = defHold;
        }
        if (!doc["holdoverSec"].isNull()) {
          uint16_t hs = doc["holdoverSec"].as<uint16_t>();
          if (hs < 10) hs = 10;
          if (hs > 600) hs = 600;
          gSettings.holdoverSec = hs;
        }
        copy = gSettings;
        settingsUnlock();
        locked = true;
      }
      if (locked) {
        gStore.save(copy);
        savedPolicy = true;
      }
    }
  }

  bool savedAcl = false;
  const bool haveAclMode = !doc["ntpAclMode"].isNull();
  const bool haveAclList = doc["ntpAcl"].is<JsonArray>();
  if (haveAclMode || haveAclList) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(200))) {
      if (haveAclMode) {
        const int m = doc["ntpAclMode"].as<int>();
        gSettings.ntpAclMode =
            (m == static_cast<int>(NtpAclMode::AllowList)) ? NtpAclMode::AllowList : NtpAclMode::Off;
      }
      if (haveAclList) {
        JsonArray arr = doc["ntpAcl"].as<JsonArray>();
        uint8_t n = 0;
        for (JsonVariant v : arr) {
          if (n >= NTP_ACL_MAX_ENTRIES) {
            break;
          }
          if (!v.is<const char*>()) {
            continue;
          }
          IPAddress ip;
          if (ip.fromString(v.as<const char*>()) && static_cast<uint32_t>(ip) != 0) {
            gSettings.ntpAcl[n++] = ip;
          }
        }
        gSettings.ntpAclCount = n;
      }
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
      savedAcl = true;
    }
  }

  bool savedTemp = false;
  const bool haveTempComp = !doc["tempComp"].isNull();
  const bool haveTempCoeff = !doc["tempCoeff"].isNull();
  if (haveTempComp || haveTempCoeff) {
    AppSettings copy;
    bool locked = false;
    if (settingsLock(pdMS_TO_TICKS(200))) {
      if (haveTempComp) {
        gSettings.tempComp = doc["tempComp"].as<bool>();
      }
      if (haveTempCoeff) {
        float k = doc["tempCoeff"].as<float>();
        if (k < -5.0f) {
          k = -5.0f;
        }
        if (k > 5.0f) {
          k = 5.0f;
        }
        gSettings.tempCoeffCenti = static_cast<int16_t>(lroundf(k * 100.0f));
      }
      copy = gSettings;
      settingsUnlock();
      locked = true;
    }
    if (locked) {
      gStore.save(copy);
      savedTemp = true;
    }
  }

  // Only touch WiFi creds when ssid is a real JSON string (not missing/null).
  if (doc["ssid"].is<const char*>()) {
    pendingSsid_ = doc["ssid"].as<const char*>();
    pendingPass_ = doc["pass"].is<const char*>() ? String(doc["pass"].as<const char*>()) : String();
    if (pendingSsid_ == "null" || pendingSsid_ == "undefined") {
      pendingSsid_ = "";
    }
    if (!pendingSsid_.isEmpty()) {
      AppSettings copy;
      bool locked = false;
      if (settingsLock(pdMS_TO_TICKS(200))) {
        gSettings.wifiSsid = pendingSsid_;
        gSettings.wifiPass = pendingPass_;
        copy = gSettings;
        settingsUnlock();
        locked = true;
      }
      if (locked) {
        gStore.save(copy);
      }
      pendingConnect_ = true;
      server_.send(200, "text/plain", "Saved. Connecting...");
      return;
    }
  }

  if (savedPolicy || touchMgmt || savedAcl || savedTemp) {
    if (savedTemp) {
      server_.send(200, "text/plain", "Temp comp saved");
    } else if (savedAcl) {
      server_.send(200, "text/plain", "ACL saved");
    } else {
      server_.send(200, "text/plain", savedPolicy ? "Policy saved" : "Password updated");
    }
    return;
  }
  server_.send(400, "text/plain", "SSID or anomalyPolicy or ntpAcl required");
}

void WebPortal::handleStatus() {
  JsonDocument doc;
  doc["sta"] = wifi_->isStaConnected();
  doc["ip"] = wifi_->localIp().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["mac"] = wifi_->macAddress();
  doc["uptimeSec"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();

  AnomalyPolicy apol = AnomalyPolicy::Refuse;
  uint16_t hold = 0;
  String savedSsid;
  NtpAclMode aclMode = NtpAclMode::Off;
  uint8_t aclCount = 0;
  bool tcmp = false;
  int16_t tcpc = CLK_TEMP_COEFF_CENTI;
  IPAddress aclIps[NTP_ACL_MAX_ENTRIES];
  if (settingsLock(pdMS_TO_TICKS(20))) {
    doc["tzHours"] = gSettings.timezoneHours;
    apol = gSettings.anomalyPolicy;
    hold = gSettings.holdoverSec;
    savedSsid = gSettings.wifiSsid;
    tcmp = gSettings.tempComp;
    tcpc = gSettings.tempCoeffCenti;
    aclMode = gSettings.ntpAclMode;
    aclCount = gSettings.ntpAclCount;
    if (aclCount > NTP_ACL_MAX_ENTRIES) {
      aclCount = NTP_ACL_MAX_ENTRIES;
    }
    for (uint8_t i = 0; i < aclCount; ++i) {
      aclIps[i] = gSettings.ntpAcl[i];
    }
    settingsUnlock();
  } else {
    doc["tzHours"] = 8;
  }
  doc["anomalyPolicy"] = static_cast<uint8_t>(apol);
  doc["anomalyLabel"] = anomalyPolicyMenuLabel(apol);
  doc["holdoverSec"] = hold;
  doc["savedSsid"] = savedSsid;
  doc["hasSavedWifi"] = !savedSsid.isEmpty();
  doc["softApPasswordDefault"] = derivedSoftApPassword();
  doc["ntpAclMode"] = static_cast<uint8_t>(aclMode);
  doc["ntpAclLabel"] = ntpAclModeMenuLabel(aclMode);
  doc["tempComp"] = tcmp;
  doc["tempCoeff"] = tempCoeffPpmPerC(tcpc);
  {
    JsonArray arr = doc["ntpAcl"].to<JsonArray>();
    for (uint8_t i = 0; i < aclCount; ++i) {
      arr.add(aclIps[i].toString());
    }
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

  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["state"] = clockStateLabel(st.clockState);
  clock["stateCode"] = static_cast<uint8_t>(st.clockState);
  clock["residualMs"] = st.residualMs;
  clock["freqPpm"] = st.freqPpm;
  clock["tempC"] = st.tempC;
  clock["tempCorrPpm"] = st.tempCorrPpm;
  clock["tempComp"] = st.tempComp;
  clock["holdoverMs"] = st.holdoverMs;

  JsonObject ntp = doc["ntp"].to<JsonObject>();
  const bool syncOk = st.timeValid && (st.clockState == ClockState::Locked ||
                                       st.clockState == ClockState::Degraded ||
                                       st.clockState == ClockState::Holdover);
  ntp["synced"] = syncOk;
  ntp["stratum"] = syncOk ? 1 : 16;
  ntp["stratum1Ready"] = st.timeValid && st.clockState == ClockState::Locked && st.ppsFresh;
  ntp["refId"] = syncOk ? "GPSS" : "INIT";
  // LI is leap-second indicator only; holdover stays LI=0 with rising dispersion.
  ntp["li"] = syncOk ? 0 : 3;
  ntp["requests"] = ntp_ ? ntp_->requestCount() : 0;
  ntp["served"] = ntp_ ? ntp_->servedCount() : 0;
  ntp["rateLimited"] = ntp_ ? ntp_->rateLimitedCount() : 0;
  ntp["denied"] = ntp_ ? ntp_->deniedCount() : 0;
  ntp["dropped"] = ntp_ ? ntp_->droppedCount() : 0;
  ntp["aclDenied"] = ntp_ ? ntp_->aclDeniedCount() : 0;
  ntp["clients"] = ntp_ ? ntp_->activeClientCount() : 0;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebPortal::handleMetrics() {
  // Prometheus-ish text; no auth (read-only, same as /status).
  char buf[640];
  const uint32_t served = ntp_ ? ntp_->servedCount() : 0;
  const uint32_t rate = ntp_ ? ntp_->rateLimitedCount() : 0;
  const uint32_t denied = ntp_ ? ntp_->deniedCount() : 0;
  const uint32_t dropped = ntp_ ? ntp_->droppedCount() : 0;
  const uint32_t aclDenied = ntp_ ? ntp_->aclDeniedCount() : 0;
  const uint32_t reqs = ntp_ ? ntp_->requestCount() : 0;
  const uint8_t clients = ntp_ ? ntp_->activeClientCount() : 0;
  const unsigned heap = ESP.getFreeHeap();
  const unsigned aclMode = ntp_ ? static_cast<unsigned>(ntp_->aclMode()) : 0;
  const unsigned aclCount = ntp_ ? ntp_->aclCount() : 0;
  snprintf(buf, sizeof(buf),
           "# TYPE ntp_requests_total counter\n"
           "ntp_requests_total %lu\n"
           "# TYPE ntp_served_total counter\n"
           "ntp_served_total %lu\n"
           "# TYPE ntp_rate_limited_total counter\n"
           "ntp_rate_limited_total %lu\n"
           "# TYPE ntp_denied_total counter\n"
           "ntp_denied_total %lu\n"
           "# TYPE ntp_dropped_total counter\n"
           "ntp_dropped_total %lu\n"
           "# TYPE ntp_acl_denied_total counter\n"
           "ntp_acl_denied_total %lu\n"
           "# TYPE ntp_acl_mode gauge\n"
           "ntp_acl_mode %u\n"
           "# TYPE ntp_acl_entries gauge\n"
           "ntp_acl_entries %u\n"
           "# TYPE ntp_clients gauge\n"
           "ntp_clients %u\n"
           "# TYPE esp_free_heap_bytes gauge\n"
           "esp_free_heap_bytes %u\n",
           static_cast<unsigned long>(reqs), static_cast<unsigned long>(served),
           static_cast<unsigned long>(rate), static_cast<unsigned long>(denied),
           static_cast<unsigned long>(dropped), static_cast<unsigned long>(aclDenied), aclMode,
           aclCount, static_cast<unsigned>(clients), heap);
  server_.send(200, "text/plain; charset=utf-8", buf);
}
