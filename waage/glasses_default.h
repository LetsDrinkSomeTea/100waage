#pragma once
#include "glass_core.h"

// Standardglaeser der Firmware (erzeugt von der Weboberflaeche,
// "Als Firmware-Liste exportieren"). IDs nie wiederverwenden.
// Felder: ID, Name, Leergewicht [g], Nennfuellung [g].

namespace glass {

constexpr Glass DEFAULTS[] = {
    {1, "Gläsle", 236.5f, 300.0f},
    {2, "Halbe", 800.0f, 400.0f},
    {3, "Krügle", 576.0f, 500.0f},
};

constexpr int DEFAULT_COUNT = (int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]));
constexpr uint16_t DEFAULT_NEXT_ID = 6; // erste freie ID

} // namespace glass
