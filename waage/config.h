#pragma once
#include "types.h"

// Steht die SSID auf diesem Default, haengt der AP zur Laufzeit die letzten
// MAC-Bytes an (100-Waage-XXXX), damit mehrere Waagen unterscheidbar sind.
constexpr char DEFAULT_AP_SSID[] = "100-Waage-Config";

WaageConfig defaultConfig();
bool        loadConfig(WaageConfig& cfg);
void        saveConfig(const WaageConfig& cfg);
void        clearConfig();
