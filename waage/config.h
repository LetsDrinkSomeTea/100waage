#pragma once
#include "config_core.h"

// ── Konfiguration speichern (NVS) ─────────────────────────────────────────────
// Eine einzige Instanz; Lesen ueber config_get(), Aendern nur ueber
// config_set() (plausibilisiert und schreibt nur geaenderte Schluessel).
// Beim ersten Start wird das alte EEPROM-Abbild einmalig importiert, damit
// Kalibrierung und Einstellungen erhalten bleiben.

void config_begin();
const cfg::Config &config_get();

// Uebernimmt next (vorher sanitize). Liefert die Aenderungsmaske (cfg::Change).
uint32_t config_set(const cfg::Config &next);

// Werkseinstellungen (schreibt Defaults und das Schema; kein erneuter Import).
void config_factoryReset();

// Gemerkter Zustand fuer "Klassik zurueck" (eigene Schluessel im selben
// Namespace, ueberlebt Deep-Sleep und Neustart). Schreibt nur bei Aenderung;
// ein ungueltiger Zustand loescht nur das Gueltig-Flag.
const cfg::ClassicBackup &config_classicBackup();
void config_setClassicBackup(const cfg::ClassicBackup &b);
