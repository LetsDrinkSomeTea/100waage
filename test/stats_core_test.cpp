// Statistik: Stufen, Bestwert, schnellste Zeit, Erfolge, Duell, Verlauf.
#include "check.h"
#include "stats_core.h"
#include <cstring>

using namespace stats;

static Round solo(int32_t drankCg, uint32_t ms, int32_t goalCg = 10000) {
  return {drankCg, goalCg, ms, false};
}

static void testLevels() {
  Tracker t;
  t.record(solo(10000, 3000)); // 0 → Perfekt
  t.record(solo(10010, 3000)); // 10 → Not Bad
  t.record(solo(9989, 3000));  // 11 → Ganz ok
  t.record(solo(10100, 3000)); // 100 → Ganz ok
  t.record(solo(10101, 3000)); // 101 → keine Stufe
  t.record(solo(5000, 3000));  // weit daneben
  const Totals &s = t.totals();
  CHECK(s.rounds == 6);
  CHECK(s.perfect == 1 && s.notBad == 1 && s.ok == 2);
}

static void testRecord() {
  Tracker t;
  CHECK(!t.totals().hasBest);
  // erste Runde ist immer ein Rekord, auch weit daneben
  CHECK(t.record(solo(8000, 5000)) == Achievement::Record);
  CHECK(t.totals().hasBest && t.totals().bestDevCg == 2000);
  // schlechter oder gleich: kein Rekord
  CHECK(t.record(solo(12000, 5000)) == Achievement::None);
  CHECK(t.record(solo(8000, 5000)) == Achievement::None);
  // besser (Betrag), auch zu viel getrunken
  CHECK(t.record(solo(10500, 4000, 10000)) == Achievement::Record);
  CHECK(t.totals().bestDevCg == 500 && t.totals().bestMs == 4000);
  // anderes Ziel zaehlt gemeinsam
  CHECK(t.record(solo(4997, 6000, 5000)) == Achievement::Record);
  CHECK(t.totals().bestDevCg == 3 && t.totals().bestGoalCg == 5000);
}

static void testFastest() {
  Tracker t;
  t.record(solo(10000, 5000)); // Rekord und schnellste: Rekord gewinnt
  CHECK(t.totals().hasFastest && t.totals().fastestMs == 5000);
  // schneller, aber > 10 % daneben: zaehlt nicht
  CHECK(t.record(solo(11001, 1000)) == Achievement::None);
  CHECK(t.record(solo(8999, 1000)) == Achievement::None);
  CHECK(t.totals().fastestMs == 5000);
  // schneller und genau 10 % daneben: schnellste Zeit
  CHECK(t.record(solo(9000, 4000)) == Achievement::Fastest);
  CHECK(t.totals().fastestMs == 4000 && t.totals().fastestDevCg == -1000);
  // 10 % gilt relativ zum Ziel der Runde
  CHECK(t.record(solo(5500, 3000, 5000)) == Achievement::Fastest);
  CHECK(t.record(solo(5501, 2000, 5000)) == Achievement::None);
  // gleich schnell: nichts
  CHECK(t.record(solo(9950, 4000)) == Achievement::None);

  // erste gute Runde nach schlechten: schnellste Zeit, kein Rekord
  Tracker u;
  u.record(solo(10050, 9000)); // Rekord 50, gut → schnellste 9000
  CHECK(u.record(solo(10080, 8000)) == Achievement::Fastest);
  // Rekord und schneller zugleich: nur Rekord gemeldet, beides gespeichert
  CHECK(u.record(solo(10001, 2000)) == Achievement::Record);
  CHECK(u.totals().fastestMs == 2000 && u.totals().bestDevCg == 1);
}

static void testDuel() {
  Tracker t;
  t.record({10000, 10000, 3000, true});
  t.duelFinal(1, 3, false);
  CHECK(t.totals().duels == 1 && t.totals().wins == 1);
  CHECK(t.recent(0).duel && t.recent(0).rank == 1);
  // zweiter finaler Stand derselben Runde aendert den Rang nicht
  t.duelFinal(2, 3, false);
  CHECK(t.recent(0).rank == 1);

  t.record({9000, 10000, 3000, true});
  t.duelFinal(2, 2, false);
  CHECK(t.totals().duels == 3 && t.totals().wins == 1);
  CHECK(t.recent(0).rank == 2);

  // Aufgabe: Duell zaehlt, kein Sieg, kein Rang
  t.record({9000, 10000, 3000, true});
  t.duelFinal(1, 2, true);
  CHECK(t.totals().duels == 4 && t.totals().wins == 1);
  CHECK(t.recent(0).rank == 0);

  // allein ist kein Duell
  t.duelFinal(1, 1, false);
  CHECK(t.totals().duels == 4);

  // Solo-Eintrag bekommt keinen Rang
  t.record(solo(10000, 3000));
  t.duelFinal(1, 2, false);
  CHECK(t.recent(0).rank == 0 && !t.recent(0).duel);
}

static void testRecent() {
  Tracker t;
  CHECK(t.recentCount() == 0);
  for (int i = 0; i < RECENT + 3; i++)
    t.record(solo(10000 + i, 1000 + (uint32_t)i));
  CHECK(t.recentCount() == RECENT);
  // neueste zuerst
  CHECK(t.recent(0).devCg == RECENT + 2);
  CHECK(t.recent(RECENT - 1).devCg == 3);
  CHECK(t.recent(0).durationMs == 1000 + RECENT + 2);
  CHECK(t.totals().rounds == RECENT + 3);
}

static void testLoadReset() {
  Tracker t;
  t.record(solo(10000, 3000));
  Totals saved = t.totals();
  Tracker u;
  u.load(saved);
  CHECK(u.totals().rounds == 1 && u.totals().perfect == 1);
  CHECK(u.recentCount() == 0); // Verlauf nur im RAM
  CHECK(u.record(solo(10000, 3000)) == Achievement::None);
  u.reset();
  CHECK(u.totals().rounds == 0 && !u.totals().hasBest &&
        !u.totals().hasFastest && u.recentCount() == 0);
}

// Prozent-Runden: Bestwert merkt Prozent und Glas, Verlauf das Prozent-Ziel
static void testPercentRounds() {
  Tracker t;
  Round r = {14790, 14820, 4000, false, 50, "Tulpe 0,3"};
  CHECK(t.record(r) == Achievement::Record);
  CHECK(t.totals().bestPct == 50);
  CHECK(strcmp(t.totals().bestGlass, "Tulpe 0,3") == 0);
  CHECK(t.recent(0).pct == 50);
  // Neuer Rekord in Gramm loescht Prozent und Glas
  CHECK(t.record({10000, 10000, 3000, false}) == Achievement::Record);
  CHECK(t.totals().bestPct == 0 && t.totals().bestGlass[0] == 0);
  CHECK(t.recent(0).pct == 0 && t.recent(1).pct == 50);
  // Prozent ohne Glasname
  Tracker u;
  u.record({100, 200, 1000, false, 30, nullptr});
  CHECK(u.totals().bestPct == 30 && u.totals().bestGlass[0] == 0);
}

int main() {
  testPercentRounds();
  testLevels();
  testRecord();
  testFastest();
  testDuel();
  testRecent();
  testLoadReset();
  return finish("stats_core_test");
}
