#include <Adafruit_SSD1306.h>
#include <cstring>
#include <WiFi.h>
#include "types.h"
#include "esp_sleep.h"

// ── Feature flags ─────────────────────────────────────────────────────────────
// Uncomment to enable battery voltage reading on GPIO 2 (requires voltage divider)
// #define BATTERY_CONNECTED
// Uncomment to enable boot-time factory reset by holding button while powering on
#define RESET_CONFIG  // Hold button on power-up to reset config to defaults (including WiFi credentials)

// Forward declarations (defined in webconfig.ino / displaying.ino)
void displayLines(String line, String line2 = "", String line3 = "", boolean border = false);
void displayText(String line, boolean border = false);
void displayLoadingAnimation(int frame);
void drawBatteryIcon(int x, int y, int percent);
void drawWifiIcon(int x, int y);
String getTrinkspruch();
void startWebConfig();
void stopWebConfig();
WaageConfig getConfig();
void saveConfig();
void clearConfig();
unsigned long getLastHttpActivity();
void persistScaleMode(uint8_t mode);

// ── Display ───────────────────────────────────────────────────────────────────
#define OLED_SDA      8
#define OLED_SCL      9
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── HX711 ─────────────────────────────────────────────────────────────────────
#include "HX711.h"
#define HX711_DAT 21
#define HX711_CLK 20
HX711 hx711;

// ── Pins ──────────────────────────────────────────────────────────────────────
#define BTN_PIN  5
#define BATT_PIN 2

// ── Battery (Li-ion 3.0 V–4.2 V, voltage divider on GPIO 2) ──────────────────
#define BATT_ADC_MAX     4095.0f
#define BATT_REF_VOLTAGE 3.3f
#define BATT_MIN_VOLTAGE 3.0f
#define BATT_MAX_VOLTAGE 4.2f

// ── Timing constants ─────────────────────────────────────────────────────────
#define timeToSettle          500UL
#define resultUpdateInterval 3000UL
#define battReadInterval     5000UL
#define weightChangeThreshold  2.0f

// ── Runtime config (loaded from EEPROM) ───────────────────────────────────────
float   scaleFactor      = 708.0f;
long    tareOffset       = 0;
float   goal             = 100.0f;
float   tolerance        = 10.0f;
float   battDividerRatio = 2.0f;
uint8_t autoResetRange   = 10;
uint8_t sleepTimeoutMin  = 5;
uint8_t wifiTimeoutMin   = 10;

// ── State machine ─────────────────────────────────────────────────────────────
enum States     { Idle, Tare, Drinking, Result };
enum DisplayMode{ ShowResult, ShowTime };
enum ScaleMode  { Game, Standard };

volatile States      state       = Idle;
volatile DisplayMode displayMode = ShowResult;
volatile ScaleMode   scaleMode   = Game;

// ── Weight ────────────────────────────────────────────────────────────────────
float weight       = 0.0f;
float full_weight  = 0.0f;
float empty_weight = 0.0f;
float final_weight = 0.0f;
unsigned long timeStarted      = 0;
unsigned long timeEnd          = 0;
unsigned long lastResultUpdate = 0;
boolean       rdyDisplayed     = false;

// ── Button ────────────────────────────────────────────────────────────────────
volatile boolean buttonStateChanged  = false;
unsigned long    buttonPressStart    = 0;
boolean          holdTriggered3s     = false;
boolean          holdTriggered5s     = false;

// pending state set at milestones, committed on release
ScaleMode        previewMode         = Game;
boolean          pendingWifiToggle   = false;

// ── Power / activity tracking ─────────────────────────────────────────────────
unsigned long lastActivityTime    = 0;
unsigned long lastWeightCheckTime = 0;
float         lastCheckedWeight   = 0.0f;
unsigned long lastBattRead        = 0;
int           batteryPercent      = 100;

// ── WiFi state ────────────────────────────────────────────────────────────────
boolean wifiActive = false;

// ── Display lock (prevents stateIdle from overwriting preview messages) ───────
volatile unsigned long displayLockedUntil = 0;
void lockDisplay(unsigned long ms) { displayLockedUntil = millis() + ms; }
bool isDisplayLocked()             { return millis() < displayLockedUntil; }

// ── Auto-reset tracking ───────────────────────────────────────────────────────
unsigned long weightReleasedSince = 0;

// ── ISR ──────────────────────────────────────────────────────────────────────
void IRAM_ATTR handleInterrupt() {
  buttonStateChanged = true;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
void updateWeight() {
  weight = hx711.get_units(10);
}

int readBatteryPercent() {
  int   raw   = analogRead(BATT_PIN);
  float adcV  = (raw / BATT_ADC_MAX) * BATT_REF_VOLTAGE;
  float battV = adcV * battDividerRatio;
  float pct   = (battV - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE) * 100.0f;
  return (int)constrain(pct, 0.0f, 100.0f);
}

void resetState() {
  state               = Idle;
  weight              = full_weight = empty_weight = final_weight = 0.0f;
  rdyDisplayed        = false;
  displayMode         = ShowResult;
  weightReleasedSince = 0;
  hx711.tare(10);
  lastActivityTime    = millis();
}

void enterDeepSleep() {
  // Power down HX711: hold CLK high for >60 µs
  pinMode(HX711_CLK, OUTPUT);
  digitalWrite(HX711_CLK, HIGH);
  delayMicroseconds(100);
  // OLED off
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  // Wake on button HIGH (GPIO 5)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

// ── WiFi management ───────────────────────────────────────────────────────────
void startWiFiAP() {
  if (!wifiActive) {
    startWebConfig();
    wifiActive = true;
    lastActivityTime = millis();
  }
}

void stopWiFiAP() {
  if (wifiActive) {
    stopWebConfig();
    wifiActive = false;
    displayText("WiFi AUS");
    delay(1000);
    lastActivityTime = millis();
  }
}

// ── Mode toggle (commit only) ─────────────────────────────────────────────────
void applyScaleMode(ScaleMode newMode) {
  scaleMode = newMode;
  resetState();
  persistScaleMode((scaleMode == Standard) ? 1 : 0);
}

// ── Button handling ───────────────────────────────────────────────────────────
void handleButtonPress() {
  if (buttonStateChanged) {
    buttonStateChanged = false;
    int btn = digitalRead(BTN_PIN);

    if (btn == HIGH && buttonPressStart == 0) {
      // Press start — record initial state for preview
      buttonPressStart  = millis();
      holdTriggered3s   = false;
      holdTriggered5s   = false;
      pendingWifiToggle = false;
      previewMode       = scaleMode;

    } else if (btn == LOW && buttonPressStart > 0) {
      // Release — commit based on which milestones fired
      if (holdTriggered5s) {
        // ≥ 5 s: mode unchanged, WiFi toggled
        if (pendingWifiToggle) {
          if (wifiActive) stopWiFiAP();
          else            startWiFiAP();
        }
      } else if (holdTriggered3s) {
        // 3–5 s: commit mode change silently
        applyScaleMode(previewMode);
      } else {
        // < 3 s: tare / reset
        resetState();
      }
      buttonPressStart = 0;
    }
  }

  if (buttonPressStart > 0) {
    unsigned long held = millis() - buttonPressStart;

    if (held >= 3000UL && !holdTriggered3s) {
      holdTriggered3s = true;
      // Preview: flip mode on display, don't commit yet
      previewMode = (scaleMode == Game) ? Standard : Game;
      displayText(previewMode == Game ? "Game Mode" : "Standard Mode");
      lockDisplay(1000);
    }

    if (held >= 5000UL && !holdTriggered5s) {
      holdTriggered5s   = true;
      previewMode       = scaleMode;   // revert mode preview
      pendingWifiToggle = true;
      // Preview: show what WiFi will do on release
      displayText(wifiActive ? "WiFi AUS" : "WiFi AN");
      lockDisplay(1000);
    }
  }
}

// ── State functions ───────────────────────────────────────────────────────────
void stateIdle() {
  if (isDisplayLocked()) return;  // hold preview messages visible
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);

  if (scaleMode == Standard) {
    String txt = String(weight, 1) + "g";
    if (txt == "-0.0g") txt = "0.0g"; // avoid negative zero display
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
  } else {
    String txt = String(goal, 1) + "g?";
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    display.print(txt);
    if (abs(weight) > tolerance)
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  }

  // Top-right corner: WiFi symbol when WiFi is active, battery when not
  if (wifiActive) drawWifiIcon(SCREEN_WIDTH - 14, 0);
#ifdef BATTERY_CONNECTED
  else            drawBatteryIcon(SCREEN_WIDTH - 14, 0, batteryPercent);
#endif
  display.display();

  if (scaleMode == Game && weight >= goal) {
    delay(timeToSettle);
    updateWeight();
    full_weight = weight;
    state       = Tare;
    lastActivityTime = millis();
  }
}

void stateTare() {
  if (!rdyDisplayed) {
    displayText("Bereit?");
    delay(500);
    rdyDisplayed = true;
    displayText(getTrinkspruch());
  }
  if (weight > full_weight - tolerance) return;
  delay(timeToSettle);
  timeStarted  = millis();
  updateWeight();
  empty_weight = weight;
  state        = Drinking;
  lastActivityTime = millis();
}

void stateDrinking() {
  static int frame = 0;
  if (weight < empty_weight + tolerance) {
    displayLoadingAnimation(frame++);
    return;
  }
  delay(timeToSettle);
  timeEnd      = millis();
  updateWeight();
  final_weight = weight;
  state        = Result;
  lastActivityTime = millis();
}

void stateResult() {
  // Auto-reset for BAD results (outside ±autoResetRange % of goal) on glass removal
  if (abs(weight) < tolerance) {
    if (weightReleasedSince == 0) weightReleasedSince = millis();
    if (millis() - weightReleasedSince > 1000UL) {
      float drankWeight = full_weight - final_weight;
      float pctDiff = (goal > 0.0f) ? abs(drankWeight - goal) / goal * 100.0f : 100.0f;
      if (pctDiff > (float)autoResetRange) {
        weightReleasedSince = 0;
        resetState();
        return;
      }
    }
  } else {
    weightReleasedSince = 0;
  }

  if (millis() - lastResultUpdate < resultUpdateInterval) return;

  float  drankWeight = full_weight - final_weight;
  int    result      = (int)(drankWeight * 100);
  int    goal_int    = (int)(goal * 100);
  String duration    = String((timeEnd - timeStarted) / 1000.0, 2) + "s";
  String result_fmt  = (displayMode == ShowTime) ? duration : String(drankWeight, 2) + "g";

  if (result == goal_int)
    displayLines(result_fmt, "Perfekt!");
  else if (abs(result - goal_int) <= 10)
    displayLines(result_fmt, "Not Bad!");
  else if (abs(result - goal_int) <= 100)
    displayLines(result_fmt, "Ganz ok!");
  else if (result < goal_int)
    displayLines(result_fmt, "Schuchtern");
  else
    displayLines(result_fmt, "Zu gierig!");

  lastResultUpdate = millis();
  displayMode = (displayMode == ShowResult) ? ShowTime : ShowResult;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  pinMode(BTN_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), handleInterrupt, CHANGE);
  pinMode(BATT_PIN, INPUT);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.setTextColor(SSD1306_WHITE);
  display.cp437();

#ifdef RESET_CONFIG
  // ── Boot-time factory reset: hold button while powering on ────────────────
  if (digitalRead(BTN_PIN) == HIGH) {
    displayText("Reset? Halten...");
    unsigned long t = millis();
    while (digitalRead(BTN_PIN) == HIGH && millis() - t < 3000) delay(10);
    if (millis() - t >= 3000) {
      clearConfig();           // wipe valid flags → all defaults on next load
      displayText("Reset OK!");
      delay(1500);
    }
  }
#endif

  WaageConfig cfg = getConfig();
  display.setRotation(cfg.displayRotation);
  display.setTextColor(SSD1306_WHITE);
  display.cp437();

  if (cfg.valid) {
    scaleFactor      = cfg.scaleFactor;
    tareOffset       = cfg.tareOffset;
    goal             = cfg.goal;
    tolerance        = cfg.tolerance;
    battDividerRatio = cfg.battDividerRatio;
    autoResetRange   = cfg.autoResetRange;
    sleepTimeoutMin  = cfg.sleepTimeout;
    wifiTimeoutMin   = cfg.wifiTimeout;
    scaleMode        = (cfg.scaleMode == 1) ? Standard : Game;
  }

  hx711.begin(HX711_DAT, HX711_CLK);
  if (abs(scaleFactor) > 1e-6f) hx711.set_scale(scaleFactor);
  hx711.tare(10);

  lastActivityTime = millis();
  lastBattRead     = millis();
#ifdef BATTERY_CONNECTED
  batteryPercent   = readBatteryPercent();
#endif
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  handleButtonPress();

  // Periodic battery read
#ifdef BATTERY_CONNECTED
  if (millis() - lastBattRead > battReadInterval) {
    batteryPercent = readBatteryPercent();
    lastBattRead   = millis();
  }
#endif

  // WiFi auto-off
  if (wifiActive && wifiTimeoutMin > 0) {
    if (millis() - getLastHttpActivity() > (unsigned long)wifiTimeoutMin * 60000UL) {
      stopWiFiAP();
    }
  }

  // Deep sleep (Idle only, WiFi off, sleep timeout configured)
  if (!wifiActive && sleepTimeoutMin > 0 && state == Idle) {
    if (millis() - lastWeightCheckTime > 2000UL) {
      if (abs(weight - lastCheckedWeight) > weightChangeThreshold) {
        lastActivityTime  = millis();
        lastCheckedWeight = weight;
      }
      lastWeightCheckTime = millis();
    }
    if (millis() - lastActivityTime > (unsigned long)sleepTimeoutMin * 60000UL) {
      enterDeepSleep();
    }
  } else if (state != Idle) {
    lastActivityTime = millis(); // Keep alive during active states
  }

  // State machine
  if (scaleMode == Game) {
    switch (state) {
      case Idle:     stateIdle();     break;
      case Tare:     stateTare();     break;
      case Drinking: stateDrinking(); break;
      case Result:   stateResult();   break;
    }
  } else {
    stateIdle();
  }

  updateWeight();
}

