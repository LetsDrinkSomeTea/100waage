// Unit-Tests fuer den entprellten Taster (Entprellung, Zonen, Overlay,
// Boot-Druck, Flanken, langsame/schnelle Loops, millis-Wrap).
#include "button_core.h"
#include "check.h"
#include <cstdint>
#include <vector>

using namespace button;

namespace {

// Startzeiten, an denen alle Szenarien laufen: normal, mitten drin und kurz vor
// dem Ueberlauf (Druecke laufen ueber 0xFFFFFFFF -> 0).
const uint32_t BASES[] = {0u, 123456u, 0xFFFFF000u,
                          0xFFFFFFFFu - MODE_MS - 50u};

// Erwartete Zone laut Header-Kommentar (unabhaengig vom Code formuliert).
Zone expectZone(uint32_t heldMs) {
  if (heldMs < MIN_PRESS_MS)
    return Zone::None;
  if (heldMs < MODE_MS)
    return Zone::Short;
  if (heldMs < RADIO_MS)
    return Zone::Mode;
  if (heldMs < CANCEL_MS)
    return Zone::Radio;
  return Zone::Cancel;
}

struct Seg {
  bool level;
  uint32_t ms;
};

// Rohpegel ab t0 als Folge von Segmenten; davor und danach LOW.
struct Wave {
  uint32_t t0 = 0;
  std::vector<Seg> segs;

  Wave &add(bool level, uint32_t ms) {
    segs.push_back({level, ms});
    return *this;
  }
  uint32_t length() const {
    uint32_t n = 0;
    for (const Seg &s : segs)
      n += s.ms;
    return n;
  }
  bool at(uint32_t t) const {
    uint32_t off = t - t0;
    for (const Seg &s : segs) {
      if (off < s.ms)
        return s.level;
      off -= s.ms;
    }
    return false;
  }
};

struct Log {
  std::vector<Zone> zones;      // Ereignisse != None
  std::vector<uint32_t> at;     // Zeitpunkte der Ereignisse
  std::vector<Zone> zoneBefore; // zone() nach dem Poll direkt vor dem Ereignis
  int edges = 0;

  int count(Zone z) const {
    int n = 0;
    for (Zone e : zones)
      n += e == z;
    return n;
  }
};

// Pollt ab first im Abstand step bis first + span (inklusive). repeat > 1
// simuliert einen Loop, der mehrmals pro Millisekunde laeuft. probe(now) wird
// nach jedem Poll aufgerufen.
template <class Probe>
void run(Button &b, const Wave &w, uint32_t first, uint32_t span, uint32_t step,
         Log &log, Probe probe, int repeat = 1) {
  Zone prev = b.zone(first);
  for (uint32_t k = 0; k <= span; k += step) {
    uint32_t now = first + k;
    for (int r = 0; r < repeat; r++) {
      Zone z = b.update(w.at(now), now);
      if (z != Zone::None) {
        log.zones.push_back(z);
        log.at.push_back(now);
        log.zoneBefore.push_back(prev);
      }
      if (b.takeEdge())
        log.edges++;
      prev = b.zone(now);
    }
    probe(now);
  }
}

void run(Button &b, const Wave &w, uint32_t first, uint32_t span, uint32_t step,
         Log &log, int repeat = 1) {
  run(b, w, first, span, step, log, [](uint32_t) {}, repeat);
}

// Sauberer Druck: LOW 100 ms, HIGH holdMs, LOW 300 ms; Poll ab base + phase.
Log cleanPress(uint32_t base, uint32_t holdMs, uint32_t step = 1,
               uint32_t phase = 0) {
  Button b;
  b.begin(false, base);
  Wave w;
  w.t0 = base;
  w.add(false, 100).add(true, holdMs).add(false, 300);
  Log log;
  run(b, w, base + phase, w.length(), step, log);
  return log;
}

// Deterministischer Zufall fuer Prell-Muster.
struct Lcg {
  uint32_t s;
  uint32_t next(uint32_t n) {
    s = s * 1664525u + 1013904223u;
    return (s >> 8) % n;
  }
};

// Prellen: Segmente abwechselnd first/!first, jedes 1..maxSeg ms, insgesamt
// hoechstens maxTotal ms, endet mit !first (der stabile Pegel folgt danach).
uint32_t addBurst(Wave &w, bool first, Lcg &r, uint32_t maxSeg,
                  uint32_t maxTotal) {
  uint32_t total = 0;
  int pairs = 1 + (int)r.next(6);
  for (int i = 0; i < pairs; i++) {
    uint32_t a = 1 + r.next(maxSeg), b = 1 + r.next(maxSeg);
    if (total + a + b > maxTotal)
      break;
    w.add(first, a).add(!first, b);
    total += a + b;
  }
  return total;
}

// ── Entprellung: exakte Zeitpunkte ────────────────────────────────────────────

void testDebounceTiming() {
  const uint32_t T = 1000;
  Button b;
  b.begin(false, T);
  CHECK(!b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.zone(T) == Zone::None);
  CHECK(b.heldMs(T) == 0);
  CHECK(!b.overlay(T));
  CHECK(b.update(false, T) == Zone::None);

  // Druecken: erst nach DEBOUNCE_MS stabil
  const uint32_t P = T + 100;
  CHECK(b.update(true, P) == Zone::None);
  CHECK(!b.pressed());
  CHECK(b.zone(P) == Zone::None); // noch nicht entprellt
  CHECK(b.update(true, P + DEBOUNCE_MS - 1) == Zone::None);
  CHECK(!b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.update(true, P + DEBOUNCE_MS) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.takeEdge());
  CHECK(!b.takeEdge());
  // Haltedauer zaehlt ab Beginn des stabilen Pegels, nicht ab der Entprellung
  CHECK(b.heldMs(P + DEBOUNCE_MS) == DEBOUNCE_MS);
  CHECK(b.zone(P + DEBOUNCE_MS) == Zone::Short);
  CHECK(!b.overlay(P + DEBOUNCE_MS));
  CHECK(b.update(true, P + OVERLAY_MS) == Zone::None);
  CHECK(b.heldMs(P + OVERLAY_MS) == OVERLAY_MS);
  CHECK(b.overlay(P + OVERLAY_MS));
  CHECK(!b.overlay(P + OVERLAY_MS - 1));

  // Loslassen nach 500 ms: Ereignis genau DEBOUNCE_MS spaeter
  const uint32_t R = P + 500;
  CHECK(b.update(false, R) == Zone::None);
  CHECK(b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.heldMs(R + 20) == 500); // eingefroren auf die echte Haltedauer
  CHECK(b.zone(R + 20) == Zone::Short);
  CHECK(b.overlay(R + 20));
  CHECK(b.update(false, R + DEBOUNCE_MS - 1) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.update(false, R + DEBOUNCE_MS) == Zone::Short);
  CHECK(!b.pressed());
  CHECK(b.takeEdge());
  CHECK(!b.takeEdge());
  CHECK(b.heldMs(R + DEBOUNCE_MS) == 0);
  CHECK(b.zone(R + DEBOUNCE_MS) == Zone::None);
  CHECK(!b.overlay(R + DEBOUNCE_MS));

  // Ereignis kommt nur einmal
  CHECK(b.update(false, R + DEBOUNCE_MS) == Zone::None);
  CHECK(b.update(false, R + DEBOUNCE_MS + 1) == Zone::None);
  CHECK(b.update(false, R + 60000) == Zone::None);
  CHECK(!b.takeEdge());
}

// Ohne begin(): verhaelt sich wie begin(false, 0).
void testDefaultConstructed() {
  Button b;
  CHECK(!b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.zone(0) == Zone::None);
  CHECK(b.heldMs(5000) == 0);
  CHECK(!b.overlay(5000));
  CHECK(b.update(true, 10) == Zone::None);
  CHECK(b.update(true, 10 + DEBOUNCE_MS) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.takeEdge());
  CHECK(b.update(false, 510) == Zone::None);
  CHECK(b.update(false, 510 + DEBOUNCE_MS) == Zone::Short);
}

// ── Zonen beim Loslassen (Grenzen) ────────────────────────────────────────────

void testReleaseBoundaries() {
  struct Case {
    uint32_t hold;
    Zone z;
  };
  const Case cases[] = {
      {DEBOUNCE_MS + 1, Zone::None}, // entprellt, aber zu kurz
      {MIN_PRESS_MS - 1, Zone::None}, {MIN_PRESS_MS, Zone::Short},
      {MODE_MS - 1, Zone::Short},     {MODE_MS, Zone::Mode},
      {RADIO_MS - 1, Zone::Mode},     {RADIO_MS, Zone::Radio},
      {CANCEL_MS - 1, Zone::Radio},   {CANCEL_MS, Zone::Cancel},
      {30000, Zone::Cancel},
  };
  for (uint32_t base : BASES) {
    for (const Case &c : cases) {
      Log l = cleanPress(base, c.hold);
      CHECK(l.edges == 2); // Druecken + Loslassen, auch bei zu kurzen Druecken
      if (c.z == Zone::None) {
        CHECK(l.zones.empty());
        continue;
      }
      CHECK(l.zones.size() == 1);
      if (l.zones.size() != 1)
        continue;
      CHECK(l.zones[0] == c.z);
      CHECK(l.zones[0] == expectZone(c.hold));
      // genau DEBOUNCE_MS nach dem echten Loslassen
      CHECK(l.at[0] == base + 100 + c.hold + DEBOUNCE_MS);
      // Anzeige direkt vor dem Ereignis zeigt dieselbe Zone
      CHECK(l.zoneBefore[0] == c.z);
    }
  }
}

// ── Zone, Haltedauer und Overlay waehrend des Haltens ─────────────────────────

void testWhileHeld() {
  for (uint32_t base : BASES) {
    Button b;
    b.begin(false, base);
    const uint32_t P = base + 100;
    const uint32_t hold = CANCEL_MS + 500;
    Wave w;
    w.t0 = base;
    w.add(false, 100).add(true, hold).add(false, 300);

    bool heldOk = true, zoneOk = true, overlayOk = true, pressedOk = true;
    // Zone direkt vor und an jeder Grenze
    const uint32_t bounds[] = {MODE_MS, RADIO_MS, CANCEL_MS};
    Zone atBefore[3] = {}, atBound[3] = {}, before = Zone::Cancel;
    bool ov299 = true, ov300 = false;
    Log log;
    run(b, w, base, w.length(), 1, log, [&](uint32_t now) {
      uint32_t off = now - P; // ueberlaufsicher
      bool inPress = off < hold + DEBOUNCE_MS;
      bool shouldBePressed = inPress && off >= DEBOUNCE_MS;
      if (b.pressed() != shouldBePressed)
        pressedOk = false;
      if (!shouldBePressed) {
        if (b.heldMs(now) != 0 || b.zone(now) != Zone::None || b.overlay(now)) {
          heldOk = zoneOk = overlayOk = false;
        }
        if (now - base == 100 + DEBOUNCE_MS - 1)
          before = b.zone(now);
        return;
      }
      uint32_t expHeld =
          off < hold ? off : hold; // nach dem Loslassen eingefroren
      if (b.heldMs(now) != expHeld)
        heldOk = false;
      Zone ez = expHeld < MODE_MS ? Zone::Short : expectZone(expHeld);
      if (b.zone(now) != ez)
        zoneOk = false;
      if (b.overlay(now) != (expHeld >= OVERLAY_MS))
        overlayOk = false;
      if (off == OVERLAY_MS - 1)
        ov299 = b.overlay(now);
      if (off == OVERLAY_MS)
        ov300 = b.overlay(now);
      for (int i = 0; i < 3; i++) {
        if (off == bounds[i] - 1)
          atBefore[i] = b.zone(now);
        if (off == bounds[i])
          atBound[i] = b.zone(now);
      }
    });
    CHECK(pressedOk);
    CHECK(heldOk);
    CHECK(zoneOk);
    CHECK(overlayOk);
    CHECK(before == Zone::None); // vor der Entprellung noch keine Zone
    CHECK(!ov299);
    CHECK(ov300);
    CHECK(atBefore[0] == Zone::Short);
    CHECK(atBound[0] == Zone::Mode);
    CHECK(atBefore[1] == Zone::Mode);
    CHECK(atBound[1] == Zone::Radio);
    CHECK(atBefore[2] == Zone::Radio);
    CHECK(atBound[2] == Zone::Cancel);
    CHECK(log.zones.size() == 1 && log.zones[0] == Zone::Cancel);
    CHECK(log.edges == 2);
  }
}

// ── Prellen ───────────────────────────────────────────────────────────────────

// Fest vorgegebene Prell-Bursts beim Druecken und Loslassen.
void testBounceBursts() {
  for (uint32_t base : BASES) {
    Wave w;
    w.t0 = base;
    w.add(false, 100);
    w.add(true, 2).add(false, 1).add(true, 3).add(false, 5).add(true, 1).add(
        false, 2); // 14 ms
    const uint32_t rise = base + 100 + 14;
    w.add(true, 800);
    w.add(false, 1).add(true, 4).add(false, 2).add(true, 1).add(false, 3).add(
        true, 6); // 17 ms
    const uint32_t fall = rise + 800 + 17;
    w.add(false, 500);

    Button b;
    b.begin(false, base);
    Log l;
    run(b, w, base, w.length(), 1, l);
    CHECK(l.edges == 2);
    CHECK(l.zones.size() == 1);
    if (l.zones.size() != 1)
      continue;
    CHECK(l.zones[0] == Zone::Short);
    CHECK(l.at[0] == fall + DEBOUNCE_MS);
    CHECK(l.zoneBefore[0] == Zone::Short);
  }
}

// Nach Halten in der Radio-Zone prellt das Loslassen: genau ein Radio, kein
// zusaetzliches Short.
void testReleaseBounceAfterRadio() {
  for (uint32_t base : BASES) {
    // a) nur Prellen beim Loslassen, b) plus spaete Spitze < DEBOUNCE_MS,
    // c) plus spaete Spitze >= DEBOUNCE_MS aber < MIN_PRESS_MS
    for (int variant = 0; variant < 3; variant++) {
      Wave w;
      w.t0 = base;
      w.add(false, 100).add(true, 3).add(false, 2).add(true, RADIO_MS + 500);
      w.add(false, 2).add(true, 1).add(false, 3).add(true, 4).add(false, 1).add(
          true, 2);
      w.add(false, 60);
      if (variant == 1)
        w.add(true, DEBOUNCE_MS - 1).add(false, 1);
      if (variant == 2)
        w.add(true, MIN_PRESS_MS - 5).add(false, 1);
      w.add(false, 500);

      Button b;
      b.begin(false, base);
      Log l;
      run(b, w, base, w.length(), 1, l);
      CHECK(l.zones.size() == 1);
      CHECK(l.count(Zone::Radio) == 1);
      CHECK(l.count(Zone::Short) == 0);
      CHECK(l.edges == (variant == 2 ? 4 : 2));
      CHECK(!b.pressed());
    }

    // Auch im langsamen Loop (Prellen faellt zwischen die Polls)
    for (uint32_t phase = 0; phase < 60; phase += 7) {
      Wave w;
      w.t0 = base;
      w.add(false, 100).add(true, 3).add(false, 2).add(true, RADIO_MS + 500);
      w.add(false, 2).add(true, 1).add(false, 3).add(true, 4).add(false, 1).add(
          true, 2);
      w.add(false, 500);
      Button b;
      b.begin(false, base);
      Log l;
      run(b, w, base + phase, w.length(), 60, l);
      CHECK(l.zones.size() == 1 && l.zones[0] == Zone::Radio);
      CHECK(l.edges == 2);
    }
  }
}

// Zufaellige Prell-Muster in verschiedenen Loop-Takten: immer genau ein
// Ereignis.
void testRandomBursts() {
  const uint32_t holds[] = {500, MODE_MS + 500, RADIO_MS + 500,
                            CANCEL_MS + 2000};
  const uint32_t steps[] = {1, 3, 7, 16, 60};
  Lcg r{12345};
  int bad = 0, trials = 0;
  for (uint32_t base : BASES) {
    for (uint32_t hold : holds) {
      for (uint32_t step : steps) {
        for (int i = 0; i < 6; i++) {
          // realistisches Prellen: Burst insgesamt < DEBOUNCE_MS
          Wave w;
          w.t0 = base;
          w.add(false, 100);
          uint32_t pb = addBurst(w, true, r, 6, DEBOUNCE_MS - 5);
          w.add(true, hold);
          uint32_t rb = addBurst(w, false, r, 6, DEBOUNCE_MS - 5);
          w.add(false, 400);
          uint32_t fall = base + 100 + pb + hold + rb;

          Button b;
          b.begin(false, base);
          Log l;
          run(b, w, base + r.next(step), w.length(), step, l);
          trials++;
          bool ok = l.zones.size() == 1 && l.edges == 2 &&
                    l.zones[0] == expectZone(hold) && !b.pressed();
          // Ereignis spaetestens zwei Polls nach Ende des Prellens
          if (ok)
            ok = (uint32_t)(l.at[0] - fall) <= DEBOUNCE_MS + 2 * step;
          if (ok && step == 1)
            ok = l.at[0] == fall + DEBOUNCE_MS && l.zoneBefore[0] == l.zones[0];
          if (!ok)
            bad++;
        }
      }
    }
  }
  CHECK(trials > 0);
  CHECK(bad == 0);

  // Extremes Prellen im 1-ms-Loop: viele Segmente bis knapp unter DEBOUNCE_MS
  bad = 0;
  for (uint32_t base : BASES) {
    for (int i = 0; i < 40; i++) {
      uint32_t hold = 200 + r.next(12000);
      Wave w;
      w.t0 = base;
      w.add(false, 100);
      uint32_t pb = addBurst(w, true, r, DEBOUNCE_MS - 1, 400);
      w.add(true, hold);
      uint32_t rb = addBurst(w, false, r, DEBOUNCE_MS - 1, 400);
      w.add(false, 400);
      uint32_t rise = base + 100 + pb;
      uint32_t fall = rise + hold + rb;

      Button b;
      b.begin(false, base);
      Log l;
      run(b, w, base, w.length(), 1, l);
      bool ok = l.zones.size() == 1 && l.edges == 2 &&
                l.zones[0] == expectZone(fall - rise) &&
                l.at[0] == fall + DEBOUNCE_MS && l.zoneBefore[0] == l.zones[0];
      if (!ok)
        bad++;
    }
  }
  CHECK(bad == 0);
}

// ── Stoerungen kuerzer als DEBOUNCE_MS ────────────────────────────────────────

void testGlitches() {
  for (uint32_t base : BASES) {
    // HIGH-Spitzen im Ruhezustand: nichts passiert
    bool everPressed = false;
    Button b;
    b.begin(false, base);
    Wave w;
    w.t0 = base;
    for (uint32_t len = 1; len < DEBOUNCE_MS; len++)
      w.add(false, 40).add(true, len);
    w.add(false, 100);
    Log l;
    run(b, w, base, w.length(), 1, l, [&](uint32_t now) {
      if (b.pressed() || b.zone(now) != Zone::None || b.overlay(now))
        everPressed = true;
    });
    CHECK(!everPressed);
    CHECK(l.zones.empty());
    CHECK(l.edges == 0);

    // Knapp ueber DEBOUNCE_MS: entprellt (Flanken), aber zu kurz fuer ein
    // Ereignis
    Log l2 = cleanPress(base, DEBOUNCE_MS + 1);
    CHECK(l2.zones.empty());
    CHECK(l2.edges == 2);

    // LOW-Luecken waehrend des Haltens trennen den Druck nicht
    Button b2;
    b2.begin(false, base);
    Wave w2;
    w2.t0 = base;
    w2.add(false, 100);
    uint32_t total = 0;
    for (uint32_t len = 1; len < DEBOUNCE_MS; len++) {
      w2.add(true, 150).add(false, len);
      total += 150 + len;
    }
    w2.add(true, 100).add(false, 300);
    total += 100;
    bool released = false;
    Log l3;
    run(b2, w2, base, w2.length(), 1, l3, [&](uint32_t now) {
      uint32_t off = now - base;
      if (off >= 100 + DEBOUNCE_MS && off < 100 + total && !b2.pressed())
        released = true;
    });
    CHECK(!released);
    CHECK(l3.edges == 2);
    CHECK(l3.zones.size() == 1 && l3.zones[0] == expectZone(total));
    CHECK(l3.zones.size() == 1 && l3.at[0] == base + 100 + total + DEBOUNCE_MS);
  }
}

// Kurze Luecke verbindet zwei Druecke, lange Luecke trennt sie (Doppeltipp).
void testGapsAndDoubleTap() {
  for (uint32_t base : BASES) {
    // Luecke < DEBOUNCE_MS: ein langer Druck (je Haelfte Short, zusammen Mode)
    const uint32_t half = MODE_MS * 7 / 10;
    Wave w;
    w.t0 = base;
    w.add(false, 100)
        .add(true, half)
        .add(false, DEBOUNCE_MS - 10)
        .add(true, half)
        .add(false, 300);
    Button b;
    b.begin(false, base);
    Log l;
    run(b, w, base, w.length(), 1, l);
    CHECK(l.zones.size() == 1 && l.zones[0] == Zone::Mode);
    CHECK(l.edges == 2);

    // Luecke >= DEBOUNCE_MS: zwei kurze Druecke
    Wave w2;
    w2.t0 = base;
    w2.add(false, 100)
        .add(true, 200)
        .add(false, DEBOUNCE_MS + 10)
        .add(true, 200)
        .add(false, 300);
    Button b2;
    b2.begin(false, base);
    Log l2;
    run(b2, w2, base, w2.length(), 1, l2);
    CHECK(l2.zones.size() == 2 && l2.count(Zone::Short) == 2);
    CHECK(l2.edges == 4);
  }
}

// ── Loop-Takt ─────────────────────────────────────────────────────────────────

// Loop schneller als 1 ms: mehrere update() mit demselben Zeitstempel.
void testVeryFastLoop() {
  for (uint32_t base : BASES) {
    for (uint32_t hold :
         {60u, MODE_MS / 2, MODE_MS + 500, RADIO_MS + 500, CANCEL_MS + 1000}) {
      Wave w;
      w.t0 = base;
      w.add(false, 100)
          .add(true, 2)
          .add(false, 1)
          .add(true, hold)
          .add(false, 300);
      Button b;
      b.begin(false, base);
      Log l;
      run(b, w, base, w.length(), 1, l, 4);
      CHECK(l.zones.size() == 1 && l.zones[0] == expectZone(hold));
      CHECK(l.edges == 2);
      CHECK(l.zones.size() == 1 && l.at[0] == base + 103 + hold + DEBOUNCE_MS);
    }
  }
}

// Langsamer Loop (60 ms): Druecke werden erkannt, Zonen passen, Latenz
// begrenzt.
void testSlowLoop() {
  const uint32_t SLOW = 60;
  const uint32_t holds[] = {2 * SLOW + 10,   300,
                            MODE_MS + 500,   RADIO_MS + 500,
                            CANCEL_MS - 500, CANCEL_MS + 1000};
  for (uint32_t base : BASES) {
    for (uint32_t hold : holds) {
      for (uint32_t phase = 0; phase < SLOW; phase += 5) {
        Log l = cleanPress(base, hold, SLOW, phase);
        CHECK(l.edges == 2);
        CHECK(l.zones.size() == 1);
        if (l.zones.size() != 1)
          continue;
        CHECK(l.zones[0] == expectZone(hold));
        // Ereignis spaetestens zwei Polls nach dem echten Loslassen
        uint32_t lat = l.at[0] - (base + 100 + hold);
        CHECK(lat >= DEBOUNCE_MS && lat <= 2 * SLOW);
      }
    }
    // Spitzen, die nur ein einzelner Poll sieht, bleiben folgenlos
    // (Periode 70 ms gegen 60-ms-Takt: jeder 7. Poll trifft eine Spitze)
    Wave w;
    w.t0 = base;
    for (int i = 0; i < 30; i++)
      w.add(false, 60).add(true, 10);
    Button b;
    b.begin(false, base);
    Log l;
    int seenHigh = 0;
    bool everPressed = false;
    run(b, w, base, w.length(), SLOW, l, [&](uint32_t now) {
      seenHigh += w.at(now);
      if (b.pressed())
        everPressed = true;
    });
    CHECK(seenHigh > 0);
    CHECK(!everPressed);
    CHECK(l.zones.empty());
    CHECK(l.edges == 0);
  }
}

// ── Boot-Druck ────────────────────────────────────────────────────────────────

void testBootPress() {
  for (uint32_t base : BASES) {
    // Weck-Druck in der Radio-Zone gehalten, mit Prellen beim Loslassen
    const uint32_t BOOT_HOLD = RADIO_MS + 1000;
    Button b;
    b.begin(true, base);
    CHECK(b.pressed());
    CHECK(!b.takeEdge());
    CHECK(b.zone(base) == Zone::None);
    CHECK(!b.overlay(base + 1000));
    Wave w;
    w.t0 = base;
    w.add(true, BOOT_HOLD)
        .add(false, 2)
        .add(true, 3)
        .add(false, 1)
        .add(true, 2)
        .add(false, 400);
    const uint32_t fall = base + BOOT_HOLD + 8;
    bool shown = false, notPressed = false;
    Log l;
    run(b, w, base, w.length(), 1, l, [&](uint32_t now) {
      if (b.zone(now) != Zone::None || b.overlay(now) || b.heldMs(now) != 0)
        shown = true;
      if ((uint32_t)(now - base) < BOOT_HOLD + 8 + DEBOUNCE_MS && !b.pressed())
        notPressed = true;
    });
    CHECK(!shown);
    CHECK(!notPressed); // physisch gedrueckt: pressed() bleibt true
    CHECK(l.zones.empty());
    CHECK(l.edges == 1); // nur das Loslassen zaehlt als Aktivitaet
    CHECK(!b.pressed());

    // Danach funktioniert ein normaler Druck
    uint32_t t = fall + 400;
    CHECK(b.update(true, t) == Zone::None);
    CHECK(b.update(true, t + DEBOUNCE_MS) == Zone::None);
    CHECK(b.pressed());
    CHECK(b.takeEdge());
    CHECK(b.zone(t + 100) == Zone::Short);
    CHECK(b.update(false, t + 200) == Zone::None);
    CHECK(b.update(false, t + 200 + DEBOUNCE_MS) == Zone::Short);
    CHECK(b.takeEdge());

    // LOW-Stoerung < DEBOUNCE_MS beendet den ignorierten Druck nicht
    Button b2;
    b2.begin(true, base);
    Wave w2;
    w2.t0 = base;
    w2.add(true, 2000)
        .add(false, DEBOUNCE_MS - 1)
        .add(true, 4000)
        .add(false, 300);
    Log l2;
    run(b2, w2, base, w2.length(), 1, l2);
    CHECK(l2.zones.empty());
    CHECK(l2.edges == 1);
    CHECK(!b2.pressed());

    // Schon beim ersten Poll losgelassen: eine Flanke, kein Ereignis
    Button b3;
    b3.begin(true, base);
    CHECK(b3.update(false, base + 5) == Zone::None);
    CHECK(b3.pressed());
    CHECK(b3.update(false, base + 5 + DEBOUNCE_MS) == Zone::None);
    CHECK(!b3.pressed());
    CHECK(b3.takeEdge());
    CHECK(!b3.takeEdge());

    // Langsamer Loop: Boot-Druck ebenfalls ohne Ereignis
    Button b4;
    b4.begin(true, base);
    Wave w4;
    w4.t0 = base;
    w4.add(true, 9000).add(false, 500);
    Log l4;
    run(b4, w4, base, w4.length(), 60, l4);
    CHECK(l4.zones.empty());
    CHECK(l4.edges == 1);

    // begin(false) bei gedruecktem Taster: normaler Druck ab dem ersten Poll
    Button b5;
    b5.begin(false, base);
    CHECK(b5.update(true, base + 10) == Zone::None);
    CHECK(b5.update(true, base + 10 + DEBOUNCE_MS) == Zone::None);
    CHECK(b5.pressed());
    CHECK(b5.heldMs(base + 10 + DEBOUNCE_MS) == DEBOUNCE_MS);
    CHECK(b5.update(false, base + 10 + MODE_MS) == Zone::None);
    CHECK(b5.update(false, base + 10 + MODE_MS + DEBOUNCE_MS) == Zone::Mode);
  }
}

// begin() setzt einen laufenden Zustand vollstaendig zurueck.
void testReBegin() {
  Button b;
  b.begin(false, 0);
  b.update(true, 100);
  b.update(true, 200);
  CHECK(b.pressed());
  b.begin(false, 5000);
  CHECK(!b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.zone(5000) == Zone::None);
  CHECK(b.update(true, 5001) ==
        Zone::None); // neuer, noch nicht entprellter Druck
  CHECK(!b.pressed());
  CHECK(b.update(false, 5010) == Zone::None);
  CHECK(b.update(false, 6000) == Zone::None);
  CHECK(!b.takeEdge());

  // begin(true) mitten in einem Druck: der Rest wird ignoriert
  b.update(true, 7000);
  b.update(true, 7100);
  CHECK(b.pressed());
  b.begin(true, 7200);
  CHECK(b.zone(7300) == Zone::None);
  b.update(false, 9000);
  CHECK(b.update(false, 9000 + DEBOUNCE_MS) == Zone::None);
  CHECK(b.takeEdge());
}

// ── Flanken-Flag ──────────────────────────────────────────────────────────────

void testEdgeFlag() {
  Button b;
  b.begin(false, 0);
  CHECK(!b.takeEdge());
  // Druecken und Loslassen ohne Abholen: ein gesammeltes Flag
  b.update(true, 10);
  b.update(true, 10 + DEBOUNCE_MS);
  b.update(false, 400);
  CHECK(b.update(false, 400 + DEBOUNCE_MS) == Zone::Short);
  CHECK(b.takeEdge());
  CHECK(!b.takeEdge());
  // Stoerung setzt kein Flag
  b.update(true, 1000);
  b.update(false, 1000 + DEBOUNCE_MS - 1);
  b.update(false, 2000);
  CHECK(!b.takeEdge());
  // Zu kurzer Druck: kein Ereignis, aber beide Flanken sind Aktivitaet
  b.update(true, 3000);
  b.update(true, 3000 + DEBOUNCE_MS);
  CHECK(b.takeEdge());
  b.update(false, 3000 + MIN_PRESS_MS - 1);
  CHECK(!b.takeEdge());
  CHECK(b.update(false, 3000 + MIN_PRESS_MS - 1 + DEBOUNCE_MS) == Zone::None);
  CHECK(b.takeEdge());
  CHECK(!b.takeEdge());
}

// ── Ueberlauf und ungueltige Zeitstempel ──────────────────────────────────────

void testWrapAround() {
  // Entprellung genau ueber den Ueberlauf
  Button b;
  b.begin(false, 0xFFFFFFE0u);
  CHECK(b.update(true, 0xFFFFFFF0u) == Zone::None);
  CHECK(b.update(true, 0xFFFFFFF0u + DEBOUNCE_MS - 1) == Zone::None);
  CHECK(!b.pressed());
  CHECK(b.update(true, 0xFFFFFFF0u + DEBOUNCE_MS) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.heldMs(0xFFFFFFF0u + DEBOUNCE_MS) == DEBOUNCE_MS);
  CHECK(b.heldMs(0xFFFFFFF0u + MODE_MS) == MODE_MS);
  CHECK(b.zone(0xFFFFFFF0u + MODE_MS - 1) == Zone::Short);
  CHECK(b.zone(0xFFFFFFF0u + MODE_MS) == Zone::Mode);
  CHECK(b.overlay(0xFFFFFFF0u + OVERLAY_MS));
  CHECK(!b.overlay(0xFFFFFFF0u + OVERLAY_MS - 1));
  CHECK(b.update(false, 0xFFFFFFF0u + RADIO_MS) == Zone::None);
  CHECK(b.update(false, 0xFFFFFFF0u + RADIO_MS + DEBOUNCE_MS) == Zone::Radio);

  // Druck beginnt kurz vor dem Ueberlauf, Loslassen exakt bei 0
  Button c;
  c.begin(false, 0xFFFFF000u);
  c.update(true, 0u - MODE_MS);
  c.update(true, 0u - MODE_MS + DEBOUNCE_MS);
  CHECK(c.pressed());
  CHECK(c.zone(0xFFFFFFFFu) == Zone::Short);
  CHECK(c.zone(0u) == Zone::Mode);
  c.update(false, 0u);
  CHECK(c.update(false, DEBOUNCE_MS) == Zone::Mode);
}

// Zeitstempel aus der Vergangenheit (z. B. veraltetes now) sind ungueltig:
// sie gelten nicht als stabil und ergeben keine riesige Haltedauer.
void testStaleTimestamps() {
  Button b;
  b.begin(false, 1000);
  CHECK(b.update(true, 1000) == Zone::None);
  CHECK(b.update(true, 990) == Zone::None); // Zeit rueckwaerts
  CHECK(!b.pressed());
  CHECK(!b.takeEdge());
  CHECK(b.update(true, 1000 + DEBOUNCE_MS) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.takeEdge());
  CHECK(b.heldMs(900) == 0);
  CHECK(b.zone(900) == Zone::Short);
  CHECK(!b.overlay(900));
  CHECK(b.update(false, 1500) == Zone::None);
  CHECK(b.update(false, 1400) == Zone::None);
  CHECK(b.pressed());
  CHECK(b.update(false, 1500 + DEBOUNCE_MS) == Zone::Short);
  CHECK(!b.pressed());
}

// ── Weitere Grenz- und Robustheitsfaelle ──────────────────────────────────────

// Boot-Druck, erster Poll erst lange nach begin() (Setup dauert): ein einzelner
// LOW-Wert darf den ignorierten Druck nicht sofort beenden, die Entprellung
// laeuft ab dem ersten LOW-Wert, nicht ab begin().
void testBootFirstPollLate() {
  for (uint32_t base : BASES) {
    Button b;
    b.begin(true, base);
    CHECK(b.update(false, base + 200) ==
          Zone::None); // LOW-Spitze beim ersten Poll
    CHECK(b.pressed());
    CHECK(!b.takeEdge());
    CHECK(b.update(true, base + 201) == Zone::None); // weiter gehalten
    CHECK(b.update(true, base + 1000) == Zone::None);
    CHECK(b.pressed());
    CHECK(!b.takeEdge());
    CHECK(b.zone(base + 1000) == Zone::None);
    CHECK(b.heldMs(base + 1000) == 0);
    CHECK(!b.overlay(base + 1000));
    // echtes Loslassen: genau DEBOUNCE_MS nach dem ersten LOW-Wert
    CHECK(b.update(false, base + 1500) == Zone::None);
    CHECK(b.update(false, base + 1500 + DEBOUNCE_MS - 1) == Zone::None);
    CHECK(b.pressed());
    CHECK(b.update(false, base + 1500 + DEBOUNCE_MS) == Zone::None);
    CHECK(!b.pressed());
    CHECK(b.takeEdge());

    // Gleich beim ersten (spaeten) Poll LOW und dann LOW: Entprellung ab dem
    // Poll
    Button c;
    c.begin(true, base);
    CHECK(c.update(false, base + 500) == Zone::None);
    CHECK(c.pressed());
    CHECK(c.update(false, base + 500 + DEBOUNCE_MS - 1) == Zone::None);
    CHECK(c.pressed());
    CHECK(c.update(false, base + 500 + DEBOUNCE_MS) == Zone::None);
    CHECK(!c.pressed());
    CHECK(c.takeEdge());

    // begin(false), erster (spaeter) Poll HIGH: ein Wert reicht nicht
    Button d;
    d.begin(false, base);
    CHECK(d.update(true, base + 500) == Zone::None);
    CHECK(!d.pressed());
    CHECK(!d.takeEdge());
    CHECK(d.update(true, base + 500 + DEBOUNCE_MS - 1) == Zone::None);
    CHECK(!d.pressed());
    CHECK(d.update(true, base + 500 + DEBOUNCE_MS) == Zone::None);
    CHECK(d.pressed());
    CHECK(d.heldMs(base + 500 + DEBOUNCE_MS) == DEBOUNCE_MS);
  }
}

// Exakte Grenze der Entprellung im 1-ms-Loop: ein Pegel, der nur in
// DEBOUNCE_MS Abtastwerten anliegt, gilt nicht; DEBOUNCE_MS + 1 Werte gelten.
void testDebounceBoundaryPulse() {
  for (uint32_t base : BASES) {
    for (uint32_t n = DEBOUNCE_MS - 1; n <= DEBOUNCE_MS + 2; n++) {
      const bool counts = n > DEBOUNCE_MS;
      // HIGH-Puls im Ruhezustand
      Button b;
      b.begin(false, base);
      const uint32_t t = base + 50;
      bool wasPressed = false;
      for (uint32_t k = 0; k < n; k++) {
        CHECK(b.update(true, t + k) == Zone::None);
        wasPressed = wasPressed || b.pressed();
      }
      CHECK(b.update(false, t + n) == Zone::None);
      CHECK(wasPressed == counts);
      CHECK(b.takeEdge() == counts);
      Zone ev = Zone::None;
      for (uint32_t k = 1; k <= DEBOUNCE_MS + 5; k++) {
        Zone z = b.update(false, t + n + k);
        if (z != Zone::None)
          ev = z;
      }
      CHECK(ev == Zone::None); // hoechstens DEBOUNCE_MS + 2 lang: zu kurz
      CHECK(!b.pressed());
      CHECK(b.takeEdge() == counts);

      // LOW-Luecke waehrend des Haltens
      Button c;
      c.begin(false, base);
      const uint32_t H = MODE_MS / 2; // bleibt Short
      for (uint32_t k = 0; k <= H; k++)
        c.update(true, t + k);
      CHECK(c.pressed());
      CHECK(c.takeEdge());
      const uint32_t g = t + H + 1;
      Zone gapEv = Zone::None;
      bool released = false;
      for (uint32_t k = 0; k < n; k++) {
        Zone z = c.update(false, g + k);
        if (z != Zone::None)
          gapEv = z;
        released = released || !c.pressed();
      }
      CHECK(released == counts);
      CHECK(gapEv == (counts ? Zone::Short : Zone::None));
      CHECK(c.takeEdge() == counts);
      // Haltedauer bis zum Beginn der Luecke (ab Beginn des stabilen HIGH)
      if (!counts)
        CHECK(c.heldMs(g + n - 1) == H + 1);
    }
  }
}

// Sehr langer Druck mit seltenen Polls (20 Tage, noch unter 2^31 ms) und eine
// lange Ruhepause zwischen zwei Polls (> 2^31 ms): nichts klemmt oder springt.
void testLongTimes() {
  for (uint32_t base : BASES) {
    Button b;
    b.begin(false, base);
    const uint32_t P = base + 10;
    b.update(true, P);
    b.update(true, P + DEBOUNCE_MS);
    CHECK(b.pressed());
    const uint32_t HOUR = 3600u * 1000u, DAYS20 = 20u * 24u * HOUR;
    bool ok = true;
    uint32_t last = 0;
    for (uint32_t k = HOUR; k <= DAYS20; k += HOUR) {
      if (b.update(true, P + k) != Zone::None)
        ok = false;
      uint32_t h = b.heldMs(P + k);
      if (h != k || h < last || b.zone(P + k) != Zone::Cancel ||
          !b.overlay(P + k))
        ok = false;
      last = h;
    }
    CHECK(ok);
    CHECK(b.update(false, P + DAYS20 + 1) == Zone::None);
    CHECK(b.heldMs(P + DAYS20 + 20) == DAYS20 + 1);
    CHECK(b.update(false, P + DAYS20 + 1 + DEBOUNCE_MS) == Zone::Cancel);
    CHECK(!b.pressed());

    // Ruhepause > 2^31 ms zwischen zwei Polls, danach normaler Druck
    Button c;
    c.begin(false, base);
    c.update(false, base + 5);
    const uint32_t t = base + 0x90000000u;
    CHECK(c.update(false, t) == Zone::None);
    CHECK(!c.pressed());
    CHECK(!c.takeEdge());
    CHECK(c.update(true, t + 1) == Zone::None);
    CHECK(!c.pressed());
    CHECK(c.update(true, t + 1 + DEBOUNCE_MS) == Zone::None);
    CHECK(c.pressed());
    CHECK(c.update(false, t + MODE_MS + 500) == Zone::None);
    CHECK(c.update(false, t + MODE_MS + 500 + DEBOUNCE_MS) == Zone::Mode);
  }
}

// Unabhaengiges Referenzmodell auf 64-bit-Zeit (ohne Ueberlauf), direkt aus dem
// Header formuliert: ein Pegel gilt, sobald der laufende Lauf gleicher
// Abtastwerte DEBOUNCE_MS umfasst; Haltedauer = Beginn des stabilen LOW minus
// Beginn des stabilen HIGH; vor dem Ereignis friert die Anzeige ein.
struct RefButton {
  bool level = false, deb = false, ign = false;
  uint64_t runStart = 0, pressStart = 0;

  void begin(bool lvl, uint64_t t) {
    level = deb = ign = lvl;
    runStart = pressStart = t;
  }
  Zone update(bool lvl, uint64_t t) {
    if (lvl != level) {
      level = lvl;
      runStart = t;
    }
    if (level == deb || t - runStart < DEBOUNCE_MS)
      return Zone::None;
    deb = level;
    if (deb) {
      pressStart = runStart;
      return Zone::None;
    }
    if (ign) {
      ign = false;
      return Zone::None;
    }
    return expectZone((uint32_t)(runStart - pressStart));
  }
  uint64_t held(uint64_t t) const {
    if (!deb || ign)
      return 0;
    return (level ? t : runStart) - pressStart;
  }
};

// Zufaelliger Rohpegel (Ruhe, Prellen, Halten nahe den Zonengrenzen,
// Stoerungen) und unregelmaessiger Poll-Takt inkl. mehrfacher Polls pro
// Millisekunde. Der Taster bekommt (uint32_t)(base + t), das Modell t ohne
// Ueberlauf.
void testFuzzAgainstModel() {
  Lcg r{0xB0771E5u};
  const uint32_t holdsNear[] = {MIN_PRESS_MS, OVERLAY_MS, MODE_MS, RADIO_MS,
                                CANCEL_MS};
  int mismatches = 0, invariantFails = 0, events = 0, trials = 0;
  for (int trial = 0; trial < 160; trial++) {
    const uint32_t base =
        trial % 5 == 4 ? r.next(0x7FFFFFFFu) * 2u : BASES[trial % 5 % 4];
    const bool boot = trial % 4 == 0;

    // Rohpegel als Segmentliste (relativ zu t = 0)
    Wave w;
    w.t0 = 0;
    if (boot)
      w.add(true, 50 + r.next(4000));
    for (int p = 0; p < 6; p++) {
      w.add(false, 20 + r.next(300));
      if (r.next(3) == 0)
        w.add(true, 1 + r.next(DEBOUNCE_MS + 10)).add(false, 1 + r.next(80));
      addBurst(w, true, r, 8, 40);
      uint32_t hold;
      switch (r.next(4)) {
      case 0:
        hold = 1 + r.next(120);
        break;
      case 1:
        hold = holdsNear[r.next(5)] - 15 + r.next(30);
        break;
      case 2:
        hold = 100 + r.next(9000);
        break;
      default:
        hold = 200 + r.next(1500);
        break;
      }
      // Halten, evtl. mit kurzen LOW-Stoerungen
      uint32_t left = hold;
      while (left > 0) {
        uint32_t part = 1 + r.next(left);
        w.add(true, part);
        left -= part;
        if (left > 0 && r.next(4) == 0)
          w.add(false, 1 + r.next(DEBOUNCE_MS + 5));
      }
      addBurst(w, false, r, 8, 40);
    }
    w.add(false, 500);
    const uint32_t len = w.length();

    Button b;
    RefButton m;
    b.begin(w.at(0), base);
    m.begin(w.at(0), 0);
    trials++;

    uint64_t t = 0;
    Zone prevZone = b.zone(base);
    bool prevPressed = b.pressed();
    uint32_t prevHeld = b.heldMs(base);
    bool bootHeld = boot; // ignorierter Weck-Druck bis zum ersten Loslassen
    while (t <= len) {
      const uint32_t now = (uint32_t)(base + t);
      const bool lvl = w.at((uint32_t)t);
      Zone ez = m.update(lvl, t);
      Zone z = b.update(lvl, now);
      bool edge = b.takeEdge();
      if (z != ez || b.pressed() != m.deb || b.heldMs(now) != m.held(t))
        mismatches++;

      // Invarianten, unabhaengig vom Modell
      if (edge != (b.pressed() != prevPressed))
        invariantFails++;
      if (z != Zone::None) {
        events++;
        if (prevPressed == false || b.pressed())
          invariantFails++; // nur beim Loslassen
        if (prevZone != z)
          invariantFails++; // Anzeige vor dem Ereignis = Aktion
      }
      if (prevPressed && !b.pressed())
        bootHeld = false;
      const bool active = b.pressed() && !bootHeld;
      if ((b.zone(now) != Zone::None) != active)
        invariantFails++;
      if (b.overlay(now) != (active && b.heldMs(now) >= OVERLAY_MS))
        invariantFails++;
      if (active && b.zone(now) != (b.heldMs(now) < MODE_MS
                                        ? Zone::Short
                                        : expectZone(b.heldMs(now)))) {
        invariantFails++;
      }
      // Haltedauer steigt waehrend eines Drucks nie ab
      if (b.pressed() && prevPressed && !edge && b.heldMs(now) < prevHeld)
        invariantFails++;

      prevZone = b.zone(now);
      prevPressed = b.pressed();
      prevHeld = b.heldMs(now);

      // Unregelmaessiger Takt: oft 0..2 ms (auch derselbe Zeitstempel),
      // manchmal Pausen
      uint32_t k = r.next(20);
      t += k < 12 ? r.next(3) : (k < 18 ? 3 + r.next(18) : 20 + r.next(70));
    }
  }
  CHECK(trials == 160);
  CHECK(events > 300); // Fuzz erreicht tatsaechlich Ereignisse
  CHECK(mismatches == 0);
  CHECK(invariantFails == 0);
}

} // namespace

int main() {
  testDebounceTiming();
  testDefaultConstructed();
  testReleaseBoundaries();
  testWhileHeld();
  testBounceBursts();
  testReleaseBounceAfterRadio();
  testRandomBursts();
  testGlitches();
  testGapsAndDoubleTap();
  testVeryFastLoop();
  testSlowLoop();
  testBootPress();
  testReBegin();
  testEdgeFlag();
  testWrapAround();
  testStaleTimestamps();
  testBootFirstPollLate();
  testDebounceBoundaryPulse();
  testLongTimes();
  testFuzzAgainstModel();
  return finish("button_core_test");
}
