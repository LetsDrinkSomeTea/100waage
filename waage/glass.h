#pragma once
#include "glass_core.h"

// ── Glaeser speichern (NVS) und Gedaechtnis ueber den Deep-Sleep (RTC) ────────
// Die Liste = Standardglaeser der Firmware (glasses_default.h) + Abweichungen
// im NVS (Namespace "glass"). Das Gedaechtnis der Glasbestimmung und die
// Leer-Referenz der Waage liegen im RTC-Speicher: sie ueberleben den
// Deep-Sleep, nicht aber einen Neustart.

void glass_begin();
glass::List &glass_list();
bool glass_save(); // Abweichungen ins NVS

struct GlassRtc {
  glass::Memory mem;
  bool emptyKnown;
  float emptyOffset; // Leer-Referenz der Waage (Zaehlschritte)
};
// false, wenn nicht aus dem Deep-Sleep aufgewacht oder nichts gespeichert
bool glass_rtcRestore(GlassRtc *out);
void glass_rtcStore(const GlassRtc &s);
