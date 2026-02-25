#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

#define AP_SSID     "100-Waage-Config"
#define AP_PASSWORD ""

// ── EEPROM addresses ──────────────────────────────────────────────────────────
#define EEPROM_SIZE                 512
#define EEPROM_AP_SSID_ADDR           0
#define EEPROM_SCALE_FACTOR_ADDR     64
#define EEPROM_TARE_OFFSET_ADDR      68
#define EEPROM_GOAL_ADDR             72
#define EEPROM_TOLERANCE_ADDR        76
#define EEPROM_DISPLAY_ROTATION_ADDR 80
#define EEPROM_VALID_FLAG_ADDR       81
// Extended fields (guarded by EEPROM_EXT_VALID_ADDR == 0xBB)
#define EEPROM_ADMIN_PWD_ADDR        82   // 32 bytes
#define EEPROM_WIFI_TIMEOUT_ADDR    114
#define EEPROM_SLEEP_TIMEOUT_ADDR   115
#define EEPROM_BATT_DIVIDER_ADDR    116   // 4 bytes (float)
#define EEPROM_SCALE_MODE_ADDR      120
#define EEPROM_AUTO_RESET_RANGE_ADDR 121
#define EEPROM_EXT_VALID_ADDR       122

// ── Globals ───────────────────────────────────────────────────────────────────
WebServer*   webServer          = nullptr;
DNSServer*   dnsServer          = nullptr;
const byte   DNS_PORT           = 53;
TaskHandle_t webConfigTaskHandle = nullptr;
unsigned long lastHttpActivity  = 0;

WaageConfig config;

unsigned long getLastHttpActivity() { return lastHttpActivity; }

// Forward declaration
String buildConfigHTML();

/* ===== EEPROM ================================================================ */
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  config.valid = (EEPROM.read(EEPROM_VALID_FLAG_ADDR) == 0xAA);

  if (config.valid) {
    for (int i = 0; i < 64; i++)
      config.apSSID[i] = EEPROM.read(EEPROM_AP_SSID_ADDR + i);
    EEPROM.get(EEPROM_SCALE_FACTOR_ADDR,     config.scaleFactor);
    EEPROM.get(EEPROM_TARE_OFFSET_ADDR,      config.tareOffset);
    EEPROM.get(EEPROM_GOAL_ADDR,             config.goal);
    EEPROM.get(EEPROM_TOLERANCE_ADDR,        config.tolerance);
    config.displayRotation = EEPROM.read(EEPROM_DISPLAY_ROTATION_ADDR);

    // Extended fields
    if (EEPROM.read(EEPROM_EXT_VALID_ADDR) == 0xBB) {
      for (int i = 0; i < 32; i++)
        config.adminPassword[i] = EEPROM.read(EEPROM_ADMIN_PWD_ADDR + i);
      config.wifiTimeout     = EEPROM.read(EEPROM_WIFI_TIMEOUT_ADDR);
      config.sleepTimeout    = EEPROM.read(EEPROM_SLEEP_TIMEOUT_ADDR);
      EEPROM.get(EEPROM_BATT_DIVIDER_ADDR,   config.battDividerRatio);
      config.scaleMode       = EEPROM.read(EEPROM_SCALE_MODE_ADDR);
      config.autoResetRange  = EEPROM.read(EEPROM_AUTO_RESET_RANGE_ADDR);
    } else {
      strcpy(config.adminPassword, "admin");
      config.wifiTimeout     = 10;
      config.sleepTimeout    = 5;
      config.battDividerRatio = 2.0f;
      config.scaleMode       = 0;
      config.autoResetRange  = 10;
    }
  } else {
    strcpy(config.apSSID,       AP_SSID);
    config.scaleFactor      = 708.0f;
    config.tareOffset       = 0;
    config.goal             = 100.0f;
    config.tolerance        = 10.0f;
    config.displayRotation  = 2;
    strcpy(config.adminPassword, "admin");
    config.wifiTimeout      = 10;
    config.sleepTimeout     = 5;
    config.battDividerRatio = 2.0f;
    config.scaleMode        = 0;
    config.autoResetRange   = 10;
  }

  EEPROM.end();
}

// Called from waage.ino boot to wipe valid flags → forces factory defaults
void clearConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(EEPROM_VALID_FLAG_ADDR, 0x00);
  EEPROM.write(EEPROM_EXT_VALID_ADDR,  0x00);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("EEPROM cleared — using defaults");
}

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);

  for (int i = 0; i < 64; i++)
    EEPROM.write(EEPROM_AP_SSID_ADDR + i, config.apSSID[i]);
  EEPROM.put(EEPROM_SCALE_FACTOR_ADDR,     config.scaleFactor);
  EEPROM.put(EEPROM_TARE_OFFSET_ADDR,      config.tareOffset);
  EEPROM.put(EEPROM_GOAL_ADDR,             config.goal);
  EEPROM.put(EEPROM_TOLERANCE_ADDR,        config.tolerance);
  EEPROM.write(EEPROM_DISPLAY_ROTATION_ADDR, config.displayRotation);
  EEPROM.write(EEPROM_VALID_FLAG_ADDR,     0xAA);

  // Extended fields
  for (int i = 0; i < 32; i++)
    EEPROM.write(EEPROM_ADMIN_PWD_ADDR + i, config.adminPassword[i]);
  EEPROM.write(EEPROM_WIFI_TIMEOUT_ADDR,   config.wifiTimeout);
  EEPROM.write(EEPROM_SLEEP_TIMEOUT_ADDR,  config.sleepTimeout);
  EEPROM.put(EEPROM_BATT_DIVIDER_ADDR,     config.battDividerRatio);
  EEPROM.write(EEPROM_SCALE_MODE_ADDR,     config.scaleMode);
  EEPROM.write(EEPROM_AUTO_RESET_RANGE_ADDR, config.autoResetRange);
  EEPROM.write(EEPROM_EXT_VALID_ADDR,      0xBB);

  EEPROM.commit();
  EEPROM.end();
}

void persistScaleMode(uint8_t mode) {
  config.scaleMode = mode;
  saveConfig();
}

/* ===== Web handlers ========================================================= */

static void touchActivity() { lastHttpActivity = millis(); }

void handleRoot() {
  touchActivity();
  webServer->send(200, "text/html", buildConfigHTML());
}

void handleSave() {
  touchActivity();

  bool isAdmin = webServer->hasArg("adminPassword") &&
                 webServer->arg("adminPassword") == String(config.adminPassword);

  // ── Public fields (always accepted) ──────────────────────────────────────
  if (webServer->hasArg("goal") && webServer->arg("goal").length() > 0)
    config.goal = webServer->arg("goal").toFloat();

  if (webServer->hasArg("displayRotation") && webServer->arg("displayRotation").length() > 0) {
    config.displayRotation = (uint8_t)webServer->arg("displayRotation").toInt();
    extern Adafruit_SSD1306 display;
    display.setRotation(config.displayRotation);
  }

  // ── Admin fields (only with correct password) ─────────────────────────────
  bool needsRestart = false;
  if (isAdmin) {
    needsRestart = true;
    if (webServer->hasArg("apSSID") && webServer->arg("apSSID").length() > 0)
      webServer->arg("apSSID").toCharArray(config.apSSID, 64);
    if (webServer->hasArg("scaleFactor") && webServer->arg("scaleFactor").length() > 0)
      config.scaleFactor = webServer->arg("scaleFactor").toFloat();
    if (webServer->hasArg("tareOffset") && webServer->arg("tareOffset").length() > 0)
      config.tareOffset = webServer->arg("tareOffset").toInt();
    if (webServer->hasArg("tolerance") && webServer->arg("tolerance").length() > 0)
      config.tolerance = webServer->arg("tolerance").toFloat();
    if (webServer->hasArg("wifiTimeout") && webServer->arg("wifiTimeout").length() > 0)
      config.wifiTimeout = (uint8_t)webServer->arg("wifiTimeout").toInt();
    if (webServer->hasArg("sleepTimeout") && webServer->arg("sleepTimeout").length() > 0)
      config.sleepTimeout = (uint8_t)webServer->arg("sleepTimeout").toInt();
    if (webServer->hasArg("battDividerRatio") && webServer->arg("battDividerRatio").length() > 0)
      config.battDividerRatio = webServer->arg("battDividerRatio").toFloat();
    if (webServer->hasArg("autoResetRange") && webServer->arg("autoResetRange").length() > 0)
      config.autoResetRange = (uint8_t)webServer->arg("autoResetRange").toInt();
    if (webServer->hasArg("newAdminPassword") && webServer->arg("newAdminPassword").length() >= 4)
      webServer->arg("newAdminPassword").toCharArray(config.adminPassword, 32);
  }

  config.valid = true;
  saveConfig();

  // Update runtime goal immediately
  extern float goal;
  goal = config.goal;

  if (needsRestart) {
    webServer->send(200, "text/html",
      R"(<!DOCTYPE html><html lang="de"><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gespeichert</title></head><body style="font-family:Arial;text-align:center;margin-top:40px">
<h2>&#x2696;&#xFE0F; Waage Konfiguration</h2>
<p style="color:green;font-size:20px">&#x2713; Admin-Einstellungen gespeichert!</p>
<p>Die Waage wird neu gestartet...</p></body></html>)");
    delay(2000);
    ESP.restart();
  } else {
    webServer->send(200, "text/html",
      R"(<!DOCTYPE html><html lang="de"><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gespeichert</title></head><body style="font-family:Arial;text-align:center;margin-top:40px">
<h2>&#x2696;&#xFE0F; Waage Konfiguration</h2>
<p style="color:green;font-size:20px">&#x2713; Einstellungen gespeichert!</p>
<p><a href="/">&#x2190; Zurück</a></p>
<script>setTimeout(()=>window.location.href='/',2000);</script></body></html>)");
  }
}

void handleStatus() {
  extern float              weight;
  extern int                batteryPercent;
  extern boolean            wifiActive;
  extern volatile ScaleMode scaleMode;
  String json = "{";
  json += "\"apSSID\":\""          + String(config.apSSID)              + "\",";
  json += "\"scaleFactor\":"       + String(config.scaleFactor, 4)      + ",";
  json += "\"tareOffset\":"        + String(config.tareOffset)          + ",";
  json += "\"weight\":"            + String(weight, 2)                  + ",";
  json += "\"goal\":"              + String(config.goal, 2)             + ",";
  json += "\"tolerance\":"         + String(config.tolerance, 2)        + ",";
  json += "\"displayRotation\":"   + String(config.displayRotation)     + ",";
  json += "\"batteryPercent\":"    + String(batteryPercent)             + ",";
  json += "\"wifiActive\":"        + String(wifiActive ? "true":"false") + ",";
  json += "\"scaleMode\":"         + String(scaleMode == 0 ? "\"Game\"" : "\"Standard\"") + ",";
  json += "\"wifiTimeout\":"       + String(config.wifiTimeout)         + ",";
  json += "\"sleepTimeout\":"      + String(config.sleepTimeout)        + ",";
  json += "\"battDividerRatio\":"  + String(config.battDividerRatio, 4) + ",";
  json += "\"autoResetRange\":"    + String(config.autoResetRange)      + ",";
  json += "\"valid\":"             + String(config.valid ? "true":"false");
  json += "}";
  webServer->send(200, "application/json", json);
}

void handleCalibrate() {
  touchActivity();
  if (!webServer->hasArg("weight")) {
    webServer->send(400, "text/plain", "Fehler: Gewicht fehlt");
    return;
  }
  float knownWeight = webServer->arg("weight").toFloat();

  String response = R"rawliteral(
<!DOCTYPE html><html lang="de">
<head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Kalibrierung</title></head>
<body style="font-family:Arial;text-align:center;margin-top:40px">
  <h2>&#x2696;&#xFE0F; Kalibrierung</h2>
  <p style="color:blue;font-size:18px">Kalibrierung läuft...</p>
  <p>Bitte )rawliteral" + String(knownWeight, 1) + R"rawliteral(g auf die Waage legen.</p>
  <script>setTimeout(()=>window.location.href='/calibrate_result',11000);</script>
</body></html>)rawliteral";

  webServer->send(200, "text/html", response);

  extern HX711 hx711;
  delay(100);
  hx711.set_scale();
  hx711.tare(10);
  displayLines("Kalibrierung", "Gewicht legen");
  delay(10000);
  hx711.calibrate_scale(knownWeight);
  config.scaleFactor = hx711.get_scale();
  saveConfig();
}

void handleCalibrateResult() {
  touchActivity();
  webServer->send(200, "text/html",
    String(R"(<!DOCTYPE html><html lang="de"><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kalibrierung</title></head><body style="font-family:Arial;text-align:center;margin-top:40px">
<h2>&#x2696;&#xFE0F; Kalibrierung</h2>
<p style="color:green;font-size:20px">&#x2713; Kalibrierung abgeschlossen!</p>
<p>Neuer Kalibrierfaktor: )") + String(config.scaleFactor, 4) +
    R"(</p><p><a href="/">&#x2190; Zurück</a></p></body></html>)");
}

/* ===== HTML ================================================================= */
String buildConfigHTML() {
  // pre-compute rotation options
  String rot0sel = (config.displayRotation == 0) ? " selected" : "";
  String rot2sel = (config.displayRotation == 2) ? " selected" : "";

  String html = F("<!DOCTYPE html>\n"
    "<html lang='de'>\n"
    "<head>\n"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
    "<title>Waage Konfiguration</title>\n"
    "<style>\n"
    "body{font-family:Arial,sans-serif;max-width:500px;margin:20px auto;padding:20px}\n"
    "h2,h3{text-align:center}\n"
    ".form-group{margin-bottom:14px}\n"
    "label{display:block;margin-bottom:2px;font-weight:bold;font-size:14px}\n"
    ".hint{font-size:12px;color:#888;margin-bottom:4px}\n"
    "input,select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}\n"
    "button{width:100%;padding:12px;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:15px;margin-top:4px}\n"
    ".btn-primary{background:#4CAF50}.btn-primary:hover{background:#45a049}\n"
    ".btn-blue{background:#2196F3}.btn-blue:hover{background:#1976D2}\n"
    ".btn-admin{background:#FF9800}.btn-admin:hover{background:#F57C00}\n"
    ".info{background:#f5f5f5;padding:10px;border-radius:4px;margin-bottom:16px;font-size:13px}\n"
    ".admin-locked{opacity:.45;pointer-events:none;transition:opacity .3s}\n"
    ".section{border:1px solid #e0e0e0;border-radius:8px;padding:16px;margin-bottom:16px}\n"
    ".section-admin{border-color:#FFB74D}\n"
    "hr{margin:24px 0}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h2>&#x2696;&#xFE0F; Waage Konfiguration</h2>\n"
    "\n"
    "<div class='info'>\n"
    "  <strong>Live-Status:</strong><br>\n"
    "  Gewicht: <span id='sWeight'>--</span> g &nbsp;|&nbsp;\n"
    "  Akku: <span id='sBatt'>--</span>%<br>\n"
    "  Modus: <span id='sMode'>--</span> &nbsp;|&nbsp;\n"
    "  WiFi: <span id='sWifi'>--</span>\n"
    "</div>\n"
    "\n"
    "<form action='/save' method='POST'>\n"
    "\n"
    "<!-- ===== PUBLIC SECTION ===== -->\n"
    "<div class='section'>\n"
    "<h3 style='margin-top:0'>&#x2699;&#xFE0F; Allgemein</h3>\n"
    "\n"
    "  <div class='form-group'>\n"
    "    <label>Zielgewicht [g]</label>\n"
    "    <span class='hint'>Wie viel soll getrunken werden?</span>\n"
    "    <input type='number' step='0.1' name='goal' id='inGoal' value='");
  html += String(config.goal, 2);
  html += F("'>\n"
    "  </div>\n"
    "\n"
    "  <div class='form-group'>\n"
    "    <label>Display-Rotation</label>\n"
    "    <span class='hint'>Ausrichtung des OLED-Displays</span>\n"
    "    <select name='displayRotation'>\n"
    "      <option value='0'");
  html += rot0sel;
  html += F(">Normal (0&deg;)</option>\n"
    "      <option value='2'");
  html += rot2sel;
  html += F(">Gedreht (180&deg;)</option>\n"
    "    </select>\n"
    "  </div>\n"
    "\n"
    "  <button type='submit' class='btn-primary'>&#x1F4BE; Speichern</button>\n"
    "</div>\n"
    "\n"
    "<!-- ===== ADMIN SECTION ===== -->\n"
    "<div class='section section-admin'>\n"
    "<h3 style='margin-top:0'>&#x1F512; Admin-Einstellungen</h3>\n"
    "\n"
    "  <div class='form-group'>\n"
    "    <label>Admin-Passwort</label>\n"
    "    <span class='hint'>Eingabe schaltet die Felder unten frei</span>\n"
    "    <input type='password' name='adminPassword' id='adminPwd'\n"
    "           oninput='onPwd(this.value)' autocomplete='current-password'\n"
    "           placeholder='Passwort eingeben...'>\n"
    "  </div>\n"
    "\n"
    "  <div id='adminSection' class='admin-locked'>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Access Point SSID</label>\n"
    "      <span class='hint'>WLAN-Name der Waage</span>\n"
    "      <input type='text' name='apSSID' value='");
  html += String(config.apSSID);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Kalibrierfaktor</label>\n"
    "      <span class='hint'>Rohwert-zu-Gramm-Umrechnungsfaktor der Wiegezelle</span>\n"
    "      <input type='number' step='0.0001' name='scaleFactor' value='");
  html += String(config.scaleFactor, 4);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Tara-Offset</label>\n"
    "      <span class='hint'>Nullpunkt-Korrektur (Rohwert)</span>\n"
    "      <input type='number' name='tareOffset' value='");
  html += String(config.tareOffset);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Toleranz [g]</label>\n"
    "      <span class='hint'>Messtoleranz f&uuml;r Start/Stop-Erkennung</span>\n"
    "      <input type='number' step='0.1' name='tolerance' value='");
  html += String(config.tolerance, 2);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Auto-Reset Bereich [%]</label>\n"
    "      <span class='hint'>Schlechte Ergebnisse (au&szlig;erhalb dieses Bereichs) werden automatisch zur&uuml;ckgesetzt</span>\n"
    "      <input type='number' step='1' min='0' max='100' name='autoResetRange' value='");
  html += String(config.autoResetRange);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Spannungsteiler-Verh&auml;ltnis (Akku)</label>\n"
    "      <span class='hint'>Teilerfaktor des Spannungsteilers am Akku-Pin</span>\n"
    "      <input type='number' step='0.01' name='battDividerRatio' value='");
  html += String(config.battDividerRatio, 4);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>WiFi Auto-Aus nach [min]</label>\n"
    "      <span class='hint'>0 = nie automatisch ausschalten</span>\n"
    "      <input type='number' step='1' min='0' max='255' name='wifiTimeout' value='");
  html += String(config.wifiTimeout);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Deep-Sleep nach [min] Inaktivit&auml;t</label>\n"
    "      <span class='hint'>0 = nie schlafen legen</span>\n"
    "      <input type='number' step='1' min='0' max='255' name='sleepTimeout' value='");
  html += String(config.sleepTimeout);
  html += F("'>\n"
    "    </div>\n"
    "\n"
    "    <div class='form-group'>\n"
    "      <label>Neues Admin-Passwort</label>\n"
    "      <span class='hint'>Mindestens 4 Zeichen &mdash; leer lassen um es nicht zu &auml;ndern</span>\n"
    "      <input type='password' name='newAdminPassword' autocomplete='new-password'\n"
    "             placeholder='Leer lassen = nicht &auml;ndern'>\n"
    "    </div>\n"
    "\n"
    "    <button type='submit' class='btn-admin'>&#x1F512; Admin-Einstellungen speichern &amp; neustarten</button>\n"
    "  </div>\n"
    "</div>\n"
    "\n"
    "</form>\n"
    "\n"
    "<!-- ===== CALIBRATION ===== -->\n"
    "<div class='section'>\n"
    "<h3 style='margin-top:0'>&#x1F527; Kalibrierung</h3>\n"
    "<div id='calSection' class='admin-locked'>\n"
    "  <form action='/calibrate' method='POST'>\n"
    "    <div class='form-group'>\n"
    "      <label>Bekanntes Gewicht [g]</label>\n"
    "      <span class='hint'>Gewicht des Kalibriergewichts eingeben, dann auf die Waage legen</span>\n"
    "      <input type='number' step='0.1' name='weight' placeholder='z.B. 50.0' required>\n"
    "    </div>\n"
    "    <button type='submit' class='btn-blue'>&#x1F3AF; Jetzt kalibrieren</button>\n"
    "  </form>\n"
    "</div>\n"
    "<p id='calLock' style='text-align:center;color:#999;font-size:13px'>\n"
    "  &#x1F512; Admin-Passwort eingeben um Kalibrierung freizuschalten\n"
    "</p>\n"
    "</div>\n"
    "\n"
    "<script>\n"
    "function onPwd(v){\n"
    "  var u=v.length>=1;\n"
    "  document.getElementById('adminSection').className=u?'':'admin-locked';\n"
    "  document.getElementById('calSection').className=u?'':'admin-locked';\n"
    "  document.getElementById('calLock').style.display=u?'none':'';\n"
    "}\n"
    "function loadStatus(){\n"
    "  fetch('/status').then(r=>r.json()).then(d=>{\n"
    "    document.getElementById('sWeight').textContent=d.weight;\n"
    "    document.getElementById('sBatt').textContent=d.batteryPercent;\n"
    "    document.getElementById('sMode').textContent=d.scaleMode;\n"
    "    document.getElementById('sWifi').textContent=d.wifiActive?'An':'Aus';\n"
    "  }).catch(()=>{});\n"
    "}\n"
    "setInterval(loadStatus,3000);\n"
    "loadStatus();\n"
    "</script>\n"
    "</body>\n"
    "</html>\n");
  return html;
}

/* ===== Web Config Task ====================================================== */
void webConfigTask(void* parameter) {
  loadConfig();

  String apName = (strlen(config.apSSID) > 0) ? String(config.apSSID) : String(AP_SSID);

  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP(apName.c_str(), AP_PASSWORD);
  delay(500);  // give the AP time to fully initialise before accepting scans

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP SSID: "); Serial.println(apName);
  Serial.print("AP IP:   "); Serial.println(ip);

  // Show SSID on display; lock prevents main loop from overwriting it
  displayLines(apName);
  lockDisplay(1000);

  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", ip);

  webServer = new WebServer(80);
  webServer->on("/",                 handleRoot);
  webServer->on("/save",  HTTP_POST, handleSave);
  webServer->on("/status",           handleStatus);
  webServer->on("/calibrate",  HTTP_POST, handleCalibrate);
  webServer->on("/calibrate_result", handleCalibrateResult);
  webServer->onNotFound([]() {
    webServer->sendHeader("Location", "/", true);
    webServer->send(302, "text/plain", "");
  });
  webServer->begin();

  lastHttpActivity = millis();

  while (true) {
    if (dnsServer) dnsServer->processNextRequest();
    if (webServer) webServer->handleClient();
    delay(1);
  }
}

/* ===== Public API =========================================================== */
void startWebConfig() {
  if (webConfigTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(
      webConfigTask, "WebConfigTask", 8192, nullptr, 1,
      &webConfigTaskHandle, 0);
  }
}

void stopWebConfig() {
  if (webConfigTaskHandle != nullptr) {
    vTaskDelete(webConfigTaskHandle);
    webConfigTaskHandle = nullptr;
  }
  if (webServer) { webServer->stop(); delete webServer; webServer = nullptr; }
  if (dnsServer) { dnsServer->stop(); delete dnsServer; dnsServer = nullptr; }
  WiFi.softAPdisconnect(true);
}

WaageConfig getConfig() {
  loadConfig();
  return config;
}

