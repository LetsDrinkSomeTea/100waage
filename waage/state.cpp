#include "types.h"
#include "state.h"
#include "display.h"
#include "HX711.h"
#include "duell.h"

constexpr int HX711_DAT = 21;
constexpr int HX711_CLK = 20;
constexpr int HX711_OVERSAMPLE = 10;
constexpr int WEIGHT_AVG_N = 10;
// Muss laenger sein als das Mittelungsfenster (10 Samples @ 10 SPS = 1 s),
// damit der gleitende Mittelwert beim Uebernehmen nur Werte nach dem
// Gewichtswechsel enthaelt.
constexpr unsigned long SETTLE_MS = 1500UL;
constexpr unsigned long RESULT_INTERVAL_MS = 3000UL;
constexpr unsigned long ANIM_FRAME_MS = 150UL;
constexpr unsigned long WAITREADY_TIMEOUT_MS = 60000UL;
constexpr unsigned long WAITRESULT_TIMEOUT_MS = 20000UL;

static HX711 hx711;

static float weight = 0.0f;
static float fullWeight = 0.0f;
static float emptyWeight = 0.0f;
static float finalWeight = 0.0f;
static float duellTarget = 0.0f;
static float localGameGoal = 0.0f;

static unsigned long timeStarted = 0;
static unsigned long timeEnd = 0;
static unsigned long lastResultUpdate = 0;
static unsigned long weightReleasedSince = 0;
static unsigned long autoZeroStableSince = 0;
static unsigned long autoZeroLastTare = 0;

// Settle-Fenster: Zustandswechsel erst, wenn die Bedingung SETTLE_MS lang haelt
static unsigned long settleIdle = 0;
static unsigned long settleTare = 0;
static unsigned long settleDrink = 0;
static unsigned long tareMsgShownAt = 0;
static unsigned long lastAnimFrame = 0;

static bool rdyDisplayed = false;
static bool wasDuell = false;  // Runde lief im Duell — auch nach Solo-Fallback resetten

static State currentState = State::Idle;
static ScaleMode currentMode = ScaleMode::Game;
static MultiplayerState mpState = MultiplayerState::Offline;
static unsigned long mpStateSince = 0;

enum class DisplayMode { Result,
                         Time };

static DisplayMode displayMode = DisplayMode::Result;

static void setMpState(MultiplayerState s) {
  mpState = s;
  mpStateSince = millis();
}

// ── HX711 ─────────────────────────────────────────────────────────────────────

static float weightSamples[WEIGHT_AVG_N];
static int weightSampleIdx = 0;
static int weightSampleCount = 0;

// Nach jedem tare() aufrufen, sonst verfaelschen alte Samples den Mittelwert
static void resetWeightFilter() {
  weightSampleIdx = 0;
  weightSampleCount = 0;
  weight = 0.0f;
}

void initScale(float scaleFactor) {
  hx711.begin(HX711_DAT, HX711_CLK);
  if (scaleFactor > 1e-6f || scaleFactor < -1e-6f) hx711.set_scale(scaleFactor);
  hx711.tare(HX711_OVERSAMPLE);
  resetWeightFilter();
}

float calibrateScale(float knownWeight) {
  hx711.set_scale();
  hx711.tare(HX711_OVERSAMPLE);
  delay(10000);
  hx711.calibrate_scale(knownWeight);
  resetWeightFilter();
  return hx711.get_scale();
}

// Nicht-blockierend: pro Aufruf hoechstens ein Sample lesen, gleitender
// Mittelwert ueber WEIGHT_AVG_N Samples (gleiche Glaettung wie frueher
// get_units(10), aber ohne den Loop ~1 s zu blockieren).
void updateWeight() {
  if (!hx711.is_ready()) return;
  weightSamples[weightSampleIdx] = hx711.get_units(1);
  weightSampleIdx = (weightSampleIdx + 1) % WEIGHT_AVG_N;
  if (weightSampleCount < WEIGHT_AVG_N) weightSampleCount++;
  float sum = 0.0f;
  for (int i = 0; i < weightSampleCount; i++) sum += weightSamples[i];
  weight = sum / weightSampleCount;
}

float getCurrentWeight() {
  return weight;
}

// ── State accessors ───────────────────────────────────────────────────────────

State getCurrentState() {
  return currentState;
}
ScaleMode getCurrentScaleMode() {
  return currentMode;
}
void setScaleMode(ScaleMode mode) {
  currentMode = mode;
}

float getLocalGameGoal() {
  return localGameGoal;
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void resetState(const WaageConfig& cfg) {
  currentState = State::Idle;
  setMpState(MultiplayerState::Offline);
  displayMode = DisplayMode::Result;
  rdyDisplayed = false;
  wasDuell = false;
  weight = fullWeight = emptyWeight = finalWeight = 0.0f;
  weightReleasedSince = 0;
  autoZeroStableSince = 0;
  autoZeroLastTare = 0;
  settleIdle = settleTare = settleDrink = 0;
  tareMsgShownAt = 0;
  lastResultUpdate = 0;
  hx711.tare(HX711_OVERSAMPLE);
  resetWeightFilter();
  duell_reset_state();

  if (currentMode == ScaleMode::Game) {
    if (cfg.randomModeEnabled) {
      float minG = max(cfg.randomMin, cfg.tolerance);
      float maxG = max(cfg.goal, minG);
      localGameGoal = random((int)minG, (int)maxG + 1);
    } else {
      localGameGoal = cfg.goal;
    }
  }
}


// ── Zero tracking ─────────────────────────────────────────────────────────────

static void handleAutoZero(const WaageConfig& cfg) {
  if (!cfg.autoZeroEnabled) return;
  unsigned long cooldown = (unsigned long)cfg.autoZeroDelay * 3000UL;
  if (millis() - autoZeroLastTare < cooldown) {
    autoZeroStableSince = 0;
    return;
  }
  if (abs(weight) < cfg.autoZeroThreshold) {
    if (autoZeroStableSince == 0) autoZeroStableSince = millis();
    if (millis() - autoZeroStableSince >= (unsigned long)cfg.autoZeroDelay * 1000UL) {
      hx711.tare(HX711_OVERSAMPLE);
      resetWeightFilter();
      autoZeroLastTare = millis();
      autoZeroStableSince = 0;
    }
  } else {
    autoZeroStableSince = 0;
  }
}

// ── State handlers ────────────────────────────────────────────────────────────

static void stateIdle(const WaageConfig& cfg, bool wifiActive, int batteryPercent) {
  handleAutoZero(cfg);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);

  if (currentMode == ScaleMode::Standard) {
    float displayWeight = (cfg.autoZeroEnabled && abs(weight) < cfg.autoZeroThreshold) ? 0.0f : weight;
    String txt = String(displayWeight, 1) + "g";
    if (txt == "-0.0g") txt = "0.0g";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
  } else {
    String txt = String(localGameGoal, 1) + "g?";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
    if (abs(weight) > cfg.tolerance)
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

    if (cfg.randomModeEnabled) {
      drawShuffleIcon(0, 0);
    }
  }

  if (wifiActive) {
    if (currentMode == ScaleMode::Game && duell_is_active()) {
      drawDuellIcon(SCREEN_WIDTH - 26, 0, duell_get_peers_count());
    } else {
      drawWifiIcon(SCREEN_WIDTH - 14, 0);
    }
  } else if (batteryPercent >= 0) drawBatteryIcon(SCREEN_WIDTH - 14, 0, batteryPercent);
  display.display();

  if (currentMode == ScaleMode::Game && weight >= localGameGoal) {
    if (settleIdle == 0) settleIdle = millis();
    if (millis() - settleIdle >= SETTLE_MS) {
      settleIdle = 0;
      fullWeight = weight;

      if (wifiActive && duell_is_active()) {
        setMpState(MultiplayerState::WaitReady);
        wasDuell = true;
        duell_send_ready();
        currentState = State::Tare;  // We hijack State::Tare for multiplayer flow
      } else {
        currentState = State::Tare;
      }
    }
  } else {
    settleIdle = 0;
  }
}


static void stateTare(const WaageConfig& cfg) {
  if (mpState == MultiplayerState::WaitReady) {
    if (duell_has_start_signal(&duellTarget)) {
      setMpState(MultiplayerState::WaitStart);
    } else if (!duell_is_active()
               || millis() - mpStateSince > WAITREADY_TIMEOUT_MS
               || weight < fullWeight - cfg.tolerance) {
      // Gegner weg, Timeout oder Glas trotzdem abgehoben → solo weiterspielen
      setMpState(MultiplayerState::Offline);
      duell_reset_state();
    } else {
      displayText("Warte auf Gegner...");
      return;
    }
  }

  if (!rdyDisplayed) {
    if (tareMsgShownAt == 0) {
      tareMsgShownAt = millis();
      displayText("Bereit?");
    } else if (millis() - tareMsgShownAt >= 500UL) {
      if (mpState == MultiplayerState::WaitStart) {
        displayLines("Ziel", String(duellTarget, 1) + "g");
      } else {
        displayText(getRandomTrinkspruch());
      }
      rdyDisplayed = true;
    }
  }

  if (weight > fullWeight - cfg.tolerance) {
    settleTare = 0;
    return;
  }
  if (settleTare == 0) {
    settleTare = millis();
    timeStarted = settleTare;
    return;
  }
  if (millis() - settleTare < SETTLE_MS) return;
  settleTare = 0;
  emptyWeight = weight;
  if (mpState != MultiplayerState::Offline) duell_set_phase(DuellPhase::Drinking);
  currentState = State::Drinking;
}


static void stateDrinking(const WaageConfig& cfg) {
  static int frame = 0;
  if (weight < emptyWeight + cfg.tolerance) {
    settleDrink = 0;
    if (millis() - lastAnimFrame >= ANIM_FRAME_MS) {
      displayLoadingAnimation(frame++);
      lastAnimFrame = millis();
    }
    return;
  }
  if (settleDrink == 0) {
    settleDrink = millis();
    timeEnd = settleDrink;
    return;
  }
  if (millis() - settleDrink < SETTLE_MS) return;
  settleDrink = 0;
  finalWeight = weight;

  if (mpState != MultiplayerState::Offline) {
    setMpState(MultiplayerState::WaitResult);
    duell_send_result(fullWeight - finalWeight);
  }
  currentState = State::Result;
}

static void stateResult(const WaageConfig& cfg) {
  // Glas-entfernt-Auto-Reset zuerst pruefen — muss auch greifen, wenn das
  // Ranking nie ankommt, sonst haengt die Waage in "Auswertung..." fest.
  // Im WaitResult etwas laenger warten, damit ein gleich eintreffendes
  // Ranking noch angezeigt werden kann.
  unsigned long releaseHold = (mpState == MultiplayerState::WaitResult) ? 5000UL : 1000UL;
  if (abs(weight) < cfg.tolerance) {
    if (weightReleasedSince == 0) weightReleasedSince = millis();
    if (millis() - weightReleasedSince > releaseHold) {
      float drank = fullWeight - finalWeight;
      float refGoal = (currentMode == ScaleMode::Game) ? localGameGoal : cfg.goal;
      float pctDiff = (refGoal > 0.0f) ? abs(drank - refGoal) / refGoal * 100.0f : 100.0f;
      if (pctDiff > (float)cfg.autoResetRange || mpState != MultiplayerState::Offline || wasDuell) {
        weightReleasedSince = 0;
        resetState(cfg);
        return;
      }
    }
  } else {
    weightReleasedSince = 0;
  }

  if (mpState == MultiplayerState::WaitResult) {
    int rank;
    if (duell_has_ranking(&rank)) {
      setMpState(MultiplayerState::Result);
      duell_set_phase(DuellPhase::ShowingResult);  // Master hoert auf zu wiederholen
      lastResultUpdate = 0;
      weightReleasedSince = 0;  // Rang mindestens kurz anzeigen
    } else if (millis() - mpStateSince > WAITRESULT_TIMEOUT_MS) {
      // Ranking kommt nicht mehr (Master weg?) → Solo-Auswertung ohne Rang
      setMpState(MultiplayerState::Offline);
      duell_reset_state();
      lastResultUpdate = 0;
      weightReleasedSince = 0;
    } else {
      displayText("Auswertung...");
      return;
    }
  }

  if (lastResultUpdate != 0 && millis() - lastResultUpdate < RESULT_INTERVAL_MS) return;

  float drank = fullWeight - finalWeight;
  int drankInt = (int)(drank * 100);
  float refGoal = (currentMode == ScaleMode::Game) ? localGameGoal : cfg.goal;
  int goalInt = (int)(refGoal * 100);
  String duration = String((timeEnd - timeStarted) / 1000.0f, 2) + "s";

  if (mpState == MultiplayerState::Result) {
    int rank = 0;
    duell_has_ranking(&rank);
    String resultFmt = String(drank, 1) + "g";
    String rankFmt = String(rank) + ". Platz!";
    if (displayMode == DisplayMode::Time) {
      displayLines(resultFmt, duration);
    } else {
      displayLines(resultFmt, rankFmt);
    }
  } else {
    String resultFmt = (displayMode == DisplayMode::Time) ? duration : String(drank, 2) + "g";

    if (drankInt == goalInt) displayLines(resultFmt, "Perfekt!");
    else if (abs(drankInt - goalInt) <= 10) displayLines(resultFmt, "Not Bad!");
    else if (abs(drankInt - goalInt) <= 100) displayLines(resultFmt, "Ganz ok!");
    else if (drankInt < goalInt) displayLines(resultFmt, "Schuchtern");
    else displayLines(resultFmt, "Zu gierig!");
  }

  lastResultUpdate = millis();
  displayMode = (displayMode == DisplayMode::Result) ? DisplayMode::Time : DisplayMode::Result;
}


// ── Main update ───────────────────────────────────────────────────────────────

void updateState(const WaageConfig& cfg, bool wifiActive, int batteryPercent) {
  if (currentMode == ScaleMode::Standard) {
    stateIdle(cfg, wifiActive, batteryPercent);
    return;
  }
  switch (currentState) {
    case State::Idle: stateIdle(cfg, wifiActive, batteryPercent); break;
    case State::Tare: stateTare(cfg); break;
    case State::Drinking: stateDrinking(cfg); break;
    case State::Result: stateResult(cfg); break;
  }
}
