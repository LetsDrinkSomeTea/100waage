#pragma once
#include <stdint.h>

// ── Statistik (rein, ohne Arduino) ────────────────────────────────────────────
// Zaehlt die Runden einer Waage und merkt sich Bestwerte. Die Summen werden in
// stats.cpp im NVS gespeichert, der Verlauf liegt nur im RAM.
//
// Regeln (siehe Specs.md):
//  - Jede fertige Runde zaehlt (solo oder Duell), Abweichung d = getrunken -
//    Ziel in Centigramm.
//  - Stufen getrennt: |d| = 0 Perfekt, <= 10 Not Bad, <= 100 Ganz ok.
//  - Bestwert: kleinstes |d| ueber alle Ziele (strikt kleiner ersetzt).
//  - Schnellste Zeit: nur Runden mit |d| <= FAST_PCT % des Ziels (strikt
//    schneller ersetzt).
//  - Erfolg einer Runde: Rekord vor schnellster Zeit, hoechstens einer.
//  - Duell: final mit mindestens 2 Teilnehmern zaehlt als Duell, Rang 1 ohne
//    Aufgabe als Sieg.

namespace stats {

constexpr int RECENT = 10; // Verlauf im RAM
constexpr int32_t NOT_BAD_CG = 10;
constexpr int32_t OK_CG = 100;
constexpr int32_t FAST_PCT = 10; // max. Abweichung fuer die schnellste Zeit

struct Totals {
  uint32_t rounds;
  uint32_t perfect, notBad, ok; // getrennte Stufen
  bool hasBest;
  int32_t bestDevCg; // Betrag
  int32_t bestGoalCg;
  uint32_t bestMs;
  bool hasFastest;
  uint32_t fastestMs;
  int32_t fastestGoalCg;
  int32_t fastestDevCg; // mit Vorzeichen
  uint32_t duels, wins;
};

struct Round {
  int32_t drankCg, goalCg;
  uint32_t durationMs;
  bool duel;
};

struct Entry {
  int32_t devCg; // mit Vorzeichen
  int32_t goalCg;
  uint32_t durationMs;
  bool duel;
  uint8_t rank; // Duell: finaler Rang, 0 = (noch) keiner
};

enum class Achievement : uint8_t { None, Record, Fastest };

class Tracker {
public:
  void load(const Totals &t); // z. B. aus dem NVS; Verlauf bleibt leer
  void reset();               // alles auf null, auch der Verlauf
  const Totals &totals() const { return t_; }

  Achievement record(const Round &r);
  // Duell-Runde final: Teilnahme/Sieg zaehlen, Rang im neuesten Eintrag.
  void duelFinal(uint8_t rank, uint8_t total, bool forfeit);

  int recentCount() const { return count_; }
  const Entry &recent(int i) const; // 0 = neueste

private:
  Totals t_ = {};
  Entry recent_[RECENT] = {};
  int head_ = 0, count_ = 0; // head_ = naechster Schreibplatz
};

} // namespace stats
