#include <Adafruit_SSD1306.h>
#include <cstring>
#include <WiFi.h>
#include "types.h"

void displayLines(String line, String line2 = "", String line3 = "", boolean border = false);
void displayText(String line, boolean border = false);
void displayLoadingAnimation(int frame);
String getTrinkspruch();

#define OLED_SDA 8
#define OLED_SCL 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "HX711.h"

#define HX711_DAT 21
#define HX711_CLK 20

#define timeToSettle 500  // Zeit in ms zum Warten, bis sich das Gewicht stabilisiert hat
#define zeroDisplayThreshold 0.05  // Threshold to avoid displaying signed zero in Standard mode

HX711 hx711;
#define BTN_PIN 5

// Config aus webconfig.ino - werden im setup() geladen
float scaleFactor = 708.0;
long tareOffset = 0;
float goal = 100.0;
float tolerance = 10.0;
unsigned long timeStarted = 0;
unsigned long timeEnd = 0;
enum DisplayMode { ShowResult,
                   ShowTime };
volatile DisplayMode displayMode = ShowResult;

enum States { Idle,
              Tare,
              Drinking,
              Result };
volatile States state = Idle;

enum ScaleMode { Game, Standard };
volatile ScaleMode scaleMode = Game;

volatile boolean buttonStateChanged = false;
unsigned long buttonPressStartTime = 0;
boolean buttonHoldDetected = false;

float weight = 0.0;
float full_weight = 0.0;
float empty_weight = 0.0;
float final_weight = 0.0;
unsigned long lastResultUpdate = millis();
#define resultUpdateInterval 3000
boolean rdyDisplayed = false;

void IRAM_ATTR handleInterrupt() {
  // Edge-triggered ISR - sets flag for main loop
  buttonStateChanged = true;
}

void updateWeight() {
  weight = hx711.get_units(10);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  pinMode(BTN_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), handleInterrupt, CHANGE);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  
  // Lade Konfiguration aus EEPROM
  WaageConfig cfg = getConfig();
  display.setRotation(cfg.displayRotation);
  display.setTextColor(SSD1306_WHITE);
  display.cp437();
  if (cfg.valid) {
    scaleFactor = cfg.scaleFactor;
    tareOffset = cfg.tareOffset;
    goal = cfg.goal;
    tolerance = cfg.tolerance;
    Serial.println("Konfiguration geladen:");
    Serial.print("  AP SSID: ");
    Serial.println(cfg.apSSID);
    Serial.print("  Scale Factor: ");
    Serial.println(scaleFactor);
    Serial.print("  Tare Offset: ");
    Serial.println(tareOffset);
    Serial.print("  Goal: ");
    Serial.println(goal);
    Serial.print("  Tolerance: ");
    Serial.println(tolerance);
  } else {
    Serial.println("Keine Konfiguration - Config Mode aktiv");
    displayLines("Config Mode", "Waage-Config");
  }

  // Starte Web-Config im Hintergrund (immer, unabhängig von valid)
  startWebConfig();

  hx711.begin(HX711_DAT, HX711_CLK);

  if (abs(scaleFactor) > 1e-6) {
    hx711.set_scale(scaleFactor);
  }
  hx711.tare(10);
}

void stateIdle() {
  if (scaleMode == Standard) {
    // Avoid displaying signed zero (-0.0 or +0.0)
    float displayWeight = (fabs(weight) < zeroDisplayThreshold) ? 0.0 : weight;
    displayText(String(displayWeight, 1) + "g", false);
  } else {
    displayText("Durst auf " + String(goal, 2) + "g?", int(weight) != 0);
  }
  
  if (scaleMode == Game && weight >= goal) {
    delay(timeToSettle);
    updateWeight();
    full_weight = weight;
    state = Tare;
  }
}

void stateTare() {
  if (!rdyDisplayed) {
    displayText("Bereit?");
    delay(500);
    rdyDisplayed = true;
    displayText(getTrinkspruch());
  }
  if (weight > full_weight - tolerance) {
    return;
  }
  delay(timeToSettle);
  timeStarted = millis();
  updateWeight();
  empty_weight = weight;
  state = Drinking;
}

void stateDrinking() {
  static int i = 0;
  if (weight < empty_weight + tolerance) {
    displayLoadingAnimation(i++);
    return;
  }
  delay(timeToSettle);
  timeEnd = millis();
  updateWeight();
  final_weight = weight;
  state = Result;
}

void stateResult() {
  if (millis() - lastResultUpdate < resultUpdateInterval) {
    return;
  }
  String duration = String((timeEnd - timeStarted) / 1000.0, 2) + "s";
  int result = (full_weight - final_weight) * 100;
  int goal_int = int(goal * 100);
  String result_formatted;

  Serial.print("Result: ");
  Serial.print(result);
  Serial.print(" Goal: ");
  Serial.println(goal_int);

  if (displayMode == ShowTime) {
    result_formatted = duration;
  }else if (displayMode == ShowResult) {
    result_formatted = String((full_weight - final_weight), 2) + "g";
  }

  if (result == goal) {
    displayLines(result_formatted, "Perfekt!");
  } else if (abs(result - goal_int) <= 10) {
    // Display when within 0.1g
    displayLines(result_formatted, "Not Bad!");
  } else if (abs(result - goal_int) <= 100) {
    // Display when within 1g
    displayLines(result_formatted, "Ganz ok!");
  } else if (result / 100 < goal_int / 100) {
    displayLines(result_formatted, "schuchtern");
  } else if (result / 100 > goal_int / 100) {
    displayText(result_formatted, "Zu gierig!");
  } else {
    displayText(result_formatted);
  }
  lastResultUpdate = millis();
  if (displayMode == ShowResult) {
    displayMode = ShowTime;
  } else {
    displayMode = ShowResult;
  }
}

void handleButtonPress() {
  // Check if ISR detected a state change
  if (buttonStateChanged) {
    buttonStateChanged = false;
    int buttonState = digitalRead(BTN_PIN);
    
    if (buttonState == HIGH && buttonPressStartTime == 0) {
      // Button pressed - start timer
      buttonPressStartTime = millis();
      buttonHoldDetected = false;
    } else if (buttonState == LOW && buttonPressStartTime > 0) {
      // Button released
      unsigned long holdDuration = millis() - buttonPressStartTime;
      if (holdDuration < 3000) {
        // Short press - reset/tare (works in both modes)
        state = Idle;
        weight = full_weight = empty_weight = final_weight = 0.;
        rdyDisplayed = false;
        displayMode = ShowResult;
        hx711.tare(10);
      }
      buttonPressStartTime = 0;
      buttonHoldDetected = false;
    }
  }
  
  // Check for long hold (button still pressed)
  if (buttonPressStartTime > 0 && !buttonHoldDetected) {
    unsigned long holdDuration = millis() - buttonPressStartTime;
    if (holdDuration >= 3000) {
      // Held for 3 seconds - toggle mode
      buttonHoldDetected = true;
      scaleMode = (scaleMode == Game) ? Standard : Game;
      
      // Reset state when switching mode
      state = Idle;
      weight = full_weight = empty_weight = final_weight = 0.;
      rdyDisplayed = false;
      displayMode = ShowResult;
      hx711.tare(10);
      
      // Show mode change feedback
      displayText(scaleMode == Game ? "Game Mode" : "Standard Mode");
      delay(1000);
    }
  }
}

void loop() {
  handleButtonPress();
  
  if (scaleMode == Game) {
    switch (state) {
      case Idle:
        stateIdle();
        break;
      case Tare:
        stateTare();
        break;
      case Drinking:
        stateDrinking();
        break;
      case Result:
        stateResult();
        break;
    }
  } else {
    // Standard mode - always show current weight
    stateIdle();
  }
  
  updateWeight();
}
