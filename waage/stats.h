#pragma once
#include "game_core.h"
#include "stats_core.h"
#include "web_core.h"

// ── Statistik der Waage ───────────────────────────────────────────────────────
// Summen und Bestwerte im NVS (Namespace "stats"), Verlauf nur im RAM.

void stats_begin();
const stats::Tracker &stats_tracker();
stats::Achievement stats_record(const game::RoundDone &r); // speichert
void stats_duelFinal(const game::DuelFinal &f);            // speichert
void stats_reset();                                        // alles auf null
void stats_writeJson(web::JsonWriter &j);
