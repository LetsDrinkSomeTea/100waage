#pragma once
#include "glass_core.h"

// Standardglaeser der Firmware (erzeugt von der Weboberflaeche,
// "Als Firmware-Liste exportieren"). IDs nie wiederverwenden.
// Felder: ID, Name, Leergewicht [g], Nennfuellung [g].
//
// TODO: Leergewichte sind Platzhalter, bis die echten Glaeser gewogen sind
// (je 3-5 Stueck, Mittelwert).

namespace glass {

constexpr Glass DEFAULTS[] = {
    {1, "Tulpe 0,3", 270.0f, 300.0f},
    {2, "Krug 0,4", 520.0f, 400.0f},
    {3, "Euro 0,5", 370.0f, 500.0f},
    {4, "Euro 0,33", 260.0f, 330.0f},
};

constexpr int DEFAULT_COUNT = (int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]));
constexpr uint16_t DEFAULT_NEXT_ID = 5; // erste freie ID

} // namespace glass
