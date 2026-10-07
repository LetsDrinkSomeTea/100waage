#pragma once
#include "scale_core.h"
#include <stdint.h>

// ── HX711-Anbindung ───────────────────────────────────────────────────────────
// Liest nicht blockierend (nur wenn ein Sample bereit ist) und reicht die
// Rohwerte an scale::Core weiter. Ein Waechter meldet einen Sensorfehler, wenn
// laenger keine Samples kommen (z. B. Kabel lose), statt ewig zu haengen.

void scale_begin(float factor);
void scale_poll(uint32_t now);
bool scale_ok();            // Samples kommen an
bool scale_takeRecovered(); // einmal true nach Rueckkehr aus Sensorfehler
scale::Core &scale_core();
void scale_powerDown(); // vor dem Deep-Sleep: HX711 aus, CLK-Pad gehalten
