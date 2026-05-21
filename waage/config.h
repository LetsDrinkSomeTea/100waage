#pragma once
#include "types.h"

WaageConfig defaultConfig();
bool        loadConfig(WaageConfig& cfg);
void        saveConfig(const WaageConfig& cfg);
void        clearConfig();
