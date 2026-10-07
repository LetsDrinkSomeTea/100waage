// Spiellogik (Solo und Duell gegen eine Fake-Anbindung) mit Gewichtsskripten.
#include "check.h"
#include "fakes.h"
#include <cmath>
#include <cstring>

using namespace game;

// Volles Glas aufstellen, bis Bereit
static void place(Driver &d, float full) { d.run(600, full); }

// Abheben (Glas weg = 0 g), awayMs trinken, mit Restgewicht zurueckstellen.
// Liefert die Startzeit (erstes Unterschreiten).
static uint32_t drink(Driver &d, float rest, uint32_t awayMs = 2000) {
  uint32_t t0 = d.now;
  d.run(awayMs, 0.0f);
  d.run(600, rest);
  return t0;
}

// ── Bewertung ─────────────────────────────────────────────────────────────────

static void testRating() {
  CHECK(rate(10000, 10000) == Rating::Perfect);
  CHECK(rate(10010, 10000) == Rating::NotBad);
  CHECK(rate(9990, 10000) == Rating::NotBad);
  CHECK(rate(10011, 10000) == Rating::Ok);
  CHECK(rate(10100, 10000) == Rating::Ok);
  CHECK(rate(9900, 10000) == Rating::Ok);
  CHECK(rate(10101, 10000) == Rating::Greedy);
  CHECK(rate(9899, 10000) == Rating::Shy);
  CHECK(rate(0, 10000) == Rating::Shy);

  CHECK(isGood(10000, 10000, 10));
  CHECK(isGood(11000, 10000, 10));
  CHECK(isGood(9000, 10000, 10));
  CHECK(!isGood(11001, 10000, 10));
  CHECK(!isGood(8999, 10000, 10));
  CHECK(isGood(10000, 10000, 0));
  CHECK(!isGood(10001, 10000, 0));
  CHECK(!isGood(0, 0, 100));    // kein Ziel → nie gut
  CHECK(isGood(0, 10000, 100)); // 100 %: alles bis doppelt ist gut
  CHECK(isGood(20000, 10000, 100));
  CHECK(!isGood(20001, 10000, 100));
  CHECK(!isGood(2000000000, 500000, 100)); // kein Ueberlauf

  // Rundung statt Abschneiden
  CHECK(toCg(1.234f) == 123);
  CHECK(toCg(1.236f) == 124);
  CHECK(toCg(100.0f) == 10000);
  CHECK(toCg(-1.236f) == -124);
  CHECK(toCg(0.0f) == 0);
}

// ── Solo ──────────────────────────────────────────────────────────────────────

static void testBootTare() {
  Driver d;
  CHECK(d.lastReq == ScaleReq::Tare); // jeder Reset tariert
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.g.localGoal() == 100.0f);
  d.run(500, 0.0f, true, false); // Tara laeuft
  CHECK(d.g.view().screen == Screen::Taring);
  d.run(100, 0.0f);
  CHECK(d.g.view().screen == Screen::IdleGame);
  CHECK(!d.g.view().glassOn);
  d.run(100, 50.0f);
  CHECK(d.g.view().glassOn);
}

static void testSoloPerfectStays() {
  Driver d;
  d.run(1000, 0.0f);
  d.run(500, 300.0f); // 0,4 s stabil: noch nicht bereit
  CHECK(d.g.phase() == Phase::Idle);
  d.run(100, 300.0f);
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(d.g.view().screen == Screen::Ready);
  CHECK(d.g.gameRunning());

  uint32_t t0 = d.now;
  d.run(300, 0.0f); // 0,2 s abgehoben: noch nicht
  CHECK(d.g.phase() == Phase::Ready);
  d.run(100, 0.0f);
  CHECK(d.g.phase() == Phase::Drinking);
  CHECK(d.g.view().screen == Screen::Drinking);
  d.run(2600, 0.0f);
  uint32_t tr = d.now;
  d.run(500, 200.0f); // zurueck, 0,4 s stabil
  CHECK(d.g.phase() == Phase::Drinking);
  d.run(100, 200.0f);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().screen == Screen::ResultSolo);
  CHECK(d.g.view().drankCg == 10000);
  CHECK(d.g.view().rating == Rating::Perfect);
  CHECK(d.g.view().durationMs == tr - t0);
  CHECK(!d.g.gameRunning());

  // Gutes Ergebnis bleibt beim Abheben und lange danach
  int reqs = d.reqCount;
  d.run(10000, 0.0f);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.reqCount == reqs);
  d.run(1000, 200.0f);
  CHECK(d.g.phase() == Phase::Result);

  d.press();
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.lastReq == ScaleReq::Tare);
}

static void testSoloBadGoesOnLift() {
  Driver d;
  d.run(500, 0.0f);
  place(d, 300.0f);
  drink(d, 150.0f); // 150 g getrunken: zu gierig
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().rating == Rating::Greedy);
  d.run(1000, 150.0f);
  CHECK(d.g.phase() == Phase::Result); // steht noch
  int reqs = d.reqCount;
  d.run(400, 0.0f);
  CHECK(d.g.phase() == Phase::Result); // erst nach 0,5 s
  d.run(200, 0.0f);
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.reqCount == reqs + 1);
  CHECK(d.lastReq == ScaleReq::TareEmpty);
}

static void testSoloShy() {
  Driver d;
  place(d, 300.0f);
  drink(d, 260.0f);
  CHECK(d.g.view().drankCg == 4000);
  CHECK(d.g.view().rating == Rating::Shy);
}

static void testUnstableReturnFinishes() {
  // Schaum/Rauschen: nie stabil → spaetestens nach RETURN_MAX_MS
  Driver d;
  place(d, 300.0f);
  d.run(1000, 0.0f);
  d.run(1400, 200.0f, false);
  CHECK(d.g.phase() == Phase::Drinking);
  d.run(200, 200.0f, false);
  CHECK(d.g.phase() == Phase::Result);
}

static void testShortLiftInReady() {
  Driver d;
  place(d, 300.0f);
  for (int i = 0; i < 5; i++) {
    d.run(200, 100.0f); // kurz angehoben
    d.run(300, 300.0f);
  }
  CHECK(d.g.phase() == Phase::Ready);
}

static void testInvalidWeightPauses() {
  Driver d;
  place(d, 300.0f);
  d.run(200, 0.0f);
  d.run(5000, 0.0f, true, false); // Sensorfehler mitten im Abheben
  CHECK(d.g.phase() == Phase::Ready);
  d.run(400, 0.0f);
  CHECK(d.g.phase() == Phase::Drinking);
}

static void testEmptyTrackedWhileAway() {
  // Waehrend des Trinkens driftet die leere Waage; Ruecksetz-Erkennung folgt
  Driver d;
  place(d, 300.0f);
  d.run(400, 0.0f);
  d.run(2000, 3.0f); // Drift auf 3 g, stabil
  d.run(600, 203.0f);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().drankCg == 9700);
}

static void testTareWithGlassThenNegZero() {
  Driver d;
  d.run(1000, 0.0f);
  // Glas steht, Kurzdruck tariert mit Glas → Waage zeigt 0
  d.press();
  d.run(1000, 0.0f);
  int reqs = d.reqCount;
  // Glas weg → -250 g, stabil
  d.run(900, -250.0f);
  CHECK(d.reqCount == reqs);
  d.run(200, -250.0f);
  CHECK(d.reqCount == reqs + 1);
  CHECK(d.lastReq == ScaleReq::NegZero);
  // instabil → keine Nullung
  Driver e;
  e.run(1000, 0.0f);
  int r2 = e.reqCount;
  e.run(5000, -250.0f, false);
  CHECK(e.reqCount == r2);
  // knapp ueber -tol → keine Nullung
  e.run(5000, -9.0f);
  CHECK(e.reqCount == r2);
}

static void testNegZeroOnlyInGame() {
  Driver d;
  d.c.scaleMode = cfg::ScaleMode::Standard;
  d.c.autoZeroEnabled = false;
  d.press();
  int reqs = d.reqCount;
  d.run(5000, -250.0f);
  CHECK(d.reqCount == reqs);
  CHECK(d.g.view().screen == Screen::IdleStandard);
}

static void testAutoZero() {
  Driver d; // Reset bei 1000 → Pause 3 x 5 s
  int reqs = d.reqCount;
  d.run(19000, 1.0f);
  CHECK(d.reqCount == reqs);
  d.run(2000, 1.0f);
  CHECK(d.reqCount == reqs + 1);
  CHECK(d.lastReq == ScaleReq::AutoZero);
  // danach wieder Pause
  d.run(14000, 1.0f);
  CHECK(d.reqCount == reqs + 1);
  d.run(7000, 1.0f);
  CHECK(d.reqCount == reqs + 2);

  // ueber der Schwelle oder aus: nie
  Driver e;
  int r2 = e.reqCount;
  e.run(60000, 3.0f);
  CHECK(e.reqCount == r2);
  e.c.autoZeroEnabled = false;
  e.run(60000, 1.0f);
  CHECK(e.reqCount == r2);
}

static void testStandardMode() {
  Driver d;
  d.c.scaleMode = cfg::ScaleMode::Standard;
  d.press();
  d.run(100, 1.5f);
  CHECK(d.g.view().screen == Screen::IdleStandard);
  CHECK(d.g.view().weight == 0.0f); // Auto-Zero zeigt 0
  d.run(100, 123.4f);
  CHECK(std::fabs(d.g.view().weight - 123.4f) < 1e-4f);
  d.run(5000, 500.0f);
  CHECK(d.g.phase() == Phase::Idle); // kein Spiel
  CHECK(d.port.setReadyCalls == 0);
}

static void testGoalSettings() {
  Driver d;
  d.run(500, 0.0f);
  d.c.goal = 50.0f;
  d.g.applyGoalSettings(d.c, d.now);
  CHECK(d.g.localGoal() == 50.0f);
  int reqs = d.reqCount;
  d.run(100, 0.0f);
  CHECK(d.reqCount == reqs); // ohne Tara
  CHECK(d.g.view().goal == 50.0f);

  place(d, 300.0f);
  d.c.goal = 80.0f;
  d.g.applyGoalSettings(d.c, d.now);
  CHECK(d.g.localGoal() == 50.0f); // im Spiel erst ab dem naechsten Reset
  d.press();
  CHECK(d.g.localGoal() == 80.0f);

  // Zufallsmodus: jeder Reset wuerfelt, im Bereich
  d.c.randomModeEnabled = true;
  d.c.randomMin = 20.0f;
  float prev = -1.0f;
  bool changed = false;
  for (int i = 0; i < 20; i++) {
    d.press();
    float g = d.g.localGoal();
    CHECK(g >= 20.0f && g <= 80.0f);
    CHECK(g == std::floor(g));
    if (prev >= 0 && g != prev)
      changed = true;
    prev = g;
  }
  CHECK(changed);
}

static void testGlassBelowGoalNotReady() {
  Driver d;
  d.run(5000, 99.9f);
  CHECK(d.g.phase() == Phase::Idle);
  d.run(5000, 150.0f, false); // instabil
  CHECK(d.g.phase() == Phase::Idle);
}

static void testWrap() {
  Driver d(0xFFFFF000u); // millis() laeuft waehrend der Runde ueber
  d.run(500, 0.0f);
  place(d, 300.0f);
  CHECK(d.g.phase() == Phase::Ready);
  uint32_t t0 = drink(d, 200.0f, 3000);
  CHECK(d.now < t0); // tatsaechlich uebergelaufen
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().drankCg == 10000);
  CHECK(d.g.view().durationMs == 3000);
}

// ── Duell ─────────────────────────────────────────────────────────────────────

static Driver *duelReady(Driver &d) {
  d.radio = true;
  d.port.isActive = true;
  d.run(500, 0.0f);
  place(d, 300.0f);
  return &d;
}

static void testDuelGoodStays() {
  Driver d;
  duelReady(d);
  CHECK(d.g.duel() == Duel::WaitReady);
  CHECK(d.g.view().screen == Screen::WaitDuel);
  CHECK(d.port.setReadyCalls == 1);
  d.port.ready = 2;
  d.port.total = 3;
  d.run(100, 300.0f);
  CHECK(d.g.view().ready == 2 && d.g.view().readyTotal == 3);

  d.port.beginRound(80.0f, 3);
  d.run(100, 300.0f);
  CHECK(d.g.duel() == Duel::WaitStart);
  CHECK(d.g.view().screen == Screen::DuelStart);
  CHECK(d.g.view().goal == 80.0f);
  CHECK(d.g.ownRoundOpen());

  drink(d, 220.0f);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.duel() == Duel::Live);
  CHECK(d.port.submitCalls == 1);
  CHECK(std::fabs(d.port.submittedGrams - 80.0f) < 1e-4f);
  CHECK(d.g.view().screen == Screen::ResultDuel);

  // vorlaeufiger Rang
  d.port.v.rank = 1;
  d.port.v.settled = 1;
  d.run(100, 220.0f);
  CHECK(d.g.view().rank == 1 && !d.g.view().isFinal);
  uint32_t sig1 = d.g.view().resultSig;
  CHECK(d.g.ownRoundOpen());

  // final
  d.port.v.settled = 3;
  d.port.v.isFinal = true;
  d.run(100, 220.0f);
  CHECK(d.g.view().isFinal);
  CHECK(d.g.view().resultSig != sig1);
  CHECK(!d.g.ownRoundOpen());

  // gut: bleibt beim Abheben
  int leaves = d.port.leaveCalls;
  d.run(10000, 0.0f);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.port.leaveCalls == leaves);

  // Funk nach dem Final aus: Rang bleibt
  d.port.v = {};
  d.port.isActive = false;
  d.run(1000, 0.0f);
  CHECK(d.g.view().screen == Screen::ResultDuel);
  CHECK(d.g.view().rank == 1 && d.g.view().isFinal);

  d.press();
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.port.leaveCalls == leaves + 1);
}

static void testDuelBadAfterFinal() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  drink(d, 250.0f); // 50 g von 80: schlecht
  CHECK(d.g.duel() == Duel::Live);
  d.port.v.rank = 2;
  d.port.v.settled = 1;

  // Glas weg, aber noch nicht final → bleibt
  d.run(5000, 0.0f);
  CHECK(d.g.phase() == Phase::Result);

  d.port.v.settled = 2;
  d.port.v.isFinal = true;
  d.run(2900, 0.0f);
  CHECK(d.g.phase() == Phase::Result); // final < 3 s
  d.run(200, 0.0f);
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.lastReq == ScaleReq::TareEmpty);
}

static void testDuelBadGlassStillOn() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  drink(d, 250.0f);
  d.port.v.rank = 2;
  d.port.v.isFinal = true;
  d.run(10000, 250.0f); // Glas bleibt stehen
  CHECK(d.g.phase() == Phase::Result);
  d.run(600, 0.0f);
  CHECK(d.g.phase() == Phase::Idle);
}

static void testDuelForfeitIsBad() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  drink(d, 220.0f);                           // eigentlich perfekt
  d.port.v.myStatus = duell::Status::Forfeit; // zu spaet
  d.port.v.isFinal = true;
  d.run(100, 220.0f);
  CHECK(d.g.view().forfeit);
  d.run(3500, 0.0f);
  CHECK(d.g.phase() == Phase::Idle);
}

static void testDuelLostBeforeFinal() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  drink(d, 220.0f);
  d.port.v.rank = 1;
  d.run(100, 220.0f);
  uint32_t seq = d.g.view().soloFallbackSeq;
  d.port.v = {}; // Funk aus vor dem Final
  d.run(100, 220.0f);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.g.view().screen == Screen::ResultSolo);
  CHECK(d.g.view().rating == Rating::Perfect); // gegen das Duell-Ziel
  CHECK(d.g.view().soloFallbackSeq == seq + 1);
  d.run(5000, 0.0f);
  CHECK(d.g.phase() == Phase::Result); // gut bleibt
}

static void testWaitReadyShortLift() {
  Driver d;
  duelReady(d);
  for (int i = 0; i < 5; i++) {
    d.run(200, 0.0f);
    d.run(300, 300.0f);
  }
  CHECK(d.g.duel() == Duel::WaitReady);
  CHECK(d.port.leaveCalls == 0);
  uint32_t seq = d.g.view().soloFallbackSeq;
  uint32_t t0 = d.now;
  d.run(400, 0.0f);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.g.phase() == Phase::Drinking);
  CHECK(d.port.leaveCalls == 1);
  CHECK(d.g.view().soloFallbackSeq == seq + 1);
  d.run(1600, 0.0f);
  uint32_t tr = d.now;
  d.run(600, 200.0f);
  CHECK(d.g.view().screen == Screen::ResultSolo);
  CHECK(d.g.view().durationMs == tr - t0);
  CHECK(d.port.submitCalls == 0);
}

static void testWaitReadyTimeout() {
  Driver d;
  duelReady(d);
  d.run(59000, 300.0f);
  CHECK(d.g.duel() == Duel::WaitReady);
  d.run(1500, 300.0f);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(d.g.view().screen == Screen::Ready);
  CHECK(d.port.leaveCalls == 1);
  CHECK(d.g.view().soloFallbackSeq == 1);
}

static void testWaitReadyPeersGone() {
  Driver d;
  duelReady(d);
  d.port.isActive = false;
  d.run(100, 300.0f);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.port.leaveCalls == 1);
  drink(d, 200.0f);
  CHECK(d.g.view().screen == Screen::ResultSolo);
  CHECK(d.g.view().rating == Rating::Perfect); // eigenes Ziel
}

static void testWaitStartRoundGone() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  CHECK(d.g.duel() == Duel::WaitStart);
  d.port.v = {};
  d.port.start = false;
  d.run(100, 300.0f);
  CHECK(d.g.duel() == Duel::Offline);
  drink(d, 220.0f);
  CHECK(d.g.view().screen == Screen::ResultSolo);
  CHECK(d.g.view().rating == Rating::Perfect); // Duell-Ziel 80
  CHECK(d.port.submitCalls == 0);
}

static void testNoDuelWithoutRadio() {
  Driver d;
  d.port.isActive = true; // Gegner sichtbar, aber Funk aus (Zustand stale)
  place(d, 300.0f);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.port.setReadyCalls == 0);
}

// Duell-Modus spielt wie Game (Zielanzeige, Bereit, Duell bei Funk)
static void testDuelModePlays() {
  Driver d;
  d.c.scaleMode = cfg::ScaleMode::Duel;
  d.press();
  d.run(100, 0.0f);
  CHECK(d.g.view().screen == Screen::IdleGame);
  d.radio = true;
  d.port.isActive = true;
  place(d, 300.0f);
  CHECK(d.g.duel() == Duel::WaitReady);
  CHECK(d.port.setReadyCalls == 1);
}

// Statistik: jedes Ergebnis wird einmal gemeldet, finaler Duell-Stand einmal
static void testRoundEvents() {
  Driver d;
  RoundDone r;
  CHECK(!d.g.takeRound(&r));
  uint32_t seq = d.g.view().roundSeq;
  place(d, 300.0f);
  drink(d, 200.5f, 3000);
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().roundSeq == seq + 1);
  CHECK(d.g.takeRound(&r));
  CHECK(r.drankCg == 9950 && r.goalCg == 10000 && !r.duel);
  CHECK(r.durationMs == d.g.view().durationMs);
  CHECK(!d.g.takeRound(&r)); // nur einmal
  DuelFinal f;
  CHECK(!d.g.takeDuelFinal(&f)); // solo: kein Duell-Ende
  // Reset aendert den Zaehler nicht
  d.press();
  CHECK(d.g.view().roundSeq == seq + 1);
}

static void testDuelFinalEvent() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  drink(d, 220.0f);
  RoundDone r;
  CHECK(d.g.takeRound(&r));
  CHECK(r.duel && r.goalCg == 8000 && r.drankCg == 8000);

  DuelFinal f;
  d.port.v.rank = 1;
  d.port.v.settled = 1;
  d.run(100, 220.0f);
  CHECK(!d.g.takeDuelFinal(&f)); // vorlaeufig
  d.port.v.settled = 2;
  d.port.v.isFinal = true;
  d.run(100, 220.0f);
  CHECK(d.g.takeDuelFinal(&f));
  CHECK(f.rank == 1 && f.total == 2 && !f.forfeit);
  d.run(1000, 220.0f);
  CHECK(!d.g.takeDuelFinal(&f)); // nur einmal pro Runde
}

static void testResetLeaves() {
  Driver d;
  duelReady(d);
  d.port.beginRound(80.0f, 2);
  d.run(100, 300.0f);
  int leaves = d.port.leaveCalls;
  d.press();
  CHECK(d.port.leaveCalls == leaves + 1);
  CHECK(d.g.duel() == Duel::Offline);
  CHECK(d.g.localGoal() == 100.0f); // Duell-Ziel vergessen
}

// ── Glas und Prozent-Ziel ─────────────────────────────────────────────────────

static const glass::Glass GLASSES[] = {
    {1, "Tulpe 0,3", 270.0f, 300.0f},
    {2, "Krug 0,4", 520.0f, 400.0f},
    {3, "Euro 0,5", 370.0f, 500.0f},
    {4, "Euro 0,33", 260.0f, 330.0f},
};

struct GlassDriver : Driver {
  glass::List list;
  explicit GlassDriver(bool percent) {
    list.begin(GLASSES, 4, 5);
    g.setGlasses(&list);
    c.goalPercent = percent;
    c.goalPct = 50;
    press();
    run(500, 0.0f);
  }
};

static void testGlassInGramMode() {
  GlassDriver d(false);
  CHECK(!d.g.percentMode());
  CHECK(!d.g.view().pct);
  place(d, 920.0f); // voller Krug
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(d.g.glassKnown() && d.g.glass().id == 2);
  CHECK(d.g.view().glassSource == glass::Source::Auto);
  drink(d, 720.0f); // 200 g getrunken, Ziel 100 g: wie bisher
  CHECK(d.g.phase() == Phase::Result);
  CHECK(d.g.view().drankCg == 20000);
  CHECK(!d.g.view().pct);
  RoundDone r;
  CHECK(d.g.takeRound(&r) && r.goalPct == 0 && r.goalCg == 10000);
  CHECK(d.g.detector().memory().refG == 720.0f); // Referenz = Endgewicht
}

static void testPercentRound() {
  GlassDriver d(true);
  CHECK(d.g.percentMode());
  CHECK(d.g.localGoal() == 50.0f);
  CHECK(d.g.view().screen == Screen::IdleGame && d.g.view().pct);
  CHECK(d.g.view().goal == 50.0f);

  d.run(400, 920.0f); // noch nicht ruhig genug lange
  CHECK(d.g.phase() == Phase::Idle);
  d.run(200, 920.0f);
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(strcmp(d.g.view().glassName, "Krug 0,4") == 0);
  drink(d, 720.0f); // 200 g von 400 g = 50 %
  CHECK(d.g.phase() == Phase::Result);
  const View &v = d.g.view();
  CHECK(v.pct);
  CHECK(v.drankCg == 20000);
  CHECK(v.goalCg == 20000);
  CHECK(v.drankPctD == 500);
  CHECK(v.rating == Rating::Perfect);
  RoundDone r;
  CHECK(d.g.takeRound(&r));
  CHECK(r.goalPct == 50 && r.goalCg == 20000 && !r.duel);
  CHECK(strcmp(r.glass, "Krug 0,4") == 0);

  // Gutes Ergebnis, Kurzdruck mit Glas (Tara mit Glas): Glas weg, wieder hin.
  // Gleiches Glas (Regel 1), nicht die Euro 0,5 (350 g = 70 % waere Kandidat)
  d.press();
  d.absOffset = 720.0f; // relativ 0 = absolut 720
  d.run(300, 0.0f);
  d.run(1100, -720.0f); // abgehoben → NegZero
  CHECK(d.lastReq == ScaleReq::NegZero);
  d.absOffset = 0.0f; // leer genullt
  d.run(300, 0.0f);
  place(d, 720.0f);
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(d.g.glass().id == 2 && d.g.glass().source == glass::Source::Same);
  drink(d, 620.0f); // 100 g von 200 g = 50 %
  CHECK(d.g.view().goalCg == 10000);
  CHECK(d.g.view().rating == Rating::Perfect);
  CHECK(d.g.view().drankPctD == 500);

  // Schlechtes Ergebnis: Abheben setzt zurueck, auch im Prozent-Modus
  d.press();
  d.run(500, 0.0f);
  place(d, 920.0f); // nachgefuellt
  CHECK(d.g.glass().id == 2 && d.g.glass().source == glass::Source::Auto);
  drink(d, 820.0f); // 100 g statt 200 g
  CHECK(d.g.view().rating == Rating::Shy);
  CHECK(d.g.view().drankPctD == 250);
  d.run(600, 0.0f);
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.lastReq == ScaleReq::TareEmpty);
}

static void testPercentUnknownGlass() {
  GlassDriver d(true);
  d.run(600, 2000.0f); // passt zu keinem Glas
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.g.view().glassUnknown);
  CHECK(d.g.view().glassName[0] == 0);
  d.run(3000, 2000.0f);
  CHECK(d.g.phase() == Phase::Idle);
  // Weg und leeres Glas auflegen: Tulpe erkannt, aber ohne Inhalt kein Start
  d.run(300, 0.0f);
  CHECK(!d.g.view().glassUnknown);
  uint32_t seq = d.g.view().glassSeq;
  d.run(600, 270.0f);
  CHECK(d.g.view().glassSeq == seq + 1);
  CHECK(d.g.view().glassSource == glass::Source::Empty);
  CHECK(strcmp(d.g.view().glassName, "Tulpe 0,3") == 0);
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(!d.g.view().glassUnknown);
  // Eingeschenkt (halb voll, 420 g): gilt als Tulpe, Start
  d.run(300, 0.0f);
  place(d, 420.0f);
  CHECK(d.g.phase() == Phase::Ready);
  CHECK(d.g.glass().id == 1);
}

static void testPercentNoAbsolute() {
  // Ohne Leer-Referenz keine Bestimmung → kein Start
  GlassDriver d(true);
  d.absOk = false;
  d.run(1000, 920.0f);
  CHECK(d.g.phase() == Phase::Idle);
  CHECK(d.g.view().glassUnknown);
  // Ohne Liste ebenso
  GlassDriver e(true);
  e.g.setGlasses(nullptr);
  e.run(1000, 920.0f);
  CHECK(e.g.phase() == Phase::Idle);
  // Gramm-Modus startet trotzdem
  GlassDriver f(false);
  f.absOk = false;
  place(f, 920.0f);
  CHECK(f.g.phase() == Phase::Ready);
  CHECK(!f.g.glassKnown());
}

static void testPercentModes() {
  // Duell-Modus spielt in Gramm
  GlassDriver d(true);
  d.c.scaleMode = cfg::ScaleMode::Duel;
  d.press();
  CHECK(!d.g.percentMode());
  CHECK(d.g.localGoal() == 100.0f);
  // Zufall: ganze Prozent zwischen randomMinPct und goalPct
  GlassDriver r(true);
  r.c.randomModeEnabled = true;
  for (int i = 0; i < 50; i++) {
    r.press();
    float p = r.g.localGoal();
    CHECK(p >= 20.0f && p <= 50.0f && p == floorf(p));
  }
  // Web-Umschalten im Idle wirkt sofort
  GlassDriver w(false);
  w.c.goalPercent = true;
  w.g.applyGoalSettings(w.c, w.now);
  CHECK(w.g.percentMode() && w.g.localGoal() == 50.0f);
}

int main() {
  testRating();
  testBootTare();
  testSoloPerfectStays();
  testSoloBadGoesOnLift();
  testSoloShy();
  testUnstableReturnFinishes();
  testShortLiftInReady();
  testInvalidWeightPauses();
  testEmptyTrackedWhileAway();
  testTareWithGlassThenNegZero();
  testNegZeroOnlyInGame();
  testAutoZero();
  testStandardMode();
  testGoalSettings();
  testGlassBelowGoalNotReady();
  testWrap();
  testDuelGoodStays();
  testDuelBadAfterFinal();
  testDuelBadGlassStillOn();
  testDuelForfeitIsBad();
  testDuelLostBeforeFinal();
  testWaitReadyShortLift();
  testWaitReadyTimeout();
  testWaitReadyPeersGone();
  testWaitStartRoundGone();
  testNoDuelWithoutRadio();
  testDuelModePlays();
  testRoundEvents();
  testDuelFinalEvent();
  testResetLeaves();
  testGlassInGramMode();
  testPercentRound();
  testPercentUnknownGlass();
  testPercentNoAbsolute();
  testPercentModes();
  return finish("game_core_test");
}
