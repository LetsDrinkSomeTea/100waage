#include "battery.h"
#include <Arduino.h>

constexpr int PIN_BATT = 0;
constexpr uint32_t READ_INTERVAL_MS = 5000;
constexpr uint32_t TOGGLE_SETTLE_MS = 1000;
constexpr int SAMPLES = 16;
constexpr int MAX_SAMPLES = 64;

static batt::Gauge gauge;
static float ratio = 2.0f;
static bool present = false, adcReady = false;
static uint32_t lastRead = 0, lastToggle = 0;
static bool toggled = false;

float battery_pinMv(int samples) {
  if (samples > MAX_SAMPLES)
    samples = MAX_SAMPLES;
  if (samples < 1)
    samples = 1;
  uint16_t mv[MAX_SAMPLES];
  for (int i = 0; i < samples; i++)
    mv[i] = (uint16_t)analogReadMilliVolts(PIN_BATT);
  return batt::trimmedMean(mv, (size_t)samples);
}

static void measure() {
  gauge.update(battery_pinMv(SAMPLES) * ratio / 1000.0f);
}

void battery_configure(bool isPresent, float dividerRatio) {
  ratio = dividerRatio;
  present = isPresent;
  gauge.reset();
  if (!present)
    return;
  if (!adcReady) {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // Messbereich bis ca. 2,5 V am Pin
    pinMode(PIN_BATT, INPUT);
    adcReady = true;
  }
  measure(); // erster Wert setzt Start- und Anzeigewert
  lastRead = millis();
}

void battery_begin(bool isPresent, float dividerRatio) {
  battery_configure(isPresent, dividerRatio);
}

bool battery_present() { return present; }

void battery_poll(uint32_t now) {
  if (!present)
    return;
  if (toggled && (uint32_t)(now - lastToggle) < TOGGLE_SETTLE_MS)
    return;
  toggled = false;
  if ((uint32_t)(now - lastRead) < READ_INTERVAL_MS)
    return;
  lastRead = now;
  measure();
}

void battery_noteRadioToggle(uint32_t now) {
  toggled = true;
  lastToggle = now;
}

const batt::Gauge &battery_gauge() { return gauge; }
