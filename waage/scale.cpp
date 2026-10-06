#include "scale.h"
#include <Arduino.h>
#include <HX711.h>
#include <driver/gpio.h>

constexpr int HX711_DAT = 21;
constexpr int HX711_CLK = 20;
constexpr uint32_t NO_SAMPLE_MS = 1000;  // so lange ohne Sample = Sensorfehler
constexpr uint32_t STALL_MS = 500;       // laengere Loop-Pause (OTA, AP-Start) zaehlt nicht

static HX711 hx711;
static scale::Core core;
static bool ok = false;
static bool everOk = false;
static bool recovered = false;
static uint32_t lastSample = 0;
static uint32_t lastPoll = 0;

void scale_begin(float factor) {
  // CLK wurde vor dem Deep-Sleep auf HIGH gehalten (HX711 aus): Halten loesen.
  // Vorher HIGH treiben, damit beim Loesen kein Glitch entsteht.
  pinMode(HX711_CLK, OUTPUT);
  digitalWrite(HX711_CLK, HIGH);
  gpio_hold_dis((gpio_num_t)HX711_CLK);
  gpio_deep_sleep_hold_dis();

  // Ohne Reset: der Reset der Library liest blockierend und haengt ohne Sensor.
  hx711.begin(HX711_DAT, HX711_CLK, false, false);
  hx711.power_up();  // CLK LOW → HX711 wacht auf
  core.begin(factor, 0.0f);
  lastSample = lastPoll = millis();
}

void scale_poll(uint32_t now) {
  // Lange Loop-Pausen (blockierende Aktionen) nicht als Sensorausfall werten
  if ((uint32_t)(now - lastPoll) > STALL_MS) lastSample = now;
  lastPoll = now;

  if (hx711.is_ready()) {
    float raw = hx711.read();  // bereit: liest sofort
    core.addSample(raw, now);
    lastSample = now;
    if (!ok) {
      ok = true;
      if (everOk) recovered = true;
      everOk = true;
    }
  } else if (ok && (uint32_t)(now - lastSample) > NO_SAMPLE_MS) {
    ok = false;
    core.clear();
  }
}

bool scale_ok() {
  return ok;
}

bool scale_takeRecovered() {
  bool r = recovered;
  recovered = false;
  return r;
}

scale::Core &scale_core() {
  return core;
}

void scale_powerDown() {
  hx711.power_down();
  gpio_hold_en((gpio_num_t)HX711_CLK);
  gpio_deep_sleep_hold_en();
}
