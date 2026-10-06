#pragma once
#include <Arduino.h>
#include "duell_core.h"

// Duell-Modus: duenne ESP-NOW-Huelle um duell::Core (Protokoll siehe duell_core.h).
// Alle Waagen funken fest auf diesem Kanal (auch der Config-AP).
constexpr uint8_t DUELL_CHANNEL = 1;

// Funk muss bereits laufen (WIFI_STA oder WIFI_AP_STA auf DUELL_CHANNEL).
// Beide Aufrufe sind idempotent.
void duell_init();
void duell_deinit();

void duell_update(float local_goal);

int duell_get_peers_count();
bool duell_is_active();
void duell_ready_count(int *ready, int *total);

// Ereignisse aus der State-Machine
void duell_set_ready();
void duell_submit_result(float drank_weight, unsigned long duration_ms);
void duell_leave();  // Runde verlassen / zurueck auf Idle (ohne Ergebnis = aufgegeben)

bool duell_has_start_signal(float *out_target_weight);
duell::View duell_get_view();
bool duell_busy();  // Runde oder Nachlauf aktiv → kein Deep-Sleep, keine Kalibrierung

String duell_status_json();
