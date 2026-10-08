#pragma once
#include "config_core.h"
#include "glass_core.h"
#include "web_core.h"
#include <stdint.h>

// ── Anwendung: verbindet Waage, Spiellogik, Taster, Funk, Akku und Anzeige ────

void app_setup();
void app_loop();

// ── Fassade fuer den Webserver ────────────────────────────────────────────────

// Spiel laeuft, eigene Duell-Runde offen, Kalibrierung oder OTA aktiv
bool app_isBusy();

enum class Apply : uint8_t {
  Ok,
  Invalid, // Validierung fehlgeschlagen (error gesetzt)
  Busy
}; // Moduswechsel waehrend laufendem Spiel

struct ApplyResult {
  Apply status;
  cfg::Error error;
  uint32_t changes; // cfg::Change-Maske
  bool appliedNow;  // Ziel/Zufall sofort wirksam (sonst ab naechster Runde)
};

// Einziger Weg, die Config zu aendern (Web, Taster, Kalibrierung).
// fromWeb: strikte Validierung, Moduswechsel waehrend busy wird abgelehnt.
ApplyResult app_applyConfig(cfg::Config next, bool fromWeb);

void app_writeStatus(web::JsonWriter &j);

// Kalibrierung (zweistufig)
enum class CalStart : uint8_t { Ok, Busy, SensorError };
CalStart app_calStart();
bool app_calMeasure(float knownG);
void app_calCancel();
void app_writeCal(web::JsonWriter &j);

// Akku-Teiler abgleichen (measuredV) oder auf Standard setzen. HTTP-Status.
int app_battCal(float measuredV, bool resetDefault, const char **err);

// Glaeser (Liste, Festlegen, Bearbeiten, Export)
void app_writeGlasses(web::JsonWriter &j);
bool app_glassSelect(uint32_t id); // 0 = automatisch; false: unbekannt
// id 0 = neues Glas
glass::Error app_glassSave(uint32_t id, const char *name, float emptyG,
                           float nominalG);
bool app_glassDelete(uint32_t id);
bool app_glassRestore(uint32_t id, bool all);
size_t app_glassExport(char *out, size_t cap); // 0 = Puffer zu klein

// OTA-Update
void app_otaBegin();
void app_otaProgress(int percent);
void app_otaEnd(bool ok);
