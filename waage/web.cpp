#include "web.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <string.h>
#include "app.h"
#include "battery.h"
#include "config.h"
#include "duell.h"
#include "radio.h"
#include "version.h"
#include "web_core.h"
#include "web_pages.h"

constexpr uint8_t DNS_PORT = 53;
constexpr char COOKIE_NAME[] = "waage_session";
constexpr char JSON_TYPE[] = "application/json; charset=utf-8";
constexpr char HTML_TYPE[] = "text/html; charset=utf-8";

static WebServer *server = nullptr;
static DNSServer *dns = nullptr;
static bool running = false;
static uint32_t lastActivity = 0;

static char token[33] = "";  // leer = keine Sitzung
static web::LoginThrottle throttle;
static char jsonBuf[2048];

static bool otaRejected = false, otaBeginOk = false, otaEnded = false;
static size_t otaSize = 0;

static void touch() {
  lastActivity = millis();
}

// ── Antworten ─────────────────────────────────────────────────────────────────

static void sendJson(int code, const web::JsonWriter &j) {
  server->sendHeader("Cache-Control", "no-store");
  if (!j.ok()) {
    server->send(500, JSON_TYPE, "{\"ok\":false,\"error\":\"Antwort zu groß\"}");
    return;
  }
  server->send(code, JSON_TYPE, j.c_str());
}

static void sendError(int code, const char *msg, const char *field = nullptr) {
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  j.beginObject().key("ok").flag(false).key("error").str(msg);
  if (field) j.key("field").str(field);
  j.endObject();
  sendJson(code, j);
}

static void sendOk(int code = 200) {
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  j.beginObject().key("ok").flag(true).endObject();
  sendJson(code, j);
}

static void sendPage(const char *page) {
  server->sendHeader("Cache-Control", "no-cache");
  server->send_P(200, HTML_TYPE, page);
}

static void redirect(const char *to) {
  server->sendHeader("Location", to, true);
  server->send(302, "text/plain", "");
}

// ── Sitzung ───────────────────────────────────────────────────────────────────

static bool authed() {
  if (!token[0] || !server->hasHeader("Cookie")) return false;
  char v[40];
  if (!web::cookieValue(server->header("Cookie").c_str(), COOKIE_NAME, v, sizeof(v))) return false;
  return web::ctEquals(v, token);
}

static bool requireApiAuth() {
  if (authed()) return true;
  sendError(401, "login");
  return false;
}

static void newToken() {
  uint32_t w[4] = { esp_random(), esp_random(), esp_random(), esp_random() };
  web::tokenHex(w, token);
}

// ── Formularfelder ────────────────────────────────────────────────────────────

// Liefert false bei vorhandenem, aber ungueltigem Wert (dann 400 gesendet).
static bool argFloat(const char *name, float *out, bool *present) {
  *present = server->hasArg(name) && server->arg(name).length() > 0;
  if (!*present) return true;
  if (cfg::parseFloat(server->arg(name).c_str(), out)) return true;
  sendError(400, "Ungültige Zahl", name);
  return false;
}

static bool argUint(const char *name, uint32_t maxValue, uint32_t *out, bool *present) {
  *present = server->hasArg(name) && server->arg(name).length() > 0;
  if (!*present) return true;
  if (cfg::parseUint(server->arg(name).c_str(), maxValue, out)) return true;
  sendError(400, "Ungültiger Wert", name);
  return false;
}

static bool argBool(const char *name, bool *out, bool *present) {
  *present = server->hasArg(name) && server->arg(name).length() > 0;
  if (!*present) return true;
  const String &v = server->arg(name);
  if (v == "1" || v == "true" || v == "on") *out = true;
  else if (v == "0" || v == "false" || v == "off") *out = false;
  else {
    sendError(400, "Ungültiger Wert", name);
    return false;
  }
  return true;
}

static void sendApplyError(const ApplyResult &r) {
  sendError(r.status == Apply::Busy ? 409 : 400, r.error.message, r.error.field);
}

// ── Oeffentliche Routen ───────────────────────────────────────────────────────

static void handleIndex() {
  touch();
  sendPage(INDEX_HTML);
}

static void handleStatus() {
  touch();
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  app_writeStatus(j);
  sendJson(200, j);
}

static void writePublicConfig(web::JsonWriter &j) {
  const cfg::Config &c = config_get();
  j.beginObject();
  j.key("goal").num(c.goal, 1);
  j.key("randomModeEnabled").flag(c.randomModeEnabled);
  j.key("randomMin").num(c.randomMin, 1);
  j.key("displayRotation").uinteger(c.displayRotation);
  j.key("scaleMode").str(c.scaleMode == cfg::ScaleMode::Game ? "Game" : "Standard");
  j.key("tolerance").num(c.tolerance, 1);
  j.endObject();
}

static void handleConfigGet() {
  touch();
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  writePublicConfig(j);
  sendJson(200, j);
}

static void handleConfigPost() {
  touch();
  cfg::Config n = config_get();
  float f;
  uint32_t u;
  bool b, p;
  if (!argFloat("goal", &f, &p)) return;
  if (p) n.goal = f;
  if (!argBool("randomModeEnabled", &b, &p)) return;
  if (p) n.randomModeEnabled = b;
  if (!argFloat("randomMin", &f, &p)) return;
  if (p) n.randomMin = f;
  if (!argUint("displayRotation", 255, &u, &p)) return;
  if (p) n.displayRotation = (uint8_t)u;
  if (server->hasArg("scaleMode") && server->arg("scaleMode").length() > 0) {
    const String &m = server->arg("scaleMode");
    if (m == "Game") n.scaleMode = cfg::ScaleMode::Game;
    else if (m == "Standard") n.scaleMode = cfg::ScaleMode::Standard;
    else return sendError(400, "Ungültiger Modus", "scaleMode");
  }

  ApplyResult r = app_applyConfig(n, true);
  if (r.status != Apply::Ok) return sendApplyError(r);

  bool later = (r.changes & (cfg::CH_GOAL | cfg::CH_RANDOM)) && !r.appliedNow;
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  j.beginObject();
  j.key("ok").flag(true);
  j.key("applied").str(later ? "next" : "now");
  j.key("config");
  writePublicConfig(j);
  j.endObject();
  sendJson(200, j);
}

static void handleLoginPage() {
  touch();
  sendPage(LOGIN_HTML);
}

static void handleLogin() {
  touch();
  uint32_t now = millis();
  if (throttle.locked(now)) return redirect("/login?e=2");
  const String pw = server->hasArg("password") ? server->arg("password") : String();
  if (pw.length() > 0 && web::ctEquals(pw.c_str(), config_get().adminPassword)) {
    throttle.success();
    newToken();
    String cookie = String(COOKIE_NAME) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict";
    server->sendHeader("Set-Cookie", cookie);
    return redirect("/admin");
  }
  throttle.failure(now);
  redirect(throttle.locked(now) ? "/login?e=2" : "/login?e=1");
}

static void handleLogout() {
  touch();
  if (authed()) token[0] = 0;  // fremde Clients koennen die Sitzung nicht beenden
  server->sendHeader("Set-Cookie", String(COOKIE_NAME) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  redirect("/");
}

// ── Admin ─────────────────────────────────────────────────────────────────────

static void handleAdmin() {
  touch();
  if (!authed()) return redirect("/login");
  sendPage(ADMIN_HTML);
}

static void writeAdminConfig(web::JsonWriter &j) {
  const cfg::Config &c = config_get();
  uint8_t mac[6];
  char name[cfg::SSID_MAX + 1];
  WiFi.macAddress(mac);
  cfg::effectiveApName(c, mac, name);
  j.beginObject();
  j.key("apSSID").str(c.apSSID);
  j.key("apName").str(name);
  j.key("tolerance").num(c.tolerance, 1);
  j.key("autoResetRange").uinteger(c.autoResetRange);
  j.key("wifiTimeout").uinteger(c.wifiTimeout);
  j.key("sleepTimeout").uinteger(c.sleepTimeout);
  j.key("autoZeroEnabled").flag(c.autoZeroEnabled);
  j.key("autoZeroThreshold").num(c.autoZeroThreshold, 1);
  j.key("autoZeroDelay").uinteger(c.autoZeroDelay);
  j.key("battDividerRatio").num(c.battDividerRatio, 3);
  j.key("scaleFactor").num(c.scaleFactor, 4);
  j.key("fw").str(FW_VERSION);
  j.key("proto").uinteger(duell::MAGIC);
  j.endObject();
}

static void handleAdminConfigGet() {
  touch();
  if (!requireApiAuth()) return;
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  writeAdminConfig(j);
  sendJson(200, j);
}

static void handleAdminConfigPost() {
  touch();
  if (!requireApiAuth()) return;
  cfg::Config n = config_get();
  float f;
  uint32_t u;
  bool b, p;
  if (server->hasArg("apSSID")) {
    const String &s = server->arg("apSSID");
    if (s.length() > cfg::SSID_MAX) return sendError(400, "SSID ist zu lang (max. 32 Bytes)", "apSSID");
    cfg::copyUtf8(n.apSSID, s.c_str(), cfg::SSID_MAX);
  }
  if (!argFloat("tolerance", &f, &p)) return;
  if (p) n.tolerance = f;
  if (!argUint("autoResetRange", 255, &u, &p)) return;
  if (p) n.autoResetRange = (uint8_t)u;
  if (!argUint("wifiTimeout", 255, &u, &p)) return;
  if (p) n.wifiTimeout = (uint8_t)u;
  if (!argUint("sleepTimeout", 255, &u, &p)) return;
  if (p) n.sleepTimeout = (uint8_t)u;
  if (!argBool("autoZeroEnabled", &b, &p)) return;
  if (p) n.autoZeroEnabled = b;
  if (!argFloat("autoZeroThreshold", &f, &p)) return;
  if (p) n.autoZeroThreshold = f;
  if (!argUint("autoZeroDelay", 255, &u, &p)) return;
  if (p) n.autoZeroDelay = (uint8_t)u;
  bool pwChange = server->hasArg("newPassword") && server->arg("newPassword").length() > 0;
  if (pwChange) {
    const String &pw = server->arg("newPassword");
    if (!cfg::validNewPassword(pw.c_str())) {
      return sendError(400, "Passwort: 4 bis 31 Zeichen", "newPassword");
    }
    strncpy(n.adminPassword, pw.c_str(), cfg::PASSWORD_MAX);
    n.adminPassword[cfg::PASSWORD_MAX] = 0;
  }

  ApplyResult r = app_applyConfig(n, true);
  if (r.status != Apply::Ok) return sendApplyError(r);

  bool relogin = (r.changes & cfg::CH_PASSWORD) != 0;
  if (relogin) token[0] = 0;
  uint8_t mac[6];
  char name[cfg::SSID_MAX + 1];
  WiFi.macAddress(mac);
  cfg::effectiveApName(config_get(), mac, name);

  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  j.beginObject();
  j.key("ok").flag(true);
  j.key("apRestart").flag(radio_apOn() && strcmp(name, radio_apName()) != 0);
  j.key("apName").str(name);
  j.key("relogin").flag(relogin);
  j.endObject();
  sendJson(200, j);
}

static void handleBattCal() {
  touch();
  if (!requireApiAuth()) return;
  bool reset = server->hasArg("reset") && server->arg("reset") == "1";
  float v = 0.0f;
  bool p = false;
  if (!reset) {
    if (!argFloat("measuredV", &v, &p)) return;
    if (!p) return sendError(400, "Spannung fehlt", "measuredV");
  }
  const char *err = nullptr;
  int code = app_battCal(v, reset, &err);
  if (code != 200) return sendError(code, err ? err : "Fehler", "measuredV");

  const batt::Gauge &g = battery_gauge();
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  j.beginObject();
  j.key("ok").flag(true);
  j.key("ratio").num(config_get().battDividerRatio, 3);
  j.key("voltage").num(g.voltage(), 2);
  j.key("percent").integer(g.percent());
  j.endObject();
  sendJson(200, j);
}

static void handleCalStart() {
  touch();
  if (!requireApiAuth()) return;
  switch (app_calStart()) {
    case CalStart::Ok: return sendOk(202);
    case CalStart::Busy: return sendError(409, "Spiel läuft – erst Taste drücken");
    case CalStart::SensorError: return sendError(503, "Sensorfehler – Wägezelle prüfen");
  }
}

static void handleCalMeasure() {
  touch();
  if (!requireApiAuth()) return;
  float g;
  bool p;
  if (!argFloat("weight", &g, &p)) return;
  if (!p || !app_calMeasure(g)) return sendError(400, "Gewicht ungültig (0,5 – 5000 g) oder falscher Schritt", "weight");
  sendOk(202);
}

static void handleCalCancel() {
  touch();
  if (!requireApiAuth()) return;
  app_calCancel();
  sendOk();
}

static void handleCalGet() {
  touch();
  if (!requireApiAuth()) return;
  web::JsonWriter j(jsonBuf, sizeof(jsonBuf));
  app_writeCal(j);
  sendJson(200, j);
}

static void handleDuell() {
  touch();
  if (!requireApiAuth()) return;
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, JSON_TYPE, duell_status_json());
}

// ── OTA ───────────────────────────────────────────────────────────────────────

static void handleUpdateAllowed() {
  touch();
  if (!requireApiAuth()) return;
  if (app_isBusy()) return sendError(409, "Spiel läuft – erst Taste drücken");
  sendOk();
}

static void handleUpdateUpload() {
  HTTPUpload &up = server->upload();
  if (up.status == UPLOAD_FILE_START) {
    otaBeginOk = otaEnded = false;
    otaRejected = !authed() || app_isBusy();
    if (otaRejected) return;
    touch();
    if (Update.isRunning()) Update.abort();  // Rest eines abgebrochenen Uploads
    otaSize = server->hasHeader("X-Update-Size") ? (size_t)server->header("X-Update-Size").toInt() : 0;
    app_otaBegin();
    otaBeginOk = Update.begin(otaSize > 0 ? otaSize : UPDATE_SIZE_UNKNOWN);
    if (!otaBeginOk) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaRejected || !otaBeginOk) return;
    touch();
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    if (otaSize > 0) app_otaProgress((int)((uint64_t)up.totalSize * 100 / otaSize));
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaRejected || !otaBeginOk) return;
    if (Update.end(true)) otaEnded = true;
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    // Verbindung nach erfolgreichem end() weg: neues Image ist schon aktiv
    if (otaEnded) {
      app_otaEnd(true);
    } else {
      if (otaBeginOk) Update.abort();
      if (!otaRejected) app_otaEnd(false);
    }
    otaBeginOk = otaEnded = false;
  }
}

static void handleUpdateDone() {
  touch();
  if (!requireApiAuth()) return;
  if (otaRejected) {
    otaRejected = false;
    return sendError(409, "Spiel läuft – erst Taste drücken");
  }
  bool ok = otaBeginOk && Update.isFinished() && !Update.hasError();
  otaBeginOk = otaEnded = false;
  if (!ok) {
    sendError(500, Update.hasError() ? Update.errorString() : "Update unvollständig");
    app_otaEnd(false);
    return;
  }
  sendOk();
  app_otaEnd(true);  // Neustart erfolgt verzoegert im Loop
}

// ── Sonstiges ─────────────────────────────────────────────────────────────────

static void handleNotFound() {
  if (server->uri().startsWith("/api/")) return sendError(404, "Unbekannt");
  redirect("/");
}

// ── API ───────────────────────────────────────────────────────────────────────

void web_start() {
  if (running) return;
  token[0] = 0;
  IPAddress ip = WiFi.softAPIP();
  dns = new DNSServer();
  dns->start(DNS_PORT, "*", ip);

  server = new WebServer(80);
  static const char *headers[] = { "Cookie", "X-Update-Size" };
  server->collectHeaders(headers, 2);
  server->on("/", HTTP_GET, handleIndex);
  server->on("/api/status", HTTP_GET, handleStatus);
  server->on("/api/config", HTTP_GET, handleConfigGet);
  server->on("/api/config", HTTP_POST, handleConfigPost);
  server->on("/login", HTTP_GET, handleLoginPage);
  server->on("/login", HTTP_POST, handleLogin);
  server->on("/logout", HTTP_GET, handleLogout);
  server->on("/admin", HTTP_GET, handleAdmin);
  server->on("/api/admin/config", HTTP_GET, handleAdminConfigGet);
  server->on("/api/admin/config", HTTP_POST, handleAdminConfigPost);
  server->on("/api/admin/battcal", HTTP_POST, handleBattCal);
  server->on("/api/admin/cal/start", HTTP_POST, handleCalStart);
  server->on("/api/admin/cal/measure", HTTP_POST, handleCalMeasure);
  server->on("/api/admin/cal/cancel", HTTP_POST, handleCalCancel);
  server->on("/api/admin/cal", HTTP_GET, handleCalGet);
  server->on("/api/admin/update/allowed", HTTP_GET, handleUpdateAllowed);
  server->on("/api/admin/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server->on("/api/admin/duell", HTTP_GET, handleDuell);
  server->onNotFound(handleNotFound);
  server->begin();

  if (MDNS.begin("waage")) MDNS.addService("http", "tcp", 80);
  lastActivity = millis();
  running = true;
}

void web_stop() {
  if (!running) return;
  running = false;
  token[0] = 0;
  server->stop();
  delete server;
  server = nullptr;
  dns->stop();
  delete dns;
  dns = nullptr;
  MDNS.end();
}

void web_handle() {
  if (!running) return;
  dns->processNextRequest();
  server->handleClient();
}

uint32_t web_lastActivity() {
  return lastActivity;
}
