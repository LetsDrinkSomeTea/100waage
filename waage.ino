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

HX711 hx711;
#define BTN_PIN 0

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

float weight = 0.0;
float full_weight = 0.0;
float empty_weight = 0.0;
float final_weight = 0.0;
unsigned long lastResultUpdate = millis();
#define resultUpdateInterval 3000
boolean rdyDisplayed = false;

void IRAM_ATTR handleInterrupt() {
  // Interrupt-Service-Routine (ISR) placeholder
  state = Idle;
  weight = full_weight = empty_weight = final_weight = 0.;
  rdyDisplayed = false;
  displayMode = ShowResult;
  hx711.tare(10);
}

void updateWeight() {
  weight = hx711.get_units(10);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  pinMode(BTN_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), handleInterrupt, HIGH);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  display.setTextColor(SSD1306_WHITE);
  display.cp437();

  // Lade Konfiguration aus EEPROM
  WaageConfig cfg = getConfig();
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
  displayText("Durst auf " + String(goal, 2) + "g?", int(weight) != 0);
  if (weight < goal) {
    return;
  }
  delay(timeToSettle);
  updateWeight();
  full_weight = weight;
  state = Tare;
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

void loop() {
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
  updateWeight();
}
