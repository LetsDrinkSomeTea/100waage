#pragma once
#include <stdint.h>
#include "config_core.h"
#include "duell_core.h"

// ── Spiellogik (rein, ohne Arduino) ───────────────────────────────────────────
// Kompletter Solo- und Duell-Ablauf. Zeit, Gewicht, Zufall und die Duell-
// Anbindung (DuelPort) werden hereingereicht; Ausgabe sind eine View fuer die
// Anzeige und Tara-Anforderungen an die Waage.
//
// Phasen: Idle → Ready (Glas steht) → Drinking (Glas abgehoben) → Result.
// Duell-Unterzustand: Offline | WaitReady | WaitStart | Live.
//
// Regeln (siehe Specs.md):
//  - Jeder Reset tariert (Kurzdruck, Moduswechsel, Boot). Es gibt keinen Reset
//    ohne Tara.
//  - Idle → Ready: Gewicht >= Ziel und stabil fuer PLACE_STABLE_MS; fullWeight
//    = dieses Gewicht. Mit Funk und sichtbaren Gegnern: WaitReady + setReady().
//  - Ready → Drinking: Gewicht <= full - tol fuer LIFT_MS; timeStarted = erstes
//    Unterschreiten; emptyWeight = Gewicht; waehrend Drinking wird emptyWeight
//    nachgefuehrt, solange das Glas weg (w < empty + tol) und stabil ist.
//  - Drinking → Result: Gewicht >= empty + tol (timeEnd = erstes Ueberschreiten),
//    dann stabil fuer RETURN_STABLE_MS oder spaetestens RETURN_MAX_MS nach dem
//    Ueberschreiten; finalWeight = Gewicht; getrunken in Centigramm
//    (lroundf(g * 100)), Bewertung gegen das Ziel (Duell: Duell-Ziel).
//  - Bewertung: d = drankCg - goalCg; 0 Perfekt, |d| <= 10 NotBad,
//    |d| <= 100 Ok, sonst Shy (d < 0) bzw. Greedy. Gut = goalCg > 0 und
//    |d| * 100 <= autoResetRange * goalCg.
//  - Result: gut bleibt bis Kurzdruck (Prahlen). Schlecht: Glas weg
//    (w < empty + tol fuer REMOVED_MS) → Reset mit TareEmpty. Im Duell erst,
//    wenn der Rang final ist und mindestens FINAL_MIN_SHOW_MS angezeigt wurde;
//    Forfeit ("Zu spät!") zaehlt als schlecht.
//  - WaitReady: Startsignal → WaitStart (Ziel = Duell-Ziel). Gegner weg oder
//    WAITREADY_TIMEOUT_MS → Offline, leave(), soloFallbackSeq++. Glas fuer
//    LIFT_MS abgehoben → Offline, leave(), soloFallbackSeq++, direkt Drinking
//    mit timeStarted = erstes Unterschreiten. Kuerzeres Anheben aendert nichts.
//  - WaitStart, Runde verschwunden (Funk aus): Offline, solo gegen Duell-Ziel.
//  - Live: Rang aus duel.view(); verschwindet die Runde nach dem Final (Funk
//    aus), bleibt die letzte finale Ansicht; vor dem Final → Offline, solo.
//  - Idle (beide Modi): Auto-Zero, wenn aktiviert, |w| < Schwelle und stabil
//    fuer autoZeroDelay s; danach Pause 3 x autoZeroDelay → ScaleReq::AutoZero.
//  - Idle (nur Game): NegZero, wenn w < -tol und stabil fuer NEGZERO_MS
//    (ein mit Glas tariertes Glas wurde abgehoben) → ScaleReq::NegZero.
//  - Standard-Modus: nur Idle mit Gewichtsanzeige, keine Duell-Aufrufe.
//  - Solange !weightValid (Tara laeuft, Sensorfehler) aendert sich nichts.

namespace game {

constexpr uint32_t PLACE_STABLE_MS = 500;
constexpr uint32_t LIFT_MS = 300;
constexpr uint32_t RETURN_STABLE_MS = 500;
constexpr uint32_t RETURN_MAX_MS = 1500;
constexpr uint32_t REMOVED_MS = 500;
constexpr uint32_t FINAL_MIN_SHOW_MS = 3000;
constexpr uint32_t WAITREADY_TIMEOUT_MS = 60000;
constexpr uint32_t NEGZERO_MS = 1000;

enum class Phase : uint8_t { Idle,
                             Ready,
                             Drinking,
                             Result };

enum class Duel : uint8_t { Offline,
                            WaitReady,
                            WaitStart,
                            Live };

enum class Rating : uint8_t { Perfect,  // "Perfekt!"
                              NotBad,   // "Not Bad!"
                              Ok,       // "Ganz ok!"
                              Shy,      // "Schüchtern"
                              Greedy };  // "Zu gierig!"

// Was die Waage tun soll (die App fuehrt es aus):
//  Tare      frische, nicht blockierende Tara
//  TareEmpty Waage ist leer: zeroFromWindow(tol, stableSpread), sonst Tare
//  AutoZero  zeroFromWindow(autoZeroThreshold, autoZeroThreshold)
//  NegZero   zeroFromWindow(unbegrenzt, stableSpread)
enum class ScaleReq : uint8_t { None,
                                Tare,
                                TareEmpty,
                                AutoZero,
                                NegZero };

// Anbindung ans Duell (Implementierung: duell.cpp bzw. Fakes im Test).
class DuelPort {
public:
  virtual bool active() = 0;  // andere Waagen sichtbar
  virtual void readyCount(int *ready, int *total) = 0;
  virtual void setReady() = 0;
  virtual bool startSignal(float *target) = 0;
  virtual duell::View view() = 0;
  virtual void submit(float grams, uint32_t durationMs) = 0;
  virtual void leave() = 0;

protected:
  ~DuelPort() = default;
};

struct Input {
  uint32_t now;
  bool weightValid;  // Waage ok, keine Tara, nicht in Kalibrierung
  float weight;      // [g], adaptiv geglaettet
  bool stable;
  bool radioOn;
};

enum class Screen : uint8_t { IdleGame,      // Ziel "100.0g?", Rahmen wenn Glas drauf
                              IdleStandard,  // aktuelles Gewicht
                              Taring,        // "Tara..."
                              WaitDuel,      // "Warte..." / "2/3 bereit"
                              Ready,         // "Bereit?" dann Trinkspruch
                              DuelStart,     // Duell-Ziel
                              Drinking,      // Ladeanimation
                              ResultSolo,
                              ResultDuel };

struct View {
  Screen screen;
  uint32_t screenSince;   // Beginn des aktuellen Bildschirms
  float weight;           // IdleStandard (0, wenn Auto-Zero aktiv und |w| < Schwelle)
  float goal;             // IdleGame: lokales Ziel; DuelStart: Duell-Ziel
  bool randomMode;
  bool glassOn;           // IdleGame: w > tol
  int ready, readyTotal;  // WaitDuel
  uint16_t toastIdx;      // Trinkspruch dieser Runde (Index modulo Anzahl)
  int32_t drankCg;        // Result
  uint32_t durationMs;    // Result
  Rating rating;          // ResultSolo
  uint8_t rank;           // ResultDuel (0 = noch kein Platz)
  uint8_t settled, total; // ResultDuel: fertige / alle Teilnehmer
  bool isFinal, forfeit;
  uint32_t resultSig;     // aendert sich, wenn sich die Duell-Anzeige aendert
  uint32_t soloFallbackSeq;  // erhoeht bei stillem Wechsel auf Solo
};

inline int32_t toCg(float g) {
  float x = g * 100.0f;
  return (int32_t)(x < 0 ? x - 0.5f : x + 0.5f);
}
Rating rate(int32_t drankCg, int32_t goalCg);
bool isGood(int32_t drankCg, int32_t goalCg, uint8_t autoResetRange);

class Game {
public:
  // rnd liefert eine Zufallszahl (Zufallsziel, Trinkspruch).
  void begin(DuelPort *duel, uint32_t (*rnd)(void *), void *rndCtx);

  // Einziger Reset (Entscheidung 1): Phase Idle, Duell verlassen, Ziel neu
  // (Zufallsmodus: neu gewuerfelt), Tara angefordert (Tare oder TareEmpty).
  void reset(const cfg::Config &c, uint32_t now, ScaleReq tare = ScaleReq::Tare);

  // Web-Aenderung von Ziel/Zufall: nur in Idle (Game) sofort wirksam, ohne
  // Tara; sonst ab dem naechsten Reset.
  void applyGoalSettings(const cfg::Config &c, uint32_t now);

  void update(const cfg::Config &c, const Input &in);

  ScaleReq takeScaleReq();  // liefert die offene Anforderung einmal
  const View &view() const { return view_; }

  Phase phase() const { return phase_; }
  Duel duel() const { return duel_; }
  float localGoal() const { return localGoal_; }
  bool gameRunning() const { return phase_ == Phase::Ready || phase_ == Phase::Drinking; }
  bool ownRoundOpen() const;  // Duell-Runde laeuft und ist noch nicht final

private:
  DuelPort *port_ = nullptr;
  uint32_t (*rnd_)(void *) = nullptr;
  void *rndCtx_ = nullptr;
  Phase phase_ = Phase::Idle;
  Duel duel_ = Duel::Offline;
  ScaleReq req_ = ScaleReq::None;
  View view_ = {};
  float localGoal_ = 0.0f, fullWeight_ = 0.0f, emptyWeight_ = 0.0f, finalWeight_ = 0.0f;
  float duelTarget_ = 0.0f;
  bool duelTargetSet_ = false;
  uint32_t timeStarted_ = 0, timeEnd_ = 0;
  duell::View cachedView_ = {};
  bool haveCached_ = false;
  uint32_t soloSeq_ = 0;
  struct Timer {
    bool on = false;
    uint32_t since = 0;
    void start(uint32_t now) {
      if (!on) {
        on = true;
        since = now;
      }
    }
    void stop() { on = false; }
    bool held(uint32_t now, uint32_t ms) const { return on && (uint32_t)(now - since) >= ms; }
  };
  void setScreen(Screen s, uint32_t now);
  float refGoal() const { return duelTargetSet_ ? duelTarget_ : localGoal_; }
  void stopTimers();
  void updateStandard(const cfg::Config &c, const Input &in);
  void updateAutoZero(const cfg::Config &c, const Input &in);
  void updateIdle(const cfg::Config &c, const Input &in);
  void updateReady(const cfg::Config &c, const Input &in);
  void startDrinking(const Input &in, uint32_t since);
  void updateDrinking(const cfg::Config &c, const Input &in);
  void finishDrinking(const Input &in);
  void updateResult(const cfg::Config &c, const Input &in);
  void request(ScaleReq r) {
    if (req_ == ScaleReq::None || r == ScaleReq::Tare) req_ = r;
  }
  uint32_t nextRandom() { return rnd_ ? rnd_(rndCtx_) : 0; }
  Timer place_, lift_, ret_, removed_, final_, waitReady_, autoZeroStable_, negZero_, emptyTrack_;
  uint32_t autoZeroLast_ = 0;
  bool autoZeroDone_ = false;
};

}  // namespace game
