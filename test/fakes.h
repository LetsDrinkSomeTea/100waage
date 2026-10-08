#pragma once
// Test-Doubles und Gewichtsskripte fuer game_core.
#include "config_core.h"
#include "game_core.h"

// Duell-Anbindung, deren Zustand der Test direkt setzt.
class FakePort final : public game::DuelPort {
public:
  bool isActive = false;
  int ready = 1, total = 2;
  bool start = false;
  float target = 0.0f;
  duell::View v = {};
  // Aufrufzaehler / letzte Werte
  int setReadyCalls = 0, leaveCalls = 0, submitCalls = 0;
  float submittedGrams = -1.0f;
  uint32_t submittedMs = 0;

  bool active() override { return isActive; }
  void readyCount(int *r, int *t) override {
    *r = ready;
    *t = total;
  }
  void setReady() override { setReadyCalls++; }
  bool startSignal(float *t) override {
    if (start)
      *t = target;
    return start;
  }
  duell::View view() override { return v; }
  void submit(float grams, uint32_t durationMs) override {
    submitCalls++;
    submittedGrams = grams;
    submittedMs = durationMs;
    v.myStatus = duell::Status::Done;
  }
  void leave() override {
    leaveCalls++;
    start = false;
    v = {};
  }
  // Runde gestartet, ich bin Pending
  void beginRound(float t, uint8_t players) {
    start = true;
    target = t;
    v = {};
    v.inRound = true;
    v.myStatus = duell::Status::Pending;
    v.total = players;
    v.target = t;
  }
};

// Zufall: gestreuter Zaehler (deterministisch, nutzt alle Bits)
inline uint32_t counterRnd(void *ctx) {
  uint32_t *c = (uint32_t *)ctx;
  return (++*c) * 2654435761u;
}

// Treibt ein Spiel mit konstantem Gewicht in 100-ms-Schritten (10 SPS).
struct Driver {
  game::Game g;
  cfg::Config c = cfg::defaults();
  FakePort port;
  uint32_t rnd = 0;
  uint32_t now = 1000;
  bool radio = false;
  bool absOk = true;      // Leer-Referenz bekannt
  float absOffset = 0.0f; // absolut = w + absOffset (Tara mit Glas)
  game::ScaleReq lastReq = game::ScaleReq::None;
  int reqCount = 0;

  explicit Driver(uint32_t start = 1000) : now(start) {
    g.begin(&port, counterRnd, &rnd);
    g.reset(c, now);
    take();
    port.leaveCalls = 0;
  }
  void take() {
    game::ScaleReq r = g.takeScaleReq();
    if (r != game::ScaleReq::None) {
      lastReq = r;
      reqCount++;
    }
  }
  void tick(float w, bool stable = true, bool valid = true) {
    game::Input in = {};
    in.now = now;
    in.weightValid = valid;
    in.weight = w;
    in.stable = stable;
    in.radioOn = radio;
    in.absValid = absOk;
    in.absWeight = w + absOffset;
    g.update(c, in);
    take();
  }
  // ms lang mit Gewicht w laufen (letzter Tick bei now + ms - 100)
  void run(uint32_t ms, float w, bool stable = true, bool valid = true) {
    for (uint32_t t = 0; t < ms; t += 100) {
      tick(w, stable, valid);
      now += 100;
    }
  }
  void press() {
    g.reset(c, now);
    take();
  }
};
