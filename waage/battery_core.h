#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Akku (rein, ohne Arduino) ─────────────────────────────────────────────────
// Prozent aus der Zellspannung ueber eine Li-Ion-Ruhespannungskurve, Glaettung
// (EMA) und Hysterese gegen den angezeigten Wert. Keine Abschaltung: die
// Hardware schuetzt den Akku selbst.

namespace batt {

constexpr float EMPTY_V = 3.35f;   // 0 %
constexpr float FULL_V = 4.20f;    // 100 %
constexpr float EMA_ALPHA = 0.2f;
constexpr int DOWN_STEP = 2;       // Anzeige faellt, wenn 2 Messungen in Folge >= 2 Punkte tiefer
constexpr int UP_STEP = 5;         // Anzeige steigt erst ab 5 Punkten (z. B. Laden)
constexpr int LOW_ON = 10;         // Warnung unter 10 %
constexpr int LOW_OFF = 13;        // Warnung aus ab 13 %

// OCV-Tabelle mit linearer Interpolation, Ergebnis 0..100, monoton steigend.
// Stuetzpunkte: 4,20→100, 4,10→90, 4,00→79, 3,92→70, 3,87→60, 3,82→50,
// 3,79→40, 3,75→30, 3,70→20, 3,62→10, 3,50→5, 3,35→0.
float percentFromVoltage(float volts);

// Mittel ohne die obersten und untersten n/8 Werte (sortiert mv in place).
float trimmedMean(uint16_t *mv, size_t n);

// Teiler aus gleichzeitig gemessener Zellspannung (Multimeter) und Pin-Spannung.
// Gueltig: measuredV 2,5..4,5 V, pinMv 300..2500 mV, Ergebnis 1..6.
// Bei Fehler false und eine deutsche Meldung in *err.
bool calibrateRatio(float measuredV, float pinMv, float *ratio, const char **err);

class Gauge {
public:
  void reset();                  // naechste Messung setzt Start- und Anzeigewert
  void update(float cellVolts);  // eine Messung (z. B. alle 5 s)
  bool valid() const { return valid_; }
  float voltage() const { return ema_; }  // geglaettete Zellspannung
  int percent() const { return shown_; }  // angezeigter Wert (mit Hysterese)
  bool low() const { return low_; }

private:
  bool valid_ = false, low_ = false;
  float ema_ = 0.0f;
  int shown_ = 0;
  int downCount_ = 0;
};

}  // namespace batt
