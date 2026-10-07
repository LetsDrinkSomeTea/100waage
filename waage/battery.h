#pragma once
#include "battery_core.h"
#include <stdint.h>

// ── Akku-Messung (ADC an GPIO 0 ueber Spannungsteiler) ────────────────────────
// Kalibrierte Pin-Spannung per analogReadMilliVolts() (eFuse-Kalibrierung des
// ESP32-C3), gemittelt; daraus Zellspannung und Prozent (battery_core).

// present = false (Waage ohne Akku, Config batteryPresent): keine Messung,
// battery_gauge() bleibt ungueltig.
void battery_begin(bool present, float dividerRatio);
void battery_poll(uint32_t now);
// Nach Aenderung von batteryPresent oder Teiler: neu einschwingen
void battery_configure(bool present, float dividerRatio);
bool battery_present();
void battery_noteRadioToggle(
    uint32_t now);                // Messung kurz aussetzen (Lastsprung)
float battery_pinMv(int samples); // gemittelte Pin-Spannung [mV]
const batt::Gauge &battery_gauge();
