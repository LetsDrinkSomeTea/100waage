#pragma once
#include <Arduino.h>
#include "duell_core.h"
#include "game_core.h"

// Duell-Modus: duenne ESP-NOW-Huelle um duell::Core (Protokoll siehe duell_core.h).
// Alle Waagen funken fest auf diesem Kanal (auch der Config-AP).
constexpr uint8_t DUELL_CHANNEL = 1;

// Funk muss bereits laufen (WIFI_STA oder WIFI_AP_STA auf DUELL_CHANNEL).
// Beide Aufrufe sind idempotent.
void duell_init();
void duell_deinit();  // verlaesst eine laufende Runde und sendet das vorher sofort

void duell_update(float local_goal);

int duell_get_peers_count();
bool duell_is_active();
void duell_ready_count(int *ready, int *total);

// Ereignisse aus der State-Machine
void duell_set_ready();
void duell_submit_result(float drank_weight, unsigned long duration_ms);
void duell_leave();  // Runde verlassen / zurueck auf Idle (ohne Ergebnis = aufgegeben)
void duell_flush_burst();  // aktuellen Zustand sofort mehrfach senden (vor Funk-aus, Moduswechsel)

bool duell_has_start_signal(float *out_target_weight);
duell::View duell_get_view();
bool duell_busy();  // Runde oder Nachlauf aktiv (Debug; Sleep/Busy sperrt game::Game::ownRoundOpen)
void duell_discard_rx();  // Empfangene Pakete verwerfen (Standard-Modus: Kern wird nicht getickt)

String duell_status_json();

// Adapter fuer game::Game
game::DuelPort &duell_port();
