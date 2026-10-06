#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Waage (rein, ohne Arduino) ────────────────────────────────────────────────
// Verarbeitet HX711-Rohwerte (Zaehlschritte) mit Zeitstempel. Alle Zeiten sind
// in ms angegeben, damit ein auf 80 SPS umgeloetetes Modul automatisch
// schneller reagiert. Gramm = (raw - offset) / factor, wie die HX711-Library.

namespace scale {

constexpr int BUF_MAX = 96;                   // Samples im Ringpuffer (>= 1 s bei 80 SPS)
constexpr uint32_t STABLE_MS = 500;           // Fenster fuer die Stabilitaetspruefung
constexpr uint32_t DISPLAY_MS = 1000;         // max. Glaettung der Anzeige
constexpr float STEP_G = 2.0f;                // Sprung > STEP_G startet die Glaettung neu
constexpr uint32_t TARE_DISCARD_MS = 100;     // nach Tara-Start verworfene Zeit
constexpr int TARE_MIN_SAMPLES = 5;           // stabile Samples fuer eine Tara
constexpr uint32_t TARE_MAX_MS = 2000;        // danach Tara mit dem, was da ist
constexpr float STABLE_SPREAD_MIN_G = 1.0f;   // stableSpread = max(1 g, Toleranz / 5)

struct Reading {
  bool valid;       // mindestens ein Sample und keine Tara aktiv
  float grams;      // adaptiv geglaettet: Anzeige und Schwellen
  float spread;     // max - min [g] der Samples der letzten STABLE_MS (ueber
                    // gleitende ~100-ms-Mittel: 10 SPS Einzelsamples, 80 SPS je 8)
  bool stable;      // STABLE_MS abgedeckt und spread <= stableSpread
  float sps;        // gemessene Samples pro Sekunde (0 = unbekannt)
};

class Core {
public:
  // factor = Zaehlschritte pro Gramm (|factor| >= 1), offset in Zaehlschritten.
  // Ungueltige Werte (nicht endlich, |factor| < 1) werden ignoriert.
  // begin() leert den Puffer; begin() und setOffset() brechen eine laufende
  // Tara ab (sonst wuerde sie den gesetzten Offset spaeter ueberschreiben).
  void begin(float factor, float offset = 0.0f);
  void setFactor(float factor);
  void setOffset(float offset);
  float factor() const { return factor_; }
  float offset() const { return offset_; }
  void setStableSpread(float grams);  // Default max(1 g, 10 g / 5); min. STABLE_SPREAD_MIN_G
  float stableSpread() const { return stableSpread_; }

  // Neues Rohsample. Waehrend einer Tara landen Samples im Tara-Puffer.
  void addSample(float raw, uint32_t now);
  Reading reading(uint32_t now) const;

  // Nicht blockierende Tara: TARE_DISCARD_MS verwerfen, dann bei
  // TARE_MIN_SAMPLES stabilen Samples (Spanne <= stableSpread) Offset = deren
  // Mittel; nach TARE_MAX_MS mit dem Mittel aller gesammelten Samples (noch
  // keine Samples, z. B. Loop blockiert: die Frist beginnt mit dem ersten
  // spaeteren Sample neu, der Offset bleibt bis dahin). Die Tara-Samples
  // bleiben danach im Puffer, die Anzeige steht also sofort stabil auf 0.
  void startTare(uint32_t now);
  bool taring() const { return taring_; }

  // Sofortige Nullung aus den bereits geprueften Samples: nur wenn die
  // letzten STABLE_MS abgedeckt sind, |Mittel| <= maxAbsG und Spanne <=
  // maxSpreadG. Verschiebt den Offset um das Mittel; liefert ob genullt wurde.
  bool zeroFromWindow(float maxAbsG, float maxSpreadG, uint32_t now);

  // Rohes Mittel/Spanne der letzten STABLE_MS (fuer die Kalibrierung).
  // Spanne wie Reading::spread, nur in Zaehlschritten; true = abgedeckt.
  bool rawWindow(uint32_t now, float *mean, float *spread, int *n) const;

  // Puffer leeren (z. B. nach Sensorfehler). Eine laufende Tara bleibt aktiv
  // und sammelt neu.
  void clear();

private:
  struct Sample {
    uint32_t t;
    float raw;
  };
  struct Window;  // Statistik der letzten STABLE_MS (scale_core.cpp)

  const Sample &at(int i) const;  // i = 0: neuestes Sample
  void push(float raw, uint32_t t);
  float filterMean(uint32_t ref) const;
  void window(uint32_t now, Window *w) const;
  float meanInterval() const;
  void finishTare(int k);

  // Ein Ringpuffer fuer alles: waehrend der Tara landen nur die verwertbaren
  // Samples darin (die neuesten tareCount_), beim Abschluss bleiben genau die
  // fuer den Offset benutzten Samples uebrig.
  Sample buf_[BUF_MAX] = {};
  int head_ = 0, count_ = 0;  // naechster Schreibplatz, Anzahl
  int filterN_ = 0;           // neueste Samples seit letztem Neustart der Glaettung
  float factor_ = 708.0f, offset_ = 0.0f, stableSpread_ = 2.0f;
  bool taring_ = false;
  uint32_t tareStart_ = 0;
  int tareCount_ = 0;
};

// ── Kalibrierung in zwei Schritten ────────────────────────────────────────────
// Prepare ("Waage leeren", PREPARE_MS) → Taring → WaitWeight (Nutzer legt das
// bekannte Gewicht auf und startet die Messung) → Measuring (stabil abwarten)
// → Done (neuer Faktor gesetzt) → RemoveWeight (bis |w| < removeTolG fuer
// REMOVE_MS) → Off. Fehler stellen alten Offset und Faktor wieder her.
// Done ist genau bis zum naechsten update() sichtbar. CAL_TIMEOUT_MS gilt auch
// fuer Taring (Sensor tot). Ungueltiges Gewicht in WaitWeight: measure() false,
// error() = BadWeight, Zustand bleibt (neuer Versuch moeglich). cancel() in
// Done/RemoveWeight/Error → Off ohne Wiederherstellen (Faktor bleibt).

enum class CalState : uint8_t { Off,
                                Prepare,
                                Taring,
                                WaitWeight,
                                Measuring,
                                Done,
                                RemoveWeight,
                                Error };

enum class CalError : uint8_t { None,
                                BadWeight,  // bekanntes Gewicht ungueltig
                                NoWeight,   // kein Gewicht erkannt (|Delta| zu klein)
                                BadFactor,  // Faktor nicht endlich oder |f| < 1
                                Timeout,
                                Cancelled };

constexpr uint32_t CAL_PREPARE_MS = 2000;
constexpr uint32_t CAL_TIMEOUT_MS = 120000;     // WaitWeight/Measuring
constexpr uint32_t CAL_MEASURE_MAX_MS = 5000;   // stabil werden bis dahin, sonst Mittel
constexpr float CAL_MIN_DELTA_COUNTS = 1000.0f;
constexpr float CAL_WEIGHT_MIN = 0.5f, CAL_WEIGHT_MAX = 5000.0f;
constexpr uint32_t CAL_REMOVE_MS = 1000;

class Calibrator {
public:
  void start(Core &s, uint32_t now);              // merkt alten Offset/Faktor
  bool measure(float knownG, uint32_t now);       // false: Gewicht ungueltig oder falscher Schritt
  void cancel(Core &s);                           // stellt alten Zustand her
  void update(Core &s, uint32_t now, float removeTolG);
  void acknowledge();                             // Error → Off

  CalState state() const { return state_; }
  CalError error() const { return error_; }
  bool active() const { return state_ != CalState::Off; }
  float oldFactor() const { return oldFactor_; }
  float newFactor() const { return newFactor_; }  // gueltig ab Done
  bool takeNewFactor(float *f);                   // einmalig true nach Erfolg (zum Speichern)
  float liveDeltaCounts(const Core &s, uint32_t now) const;  // aktuelles Mittel - Offset

private:
  void restore(Core &s) const;
  void fail(Core &s, CalError e);
  void finish(Core &s, float delta);

  CalState state_ = CalState::Off;
  CalError error_ = CalError::None;
  uint32_t since_ = 0;
  float knownG_ = 0.0f;
  float oldFactor_ = 0.0f, oldOffset_ = 0.0f, newFactor_ = 0.0f;
  bool newFactorPending_ = false;
  uint32_t removeSince_ = 0;
  bool removeTiming_ = false;
};

}  // namespace scale
