#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

// Konfiguration
#define AP_SSID "100-Waage-Config"
#define AP_PASSWORD ""  // offen für einfachen Zugriff

// EEPROM Adressen
#define EEPROM_SIZE 512
#define EEPROM_AP_SSID_ADDR 0
#define EEPROM_SCALE_FACTOR_ADDR 64
#define EEPROM_TARE_OFFSET_ADDR 68
#define EEPROM_GOAL_ADDR 72
#define EEPROM_TOLERANCE_ADDR 76
#define EEPROM_DISPLAY_ROTATION_ADDR 80
#define EEPROM_VALID_FLAG_ADDR 81

// Globale Variablen
WebServer* webServer = nullptr;
DNSServer* dnsServer = nullptr;
const byte DNS_PORT = 53;
TaskHandle_t webConfigTaskHandle = nullptr;

WaageConfig config;

/* ===== EEPROM Funktionen ===== */
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  uint8_t validFlag = EEPROM.read(EEPROM_VALID_FLAG_ADDR);
  config.valid = (validFlag == 0xAA);

  if (config.valid) {
    // AP SSID laden
    for (int i = 0; i < 64; i++) {
      config.apSSID[i] = EEPROM.read(EEPROM_AP_SSID_ADDR + i);
    }

    // Scale Factor laden
    EEPROM.get(EEPROM_SCALE_FACTOR_ADDR, config.scaleFactor);

    // Tare Offset laden
    EEPROM.get(EEPROM_TARE_OFFSET_ADDR, config.tareOffset);

    // Goal laden
    EEPROM.get(EEPROM_GOAL_ADDR, config.goal);

    // Tolerance laden
    EEPROM.get(EEPROM_TOLERANCE_ADDR, config.tolerance);

    // Display Rotation laden
    config.displayRotation = EEPROM.read(EEPROM_DISPLAY_ROTATION_ADDR);
  } else {
    // Standardwerte
    strcpy(config.apSSID, AP_SSID);
    config.scaleFactor = 708.0;
    config.tareOffset = 0;
    config.goal = 100.0;
    config.tolerance = 10.0;
    config.displayRotation = 2;
  }

  EEPROM.end();
}

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);

  // AP SSID speichern
  for (int i = 0; i < 64; i++) {
    EEPROM.write(EEPROM_AP_SSID_ADDR + i, config.apSSID[i]);
  }

  // Scale Factor speichern
  EEPROM.put(EEPROM_SCALE_FACTOR_ADDR, config.scaleFactor);

  // Tare Offset speichern
  EEPROM.put(EEPROM_TARE_OFFSET_ADDR, config.tareOffset);

  // Goal speichern
  EEPROM.put(EEPROM_GOAL_ADDR, config.goal);

  // Tolerance speichern
  EEPROM.put(EEPROM_TOLERANCE_ADDR, config.tolerance);

  // Display Rotation speichern
  EEPROM.write(EEPROM_DISPLAY_ROTATION_ADDR, config.displayRotation);

  // Valid Flag setzen
  EEPROM.write(EEPROM_VALID_FLAG_ADDR, 0xAA);

  EEPROM.commit();
  EEPROM.end();
}

/* ===== Web Handler ===== */
void handleRoot() {
  webServer->send(200, "text/html", buildConfigHTML());
}

void handleSave() {
  if (webServer->hasArg("apSSID") && webServer->arg("apSSID").length() > 0) {
    String ssid = webServer->arg("apSSID");
    ssid.toCharArray(config.apSSID, 64);
  }

  if (webServer->hasArg("scaleFactor") && webServer->arg("scaleFactor").length() > 0) {
    config.scaleFactor = webServer->arg("scaleFactor").toFloat();
  }

  if (webServer->hasArg("tareOffset") && webServer->arg("tareOffset").length() > 0) {
    config.tareOffset = webServer->arg("tareOffset").toInt();
  }

  if (webServer->hasArg("goal") && webServer->arg("goal").length() > 0) {
    config.goal = webServer->arg("goal").toFloat();
  }

  if (webServer->hasArg("tolerance") && webServer->arg("tolerance").length() > 0) {
    config.tolerance = webServer->arg("tolerance").toFloat();
  }

  if (webServer->hasArg("displayRotation") && webServer->arg("displayRotation").length() > 0) {
    config.displayRotation = webServer->arg("displayRotation").toInt();
  }

  config.valid = true;
  saveConfig();

  String response = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gespeichert</title>
<style>
body { font-family: Arial; text-align: center; margin-top: 40px; }
.success { color: green; font-size: 20px; }
</style>
</head>
<body>
  <h2>⚖️ Waage Konfiguration</h2>
  <p class="success">✓ Einstellungen gespeichert!</p>
  <p>Die Waage wird neu gestartet...</p>
  <p><a href="/">Zurück zur Konfiguration</a></p>
</body>
</html>
)rawliteral";

  webServer->send(200, "text/html", response);

  delay(2000);
  ESP.restart();
}

void handleStatus() {
  extern float weight;
  String json = "{";
  json += "\"apSSID\":\"" + String(config.apSSID) + "\",";
  json += "\"scaleFactor\":" + String(config.scaleFactor, 4) + ",";
  json += "\"tareOffset\":" + String(config.tareOffset) + ",";
  json += "\"weight\":" + String(weight, 2) + ",";
  json += "\"goal\":" + String(config.goal, 2) + ",";
  json += "\"tolerance\":" + String(config.tolerance, 2) + ",";
  json += "\"displayRotation\":" + String(config.displayRotation) + ",";
  json += "\"valid\":" + String(config.valid ? "true" : "false");
  json += "}";

  webServer->send(200, "application/json", json);
}

void handleCalibrate() {
  if (!webServer->hasArg("weight")) {
    webServer->send(400, "text/plain", "Fehler: Gewicht fehlt");
    return;
  }

  float knownWeight = webServer->arg("weight").toFloat();

  String response = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kalibrierung</title>
<style>
body { font-family: Arial; text-align: center; margin-top: 40px; }
.info { color: blue; font-size: 18px; }
</style>
</head>
<body>
  <h2>⚖️ Kalibrierung</h2>
  <p class="info">Kalibrierung läuft...</p>
  <p>Bitte )rawliteral"
                    + String(knownWeight, 1) + R"rawliteral(g auf die Waage legen.</p>
  <p>Die Seite wird automatisch aktualisiert.</p>
  <script>
    setTimeout(() => { window.location.href = '/calibrate_result'; }, 10000);
  </script>
</body>
</html>
)rawliteral";

  webServer->send(200, "text/html", response);

  // Kalibrierung durchführen (asynchron, damit Response gesendet wird)
  extern HX711 hx711;
  delay(100);
  hx711.set_scale();
  hx711.tare(10);
  displayText("Kalibrierung", "Gewicht platzieren");
  delay(10000);
  hx711.calibrate_scale(knownWeight);
  float newScaleFactor = hx711.get_scale();

  config.scaleFactor = newScaleFactor;
  saveConfig();
}

void handleCalibrateResult() {
  String response = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kalibrierung Abgeschlossen</title>
<style>
body { font-family: Arial; text-align: center; margin-top: 40px; }
.success { color: green; font-size: 20px; }
</style>
</head>
<body>
  <h2>⚖️ Kalibrierung</h2>
  <p class="success">✓ Kalibrierung abgeschlossen!</p>
  <p>Neuer Kalibrierfaktor: )rawliteral"
                    + String(config.scaleFactor, 4) + R"rawliteral(</p>
  <p><a href="/">Zurück zur Konfiguration</a></p>
</body>
</html>
)rawliteral";

  webServer->send(200, "text/html", response);
}

/* ===== HTML Interface ===== */
String buildConfigHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Waage Konfiguration</title>
<style>
body {
  font-family: Arial, sans-serif;
  max-width: 500px;
  margin: 20px auto;
  padding: 20px;
}
h2 { text-align: center; }
.form-group {
  margin-bottom: 15px;
}
label {
  display: block;
  margin-bottom: 5px;
  font-weight: bold;
}
input {
  width: 100%;
  padding: 8px;
  box-sizing: border-box;
  border: 1px solid #ddd;
  border-radius: 4px;
}
button {
  width: 100%;
  padding: 12px;
  background-color: #4CAF50;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 16px;
}
button:hover {
  background-color: #45a049;
}
.info {
  background-color: #f0f0f0;
  padding: 10px;
  border-radius: 4px;
  margin-bottom: 20px;
  font-size: 14px;
}
</style>
</head>
<body>
  <h2>⚖️ Waage Konfiguration</h2>
  
  <div class="info">
    <strong>Aktuelle Werte:</strong><br>
    AP SSID: <span id="currentSSID">--</span><br>
    Kalibrierfaktor: <span id="currentScale">--</span><br>
    Tara-Offset: <span id="currentTare">--</span><br>
    Zielgewicht (Goal): <span id="currentGoal">--</span> g<br>
    Toleranz: <span id="currentTolerance">--</span> g<br>
    Display-Rotation: <span id="currentRotation">--</span><br>
    Messwert: <span id="currentWeight">--</span> g
  </div>
  
  <form action="/save" method="POST">
    <div class="form-group">
      <label>Access Point SSID (Netzwerkname):</label>
      <input type="text" name="apSSID" placeholder="z.B. Waage-Config">
    </div>
    
    <div class="form-group">
      <label>Zielgewicht (Goal) [g]:</label>
      <input type="number" step="0.01" name="goal" placeholder="z.B. 100.0">
    </div>
    
    <div class="form-group">
      <label>Toleranz [g]:</label>
      <input type="number" step="1" name="tolerance" placeholder="z.B. 10.0">
    </div>
    
    <div class="form-group">
      <label>Kalibrierfaktor (Scale Factor):</label>
      <input type="number" step="0.0001" name="scaleFactor" placeholder="z.B. 708.0">
    </div>
    
    <div class="form-group">
      <label>Tara-Offset (Nullpunkt):</label>
      <input type="number" name="tareOffset" placeholder="z.B. 0">
    </div>
    
    <div class="form-group">
      <label>Display-Rotation:</label>
      <select name="displayRotation" style="width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px;">
        <option value="0">Normal (0°)</option>
        <option value="2" selected>Gedreht (180°)</option>
      </select>
    </div>
    
    <button type="submit">💾 Speichern & Neustarten</button>
  </form>
  
  <hr style="margin: 30px 0;">
  
  <h3>🔧 Kalibrierung</h3>
  <form action="/calibrate" method="POST">
    <div class="form-group">
      <label>Bekanntes Gewicht für Kalibrierung [g]:</label>
      <input type="number" step="0.1" name="weight" placeholder="z.B. 50.0" required>
    </div>
    <button type="submit" style="background-color: #2196F3;">🎯 Jetzt kalibrieren</button>
  </form>

<script>
function loadStatus() {
  fetch('/status')
    .then(r => r.json())
    .then(d => {
      document.getElementById('currentSSID').textContent = d.apSSID || 'nicht konfiguriert';
      document.getElementById('currentScale').textContent = d.scaleFactor;
      document.getElementById('currentWeight').textContent = d.weight;
      document.getElementById('currentTare').textContent = d.tareOffset;
      document.getElementById('currentGoal').textContent = d.goal;
      document.getElementById('currentTolerance').textContent = d.tolerance;
      document.getElementById('currentRotation').textContent = d.displayRotation;
    })
    .catch(() => {});
}
setInterval(loadStatus, 2000);
loadStatus();
</script>

</body>
</html>
)rawliteral";
}

/* ===== Web Config Task (läuft in eigenem Thread) ===== */
void webConfigTask(void* parameter) {
  loadConfig();

  WiFi.mode(WIFI_AP);
  Serial.println("Starte Access Point...");
  Serial.print("SSID: ");
  String apName = strlen(config.apSSID) > 0 ? String(config.apSSID) : String(AP_SSID);
  Serial.println(apName);
  WiFi.softAP(apName.c_str(), AP_PASSWORD);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP Adresse: ");
  Serial.println(ip);

  // DNS Server für Captive Portal
  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", ip);

  // Web Server
  webServer = new WebServer(80);
  webServer->on("/", handleRoot);
  webServer->on("/save", HTTP_POST, handleSave);
  webServer->on("/status", handleStatus);
  webServer->on("/calibrate", HTTP_POST, handleCalibrate);
  webServer->on("/calibrate_result", handleCalibrateResult);

  // Alle anderen Anfragen auf / umleiten (Captive Portal)
  webServer->onNotFound([]() {
    webServer->sendHeader("Location", "/", true);
    webServer->send(302, "text/plain", "");
  });

  webServer->begin();
  Serial.println("Web-Konfigurationsserver gestartet");

  // Event-Loop für Web Server
  while (true) {
    if (dnsServer) {
      dnsServer->processNextRequest();
    }
    if (webServer) {
      webServer->handleClient();
    }
    delay(1);  // Gib anderen Tasks Zeit
  }
}

/* ===== Public API ===== */
void startWebConfig() {
  if (webConfigTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(
      webConfigTask,         // Task-Funktion
      "WebConfigTask",       // Task-Name
      8192,                  // Stack-Größe
      nullptr,               // Parameter
      1,                     // Priorität
      &webConfigTaskHandle,  // Task Handle
      0                      // Core (0 oder 1)
    );
  }
}

void stopWebConfig() {
  if (webConfigTaskHandle != nullptr) {
    vTaskDelete(webConfigTaskHandle);
    webConfigTaskHandle = nullptr;
  }

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
}

WaageConfig getConfig() {
  loadConfig();
  return config;
}
