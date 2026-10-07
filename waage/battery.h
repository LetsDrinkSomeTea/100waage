#pragma once
#include "battery_core.h"
#include <stdint.h>

// ── Akku-Messung (ADC an GPIO 0 ueber Spannungsteiler) ────────────────────────
// Kalibrierte Pin-Spannung per analogReadMilliVolts() (eFuse-Kalibrierung des
// ESP32-C3), gemittelt; daraus Zellspannung und Prozent (battery_core).

constexpr bool BATTERY_CONNECTED = true;

void battery_begin(float dividerRatio);
void battery_poll(uint32_t now);
void battery_setRatio(
    float dividerRatio); // nach Kalibrierung: neu einschwingen
void battery_noteRadioToggle(
    uint32_t now);                // Messung kurz aussetzen (Lastsprung)
float battery_pinMv(int samples); // gemittelte Pin-Spannung [mV]
const batt::Gauge &battery_gauge();
