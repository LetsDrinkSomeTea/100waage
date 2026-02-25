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
  json += "\"apSSID\":\""       + String(config.apSSID)              + "\",";
  json += "\"scaleFactor\":"    + String(config.scaleFactor, 4)      + ",";
  json += "\"tareOffset\":"     + String(config.tareOffset)          + ",";
  json += "\"weight\":"         + String(weight, 2)                  + ",";
  json += "\"goal\":"           + String(config.goal, 2)             + ",";
  json += "\"tolerance\":"      + String(config.tolerance, 2)        + ",";
  json += "\"displayRotation\":" + String(config.displayRotation)    + ",";
  json += "\"batteryPercent\":" + String(batteryPercent)             + ",";
  json += "\"wifiActive\":"     + String(wifiActive ? "true":"false") + ",";
  json += "\"scaleMode\":"      + String(scaleMode == 0 ? "\"Game\"" : "\"Standard\"") + ",";
  json += "\"valid\":"          + String(config.valid ? "true":"false");
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
  return R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Waage Konfiguration</title>
<style>
body{font-family:Arial,sans-serif;max-width:500px;margin:20px auto;padding:20px}
h2,h3{text-align:center}
.form-group{margin-bottom:14px}
label{display:block;margin-bottom:4px;font-weight:bold;font-size:14px}
input,select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px}
button{width:100%;padding:12px;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:15px;margin-top:4px}
.btn-primary{background:#4CAF50}.btn-primary:hover{background:#45a049}
.btn-blue{background:#2196F3}.btn-blue:hover{background:#1976D2}
.btn-admin{background:#FF9800}.btn-admin:hover{background:#F57C00}
.info{background:#f5f5f5;padding:10px;border-radius:4px;margin-bottom:16px;font-size:13px}
.admin-locked{opacity:.45;pointer-events:none;transition:opacity .3s}
hr{margin:24px 0}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:12px;font-weight:bold}
.badge-game{background:#e3f2fd;color:#1565c0}.badge-std{background:#fce4ec;color:#b71c1c}
</style>
</head>
<body>
<h2>&#x2696;&#xFE0F; Waage Konfiguration</h2>

<div class="info" id="statusDiv">
  <strong>Status:</strong><br>
  Gewicht: <span id="sWeight">--</span> g &nbsp;|&nbsp;
  Akku: <span id="sBatt">--</span>%<br>
  Modus: <span id="sMode">--</span> &nbsp;|&nbsp;
  WiFi: <span id="sWifi">--</span><br>
  Zielgewicht: <span id="sGoal">--</span> g &nbsp;|&nbsp;
  Kalibrierfaktor: <span id="sScale">--</span>
</div>

<form action="/save" method="POST">
  <h3>&#x2699;&#xFE0F; Allgemein</h3>

  <div class="form-group">
    <label>Display-Rotation:</label>
    <select name="displayRotation">
      <option value="0">Normal (0°)</option>
      <option value="2" selected>Gedreht (180°)</option>
    </select>
  </div>

  <div class="form-group">
    <label>Zielgewicht [g]:</label>
    <input type="number" step="0.1" name="goal" id="inGoal" placeholder="z.B. 100.0">
  </div>

  <hr>
  <h3>&#x1F512; Admin</h3>

  <div class="form-group">
    <label>Admin-Passwort:</label>
    <input type="password" name="adminPassword" id="adminPwd"
           oninput="onPwd(this.value)" autocomplete="current-password"
           placeholder="Passwort eingeben...">
  </div>

  <div id="adminSection" class="admin-locked">
    <div class="form-group">
      <label>Access Point SSID:</label>
      <input type="text" name="apSSID" placeholder="z.B. Waage-Config">
    </div>
    <div class="form-group">
      <label>Kalibrierfaktor:</label>
      <input type="number" step="0.0001" name="scaleFactor" placeholder="z.B. 708.0">
    </div>
    <div class="form-group">
      <label>Tara-Offset:</label>
      <input type="number" name="tareOffset" placeholder="z.B. 0">
    </div>
    <div class="form-group">
      <label>Toleranz [g]:</label>
      <input type="number" step="0.1" name="tolerance" placeholder="z.B. 10.0">
    </div>
    <div class="form-group">
      <label>Auto-Reset Bereich [%] (schlechte Ergebnisse):</label>
      <input type="number" step="1" min="0" max="100" name="autoResetRange" placeholder="z.B. 10">
    </div>
    <div class="form-group">
      <label>Spannungsteiler-Verhältnis (Akku):</label>
      <input type="number" step="0.01" name="battDividerRatio" placeholder="z.B. 2.0">
    </div>
    <div class="form-group">
      <label>WiFi Auto-Aus nach [min] (0 = nie):</label>
      <input type="number" step="1" min="0" max="255" name="wifiTimeout" placeholder="z.B. 10">
    </div>
    <div class="form-group">
      <label>Deep Sleep nach [min] Idle (0 = nie):</label>
      <input type="number" step="1" min="0" max="255" name="sleepTimeout" placeholder="z.B. 5">
    </div>
    <div class="form-group">
      <label>Neues Admin-Passwort (min. 4 Zeichen):</label>
      <input type="password" name="newAdminPassword" autocomplete="new-password"
             placeholder="Leer lassen = nicht ändern">
    </div>
    <button type="submit" class="btn-admin">&#x1F512; Admin-Einstellungen speichern &amp; neustarten</button>
  </div>

  <button type="submit" class="btn-primary" style="margin-top:16px">&#x1F4BE; Allgemein speichern</button>
</form>

<hr>
<h3>&#x1F527; Kalibrierung</h3>
<div id="calSection" class="admin-locked">
  <form action="/calibrate" method="POST">
    <div class="form-group">
      <label>Bekanntes Gewicht [g]:</label>
      <input type="number" step="0.1" name="weight" placeholder="z.B. 50.0" required>
    </div>
    <button type="submit" class="btn-blue">&#x1F3AF; Jetzt kalibrieren</button>
  </form>
</div>
<p id="calLock" style="text-align:center;color:#999;font-size:13px">
  &#x1F512; Admin-Passwort eingeben um Kalibrierung freizuschalten
</p>

<script>
function onPwd(v) {
  var unlocked = v.length >= 1;
  document.getElementById('adminSection').className = unlocked ? '' : 'admin-locked';
  document.getElementById('calSection').className   = unlocked ? '' : 'admin-locked';
  document.getElementById('calLock').style.display  = unlocked ? 'none' : '';
}
function loadStatus() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('sWeight').textContent = d.weight;
    document.getElementById('sBatt').textContent   = d.batteryPercent;
    document.getElementById('sGoal').textContent   = d.goal;
    document.getElementById('sScale').textContent  = d.scaleFactor;
    document.getElementById('sMode').textContent   = d.scaleMode;
    document.getElementById('sWifi').textContent   = d.wifiActive ? 'An' : 'Aus';
    document.getElementById('inGoal').placeholder  = d.goal;
  }).catch(()=>{});
}
setInterval(loadStatus, 2000);
loadStatus();
</script>
</body>
</html>
)rawliteral";
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

