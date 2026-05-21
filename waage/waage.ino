#include <Wire.h>
#include "types.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "webconfig.h"
#include "esp_sleep.h"

// ── Feature flags ─────────────────────────────────────────────────────────────
constexpr bool BATTERY_CONNECTED = true;
constexpr bool RESET_CONFIG_ENABLED = true;

// ── Pins ──────────────────────────────────────────────────────────────────────
constexpr int PIN_OLED_SDA = 8;
constexpr int PIN_OLED_SCL = 9;
constexpr int PIN_BTN = 5;
constexpr int PIN_BATT = 2;

// ── Battery ───────────────────────────────────────────────────────────────────
constexpr float BATT_ADC_MAX = 4095.0f;
constexpr float BATT_REF_VOLTAGE = 3.3f;
constexpr float BATT_MIN_VOLTAGE = 3.0f;
constexpr float BATT_MAX_VOLTAGE = 4.2f;
constexpr unsigned long BATT_READ_INTERVAL_MS = 5000UL;

// ── Timing ────────────────────────────────────────────────────────────────────
constexpr float WEIGHT_CHANGE_THRESHOLD = 2.0f;
constexpr unsigned long WEIGHT_CHECK_INTERVAL_MS = 2000UL;

// ── Runtime state ─────────────────────────────────────────────────────────────
static WaageConfig cfg;
static bool wifiActive = false;
static int batteryPercent = BATTERY_CONNECTED ? 100 : -1;

static unsigned long lastActivityTime = 0;
static unsigned long lastWeightCheckTime = 0;
static unsigned long lastBattReadTime = 0;
static float lastCheckedWeight = 0.0f;

// ── Display lock (prevents idle from overwriting button-preview messages) ─────
static unsigned long displayLockedUntil = 0;

static void lockDisplay(unsigned long ms) {
  displayLockedUntil = millis() + ms;
}
static bool isDisplayLocked() {
  return millis() < displayLockedUntil;
}

// ── Button state ──────────────────────────────────────────────────────────────
static volatile bool buttonChanged = false;

static unsigned long buttonPressStart = 0;
static bool holdFired3s = false;
static bool holdFired5s = false;
static bool pendingWifiToggle = false;
static ScaleMode previewMode = ScaleMode::Game;

void IRAM_ATTR handleButtonISR() {
  buttonChanged = true;
}

// ── Battery ───────────────────────────────────────────────────────────────────
static int readBatteryPercent() {
  int raw = analogRead(PIN_BATT);
  float adcV = (raw / BATT_ADC_MAX) * BATT_REF_VOLTAGE;
  float battV = adcV * cfg.battDividerRatio;
  float pct = (battV - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE) * 100.0f;
  return (int)constrain(pct, 0.0f, 100.0f);
}

// ── WiFi management ───────────────────────────────────────────────────────────
static void startWiFi() {
  if (wifiActive) return;
  startWebServer(cfg);
  wifiActive = true;
  lastActivityTime = millis();
}

static void stopWiFi() {
  if (!wifiActive) return;
  stopWebServer();
  wifiActive = false;
  displayText("WiFi AUS");
  delay(1000);
  lastActivityTime = millis();
}

// ── Deep sleep ────────────────────────────────────────────────────────────────
static void enterDeepSleep() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BTN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

// ── Button handling ───────────────────────────────────────────────────────────
static void handleButton() {
  if (buttonChanged) {
    buttonChanged = false;
    int level = digitalRead(PIN_BTN);

    if (level == HIGH && buttonPressStart == 0) {
      buttonPressStart = millis();
      holdFired3s = false;
      holdFired5s = false;
      pendingWifiToggle = false;
      previewMode = getCurrentScaleMode();

    } else if (level == LOW && buttonPressStart > 0) {
      if (holdFired5s) {
        if (pendingWifiToggle) {
          if (wifiActive) stopWiFi();
          else startWiFi();
        }
      } else if (holdFired3s) {
        ScaleMode newMode = previewMode;
        setScaleMode(newMode);
        resetState();
        cfg.scaleMode = (newMode == ScaleMode::Standard) ? 1 : 0;
        saveConfig(cfg);
      } else {
        resetState();
      }
      buttonPressStart = 0;
    }
  }

  if (buttonPressStart > 0) {
    unsigned long held = millis() - buttonPressStart;

    if (held >= 3000UL && !holdFired3s) {
      holdFired3s = true;
      previewMode = (getCurrentScaleMode() == ScaleMode::Game) ? ScaleMode::Standard : ScaleMode::Game;
      displayText(previewMode == ScaleMode::Game ? "Game Mode" : "Standard");
      lockDisplay(1000);
    }

    if (held >= 5000UL && !holdFired5s) {
      holdFired5s = true;
      previewMode = getCurrentScaleMode();
      pendingWifiToggle = true;
      displayText(wifiActive ? "WiFi AUS" : "WiFi AN");
      lockDisplay(1000);
    }
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  pinMode(PIN_BTN, INPUT);
  pinMode(PIN_BATT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN), handleButtonISR, CHANGE);

  loadConfig(cfg);
  initDisplay(cfg.displayRotation);

  if (RESET_CONFIG_ENABLED && digitalRead(PIN_BTN) == HIGH) {
    displayText("Reset? Halten...");
    unsigned long t = millis();
    while (digitalRead(PIN_BTN) == HIGH && millis() - t < 3000) delay(10);
    if (millis() - t >= 3000) {
      clearConfig();
      loadConfig(cfg);
      displayText("Reset OK!");
      delay(1500);
    }
  }

  setScaleMode(cfg.scaleMode == 1 ? ScaleMode::Standard : ScaleMode::Game);
  initScale(cfg.scaleFactor);

  lastActivityTime = millis();
  lastBattReadTime = millis();

  if (BATTERY_CONNECTED) batteryPercent = readBatteryPercent();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  handleButton();

  if (isDisplayLocked()) {
    updateWeight();
    if (wifiActive) handleWebRequests();
    return;
  }

  // Battery
  if (BATTERY_CONNECTED && millis() - lastBattReadTime > BATT_READ_INTERVAL_MS) {
    batteryPercent = readBatteryPercent();
    lastBattReadTime = millis();
    setLiveBatteryPercent(batteryPercent);
  }

  // WiFi auto-off
  if (wifiActive) {
    handleWebRequests();
    if (cfg.wifiTimeout > 0 && millis() - getLastHttpActivity() > (unsigned long)cfg.wifiTimeout * 60000UL) {
      stopWiFi();
    }
  }

  // Activity tracking + deep sleep (only in Idle, WiFi off)
  if (!wifiActive && cfg.sleepTimeout > 0 && getCurrentState() == State::Idle) {
    if (millis() - lastWeightCheckTime > WEIGHT_CHECK_INTERVAL_MS) {
      float w = getCurrentWeight();
      if (abs(w - lastCheckedWeight) > WEIGHT_CHANGE_THRESHOLD) {
        lastActivityTime = millis();
        lastCheckedWeight = w;
      }
      lastWeightCheckTime = millis();
    }
    if (millis() - lastActivityTime > (unsigned long)cfg.sleepTimeout * 60000UL) {
      enterDeepSleep();
    }
  } else if (getCurrentState() != State::Idle) {
    lastActivityTime = millis();
  }

  updateWeight();
  updateState(cfg, wifiActive, batteryPercent);
}
