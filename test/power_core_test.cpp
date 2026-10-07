// Unit-Tests fuer den reinen Inaktivitaets-Timer (Deep-Sleep) und das
// AP-Auto-Aus.
#include "check.h"
#include "power_core.h"
#include <cmath>
#include <limits>

using namespace power;

static const Blockers NONE = {};
constexpr uint32_t MIN = 60000UL;

static bool upd(SleepPolicy &p, uint32_t now, uint8_t timeoutMin = 1,
                const Blockers &b = NONE) {
  return p.update(now, 0.0f, false, b, timeoutMin);
}

static bool updW(SleepPolicy &p, uint32_t now, float w, uint8_t timeoutMin = 1,
                 bool valid = true) {
  return p.update(now, w, valid, NONE, timeoutMin);
}

// ── elapsed / apTimedOut ──────────────────────────────────────────────────────

static void testElapsed() {
  CHECK(elapsed(1000, 1000, 0));
  CHECK(!elapsed(1999, 1000, 1000));
  CHECK(elapsed(2000, 1000, 1000));
  // ueber den Ueberlauf
  CHECK(elapsed(0x00000100u, 0xFFFFFF00u, 0x200u));
  CHECK(!elapsed(0x000000FFu, 0xFFFFFF00u, 0x200u));
  CHECK(elapsed(0x00000FFFu, 0xFFFFF000u, 0x1FFFu));
  CHECK(!elapsed(0x00000FFEu, 0xFFFFF000u, 0x1FFFu));
}

static void testApTimedOut() {
  // 0 → nie
  CHECK(!apTimedOut(0xFFFFFFFFu, 0, 0));
  CHECK(!apTimedOut(10 * MIN, 0, 0));
  // Grenze 1 min
  CHECK(!apTimedOut(1000 + MIN - 1, 1000, 1));
  CHECK(apTimedOut(1000 + MIN, 1000, 1));
  CHECK(!apTimedOut(1000, 1000, 1));
  // 10 min
  CHECK(!apTimedOut(10 * MIN - 1, 0, 10));
  CHECK(apTimedOut(10 * MIN, 0, 10));
  // 255 min ohne Ueberlauf in der Multiplikation
  CHECK(!apTimedOut(255 * MIN - 1, 0, 255));
  CHECK(apTimedOut(255 * MIN, 0, 255));
  // ueber den millis-Wrap
  uint32_t t0 = 0xFFFFF000u;
  CHECK(!apTimedOut(t0 + MIN - 1, t0, 1));
  CHECK(apTimedOut(t0 + MIN, t0, 1));
  CHECK((uint32_t)(t0 + MIN) < t0); // wirklich uebergelaufen
  // Aktivitaet minimal nach dem Loop-Zeitstempel ist frisch, nicht ~49 Tage alt
  CHECK(!apTimedOut(1000, 1005, 1));
  CHECK(!apTimedOut(0xFFFFFFFEu, 3, 1));
}

// ── SleepPolicy: Grundverhalten ───────────────────────────────────────────────

static void testTimeoutBasic() {
  SleepPolicy p;
  p.reset(1000);
  CHECK(!upd(p, 1000));
  CHECK(!upd(p, 1000 + MIN - 1));
  CHECK(upd(p, 1000 + MIN));
  CHECK(upd(p, 1000 + 5 * MIN)); // bleibt true

  // 5 min
  SleepPolicy q;
  q.reset(0);
  CHECK(!upd(q, 5 * MIN - 1, 5));
  CHECK(upd(q, 5 * MIN, 5));
  // gleiche Instanz, anderes Timeout → wird sofort neu bewertet
  CHECK(!upd(q, 5 * MIN, 6));

  // 255 min
  SleepPolicy r;
  r.reset(0);
  CHECK(!upd(r, 255 * MIN - 1, 255));
  CHECK(upd(r, 255 * MIN, 255));
}

static void testTimeoutZero() {
  SleepPolicy p;
  p.reset(0);
  CHECK(!upd(p, 0, 0));
  CHECK(!upd(p, 10 * MIN, 0));
  CHECK(!upd(p, 1000 * MIN, 0));
  CHECK(!upd(p, 0xFFFFFFFFu, 0));
  // spaeter eingeschaltet: zaehlt ab der letzten Abfrage mit 0, nicht ab reset
  SleepPolicy q;
  q.reset(0);
  CHECK(!upd(q, 30 * MIN, 0));
  CHECK(!upd(q, 30 * MIN + 1, 1));
  CHECK(!upd(q, 31 * MIN - 1, 1));
  CHECK(upd(q, 31 * MIN, 1));
}

static void testActivity() {
  SleepPolicy p;
  p.reset(0);
  CHECK(!upd(p, MIN - 1));
  p.activity(MIN - 1); // Tasterflanke kurz vor Ablauf
  CHECK(!upd(p, MIN));
  CHECK(!upd(p, 2 * MIN - 2));
  CHECK(upd(p, 2 * MIN - 1));
  // nach Ablauf: Aktivitaet weckt den Timer wieder
  p.activity(3 * MIN);
  CHECK(!upd(p, 3 * MIN));
  CHECK(!upd(p, 4 * MIN - 1));
  CHECK(upd(p, 4 * MIN));
  // mehrfach hintereinander
  p.activity(10 * MIN);
  p.activity(10 * MIN + 500);
  CHECK(!upd(p, 11 * MIN + 499));
  CHECK(upd(p, 11 * MIN + 500));
}

static void testResetRestartsTimer() {
  SleepPolicy p;
  p.reset(0);
  CHECK(upd(p, MIN));
  p.reset(MIN);
  CHECK(!upd(p, MIN));
  CHECK(!upd(p, 2 * MIN - 1));
  CHECK(upd(p, 2 * MIN));
}

static void testWithoutReset() {
  // Ohne reset(): erste Abfrage startet den Timer, kein sofortiger Schlaf
  SleepPolicy p;
  CHECK(!upd(p, 10 * MIN));
  CHECK(!upd(p, 11 * MIN - 1));
  CHECK(upd(p, 11 * MIN));

  // activity() vor dem ersten update startet ebenfalls
  SleepPolicy q;
  q.activity(50 * MIN);
  CHECK(!upd(q, 50 * MIN + 1));
  CHECK(upd(q, 51 * MIN));
}

// ── Blocker ───────────────────────────────────────────────────────────────────

static void testEachBlocker() {
  for (int k = 0; k < 5; k++) {
    Blockers b = {};
    switch (k) {
    case 0:
      b.apOn = true;
      break;
    case 1:
      b.ownRoundOpen = true;
      break;
    case 2:
      b.buttonBusy = true;
      break;
    case 3:
      b.calibrating = true;
      break;
    case 4:
      b.actionPending = true;
      break;
    }
    SleepPolicy p;
    p.reset(0);
    // weit ueber das Timeout hinaus: kein Schlaf, solange der Blocker steht
    CHECK(!upd(p, MIN, 1, b));
    CHECK(!upd(p, 10 * MIN, 1, b));
    CHECK(!upd(p, 100 * MIN, 1, b));
    // Blocker weg: Timer laeuft ab der letzten blockierten Abfrage
    CHECK(!upd(p, 100 * MIN + 1));
    CHECK(!upd(p, 101 * MIN - 1));
    CHECK(upd(p, 101 * MIN));
  }
}

static void testBlockerAllAndTimeoutZero() {
  Blockers all = {true, true, true, true, true};
  SleepPolicy p;
  p.reset(0);
  CHECK(!upd(p, 5 * MIN, 1, all));
  CHECK(!upd(p, 6 * MIN, 0, all));
  CHECK(!upd(p, 6 * MIN + 1));
  CHECK(upd(p, 7 * MIN));
}

static void testBlockerAfterExpiry() {
  // Timeout schon abgelaufen, aber Blocker in derselben Abfrage → false
  SleepPolicy p;
  p.reset(0);
  Blockers b = {};
  b.buttonBusy = true;
  CHECK(!upd(p, 2 * MIN, 1, b));
  CHECK(!upd(p, 2 * MIN + 1));
  CHECK(upd(p, 3 * MIN));
}

// ── Gewicht ───────────────────────────────────────────────────────────────────

static void testWeightChangeCounts() {
  // 2,1 g Aenderung zaehlt
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f)); // Referenz
  CHECK(!updW(p, 50 * 1000,
              102.1f)); // Pruefung (>= 2 s seit Referenz) → Aktivitaet
  CHECK(!updW(p, 50 * 1000 + MIN - 1, 102.1f));
  CHECK(updW(p, 50 * 1000 + MIN, 102.1f));

  // Abnahme zaehlt genauso
  SleepPolicy q;
  q.reset(0);
  CHECK(!updW(q, 0, 100.0f));
  CHECK(!updW(q, 30000, 97.9f));
  CHECK(!updW(q, 30000 + MIN - 1, 97.9f));
  CHECK(updW(q, 30000 + MIN, 97.9f));
}

static void testWeightSmallChangeIgnored() {
  // 1,9 g zaehlt nicht
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));
  for (uint32_t t = WEIGHT_CHECK_MS; t < MIN; t += WEIGHT_CHECK_MS) {
    CHECK(!updW(p, t, (t / WEIGHT_CHECK_MS) % 2 ? 101.9f : 98.1f));
  }
  CHECK(updW(p, MIN, 101.9f));

  // genau 2,0 g zaehlt nicht (nur > ACTIVITY_G)
  SleepPolicy q;
  q.reset(0);
  CHECK(!updW(q, 0, 100.0f));
  CHECK(!updW(q, 30000, 102.0f));
  CHECK(updW(q, MIN, 102.0f));
}

static void testWeightReferenceKeptWithoutActivity() {
  // Langsame Drift: Referenz bleibt, bis die Summe > 2 g ist
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));
  CHECK(!updW(p, 10000, 101.5f)); // 1,5 g → keine Aktivitaet
  CHECK(!updW(p, 20000, 102.5f)); // 2,5 g zur Referenz → Aktivitaet bei 20 s
  CHECK(!updW(p, 20000 + MIN - 1, 102.5f));
  CHECK(updW(p, 20000 + MIN, 102.5f));
}

static void testWeightNewReferenceAfterActivity() {
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));
  CHECK(!updW(p, 10000, 103.0f)); // Aktivitaet, neue Referenz 103
  CHECK(!updW(p, 20000, 104.9f)); // 1,9 g zur neuen Referenz → nichts
  CHECK(!updW(p, 10000 + MIN - 1, 104.9f));
  CHECK(updW(p, 10000 + MIN, 104.9f));
}

static void testWeightCheckInterval() {
  // Aenderung zwischen zwei Pruefungen zaehlt erst bei der naechsten Pruefung
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));                   // Referenz, letzte Pruefung 0
  CHECK(!updW(p, WEIGHT_CHECK_MS - 1, 150.0f)); // noch keine Pruefung
  // Wenn die Aenderung hier schon gezaehlt haette, waere der Timer bei 1999
  // neu gestartet. Pruefung bei 2000 zaehlt sie → Ablauf bei 2000 + 1 min.
  CHECK(!updW(p, WEIGHT_CHECK_MS, 150.0f));
  CHECK(!updW(p, WEIGHT_CHECK_MS + MIN - 1, 150.0f));
  CHECK(updW(p, WEIGHT_CHECK_MS + MIN, 150.0f));

  // Kurzes Anheben zwischen zwei Pruefungen wird nicht gesehen
  SleepPolicy q;
  q.reset(0);
  CHECK(!updW(q, 0, 100.0f));
  CHECK(!updW(q, 500, 0.0f));
  CHECK(!updW(q, 1500, 100.0f));
  CHECK(!updW(q, WEIGHT_CHECK_MS, 100.0f));
  CHECK(updW(q, MIN, 100.0f));

  // Pruefabstand ab der letzten Pruefung
  SleepPolicy r;
  r.reset(0);
  CHECK(!updW(r, 0, 100.0f));
  CHECK(!updW(r, 3000, 100.0f)); // Pruefung bei 3000, keine Aenderung
  CHECK(!updW(r, 4999, 110.0f)); // < 2 s seit 3000 → keine Pruefung
  CHECK(!updW(r, 5000, 110.0f)); // Pruefung → Aktivitaet bei 5000
  CHECK(!updW(r, 5000 + MIN - 1,
              110.0f)); // (haette 4999 gezaehlt, waere hier Schluss)
  CHECK(updW(r, 5000 + MIN, 110.0f));
}

static void testWeightInvalidIgnored() {
  // ungueltiges Gewicht zaehlt nicht, auch bei grossen Spruengen
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));
  CHECK(!updW(p, 10000, 500.0f, 1, false));
  CHECK(!updW(p, 20000, -500.0f, 1, false));
  CHECK(updW(p, MIN, 900.0f, 1, false));

  // NaN/Inf trotz weightValid werden ignoriert
  SleepPolicy q;
  q.reset(0);
  CHECK(!updW(q, 0, 100.0f));
  CHECK(!updW(q, 10000, std::nanf("")));
  CHECK(!updW(q, 20000, std::numeric_limits<float>::infinity()));
  CHECK(!updW(q, 30000, 100.0f)); // Referenz noch intakt → keine Aktivitaet
  CHECK(updW(q, MIN, 100.0f));

  // ohne gueltiges Gewicht wird auch keine Referenz gesetzt
  SleepPolicy r;
  r.reset(0);
  CHECK(!updW(r, 0, 100.0f, 1, false));
  CHECK(!updW(r, 10000,
              200.0f)); // erste gueltige Messung → Referenz, keine Aktivitaet
  CHECK(updW(r, MIN, 200.0f));

  // Referenz bleibt ueber ungueltige Phasen erhalten: danach anderes Gewicht →
  // Aktivitaet
  SleepPolicy s;
  s.reset(0);
  CHECK(!updW(s, 0, 100.0f));
  CHECK(!updW(s, 10000, 0.0f, 1, false));
  CHECK(!updW(s, 40000, 150.0f)); // Aktivitaet bei 40 s
  CHECK(!updW(s, 40000 + MIN - 1, 150.0f));
  CHECK(updW(s, 40000 + MIN, 150.0f));
}

static void testResetForgetsReference() {
  SleepPolicy p;
  p.reset(0);
  CHECK(!updW(p, 0, 100.0f));
  p.reset(10000);
  // 150 g nach reset: neue Referenz, keine Aktivitaet
  CHECK(!updW(p, 10000, 150.0f));
  CHECK(!updW(p, 20000, 150.0f));
  CHECK(!updW(p, 10000 + MIN - 1, 150.0f));
  CHECK(updW(p, 10000 + MIN, 150.0f));
  // nach dem Ablauf zaehlt eine Aenderung wieder als Aktivitaet
  CHECK(!updW(p, 10000 + MIN + WEIGHT_CHECK_MS, 160.0f));
}

static void testWeightWithBlocker() {
  // Gewicht wird auch bei Blocker verfolgt: die Referenz ist danach aktuell,
  // ein unveraendertes Gewicht nach dem Blocker zaehlt nicht als Aktivitaet.
  SleepPolicy p;
  p.reset(0);
  Blockers b = {};
  b.apOn = true;
  CHECK(!p.update(0, 100.0f, true, b, 1));
  CHECK(!p.update(10000, 200.0f, true, b, 1)); // neue Referenz 200
  CHECK(!p.update(20000, 200.0f, true, b, 1)); // letzte blockierte Abfrage
  CHECK(!p.update(20000 + MIN - 1, 200.0f, true, NONE, 1));
  CHECK(p.update(20000 + MIN, 200.0f, true, NONE, 1));
}

// ── Ueberlauf ─────────────────────────────────────────────────────────────────

static void testWrap() {
  const uint32_t t0 = 0xFFFFF000u; // ca. 4 s vor dem millis-Wrap
  SleepPolicy p;
  p.reset(t0);
  CHECK(!upd(p, t0));
  CHECK(!upd(p, t0 + 0x1000u)); // genau beim Wrap (0)
  CHECK(!upd(p, t0 + MIN - 1));
  CHECK(upd(p, t0 + MIN));

  // Aktivitaet kurz vor dem Wrap, Ablauf danach
  SleepPolicy q;
  q.reset(0xFFFFFFF0u);
  q.activity(0xFFFFFFFFu);
  CHECK(!upd(q, 0x00000000u));
  CHECK(!upd(q, MIN - 2));
  CHECK(upd(q, MIN - 1));

  // Gewichtspruefung ueber den Wrap
  SleepPolicy r;
  r.reset(t0);
  CHECK(!updW(r, t0, 100.0f));                       // Referenz bei t0
  CHECK(!updW(r, t0 + WEIGHT_CHECK_MS - 1, 110.0f)); // noch keine Pruefung
  CHECK(!updW(r, t0 + 0x1000u + 5000u, 110.0f)); // nach dem Wrap → Aktivitaet
  CHECK(!updW(r, t0 + 0x1000u + 5000u + MIN - 1, 110.0f));
  CHECK(updW(r, t0 + 0x1000u + 5000u + MIN, 110.0f));

  // Blocker ueber den Wrap
  SleepPolicy s;
  s.reset(t0);
  Blockers b = {};
  b.calibrating = true;
  CHECK(!upd(s, t0 + 0x800u, 1, b));
  CHECK(!upd(s, 0x00002000u, 1, b)); // nach dem Wrap
  CHECK(!upd(s, 0x00002000u + MIN - 1));
  CHECK(upd(s, 0x00002000u + MIN));
}

// ── Zeitstempel "in der Zukunft" ──────────────────────────────────────────────

static void testActivityAheadOfNow() {
  // activity() mit spaeter gelesenem millis() als das now des folgenden
  // update(): (now - lastActivity) waere ~49 Tage → darf nicht sofort schlafen.
  SleepPolicy p;
  p.reset(0);
  p.activity(1005);
  CHECK(!upd(p, 1000));
  CHECK(!upd(p, 1003));
  CHECK(!upd(p, 1000 + MIN - 1));
  CHECK(upd(
      p,
      1005 + MIN)); // Timer laeuft ab der Aktivitaet (bzw. wenige ms frueher)

  // gleiches Muster ueber den millis-Wrap
  SleepPolicy q;
  q.reset(0xFFFFFF00u);
  q.activity(0x00000003u);     // nach dem Wrap gelesen
  CHECK(!upd(q, 0xFFFFFFFEu)); // update mit altem now vor dem Wrap
  CHECK(!upd(q, 0x00000001u));
  CHECK(!upd(q, (uint32_t)(0xFFFFFFFEu + MIN - 1)));
  CHECK(upd(q, 0x00000003u + MIN));

  // Blocker-Abfrage mit groesserem now, danach eine mit kleinerem now
  SleepPolicy r;
  r.reset(0);
  Blockers b = {};
  b.buttonBusy = true;
  CHECK(!upd(r, 2000, 1, b));
  CHECK(!upd(r, 1999));
  CHECK(!upd(r, 1999 + MIN - 1));
  CHECK(upd(r, 2000 + MIN));

  // Schleife wie app_loop: now am Schleifenanfang, die Tasterflanke wird mit
  // einem etwas spaeteren millis() gemeldet; kein Schlaf, solange Aktivitaet
  // kommt
  SleepPolicy s;
  s.reset(0);
  bool slept = false;
  for (uint32_t t = 0; t < 10 * MIN; t += 7) {
    if (t % 30000 < 7)
      s.activity(t + 3);
    if (upd(s, t))
      slept = true;
  }
  CHECK(!slept);
}

static void testVeryOldActivity() {
  // Grenze der Zeitrechnung: bis 2^31 - 1 ms alt zaehlt normal
  SleepPolicy p;
  p.reset(0);
  CHECK(upd(p, 0x7FFFFFFFu, 255));
  // >= 2^31 ms ist von "Zukunft" nicht zu unterscheiden (update laeuft in der
  // Praxis jede Schleife): zaehlt als Aktivitaet jetzt, Timer haengt aber nicht
  SleepPolicy q;
  q.reset(0);
  CHECK(!upd(q, 0x80000000u));
  CHECK(!upd(q, 0x80000000u + MIN - 1));
  CHECK(upd(q, 0x80000000u + MIN));
}

static void testWeightExtremeValues() {
  // Endliche Extremwerte: Differenz laeuft auf inf → zaehlt als Aktivitaet,
  // kein NaN, danach normales Verhalten mit neuer Referenz
  SleepPolicy p;
  p.reset(0);
  const float big = std::numeric_limits<float>::max();
  CHECK(!updW(p, 0, big));
  CHECK(!updW(p, 10000, -big)); // Aktivitaet bei 10 s, Referenz -big
  CHECK(!updW(p, 20000, -big)); // keine Aenderung
  CHECK(!updW(p, 10000 + MIN - 1, -big));
  CHECK(updW(p, 10000 + MIN, -big));
  CHECK(!updW(p, 10000 + MIN + WEIGHT_CHECK_MS, 0.0f)); // wieder Aktivitaet

  // -0,0 und +0,0 sind gleich
  SleepPolicy q;
  q.reset(0);
  CHECK(!updW(q, 0, 0.0f));
  CHECK(!updW(q, 10000, -0.0f));
  CHECK(updW(q, MIN, -0.0f));
}

int main() {
  testElapsed();
  testApTimedOut();
  testTimeoutBasic();
  testTimeoutZero();
  testActivity();
  testResetRestartsTimer();
  testWithoutReset();
  testEachBlocker();
  testBlockerAllAndTimeoutZero();
  testBlockerAfterExpiry();
  testWeightChangeCounts();
  testWeightSmallChangeIgnored();
  testWeightReferenceKeptWithoutActivity();
  testWeightNewReferenceAfterActivity();
  testWeightCheckInterval();
  testWeightInvalidIgnored();
  testResetForgetsReference();
  testWeightWithBlocker();
  testWrap();
  testActivityAheadOfNow();
  testVeryOldActivity();
  testWeightExtremeValues();
  return finish("power_core_test");
}
