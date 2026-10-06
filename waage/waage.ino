#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "types.h"
#include "config.h"
#include "display.h"
#include "state.h"
#include "webconfig.h"
#include "esp_sleep.h"
#include "duell.h"


// ── Feature flags ─────────────────────────────────────────────────────────────
constexpr bool BATTERY_CONNECTED = true;
constexpr bool RESET_CONFIG_ENABLED = false;

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
// Funk (ESP-NOW fuers Duell) und Config-AP sind getrennt: Der WLAN-Timeout
// schaltet nur den AP ab, das Duell laeuft weiter.
static bool radioOn = false;
static bool apOn = false;
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

// ── Funk / WiFi management ────────────────────────────────────────────────────
static void startRadio() {
  if (radioOn) return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();     // nie mit einem gespeicherten Netz verbinden (Kanalwechsel)
  WiFi.setSleep(false);  // Modem-Sleep wuerde ESP-NOW-Pakete verschlucken
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_channel(DUELL_CHANNEL, WIFI_SECOND_CHAN_NONE);
  duell_init();
  radioOn = true;
}

static void stopAP() {
  if (!apOn) return;
  stopWebServer();
  apOn = false;
}

static void stopRadio() {
  if (!radioOn) return;
  stopAP();
  duell_deinit();
  WiFi.mode(WIFI_OFF);
  radioOn = false;
}

static void startAP() {
  if (apOn) return;
  startRadio();
  startWebServer(cfg);
  lockDisplay(3000);
  apOn = true;
}

// ── Deep sleep ────────────────────────────────────────────────────────────────
static void enterDeepSleep() {
  stopRadio();
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
          if (apOn) {
            stopRadio();
            displayText("WiFi AUS");
            lockDisplay(1000);
          } else {
            startAP();
          }
          lastActivityTime = millis();
        }
      } else if (holdFired3s) {
        ScaleMode newMode = previewMode;
        setScaleMode(newMode);
        resetState(cfg);
        cfg.scaleMode = (newMode == ScaleMode::Standard) ? 1 : 0;
        saveConfig(cfg);
      } else {
        resetState(cfg);
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
      displayText(apOn ? "WiFi AUS" : "WiFi AN");
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
  randomSeed(esp_random());

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

  resetState(cfg);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  handleButton();

  bool duellOn = radioOn && getCurrentScaleMode() == ScaleMode::Game;

  if (isDisplayLocked()) {
    updateWeight();
    if (apOn) handleWebRequests();
    if (duellOn) duell_update(getLocalGameGoal());
    return;
  }

  // Battery
  if (BATTERY_CONNECTED && millis() - lastBattReadTime > BATT_READ_INTERVAL_MS) {
    batteryPercent = readBatteryPercent();
    lastBattReadTime = millis();
    setLiveBatteryPercent(batteryPercent);
  }

  // AP auto-off: nur der Config-AP geht aus, der Duell-Funk bleibt an
  if (apOn) {
    handleWebRequests();
    if (cfg.wifiTimeout > 0 && millis() - getLastHttpActivity() > (unsigned long)cfg.wifiTimeout * 60000UL) {
      stopAP();
    }
  }

  // Activity tracking + deep sleep (only in Idle, AP off, keine Duell-Runde)
  if (!apOn && cfg.sleepTimeout > 0 && getCurrentState() == State::Idle && !duell_busy()) {
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
  } else {
    lastActivityTime = millis();
  }

  updateWeight();
  if (duellOn) duell_update(getLocalGameGoal());
  updateState(cfg, radioOn, batteryPercent);
}
