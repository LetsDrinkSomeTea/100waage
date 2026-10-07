// Mehrere Waagen komplett: game::Game + echte duell::Core-Instanzen ueber einen
// Broadcast-Bus mit Paketverlust. Prueft das Zusammenspiel aus Sicht der
// Anzeige: gleiche Raenge auf allen Waagen, Ergebnisregeln, Ausstieg.
#include "check.h"
#include "config_core.h"
#include "duell_core.h"
#include "game_core.h"
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

struct Bus;

struct Scale;

// DuelPort wie EspDuelPort in duell.cpp, nur gegen die Simulation
class SimPort final : public game::DuelPort {
public:
  Scale *s = nullptr;
  bool active() override;
  void readyCount(int *ready, int *total) override;
  void setReady() override;
  bool startSignal(float *target) override;
  duell::View view() override;
  void submit(float grams, uint32_t durationMs) override;
  void leave() override;
};

struct Scale {
  Bus *bus = nullptr;
  int idx = 0;
  uint8_t mac[6] = {};
  duell::Core core;
  SimPort port;
  game::Game game;
  cfg::Config c = cfg::defaults();
  bool radio = false;
  uint32_t rnd = 0;
  float weight = 0.0f; // aktuelles Gewicht (Skript)
};

struct Packet {
  int from;
  std::vector<uint8_t> data;
};

struct Bus {
  std::vector<Scale> s;
  std::mt19937 rng;
  double loss;
  uint32_t now;
  std::vector<Packet> inflight;

  Bus(int n, uint32_t seed, double lossRate, uint32_t start = 1000)
      : s(n), rng(seed), loss(lossRate), now(start) {
    for (int i = 0; i < n; i++) {
      Scale &x = s[i];
      x.bus = this;
      x.idx = i;
      const uint8_t mac[6] = {0x02, 0xBB, 0, 0, 0, (uint8_t)(i + 1)};
      memcpy(x.mac, mac, 6);
      x.port.s = &x;
      x.game.begin(&x.port, rnd32, &x);
      x.game.reset(x.c, now);
      x.game.takeScaleReq();
    }
  }

  static uint32_t rnd32(void *ctx) {
    Scale *x = (Scale *)ctx;
    return (++x->rnd) * 2654435761u + (uint32_t)x->idx * 40503u;
  }
  static void sendFn(void *ctx, const uint8_t *data, size_t len) {
    Scale *x = (Scale *)ctx;
    x->bus->inflight.push_back(
        {x->idx, std::vector<uint8_t>(data, data + len)});
  }
  static uint32_t randFn(void *ctx, uint32_t lo, uint32_t hi) {
    Scale *x = (Scale *)ctx;
    return lo + x->bus->rng() % (hi - lo);
  }

  void radioOn(int i) {
    s[i].core.begin(s[i].mac, now, sendFn, randFn, &s[i]);
    s[i].radio = true;
  }
  // wie radio_stop(): leave + Flush-Burst, dann aus
  void radioOff(int i) {
    Scale &x = s[i];
    x.core.leave(now);
    for (int k = 0; k < 3; k++) {
      x.core.flush(now);
      deliver();
    }
    x.radio = false;
  }

  void deliver() {
    std::vector<Packet> pk;
    pk.swap(inflight);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (const Packet &p : pk) {
      for (Scale &x : s) {
        if (x.idx == p.from || !x.radio)
          continue;
        if (u(rng) < loss)
          continue;
        x.core.onReceive(s[p.from].mac, p.data.data(), p.data.size(), now);
      }
    }
  }

  // 10-ms-Schritte; das Spiel bekommt alle 100 ms ein Gewicht (10 SPS)
  void step() {
    deliver();
    now += 10;
    for (Scale &x : s) {
      if (x.radio)
        x.core.tick(now, x.game.localGoal());
      if (now % 100 == 0) {
        game::Input in = {};
        in.now = now;
        in.weightValid = true;
        in.weight = x.weight;
        in.stable = true;
        in.radioOn = x.radio;
        x.game.update(x.c, in);
        x.game.takeScaleReq();
      }
    }
  }
  void run(uint32_t ms) {
    uint32_t end = now + ms;
    while ((int32_t)(end - now) > 0)
      step();
  }
  bool waitUntil(uint32_t timeoutMs, bool (*cond)(Bus &)) {
    uint32_t end = now + timeoutMs;
    while ((int32_t)(end - now) > 0) {
      if (cond(*this))
        return true;
      step();
    }
    return cond(*this);
  }
};

bool SimPort::active() {
  return s->radio && s->core.activePeers(s->bus->now) > 0;
}
void SimPort::readyCount(int *ready, int *total) {
  *ready = s->radio ? s->core.readyCount(s->bus->now) : 0;
  *total = (s->radio ? s->core.activePeers(s->bus->now) : 0) + 1;
}
void SimPort::setReady() {
  if (s->radio)
    s->core.setReady();
}
bool SimPort::startSignal(float *target) {
  return s->radio && s->core.startSignal(target);
}
duell::View SimPort::view() {
  return s->radio ? s->core.view() : duell::View();
}
void SimPort::submit(float grams, uint32_t durationMs) {
  if (s->radio)
    s->core.submitResult(grams, durationMs);
}
void SimPort::leave() {
  if (s->radio)
    s->core.leave(s->bus->now);
}

static bool allLive(Bus &b) {
  for (Scale &x : b.s)
    if (x.game.duel() != game::Duel::WaitStart)
      return false;
  return true;
}
static bool allFinal(Bus &b) {
  for (Scale &x : b.s)
    if (x.radio &&
        !(x.game.phase() == game::Phase::Result && x.game.view().isFinal))
      return false;
  return true;
}

// Alle Waagen: Funk an, volles Glas (300 g), bis die Runde laeuft
static bool startRound(Bus &b) {
  for (size_t i = 0; i < b.s.size(); i++)
    b.radioOn((int)i);
  b.run(6000); // Gegner sehen, Startschutz ablaufen lassen
  for (Scale &x : b.s)
    x.weight = 300.0f;
  return b.waitUntil(15000, allLive);
}

// Waage i trinkt `drank` Gramm: abheben, awayMs, zurueck
static void drinkAll(Bus &b, const std::vector<float> &drank,
                     const std::vector<uint32_t> &away) {
  for (Scale &x : b.s)
    x.weight = 0.0f;
  std::vector<bool> back(b.s.size(), false);
  uint32_t t0 = b.now;
  for (;;) {
    bool all = true;
    for (size_t i = 0; i < b.s.size(); i++) {
      if (!back[i] && (uint32_t)(b.now - t0) >= away[i]) {
        b.s[i].weight = 300.0f - drank[i];
        back[i] = true;
      }
      all = all && back[i];
    }
    if (all)
      break;
    b.step();
  }
}

static void testThreeScalesSameRanks(uint32_t seed, double loss) {
  Bus b(3, seed, loss);
  CHECK(startRound(b));
  float target = b.s[0].game.view().goal;
  for (Scale &x : b.s)
    CHECK(x.game.view().goal == target); // gleiches Duell-Ziel
  CHECK(target >= 11.0f && target <= 100.0f);

  // 0: perfekt, 1: 0,5 g daneben, 2: weit daneben
  drinkAll(b, {target, target + 0.5f, target - 20.0f}, {2000, 2500, 3000});
  CHECK(b.waitUntil(10000, allFinal));
  uint8_t expect[3] = {1, 2, 3};
  for (int i = 0; i < 3; i++) {
    CHECK(b.s[i].game.view().screen == game::Screen::ResultDuel);
    CHECK(b.s[i].game.view().rank == expect[i]);
    CHECK(b.s[i].game.view().total == 3);
  }

  // Glaeser weg: gut (0, 1) bleibt, schlecht (2) nach Final + 3 s weg
  for (Scale &x : b.s)
    x.weight = 0.0f;
  b.run(1000);
  CHECK(b.s[2].game.phase() == game::Phase::Result ||
        b.s[2].game.phase() == game::Phase::Idle);
  b.run(3000);
  CHECK(b.s[0].game.phase() == game::Phase::Result);
  CHECK(b.s[1].game.phase() == game::Phase::Result);
  CHECK(b.s[2].game.phase() == game::Phase::Idle);
}

static void testRadioOffMidRound() {
  Bus b(2, 7, 0.3);
  CHECK(startRound(b));
  // Waage 1 trinkt, Waage 0 schaltet den Funk ab, bevor sie zurueckstellt
  for (Scale &x : b.s)
    x.weight = 0.0f;
  b.run(1500);
  b.s[1].weight = 300.0f - b.s[1].game.view().goal;
  b.run(1000);
  CHECK(b.s[1].game.phase() == game::Phase::Result);
  CHECK(!b.s[1].game.view().isFinal);
  uint32_t off = b.now;
  b.radioOff(0);
  bool fin =
      b.waitUntil(5000, [](Bus &bb) { return bb.s[1].game.view().isFinal; });
  CHECK(fin);
  CHECK((uint32_t)(b.now - off) < 1000); // Abmeldung kommt sofort an
  CHECK(b.s[1].game.view().rank == 1);
  // Waage 0 spielt solo zu Ende
  b.s[0].weight = 200.0f;
  b.run(1000);
  CHECK(b.s[0].game.view().screen == game::Screen::ResultSolo);
  CHECK(b.s[0].game.duel() == game::Duel::Offline);
}

static void testRadioOffAfterFinalKeepsRank() {
  Bus b(2, 11, 0.2);
  CHECK(startRound(b));
  float t = b.s[0].game.view().goal;
  drinkAll(b, {t + 2.0f, t}, {2000, 2000});
  CHECK(b.waitUntil(10000, allFinal));
  CHECK(b.s[0].game.view().rank == 2);
  b.radioOff(0);
  b.run(2000);
  CHECK(b.s[0].game.view().screen == game::Screen::ResultDuel);
  CHECK(b.s[0].game.view().rank == 2 && b.s[0].game.view().isFinal);
  CHECK(b.s[1].game.view().rank == 1);
}

static void testButtonDuringRoundForfeits() {
  Bus b(3, 23, 0.2);
  CHECK(startRound(b));
  float t = b.s[0].game.view().goal;
  // Waage 2 drueckt den Taster (Reset) statt zu trinken
  b.s[2].game.reset(b.s[2].c, b.now);
  b.s[2].weight = 0.0f;
  b.s[0].weight = b.s[1].weight = 0.0f;
  b.run(2000);
  b.s[0].weight = 300.0f - t;
  b.s[1].weight = 300.0f - t - 1.5f;
  CHECK(b.waitUntil(10000, [](Bus &bb) {
    return bb.s[0].game.view().isFinal && bb.s[1].game.view().isFinal;
  }));
  CHECK(b.s[0].game.view().rank == 1);
  CHECK(b.s[1].game.view().rank == 2);
  CHECK(b.s[0].game.view().total == 3);
  CHECK(b.s[2].game.phase() == game::Phase::Idle);
}

static void testNextRoundAfterReset() {
  Bus b(2, 5, 0.2);
  CHECK(startRound(b));
  float t = b.s[0].game.view().goal;
  drinkAll(b, {t, t}, {1500, 1500});
  CHECK(b.waitUntil(10000, allFinal));
  // beide gut → Taster, neues Glas, neue Runde
  for (Scale &x : b.s) {
    x.game.reset(x.c, b.now);
    x.weight = 0.0f;
  }
  b.run(2000);
  for (Scale &x : b.s)
    x.weight = 300.0f;
  CHECK(b.waitUntil(15000, allLive));
}

int main() {
  for (uint32_t seed = 1; seed <= 20; seed++)
    testThreeScalesSameRanks(seed, 0.3);
  testThreeScalesSameRanks(99, 0.0);
  testRadioOffMidRound();
  testRadioOffAfterFinalKeepsRank();
  testButtonDuringRoundForfeits();
  testNextRoundAfterReset();
  return finish("game_duel_sim_test");
}
