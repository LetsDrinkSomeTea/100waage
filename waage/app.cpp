#include "app.h"
#include "battery.h"
#include "button_core.h"
#include "config.h"
#include "duell.h"
#include "game_core.h"
#include "power_core.h"
#include "radio.h"
#include "scale.h"
#include "stats.h"
#include "ui.h"
#include "ui_model.h"
#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <string.h>

// ── Feature-Flags und Pins ────────────────────────────────────────────────────
constexpr bool RESET_CONFIG_ENABLED =
    false; // Werksreset: Taster beim Einschalten 3 s halten

constexpr int PIN_OLED_SDA = 8;
constexpr int PIN_OLED_SCL = 9;
constexpr int PIN_BTN = 5;

constexpr uint32_t SENSOR_GRACE_MS =
    2000; // so lange nach dem Start keine Fehlermeldung
constexpr uint32_t AP_NAME_TOAST_MS = 3000;
constexpr uint32_t CAL_ERROR_SHOW_MS =
    10000; // Kalibrierfehler so lange zeigen, dann weiter spielen

static game::Game theGame;
static scale::Calibrator cal;
static button::Button btn;
static power::SleepPolicy sleepPolicy;
static ui::Model uiModel;
static uint32_t bootAt = 0;
static bool otaActive = false;
static int otaPercent = 0;
static bool calWasActive = false;
static uint32_t calErrorSince = 0;
static bool otaDone = false; // Update erfolgreich, Neustart steht an
// Erfolg der letzten Runde (Rekord, schnellste Zeit) fuer die Anzeige
static stats::Achievement lastAch = stats::Achievement::None;
static uint32_t lastAchSeq = 0;

static uint32_t randomWord(void *) { return esp_random(); }

static float stableSpreadFor(const cfg::Config &c) {
  float s = c.tolerance / 5.0f;
  return s > scale::STABLE_SPREAD_MIN_G ? s : scale::STABLE_SPREAD_MIN_G;
}

// Tara-Anforderungen der Spiellogik ausfuehren
static void applyScaleReq(game::ScaleReq r, const cfg::Config &c,
                          uint32_t now) {
  scale::Core &s = scale_core();
  switch (r) {
  case game::ScaleReq::None:
    break;
  case game::ScaleReq::Tare:
    s.startTare(now);
    break;
  case game::ScaleReq::TareEmpty:
    if (!s.zeroFromWindow(c.tolerance, s.stableSpread(), now))
      s.startTare(now);
    break;
  case game::ScaleReq::AutoZero:
    s.zeroFromWindow(c.autoZeroThreshold, c.autoZeroThreshold, now);
    break;
  case game::ScaleReq::NegZero:
    s.zeroFromWindow(1e9f, s.stableSpread(), now);
    break;
  }
}

// Einziger Reset: immer mit Tara (Entscheidung 1). Bricht eine laufende
// Kalibrierung ab, damit die Tara sie nicht verfaelscht.
static void resetGame(uint32_t now) {
  if (cal.active())
    cal.cancel(scale_core());
  const cfg::Config &c = config_get();
  theGame.reset(c, now, game::ScaleReq::Tare);
  applyScaleReq(theGame.takeScaleReq(), c, now);
}

// Funk laeuft genau dann, wenn der Duell-Modus aktiv ist oder der AP laeuft.
// Ausschalten verlaesst eine laufende Runde (radio_stop).
static void syncRadio(const cfg::Config &c) {
  bool want = c.scaleMode == cfg::ScaleMode::Duel || radio_apOn();
  if (want && !radio_isOn())
    radio_start();
  else if (!want && radio_isOn())
    radio_stop();
}

bool app_isBusy() {
  return theGame.gameRunning() || theGame.ownRoundOpen() || cal.active() ||
         otaActive;
}

// ── Config ────────────────────────────────────────────────────────────────────

ApplyResult app_applyConfig(cfg::Config next, bool fromWeb) {
  ApplyResult r = {};
  r.status = Apply::Ok;
  if (fromWeb) {
    cfg::Error e = cfg::validate(next);
    if (e.field) {
      r.status = Apply::Invalid;
      r.error = e;
      return r;
    }
  }
  const cfg::Config before = config_get();
  if ((cfg::diff(before, next) & cfg::CH_MODE) && fromWeb && app_isBusy()) {
    r.status = Apply::Busy;
    r.error = {"scaleMode", "Spiel läuft – erst Taste drücken"};
    return r;
  }

  uint32_t now = millis();
  uint32_t ch = config_set(next);
  const cfg::Config &c = config_get();
  r.changes = ch;

  if (ch & cfg::CH_ROTATION)
    ui_setRotation(c.displayRotation);
  if (ch & cfg::CH_GAME)
    scale_core().setStableSpread(stableSpreadFor(c));
  if (ch & cfg::CH_BATT)
    battery_configure(c.batteryPresent, c.battDividerRatio);
  if (ch & cfg::CH_SCALE)
    scale_core().setFactor(c.scaleFactor);
  if ((ch & cfg::CH_SSID) && radio_apOn()) {
    uint8_t mac[6];
    char name[cfg::SSID_MAX + 1];
    WiFi.macAddress(mac);
    cfg::effectiveApName(c, mac, name);
    if (strcmp(name, radio_apName()) != 0)
      radio_requestApRestart(1000);
  }
  if (ch & cfg::CH_MODE) {
    resetGame(now); // verlaesst eine Duell-Runde (leave)
    if (radio_isOn())
      duell_flush_burst();
    syncRadio(c);
    r.appliedNow = true;
  } else if (ch & (cfg::CH_GOAL | cfg::CH_RANDOM)) {
    theGame.applyGoalSettings(c, now);
    r.appliedNow =
        cfg::playsGame(c.scaleMode) && theGame.phase() == game::Phase::Idle;
  }
  return r;
}

// ── Taster ────────────────────────────────────────────────────────────────────

static void handleButton(button::Zone z, uint32_t now) {
  switch (z) {
  case button::Zone::Short:
    resetGame(now);
    break;
  case button::Zone::Mode: {
    cfg::Config n = config_get();
    n.scaleMode = cfg::nextMode(n.scaleMode);
    app_applyConfig(n, false);
    uiModel.modeToast(n.scaleMode, millis());
    break;
  }
  case button::Zone::Radio:
    if (radio_apOn()) {
      radio_stopAP();
      syncRadio(config_get()); // Funk bleibt nur im Duell-Modus an
      uiModel.toast("AP aus", now);
    } else {
      radio_startAP(config_get());
      uiModel.toast(radio_apName(), millis(), AP_NAME_TOAST_MS);
    }
    break;
  case button::Zone::Cancel:
    uiModel.toast("Abgebrochen", now);
    break;
  case button::Zone::None:
    break;
  }
}

// ── Kalibrierung ──────────────────────────────────────────────────────────────

CalStart app_calStart() {
  if (!scale_ok())
    return CalStart::SensorError;
  if (app_isBusy())
    return CalStart::Busy;
  uint32_t now = millis();
  theGame.reset(config_get(), now, game::ScaleReq::Tare);
  theGame.takeScaleReq(); // die Kalibrierung tariert selbst
  cal.start(scale_core(), now);
  return CalStart::Ok;
}

bool app_calMeasure(float knownG) { return cal.measure(knownG, millis()); }

void app_calCancel() {
  if (cal.state() == scale::CalState::Error)
    cal.acknowledge();
  else if (cal.active())
    cal.cancel(scale_core());
}

static const char *calStateName(scale::CalState s) {
  switch (s) {
  case scale::CalState::Off:
    return "Off";
  case scale::CalState::Prepare:
    return "Prepare";
  case scale::CalState::Taring:
    return "Taring";
  case scale::CalState::WaitWeight:
    return "WaitWeight";
  case scale::CalState::Measuring:
    return "Measuring";
  case scale::CalState::Done:
    return "Done";
  case scale::CalState::RemoveWeight:
    return "RemoveWeight";
  case scale::CalState::Error:
    return "Error";
  }
  return "?";
}

static const char *calErrorText(scale::CalError e) {
  switch (e) {
  case scale::CalError::None:
    return nullptr;
  case scale::CalError::BadWeight:
    return "Gewicht ungültig";
  case scale::CalError::NoWeight:
    return "Kein Gewicht erkannt";
  case scale::CalError::BadFactor:
    return "Faktor unplausibel";
  case scale::CalError::Timeout:
    return "Zeitüberschreitung";
  case scale::CalError::Cancelled:
    return "Abgebrochen";
  }
  return nullptr;
}

void app_writeCal(web::JsonWriter &j) {
  uint32_t now = millis();
  j.beginObject();
  j.key("state").str(calStateName(cal.state()));
  j.key("error").str(calErrorText(cal.error()));
  j.key("liveDeltaCounts").num(cal.liveDeltaCounts(scale_core(), now), 0);
  j.key("oldFactor").num(cal.oldFactor(), 4);
  j.key("newFactor");
  if (cal.state() == scale::CalState::Done ||
      cal.state() == scale::CalState::RemoveWeight)
    j.num(cal.newFactor(), 4);
  else
    j.null();
  j.key("factor").num(config_get().scaleFactor, 4);
  j.endObject();
}

static void updateCalibration(const cfg::Config &c, uint32_t now) {
  if (cal.active()) {
    bool wasError = cal.state() == scale::CalState::Error;
    cal.update(scale_core(), now, c.tolerance);
    float f;
    if (cal.takeNewFactor(&f)) {
      cfg::Config n = c;
      n.scaleFactor = f;
      app_applyConfig(n, false);
    }
    // Fehler (Offset/Faktor sind schon zurueck) nicht ewig stehen lassen:
    // sonst kein Deep-Sleep und eingefrorenes Spiel, wenn niemand quittiert
    if (cal.state() == scale::CalState::Error) {
      if (!wasError)
        calErrorSince = now;
      else if ((uint32_t)(now - calErrorSince) >= CAL_ERROR_SHOW_MS)
        cal.acknowledge();
    }
  }
  // Kalibrierung beendet (Gewicht entfernt oder abgebrochen) → frisch tarieren
  if (calWasActive && !cal.active())
    resetGame(now);
  calWasActive = cal.active();
}

// ── Akku ──────────────────────────────────────────────────────────────────────

int app_battCal(float measuredV, bool resetDefault, const char **err) {
  *err = nullptr;
  if (!battery_present()) {
    *err = "Akku ist in den Einstellungen deaktiviert";
    return 503;
  }
  cfg::Config n = config_get();
  if (resetDefault) {
    n.battDividerRatio = cfg::BATT_RATIO_DEFAULT;
  } else {
    float ratio;
    if (!batt::calibrateRatio(measuredV, battery_pinMv(64), &ratio, err))
      return 400;
    n.battDividerRatio = ratio;
  }
  app_applyConfig(n, false);
  return 200;
}

// ── OTA ───────────────────────────────────────────────────────────────────────

void app_otaBegin() {
  otaActive = true;
  otaPercent = 0;
  ui_force("Update...", "0 %");
}

void app_otaProgress(int percent) {
  if (percent / 10 == otaPercent / 10)
    return;
  otaPercent = percent;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d %%", percent);
  ui_force("Update...", buf);
}

void app_otaEnd(bool ok) {
  otaActive = false;
  if (ok) {
    otaDone = true; // Systembildschirm bis zum Neustart
    ui_force("Update OK", "Neustart...");
    radio_requestReboot(1000, true); // AP nach dem Neustart wieder an
  } else {
    uiModel.toast("Update fehlgeschlagen", millis(), 3000);
  }
}

// ── Status fuer das Web ───────────────────────────────────────────────────────

static const char *phaseName() {
  if (cal.active())
    return "Calibration";
  if (!scale_ok())
    return "SensorError";
  if (scale_core().taring())
    return "Taring";
  switch (theGame.phase()) {
  case game::Phase::Idle:
    return "Idle";
  case game::Phase::Ready:
    return "Ready";
  case game::Phase::Drinking:
    return "Drinking";
  case game::Phase::Result:
    return "Result";
  }
  return "?";
}

void app_writeStatus(web::JsonWriter &j) {
  uint32_t now = millis();
  const cfg::Config &c = config_get();
  scale::Reading r = scale_core().reading(now);
  j.beginObject();
  j.key("fw").str(FW_VERSION);
  j.key("weight");
  if (r.valid && scale_ok())
    j.num(r.grams, 2);
  else
    j.null();
  j.key("mode").str(cfg::modeKey(c.scaleMode));
  j.key("phase").str(phaseName());
  j.key("busy").flag(app_isBusy());
  j.key("goal").num(theGame.localGoal(), 1);
  j.key("random").flag(c.randomModeEnabled);
  j.key("radio").flag(radio_isOn());
  j.key("ap").flag(radio_apOn());
  j.key("scaleOk").flag(scale_ok());
  j.key("battery");
  const batt::Gauge &g = battery_gauge();
  if (battery_present() && g.valid()) {
    j.beginObject();
    j.key("percent").integer(g.percent());
    j.key("voltage").num(g.voltage(), 2);
    j.key("pinMv").num(g.voltage() * 1000.0f / c.battDividerRatio, 0);
    j.key("ratio").num(c.battDividerRatio, 3);
    j.key("low").flag(g.low());
    j.endObject();
  } else {
    j.null();
  }
  j.endObject();
}

// ── Anzeige ───────────────────────────────────────────────────────────────────

// Systembildschirm (Update, Kalibrierung, Sensorfehler) oder false
static bool systemScreen(uint32_t now, const char *sys[3]) {
  sys[0] = sys[1] = sys[2] = nullptr;
  if (otaDone) {
    sys[0] = "Update OK";
    sys[1] = "Neustart...";
    return true;
  }
  if (cal.active()) {
    switch (cal.state()) {
    case scale::CalState::Prepare:
      sys[0] = "Kalibrierung";
      sys[1] = "Waage leeren";
      break;
    case scale::CalState::Taring:
      sys[0] = "Kalibrierung";
      sys[1] = "Tara...";
      break;
    case scale::CalState::WaitWeight:
      sys[0] = "Gewicht auflegen,";
      sys[1] = "dann im Web messen";
      break;
    case scale::CalState::Measuring:
      sys[0] = "Kalibrierung";
      sys[1] = "Messe...";
      break;
    case scale::CalState::Done:
    case scale::CalState::RemoveWeight:
      sys[0] = "Fertig!";
      sys[1] = "Gewicht entfernen";
      break;
    case scale::CalState::Error:
      sys[0] = "Kalibrierung";
      sys[1] = calErrorText(cal.error());
      break;
    case scale::CalState::Off:
      break;
    }
    return true;
  }
  if (!scale_ok() && (uint32_t)(now - bootAt) > SENSOR_GRACE_MS) {
    sys[0] = "Waage";
    sys[1] = "Sensorfehler";
    return true;
  }
  return false;
}

static void render(const cfg::Config &c, uint32_t now) {
  const batt::Gauge &g = battery_gauge();
  ui::Status st = {};
  st.apOn = radio_apOn();
  st.peers = (radio_isOn() && c.scaleMode == cfg::ScaleMode::Duel)
                 ? duell_get_peers_count()
                 : 0;
  st.battShown = battery_present() && g.valid();
  st.battPercent = g.percent();
  st.battLow = battery_present() && g.valid() && g.low();
  st.mode = c.scaleMode;
  st.stats = &stats_tracker();
  st.statsRotation = c.statsRotation;
  st.statsAfterMs = c.statsAfterS * 1000u;
  st.statsGoalMs = c.statsGoalS * 1000u;
  st.statsStepMs = c.statsStepS * 1000u;
  st.ach = lastAch;
  st.achSeq = lastAchSeq;

  ui::Hold h = {};
  h.active = btn.overlay(now);
  h.zone = btn.zone(now);
  h.heldMs = btn.heldMs(now);

  const char *sys[3];
  bool hasSys = systemScreen(now, sys);
  ui_render(uiModel.build(theGame.view(), st, h, hasSys ? sys : nullptr, now),
            now);
}

// ── Deep-Sleep ────────────────────────────────────────────────────────────────

static void enterDeepSleep() {
  radio_stop(); // verlaesst eine finale Runde und sendet das vorher
  ui_off();
  scale_powerDown();
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BTN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.flush();
  esp_deep_sleep_start();
}

static void updateSleep(const cfg::Config &c, const scale::Reading &r,
                        uint32_t now) {
  power::Blockers b = {};
  b.apOn = radio_apOn();
  b.ownRoundOpen = theGame.ownRoundOpen();
  b.buttonBusy = btn.pressed() || digitalRead(PIN_BTN) == HIGH;
  b.calibrating = cal.active();
  b.actionPending = radio_actionPending() || otaActive;
  if (sleepPolicy.update(now, r.grams, r.valid && scale_ok(), b,
                         c.sleepTimeout))
    enterDeepSleep();
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

static void factoryResetGesture() {
  if (!RESET_CONFIG_ENABLED || digitalRead(PIN_BTN) != HIGH)
    return;
  ui_force("Reset?", "Halten...");
  uint32_t t = millis();
  while (digitalRead(PIN_BTN) == HIGH && millis() - t < 3000)
    delay(10);
  if (millis() - t >= 3000) {
    config_factoryReset();
    ui_force("Reset OK!", nullptr);
    delay(1500);
  }
}

void app_setup() {
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BTN, INPUT);

  config_begin();
  const cfg::Config &boot = config_get();
  ui_begin(boot.displayRotation);
  factoryResetGesture();
  const cfg::Config &c = config_get();
  Serial.print("100-Waage ");
  Serial.println(FW_VERSION);

  uint32_t now = millis();
  bootAt = now;
  btn.begin(digitalRead(PIN_BTN) == HIGH, now); // Weck-Druck ignorieren
  scale_begin(c.scaleFactor);
  scale_core().setStableSpread(stableSpreadFor(c));
  battery_begin(c.batteryPresent, c.battDividerRatio);
  stats_begin();
  theGame.begin(&duell_port(), randomWord, nullptr);
  resetGame(now);
  sleepPolicy.reset(now);
  uiModel.toast(FW_VERSION, now);

  if (radio_takeApAfterBoot()) {
    radio_startAP(c);
    uiModel.toast(radio_apName(), millis(), AP_NAME_TOAST_MS);
  }
  syncRadio(c); // Duell-Modus: Funk direkt an, auch nach dem Aufwachen
}

void app_loop() {
  uint32_t now = millis();
  const cfg::Config &c = config_get();

  button::Zone z = btn.update(digitalRead(PIN_BTN) == HIGH, now);
  if (btn.takeEdge())
    sleepPolicy.activity(now);

  scale_poll(now);
  if (scale_takeRecovered())
    resetGame(now);
  battery_poll(now);

  switch (radio_loop(c, now)) {
  case RadioEvent::ApTimedOut:
    syncRadio(c);
    uiModel.toast("AP aus", now);
    break;
  case RadioEvent::ApRestarted:
    uiModel.toast(radio_apName(), millis(), AP_NAME_TOAST_MS);
    break;
  case RadioEvent::None:
    break;
  }
  now = millis(); // Webserver kann gedauert haben

  // Duell nur im Duell-Modus. Sonst laeuft der Funk nur fuer den AP; die Waage
  // sendet nichts (Ausstieg wurde beim Wechsel gesendet).
  const bool duelOn = radio_isOn() && c.scaleMode == cfg::ScaleMode::Duel;
  if (duelOn)
    duell_update(theGame.localGoal());
  else if (radio_isOn())
    duell_discard_rx(); // keine alten Pakete fuer spaeter aufheben

  updateCalibration(c, now);

  scale::Reading r = scale_core().reading(now);
  game::Input in = {};
  in.now = now;
  in.weightValid = r.valid && scale_ok() && !cal.active() && !otaActive;
  in.weight = r.grams;
  in.stable = r.stable;
  in.radioOn = duelOn;
  theGame.update(c, in);
  applyScaleReq(theGame.takeScaleReq(), c, now);
  game::RoundDone round;
  if (theGame.takeRound(&round)) {
    lastAch = stats_record(round);
    lastAchSeq = theGame.view().roundSeq;
  }
  game::DuelFinal duelFinal;
  if (theGame.takeDuelFinal(&duelFinal))
    stats_duelFinal(duelFinal);

  if (z != button::Zone::None)
    handleButton(z, now);

  updateSleep(c, r, now);
  render(config_get(), now);
}
