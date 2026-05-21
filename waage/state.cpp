#include "state.h"
#include "display.h"
#include "HX711.h"

constexpr int   HX711_DAT          = 21;
constexpr int   HX711_CLK          = 20;
constexpr int   HX711_OVERSAMPLE   = 10;
constexpr unsigned long SETTLE_MS  = 500UL;
constexpr unsigned long RESULT_INTERVAL_MS = 3000UL;

static HX711 hx711;

static float weight      = 0.0f;
static float fullWeight  = 0.0f;
static float emptyWeight = 0.0f;
static float finalWeight = 0.0f;

static unsigned long timeStarted   = 0;
static unsigned long timeEnd       = 0;
static unsigned long lastResultUpdate = 0;
static unsigned long weightReleasedSince = 0;
static unsigned long autoZeroStableSince = 0;
static unsigned long autoZeroLastTare    = 0;

static bool rdyDisplayed = false;

static State     currentState = State::Idle;
static ScaleMode currentMode  = ScaleMode::Game;

enum class DisplayMode { Result, Time };
static DisplayMode displayMode = DisplayMode::Result;

// ── HX711 ─────────────────────────────────────────────────────────────────────

void initScale(float scaleFactor) {
  hx711.begin(HX711_DAT, HX711_CLK);
  if (scaleFactor > 1e-6f || scaleFactor < -1e-6f) hx711.set_scale(scaleFactor);
  hx711.tare(HX711_OVERSAMPLE);
}

float calibrateScale(float knownWeight) {
  hx711.set_scale();
  hx711.tare(HX711_OVERSAMPLE);
  delay(10000);
  hx711.calibrate_scale(knownWeight);
  return hx711.get_scale();
}

void updateWeight() {
  weight = hx711.get_units(HX711_OVERSAMPLE);
}

float getCurrentWeight() { return weight; }

// ── State accessors ───────────────────────────────────────────────────────────

State     getCurrentState()     { return currentState; }
ScaleMode getCurrentScaleMode() { return currentMode; }
void      setScaleMode(ScaleMode mode) { currentMode = mode; }

// ── Reset ─────────────────────────────────────────────────────────────────────

void resetState() {
  currentState  = State::Idle;
  displayMode   = DisplayMode::Result;
  rdyDisplayed  = false;
  weight = fullWeight = emptyWeight = finalWeight = 0.0f;
  weightReleasedSince = 0;
  autoZeroStableSince = 0;
  autoZeroLastTare    = 0;
  hx711.tare(HX711_OVERSAMPLE);
}

// ── Zero tracking ─────────────────────────────────────────────────────────────

static void handleAutoZero(const WaageConfig& cfg) {
  if (!cfg.autoZeroEnabled) return;
  unsigned long cooldown = (unsigned long)cfg.autoZeroDelay * 3000UL;
  if (millis() - autoZeroLastTare < cooldown) { autoZeroStableSince = 0; return; }
  if (abs(weight) < cfg.autoZeroThreshold) {
    if (autoZeroStableSince == 0) autoZeroStableSince = millis();
    if (millis() - autoZeroStableSince >= (unsigned long)cfg.autoZeroDelay * 1000UL) {
      hx711.tare(HX711_OVERSAMPLE);
      autoZeroLastTare    = millis();
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
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
  } else {
    String txt = String(cfg.goal, 1) + "g?";
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
    if (abs(weight) > cfg.tolerance)
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  }

  if (wifiActive)               drawWifiIcon(SCREEN_WIDTH - 14, 0);
  else if (batteryPercent >= 0) drawBatteryIcon(SCREEN_WIDTH - 14, 0, batteryPercent);
  display.display();

  if (currentMode == ScaleMode::Game && weight >= cfg.goal) {
    delay(SETTLE_MS);
    updateWeight();
    fullWeight   = weight;
    currentState = State::Tare;
  }
}

static void stateTare(const WaageConfig& cfg) {
  if (!rdyDisplayed) {
    displayText("Bereit?");
    delay(500);
    displayText(getRandomTrinkspruch());
    rdyDisplayed = true;
  }
  if (weight > fullWeight - cfg.tolerance) return;
  delay(SETTLE_MS);
  updateWeight();
  emptyWeight  = weight;
  timeStarted  = millis();
  currentState = State::Drinking;
}

static void stateDrinking(const WaageConfig& cfg) {
  static int frame = 0;
  if (weight < emptyWeight + cfg.tolerance) {
    displayLoadingAnimation(frame++);
    return;
  }
  delay(SETTLE_MS);
  timeEnd      = millis();
  updateWeight();
  finalWeight  = weight;
  currentState = State::Result;
}

static void stateResult(const WaageConfig& cfg) {
  if (abs(weight) < cfg.tolerance) {
    if (weightReleasedSince == 0) weightReleasedSince = millis();
    if (millis() - weightReleasedSince > 1000UL) {
      float drank   = fullWeight - finalWeight;
      float pctDiff = (cfg.goal > 0.0f) ? abs(drank - cfg.goal) / cfg.goal * 100.0f : 100.0f;
      if (pctDiff > (float)cfg.autoResetRange) {
        weightReleasedSince = 0;
        resetState();
        return;
      }
    }
  } else {
    weightReleasedSince = 0;
  }

  if (millis() - lastResultUpdate < RESULT_INTERVAL_MS) return;

  float drank    = fullWeight - finalWeight;
  int   drankInt = (int)(drank * 100);
  int   goalInt  = (int)(cfg.goal * 100);
  String duration   = String((timeEnd - timeStarted) / 1000.0f, 2) + "s";
  String resultFmt  = (displayMode == DisplayMode::Time) ? duration : String(drank, 2) + "g";

  if      (drankInt == goalInt)                   displayLines(resultFmt, "Perfekt!");
  else if (abs(drankInt - goalInt) <= 10)         displayLines(resultFmt, "Not Bad!");
  else if (abs(drankInt - goalInt) <= 100)        displayLines(resultFmt, "Ganz ok!");
  else if (drankInt < goalInt)                    displayLines(resultFmt, "Schuchtern");
  else                                            displayLines(resultFmt, "Zu gierig!");

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
    case State::Idle:     stateIdle(cfg, wifiActive, batteryPercent); break;
    case State::Tare:     stateTare(cfg);                             break;
    case State::Drinking: stateDrinking(cfg);                         break;
    case State::Result:   stateResult(cfg);                           break;
  }
}
