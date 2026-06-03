#include "webconfig.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

constexpr char AP_PASSWORD[] = "";
constexpr uint8_t DNS_PORT = 53;
constexpr char SESSION_COOKIE[] = "waage_session";
constexpr char SESSION_TOKEN[] = "authenticated";

static WebServer *webServer = nullptr;
static DNSServer *dnsServer = nullptr;
static bool running = false;
static unsigned long lastActivity = 0;
static WaageConfig *liveConfig = nullptr;

// Calibration state
static bool calRunning = false;
static bool calDone = false;
static int liveBattPercent = -1;

static void touchActivity() { lastActivity = millis(); }

// ── Session auth
// ──────────────────────────────────────────────────────────────

static bool isAuthenticated() {
  if (!webServer->hasHeader("Cookie"))
    return false;
  String cookie = webServer->header("Cookie");
  String expected = String(SESSION_COOKIE) + "=" + SESSION_TOKEN;
  return cookie.indexOf(expected) >= 0;
}

static void requireAuth() {
  webServer->sendHeader("Location", "/login", true);
  webServer->send(302, "text/plain", "");
}

// ── HTML helpers
// ──────────────────────────────────────────────────────────────

static const char CSS[] PROGMEM = R"css(
body{font-family:Arial,sans-serif;max-width:500px;margin:20px auto;padding:0 16px}
h2,h3{text-align:center}
.status{background:#f0f4f8;border-radius:8px;padding:12px 16px;margin-bottom:20px;font-size:14px;display:flex;gap:16px;flex-wrap:wrap}
.status span{white-space:nowrap}
.section{border:1px solid #e0e0e0;border-radius:8px;padding:16px;margin-bottom:16px}
.section-admin{border-color:#FFB74D}
.form-group{margin-bottom:14px}
label{display:block;font-weight:bold;font-size:14px;margin-bottom:2px}
.hint{font-size:12px;color:#888;margin-bottom:4px;display:block}
input[type=text],input[type=number],input[type=password],select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}
input[type=checkbox]{width:auto;margin-right:6px}
button,a.btn{display:block;width:100%;padding:12px;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:15px;margin-top:8px;text-align:center;text-decoration:none;box-sizing:border-box}
.btn-green{background:#4CAF50}.btn-green:hover{background:#45a049}
.btn-orange{background:#FF9800}.btn-orange:hover{background:#F57C00}
.btn-blue{background:#2196F3}.btn-blue:hover{background:#1976D2}
.btn-red{background:#f44336}.btn-red:hover{background:#d32f2f}
.progress{text-align:center;padding:16px;color:#2196F3;font-size:16px}
)css";

static String pageHead(const char *title) {
  String h =
      F("<!DOCTYPE html><html lang='de'><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>");
  h += title;
  h += F("</title><style>");
  h += FPSTR(CSS);
  h += F("</style></head><body><h2>&#x2696;&#xFE0F; 100-Waage <span style='font-size:12px;color:#888;font-weight:normal'>Build ");
  h += __DATE__;
  h += F("</span></h2>");
  return h;
}

static String statusBar() {
  return F("<div class='status' id='statusBar'>"
           "<span>Gewicht: <b id='sW'>--</b> g</span>"
           "<span>Akku: <b id='sB'>--</b>%</span>"
           "<span>Modus: <b id='sM'>--</b></span>"
           "</div>"
           "<script>"
           "function upd(){fetch('/status').then(r=>r.json()).then(d=>{"
           "document.getElementById('sW').textContent=d.weight;"
           "document.getElementById('sB').textContent=d.batteryPercent!==null?"
           "d.batteryPercent:'N/A';"
           "document.getElementById('sM').textContent=d.scaleMode;"
           "}).catch(()=>{});}"
           "setInterval(upd,3000);upd();"
           "</script>");
}

// ── Public page: /
// ────────────────────────────────────────────────────────────

static void handleRoot() {
  touchActivity();
  String html = pageHead("Einstellungen");
  html += statusBar();
  html += F("<div class='section'>"
            "<h3 style='margin-top:0'>Einstellungen</h3>"
            "<form action='/save' method='POST'>"
            "<div class='form-group'>"
            "<label>Zielgewicht [g]</label>"
            "<span class='hint'>Wie viel soll getrunken werden?</span>"
            "<input type='number' step='0.1' name='goal' value='");
  html += String(liveConfig->goal, 1);
  html += F("'></div>"
            "<div class='form-group'>"
            "<label style='font-weight:normal'>"
            "<input type='checkbox' name='randomModeEnabled' value='1'");
  if (liveConfig->randomModeEnabled) html += F(" checked");
  html += F("> Zuf&auml;lliges Zielgewicht</label></div>"
            "<div class='form-group'>"
            "<label>Zufall Minimum [g]</label>"
            "<span class='hint'>Maximum ist das Zielgewicht</span>"
            "<input type='number' step='0.1' name='randomMin' value='");
  html += String(liveConfig->randomMin, 1);
  html += F("'></div>"
            "<div class='form-group'>"
            "<label>Display-Rotation</label>"

            "<select name='displayRotation'>"
            "<option value='0'");
  if (liveConfig->displayRotation == 0)
    html += F(" selected");
  html += F(">Normal (0&deg;)</option>"
            "<option value='2'");
  if (liveConfig->displayRotation == 2)
    html += F(" selected");
  html +=
      F(">Gedreht (180&deg;)</option>"
        "</select></div>"
        "<button type='submit' class='btn-green'>&#x1F4BE; Speichern</button>"
        "</form></div>"
        "<a href='/admin' class='btn btn-orange'>&#x1F512; "
        "Admin-Einstellungen</a>"
        "</body></html>");
  webServer->send(200, "text/html", html);
}

static void handleSave() {
  touchActivity();
  if (webServer->hasArg("goal") && webServer->arg("goal").length() > 0)
    liveConfig->goal = webServer->arg("goal").toFloat();
  if (webServer->hasArg("displayRotation") &&
      webServer->arg("displayRotation").length() > 0) {
    liveConfig->displayRotation =
        (uint8_t)webServer->arg("displayRotation").toInt();
    display.setRotation(liveConfig->displayRotation);
  }
  liveConfig->randomModeEnabled = webServer->hasArg("randomModeEnabled");
  if (webServer->hasArg("randomMin") && webServer->arg("randomMin").length() > 0) {
    liveConfig->randomMin = webServer->arg("randomMin").toFloat();
    if (liveConfig->randomMin < liveConfig->tolerance) liveConfig->randomMin = liveConfig->tolerance;
    if (liveConfig->randomMin > liveConfig->goal) liveConfig->randomMin = liveConfig->goal;
  }
  saveConfig(*liveConfig);
  webServer->sendHeader("Location", "/", true);
  webServer->send(302, "text/plain", "");
}

// ── Admin login
// ───────────────────────────────────────────────────────────────

static void handleAdminLogin() {
  touchActivity();
  String html = pageHead("Admin-Login");
  html += F("<div class='section section-admin'>"
            "<h3 style='margin-top:0'>&#x1F512; Admin-Login</h3>"
            "<form action='/login' method='POST'>"
            "<div class='form-group'>"
            "<label>Passwort</label>"
            "<input type='password' name='password' autofocus "
            "autocomplete='current-password'>"
            "</div>"
            "<button type='submit' class='btn-orange'>Einloggen</button>"
            "</form></div></body></html>");
  webServer->send(200, "text/html", html);
}

static void handleLogin() {
  touchActivity();
  if (webServer->hasArg("password") &&
      webServer->arg("password") == String(liveConfig->adminPassword)) {
    String cookie =
        String(SESSION_COOKIE) + "=" + SESSION_TOKEN + "; Path=/; HttpOnly";
    webServer->sendHeader("Set-Cookie", cookie);
    webServer->sendHeader("Location", "/admin", true);
    webServer->send(302, "text/plain", "");
  } else {
    String html = pageHead("Admin-Login");
    html += F("<div class='section section-admin'>"
              "<h3 style='margin-top:0'>&#x1F512; Admin-Login</h3>"
              "<p style='color:red;text-align:center'>Falsches Passwort</p>"
              "<form action='/login' method='POST'>"
              "<div class='form-group'>"
              "<label>Passwort</label>"
              "<input type='password' name='password' autofocus>"
              "</div>"
              "<button type='submit' class='btn-orange'>Einloggen</button>"
              "</form></div></body></html>");
    webServer->send(401, "text/html", html);
  }
}

static void handleLogout() {
  webServer->sendHeader("Set-Cookie",
                        String(SESSION_COOKIE) + "=; Path=/; Max-Age=0");
  webServer->sendHeader("Location", "/admin", true);
  webServer->send(302, "text/plain", "");
}

// ── Admin dashboard
// ───────────────────────────────────────────────────────────

static void handleAdmin() {
  touchActivity();
  if (!isAuthenticated()) {
    requireAuth();
    return;
  }

  String html = pageHead("Admin");
  html += F("<div class='section section-admin'>"
            "<h3 style='margin-top:0'>&#x1F512; Admin-Einstellungen</h3>"
            "<form action='/admin/save' method='POST'>"

            "<div class='form-group'>"
            "<label>Access-Point SSID</label>"
            "<input type='text' name='apSSID' value='");
  html += String(liveConfig->apSSID);
  html +=
      F("'></div>"

        "<div class='form-group'>"
        "<label>Toleranz [g]</label>"
        "<span class='hint'>Messtoleranz f&uuml;r Start/Stop-Erkennung</span>"
        "<input type='number' step='0.1' name='tolerance' value='");
  html += String(liveConfig->tolerance, 1);
  html += F("'></div>"

            "<div class='form-group'>"
            "<label>Auto-Reset Bereich [%]</label>"
            "<span class='hint'>Schlechte Ergebnisse au&szlig;erhalb dieses "
            "Bereichs werden automatisch zur&uuml;ckgesetzt</span>"
            "<input type='number' step='1' min='0' max='100' "
            "name='autoResetRange' value='");
  html += String(liveConfig->autoResetRange);
  html += F("'></div>"

            "<div class='form-group'>"
            "<label>WiFi Auto-Aus nach [min]</label>"
            "<span class='hint'>0 = nie</span>"
            "<input type='number' step='1' min='0' max='255' "
            "name='wifiTimeout' value='");
  html += String(liveConfig->wifiTimeout);
  html += F("'></div>"

            "<div class='form-group'>"
            "<label>Deep-Sleep nach [min] Inaktivit&auml;t</label>"
            "<span class='hint'>0 = nie</span>"
            "<input type='number' step='1' min='0' max='255' "
            "name='sleepTimeout' value='");
  html += String(liveConfig->sleepTimeout);
  html += F("'></div>"

            "<div class='form-group'>"
            "<label>Auto-Zero</label>"
            "<span class='hint'>Automatischer Nullabgleich bei stabiler "
            "Leermessung</span>"
            "<label style='font-weight:normal'>"
            "<input type='checkbox' name='autoZeroEnabled' value='1'");
  if (liveConfig->autoZeroEnabled)
    html += F(" checked");
  html += F("> Aktiviert</label></div>"

            "<div class='form-group'>"
            "<label>Auto-Zero Schwellwert [g]</label>"
            "<span class='hint'>Maximalgewicht das als 'leer' gilt</span>"
            "<input type='number' step='0.1' min='0.1' "
            "name='autoZeroThreshold' value='");
  html += String(liveConfig->autoZeroThreshold, 1);
  html +=
      F("'></div>"

        "<div class='form-group'>"
        "<label>Auto-Zero Verz&ouml;gerung [s]</label>"
        "<span class='hint'>Wie lange die Waage stabil leer sein muss</span>"
        "<input type='number' step='1' min='1' max='60' name='autoZeroDelay' "
        "value='");
  html += String(liveConfig->autoZeroDelay);
  html += F(
      "'></div>"

      "<div class='form-group'>"
      "<label>Neues Passwort</label>"
      "<span class='hint'>Leer lassen = nicht &auml;ndern (min. 4 "
      "Zeichen)</span>"
      "<input type='password' name='newPassword' autocomplete='new-password'>"
      "</div>"

      "<button type='submit' class='btn-orange'>&#x1F4BE; Speichern &amp; "
      "Neustarten</button>"
      "</form></div>"

      "<div class='section'>"
      "<h3 style='margin-top:0'>&#x1F527; Kalibrierung</h3>"
      "<div id='calForm'>"
      "<form action='/calibrate' method='POST' onsubmit='startCal(event)'>"
      "<div class='form-group'>"
      "<label>Bekanntes Gewicht [g]</label>"
      "<span class='hint'>Gewicht auf die Waage legen, dann starten</span>"
      "<input type='number' step='0.1' name='weight' placeholder='z.B. 200.0' "
      "required>"
      "</div>"
      "<button type='submit' class='btn-blue'>&#x1F3AF; Kalibrierung "
      "starten</button>"
      "</form></div>"
      "<div id='calProgress' style='display:none'>"
      "<div class='progress'>&#x23F3; Kalibrierung l&auml;uft...</div>"
      "<div id='calResult'></div>"
      "</div>"
      "</div>"

      "<a href='/logout' class='btn btn-red'>&#x1F513; Ausloggen</a>"
      "<a href='/' class='btn btn-green' style='margin-top:8px'>&#x2190; "
      "Zur&uuml;ck</a>"

      "<script>"
      "function startCal(e){"
      "e.preventDefault();"
      "var w=e.target.weight.value;"
      "document.getElementById('calForm').style.display='none';"
      "document.getElementById('calProgress').style.display='';"
      "fetch('/calibrate',{method:'POST',headers:{'Content-Type':'application/"
      "x-www-form-urlencoded'},"
      "body:'weight='+encodeURIComponent(w)});"
      "pollCal();}"
      "function pollCal(){"
      "fetch('/calibrate/status').then(r=>r.json()).then(d=>{"
      "if(d.state==='done'){"
      "document.querySelector('#calProgress .progress').textContent='\\u2713 "
      "Fertig!';"
      "document.getElementById('calResult').innerHTML='<p>Neuer Faktor: "
      "<b>'+d.scaleFactor.toFixed(4)+'</b></p>';"
      "}else{setTimeout(pollCal,500);}}).catch(()=>setTimeout(pollCal,1000));}"
      "</script>"
      "</body></html>");
  webServer->send(200, "text/html", html);
}

static void handleAdminSave() {
  touchActivity();
  if (!isAuthenticated()) {
    requireAuth();
    return;
  }

  if (webServer->hasArg("apSSID") && webServer->arg("apSSID").length() > 0)
    webServer->arg("apSSID").toCharArray(liveConfig->apSSID,
                                         sizeof(liveConfig->apSSID));
  if (webServer->hasArg("tolerance") &&
      webServer->arg("tolerance").length() > 0)
    liveConfig->tolerance = webServer->arg("tolerance").toFloat();
  if (webServer->hasArg("autoResetRange") &&
      webServer->arg("autoResetRange").length() > 0)
    liveConfig->autoResetRange =
        (uint8_t)webServer->arg("autoResetRange").toInt();
  if (webServer->hasArg("wifiTimeout") &&
      webServer->arg("wifiTimeout").length() > 0)
    liveConfig->wifiTimeout = (uint8_t)webServer->arg("wifiTimeout").toInt();
  if (webServer->hasArg("sleepTimeout") &&
      webServer->arg("sleepTimeout").length() > 0)
    liveConfig->sleepTimeout = (uint8_t)webServer->arg("sleepTimeout").toInt();

  liveConfig->autoZeroEnabled = webServer->hasArg("autoZeroEnabled");
  if (webServer->hasArg("autoZeroThreshold") &&
      webServer->arg("autoZeroThreshold").length() > 0)
    liveConfig->autoZeroThreshold =
        webServer->arg("autoZeroThreshold").toFloat();
  if (webServer->hasArg("autoZeroDelay") &&
      webServer->arg("autoZeroDelay").length() > 0)
    liveConfig->autoZeroDelay =
        (uint8_t)webServer->arg("autoZeroDelay").toInt();

  if (webServer->hasArg("newPassword") &&
      webServer->arg("newPassword").length() >= 4)
    webServer->arg("newPassword")
        .toCharArray(liveConfig->adminPassword,
                     sizeof(liveConfig->adminPassword));

  saveConfig(*liveConfig);

  webServer->send(
      200, "text/html",
      pageHead("Gespeichert") +
          F("<p style='text-align:center;color:green;font-size:18px'>&#x2713; "
            "Gespeichert! Starte neu...</p>"
            "<script>setTimeout(()=>window.location.href='/',3000)</script>"
            "</body></html>"));
  delay(2000);
  ESP.restart();
}

// ── Status JSON
// ───────────────────────────────────────────────────────────────

static void handleStatus() {
  touchActivity();
  String json = "{";
  json += "\"weight\":" + String(getCurrentWeight(), 2) + ",";
  json += "\"batteryPercent\":" +
          (liveBattPercent >= 0 ? String(liveBattPercent) : "null") + ",";
  json +=
      "\"scaleMode\":\"" +
      String(getCurrentScaleMode() == ScaleMode::Game ? "Game" : "Standard") +
      "\",";
  json += "\"wifiActive\":true";
  json += "}";
  webServer->send(200, "application/json", json);
}

// ── Calibration (async)
// ───────────────────────────────────────────────────────

static void handleCalibrateStart() {
  touchActivity();
  if (!isAuthenticated()) {
    webServer->send(401, "text/plain", "");
    return;
  }
  if (!webServer->hasArg("weight")) {
    webServer->send(400, "text/plain", "weight missing");
    return;
  }

  float knownWeight = webServer->arg("weight").toFloat();
  webServer->send(200, "text/plain", "ok");

  calRunning = true;
  calDone = false;
  displayLines("Kalibrierung", "Gewicht legen");

  liveConfig->scaleFactor = calibrateScale(knownWeight);
  saveConfig(*liveConfig);

  calDone = true;
  calRunning = false;
}

static void handleCalibrateStatus() {
  touchActivity();
  String json = "{\"state\":\"";
  json += calDone ? "done" : "running";
  json += "\",\"scaleFactor\":";
  json += String(liveConfig->scaleFactor, 4);
  json += "}";
  webServer->send(200, "application/json", json);
}

// ── AP event handler
// ──────────────────────────────────────────────────────────

static void onWifiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    delay(100);
    WiFi.softAP(liveConfig->apSSID, AP_PASSWORD);
  }
}

// ── Public API
// ────────────────────────────────────────────────────────────────

void startWebServer(const WaageConfig &cfg) {
  if (running)
    return;
  liveConfig = const_cast<WaageConfig *>(&cfg);

  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(100);
  WiFi.softAP(liveConfig->apSSID, AP_PASSWORD, 1, 0, 4);

  delay(500);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP: ");
  Serial.print(liveConfig->apSSID);
  Serial.print("  IP: ");
  Serial.println(ip);

  displayText(String(liveConfig->apSSID));

  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", ip);

  webServer = new WebServer(80);
  static const char *headers[] = {"Cookie"};
  webServer->collectHeaders(headers, 1);
  webServer->on("/", HTTP_GET, handleRoot);
  webServer->on("/save", HTTP_POST, handleSave);
  webServer->on("/admin", HTTP_GET, handleAdmin);
  webServer->on("/admin/save", HTTP_POST, handleAdminSave);
  webServer->on("/login", HTTP_GET, handleAdminLogin);
  webServer->on("/login", HTTP_POST, handleLogin);
  webServer->on("/logout", HTTP_GET, handleLogout);
  webServer->on("/status", HTTP_GET, handleStatus);
  webServer->on("/calibrate", HTTP_POST, handleCalibrateStart);
  webServer->on("/calibrate/status", HTTP_GET, handleCalibrateStatus);
  webServer->onNotFound([]() {
    webServer->sendHeader("Location", "/", true);
    webServer->send(302, "text/plain", "");
  });
  webServer->begin();

  lastActivity = millis();
  running = true;
}

void stopWebServer() {
  if (!running)
    return;
  if (webServer) {
    webServer->stop();
    delete webServer;
    webServer = nullptr;
  }
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
  liveConfig = nullptr;
}

void handleWebRequests() {
  if (!running)
    return;
  if (dnsServer)
    dnsServer->processNextRequest();
  if (webServer)
    webServer->handleClient();
}

bool isWebServerRunning() { return running; }
unsigned long getLastHttpActivity() { return lastActivity; }
void setLiveBatteryPercent(int p) { liveBattPercent = p; }
