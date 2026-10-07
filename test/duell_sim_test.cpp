// Simulation mehrerer Waagen mit verlustbehaftetem Broadcast-Funk.
// Jede Waage ist eine echte duell::Core-Instanz; die Simulation ersetzt nur
// ESP-NOW (Bus mit Paketverlust) und millis() (virtuelle Uhr).
#include "check.h"
#include "duell_core.h"
#include <cstring>
#include <random>
#include <vector>

using namespace duell;

struct Sim;

struct Node {
  Sim *sim = nullptr;
  int idx = 0;
  uint8_t mac[6] = {};
  Core core;
  bool radio = false; // Funk an
  bool deaf = false;  // empfaengt nichts
  bool mute = false;  // sendet nichts
  float goal = 100.0f;
};

struct Packet {
  int from;
  std::vector<uint8_t> data;
};

struct Sim {
  std::vector<Node> nodes;
  std::mt19937 rng;
  double loss;
  uint32_t now = 1000;
  std::vector<Packet> inflight;

  Sim(int n, uint32_t seed, double lossRate)
      : nodes(n), rng(seed), loss(lossRate) {
    for (int i = 0; i < n; i++) {
      nodes[i].sim = this;
      nodes[i].idx = i;
      const uint8_t mac[6] = {
          0x02, 0xAA, 0x00,
          0x00, 0x00, (uint8_t)(i + 1)}; // hoechster Index = hoechste MAC
      memcpy(nodes[i].mac, mac, 6);
    }
  }

  static void sendFn(void *ctx, const uint8_t *data, size_t len) {
    Node *n = (Node *)ctx;
    if (n->mute)
      return;
    n->sim->inflight.push_back(
        {n->idx, std::vector<uint8_t>(data, data + len)});
  }

  static uint32_t randFn(void *ctx, uint32_t lo, uint32_t hi) {
    Node *n = (Node *)ctx;
    return lo + n->sim->rng() % (hi - lo);
  }

  void radioOn(int i, bool ready) {
    nodes[i].core.begin(nodes[i].mac, now, sendFn, randFn, &nodes[i]);
    nodes[i].radio = true;
    if (ready)
      nodes[i].core.setReady();
  }

  void step(uint32_t dt = 10) {
    std::vector<Packet> pk;
    pk.swap(inflight);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (const Packet &p : pk) {
      for (Node &n : nodes) {
        if (n.idx == p.from || !n.radio || n.deaf)
          continue;
        if (u(rng) < loss)
          continue;
        n.core.onReceive(nodes[p.from].mac, p.data.data(), p.data.size(), now);
      }
    }
    now += dt;
    for (Node &n : nodes) {
      if (n.radio)
        n.core.tick(now, n.goal);
    }
  }

  void runFor(uint32_t ms) {
    uint32_t end = now + ms;
    while ((int32_t)(end - now) > 0)
      step();
  }

  // Bis alle angegebenen Waagen in derselben Runde sind
  bool waitForRound(const std::vector<int> &who, uint32_t timeoutMs) {
    uint32_t end = now + timeoutMs;
    while ((int32_t)(end - now) > 0) {
      bool ok = true;
      uint16_t id = nodes[who[0]].core.currentRound().id;
      for (int i : who) {
        if (!nodes[i].core.view().inRound ||
            nodes[i].core.currentRound().id != id)
          ok = false;
      }
      if (ok)
        return true;
      step();
    }
    return false;
  }

  View view(int i) { return nodes[i].core.view(); }
};

static bool sameTable(const Round &a, const Round &b) {
  if (a.id != b.id || a.n != b.n || a.target != b.target)
    return false;
  for (int i = 0; i < a.n; i++) {
    if (memcmp(a.e[i].mac, b.e[i].mac, 6) != 0)
      return false;
    if (a.e[i].status != b.e[i].status)
      return false;
    if (a.e[i].status == Status::Done &&
        (a.e[i].result != b.e[i].result ||
         a.e[i].durationCs != b.e[i].durationCs))
      return false;
  }
  return true;
}

#define SCENARIO_CHECK(cond)                                                   \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s seed=%u line %d: %s\n", scenario, seed, __LINE__,   \
                  #cond);                                                      \
      g_failures++;                                                            \
      return;                                                                  \
    }                                                                          \
  } while (0)

// ── Szenarien ─────────────────────────────────────────────────────────────────

// Langsamer Spieler: Schnelle sehen sofort einen Live-Rang, am Ende haben
// alle dieselbe finale Rangliste (frueher: Solo-Fallback nach 20 s).
static void slowDrinker(uint32_t seed) {
  const char *scenario = "slowDrinker";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  SCENARIO_CHECK(s.nodes[0].core.currentRound().n == 3);
  SCENARIO_CHECK(s.view(0).target == 100.0f);

  s.runFor(10000);
  s.nodes[0].core.submitResult(100.5f, 4000);
  s.runFor(5000);
  s.nodes[1].core.submitResult(99.0f, 3000);
  s.runFor(5000);

  View a = s.view(0);
  SCENARIO_CHECK(a.inRound && !a.isFinal);
  SCENARIO_CHECK(a.rank == 1);
  SCENARIO_CHECK(a.settled == 2 && a.total == 3);
  SCENARIO_CHECK(s.view(1).rank == 2);

  s.runFor(80000); // Spieler 2 trinkt sehr lange, bleibt aber sichtbar
  SCENARIO_CHECK(!s.view(0).isFinal);
  s.nodes[2].core.submitResult(100.2f, 90000);
  s.runFor(3000);

  for (int i = 0; i < 3; i++)
    SCENARIO_CHECK(s.view(i).isFinal);
  SCENARIO_CHECK(s.view(2).rank == 1);
  SCENARIO_CHECK(s.view(0).rank == 2);
  SCENARIO_CHECK(s.view(1).rank == 3);
  SCENARIO_CHECK(sameTable(s.nodes[0].core.currentRound(),
                           s.nodes[1].core.currentRound()));
  SCENARIO_CHECK(sameTable(s.nodes[0].core.currentRound(),
                           s.nodes[2].core.currentRound()));
}

// Leader (hoechste MAC) faellt nach dem Start aus: Rest wird trotzdem final.
static void leaderDies(uint32_t seed) {
  const char *scenario = "leaderDies";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  s.runFor(2000);
  s.nodes[2].radio = false; // Akku leer
  s.runFor(3000);
  s.nodes[0].core.submitResult(100.1f, 3000);
  s.runFor(3000);
  s.nodes[1].core.submitResult(100.3f, 3000);
  s.runFor(5000);
  SCENARIO_CHECK(!s.view(0).isFinal); // Leader gilt noch nicht als weg

  s.runFor(30000);
  for (int i = 0; i < 2; i++)
    SCENARIO_CHECK(s.view(i).isFinal);
  SCENARIO_CHECK(s.view(0).rank == 1);
  SCENARIO_CHECK(s.view(1).rank == 2);
  SCENARIO_CHECK(s.nodes[0].core.currentRound().e[2].status == Status::Forfeit);
}

// Spieler drueckt vor dem Ergebnis den Taster: sofortiges Forfeit, kein Warten.
static void buttonBeforeResult(uint32_t seed) {
  const char *scenario = "buttonBeforeResult";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  s.runFor(5000);
  s.nodes[1].core.leave(s.now);
  SCENARIO_CHECK(!s.view(1).inRound);
  SCENARIO_CHECK(s.nodes[1].core.busy(s.now)); // Nachlauf aktiv
  s.runFor(5000);
  s.nodes[0].core.submitResult(100.4f, 3000);
  s.runFor(2000);
  s.nodes[2].core.submitResult(100.1f, 3000);
  s.runFor(3000);

  SCENARIO_CHECK(s.view(0).isFinal && s.view(2).isFinal);
  SCENARIO_CHECK(s.view(2).rank == 1);
  SCENARIO_CHECK(s.view(0).rank == 2);
  SCENARIO_CHECK(s.nodes[0].core.currentRound().e[1].status == Status::Forfeit);

  // Nachlauf endet, danach darf die Waage schlafen
  s.runFor(LINGER_MS);
  SCENARIO_CHECK(!s.nodes[1].core.busy(s.now));
}

// Letzter meldet sein Ergebnis und resettet sofort: Nachlauf liefert es aus.
static void lastLeavesImmediately(uint32_t seed) {
  const char *scenario = "lastLeavesImmediately";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  s.runFor(5000);
  s.nodes[0].core.submitResult(101.0f, 3000);
  s.runFor(1000);
  s.nodes[1].core.submitResult(102.0f, 3000);
  s.runFor(14000);
  s.nodes[2].core.submitResult(100.0f, 3000);
  s.nodes[2].core.leave(s.now); // im selben Loop-Durchlauf
  s.runFor(3000);

  SCENARIO_CHECK(s.view(0).isFinal && s.view(1).isFinal);
  SCENARIO_CHECK(s.nodes[0].core.currentRound().e[2].status == Status::Done);
  SCENARIO_CHECK(s.view(0).rank == 2);
  SCENARIO_CHECK(s.view(1).rank == 3);
  SCENARIO_CHECK(sameTable(s.nodes[0].core.currentRound(),
                           s.nodes[1].core.currentRound()));
}

// Spieler stellt das Glas nie zurueck: harter Rundentimeout.
static void neverFinishes(uint32_t seed) {
  const char *scenario = "neverFinishes";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  uint32_t started = s.now;
  s.runFor(5000);
  s.nodes[0].core.submitResult(100.2f, 3000);
  s.nodes[1].core.submitResult(100.1f, 3000);
  s.runFor(ROUND_MAX_MS - 15000);
  SCENARIO_CHECK(!s.view(0).isFinal);
  s.runFor(started + ROUND_MAX_MS + 3000 - s.now);

  for (int i = 0; i < 3; i++)
    SCENARIO_CHECK(s.view(i).isFinal);
  SCENARIO_CHECK(s.view(1).rank == 1);
  SCENARIO_CHECK(s.view(0).rank == 2);
  SCENARIO_CHECK(s.view(2).myStatus == Status::Forfeit);
  SCENARIO_CHECK(s.view(2).rank == 0);

  // Zu spaetes Ergebnis wird nicht mehr angenommen
  s.nodes[2].core.submitResult(100.0f, 200000);
  s.runFor(2000);
  SCENARIO_CHECK(s.view(0).rank == 2);
}

// Eine Waage ohne Glas blockiert den Start (alle muessen bereit sein).
static void idleBlocksStart(uint32_t seed) {
  const char *scenario = "idleBlocksStart";
  Sim s(4, seed, 0.3);
  s.radioOn(0, false); // niedrigste MAC, nicht bereit
  for (int i = 1; i < 4; i++)
    s.radioOn(i, true);
  s.runFor(30000);
  for (int i = 0; i < 4; i++)
    SCENARIO_CHECK(!s.view(i).inRound);
  SCENARIO_CHECK(s.nodes[1].core.readyCount(s.now) == 3);

  s.nodes[0].core.setReady();
  SCENARIO_CHECK(s.waitForRound({0, 1, 2, 3}, 15000));
  SCENARIO_CHECK(s.nodes[0].core.currentRound().n == 4);
}

// Waage mit hoeherer MAC schaltet waehrend der Start-Karenz ein: sie wird
// gehoert, die Karenz beginnt neu und es gibt genau eine Runde mit allen.
static void lateHigherMac(uint32_t seed) {
  const char *scenario = "lateHigherMac";
  Sim s(3, seed, 0.1);
  s.radioOn(0, true);
  s.radioOn(1, true);
  uint32_t t =
      s.now + STARTUP_GUARD_MS + 200; // Karenz von Waage 1 laeuft schon
  while (s.now < t) {
    s.step();
    SCENARIO_CHECK(!s.view(0).inRound && !s.view(1).inRound);
  }
  s.radioOn(2, true);
  for (int k = 0; k < 2000; k++) {
    s.step();
    // Nie zwei gleichzeitige Runden
    uint16_t id = 0;
    for (int i = 0; i < 3; i++) {
      if (!s.view(i).inRound)
        continue;
      uint16_t rid = s.nodes[i].core.currentRound().id;
      SCENARIO_CHECK(id == 0 || id == rid);
      id = rid;
    }
    for (int i = 0; i < 2; i++) {
      if (s.view(i).inRound)
        SCENARIO_CHECK(s.nodes[i].core.currentRound().n == 3);
    }
  }
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 1000));
}

// Teilnehmer verpasst den Start komplett (taub): wird nach dem
// Beitrittsfenster als aufgegeben gewertet, die anderen werden final.
static void missedJoin(uint32_t seed) {
  const char *scenario = "missedJoin";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  // Bereit-Meldungen sollen noch ankommen, nur der Start nicht
  s.runFor(STARTUP_GUARD_MS + 1000);
  s.nodes[1].deaf = true;
  SCENARIO_CHECK(s.waitForRound({0, 2}, 20000));
  SCENARIO_CHECK(s.nodes[0].core.currentRound().n == 3);
  s.runFor(3000);
  s.nodes[0].core.submitResult(100.3f, 3000);
  s.nodes[2].core.submitResult(100.2f, 3000);
  s.runFor(JOIN_WINDOW_MS + PEER_ACTIVE_MS + 2000);

  SCENARIO_CHECK(!s.view(1).inRound);
  SCENARIO_CHECK(s.view(0).isFinal && s.view(2).isFinal);
  SCENARIO_CHECK(s.view(2).rank == 1 && s.view(0).rank == 2);
}

// Teilnehmer verpasst den Start und spielt solo weiter (Glas abgehoben):
// die anderen werten ihn sofort als aufgegeben, nicht erst nach dem Fenster.
static void gaveUpBeforeJoin(uint32_t seed) {
  const char *scenario = "gaveUpBeforeJoin";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  s.runFor(STARTUP_GUARD_MS + 1000);
  s.nodes[1].deaf = true;
  SCENARIO_CHECK(s.waitForRound({0, 2}, 20000));
  s.runFor(1000);
  s.nodes[1].core.leave(s.now);
  s.nodes[0].core.submitResult(100.3f, 3000);
  s.nodes[2].core.submitResult(100.2f, 3000);
  s.runFor(4000);

  SCENARIO_CHECK(s.view(0).isFinal && s.view(2).isFinal);
  SCENARIO_CHECK(s.nodes[0].core.currentRound().e[1].status == Status::Forfeit);
}

// Funk aus mitten in der Runde (duell_deinit): Ausstieg + sofortiges Senden.
// Die anderen werten das sofort als aufgegeben statt nach 30 s.
static void leaveFlush(uint32_t seed) {
  const char *scenario = "leaveFlush";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  s.runFor(2000);
  s.nodes[0].core.submitResult(100.2f, 3000);
  s.nodes[2].core.submitResult(100.1f, 3000);
  s.runFor(1000);
  // Waage 1 schaltet den Funk ab: leave + 3x flush, danach stumm
  s.nodes[1].core.leave(s.now);
  for (int k = 0; k < 3; k++) {
    s.nodes[1].core.flush(s.now);
    s.step(25);
  }
  s.nodes[1].radio = false;
  s.runFor(1000);
  SCENARIO_CHECK(s.view(0).isFinal && s.view(2).isFinal);
  SCENARIO_CHECK(s.nodes[0].core.currentRound().e[1].status == Status::Forfeit);
  // Die stumme Waage blockiert nach PEER_ACTIVE_MS keinen neuen Start
  s.nodes[0].core.leave(s.now);
  s.nodes[2].core.leave(s.now);
  s.nodes[0].core.setReady();
  s.nodes[2].core.setReady();
  SCENARIO_CHECK(s.waitForRound({0, 2}, PEER_FORGET_MS + 10000));
  SCENARIO_CHECK(s.nodes[0].core.currentRound().n == 2);
}

// Zeitweiser Funkverlust: Teilnehmer wird als aufgegeben gewertet, sein
// spaeteres Ergebnis setzt sich trotzdem durch (Done schlaegt Forfeit).
static void temporaryOutage(uint32_t seed) {
  const char *scenario = "temporaryOutage";
  Sim s(3, seed, 0.3);
  for (int i = 0; i < 3; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1, 2}, 20000));
  s.runFor(3000);
  s.nodes[1].deaf = s.nodes[1].mute = true;
  s.runFor(2000);
  s.nodes[0].core.submitResult(100.5f, 3000);
  s.nodes[2].core.submitResult(100.3f, 3000);
  s.runFor(INVISIBLE_FORFEIT_MS + 2000);
  SCENARIO_CHECK(s.view(0).isFinal);
  SCENARIO_CHECK(s.view(2).rank == 1);

  s.nodes[1].core.submitResult(100.0f, 3000);
  s.runFor(3000);
  s.nodes[1].deaf = s.nodes[1].mute = false;
  s.runFor(3000);

  for (int i = 0; i < 3; i++)
    SCENARIO_CHECK(s.view(i).isFinal);
  SCENARIO_CHECK(s.view(1).rank == 1);
  SCENARIO_CHECK(s.view(2).rank == 2);
  SCENARIO_CHECK(s.view(0).rank == 3);
  SCENARIO_CHECK(sameTable(s.nodes[0].core.currentRound(),
                           s.nodes[1].core.currentRound()));
  SCENARIO_CHECK(sameTable(s.nodes[0].core.currentRound(),
                           s.nodes[2].core.currentRound()));
}

// Nach einer Runde: alle verlassen sie, naechste Runde startet sauber und
// niemand tritt versehentlich der alten Runde wieder bei.
static void consecutiveRounds(uint32_t seed) {
  const char *scenario = "consecutiveRounds";
  Sim s(2, seed, 0.3);
  for (int i = 0; i < 2; i++)
    s.radioOn(i, true);
  SCENARIO_CHECK(s.waitForRound({0, 1}, 20000));
  uint16_t first = s.nodes[0].core.currentRound().id;
  s.nodes[0].core.submitResult(100.0f, 3000);
  s.nodes[1].core.submitResult(101.0f, 3000);
  s.runFor(2000);
  SCENARIO_CHECK(s.view(0).isFinal && s.view(1).isFinal);

  s.nodes[0].core.leave(s.now);
  s.nodes[0].core.setReady(); // neues Glas sofort aufgestellt
  s.runFor(3000);
  SCENARIO_CHECK(!s.view(0).inRound); // Waage 1 zeigt noch ihr Ergebnis
  s.nodes[1].core.leave(s.now);
  s.nodes[1].core.setReady();
  SCENARIO_CHECK(s.waitForRound({0, 1}, 15000));
  SCENARIO_CHECK(s.nodes[0].core.currentRound().id != first);
  SCENARIO_CHECK(s.view(0).settled == 0);
}

int main() {
  typedef void (*Scenario)(uint32_t);
  const Scenario scenarios[] = {
      slowDrinker,        leaderDies,
      buttonBeforeResult, lastLeavesImmediately,
      neverFinishes,      idleBlocksStart,
      lateHigherMac,      missedJoin,
      gaveUpBeforeJoin,   leaveFlush,
      temporaryOutage,    consecutiveRounds,
  };
  for (Scenario sc : scenarios) {
    for (uint32_t seed = 1; seed <= 20; seed++)
      sc(seed);
  }
  return finish("duell_sim_test");
}
