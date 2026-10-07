// Unit-Tests fuer die reine Waagen-Logik: Glaettung, Stabilitaet, Tara,
// Nullung und Kalibrierung mit einer simulierten Waegezelle (Faktor 708,
// Offset 8e6, reproduzierbares Rauschen) bei 10 und 80 SPS.
#include "check.h"
#include "scale_core.h"
#include <cmath>
#include <cstdint>
#include <limits>

using namespace scale;

namespace {

const float NaN = std::numeric_limits<float>::quiet_NaN();
const float INF = std::numeric_limits<float>::infinity();
constexpr uint32_t WRAP_START =
    0xFFFFF000u; // 4096 ms vor dem millis()-Ueberlauf

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

uint32_t elapsed(uint32_t from, uint32_t to) { return to - from; }

// Reproduzierbares Rauschen: xorshift32 + Box-Muller.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  double uniform() {
    return ((next() >> 8) + 0.5) / 16777216.0; // (0, 1)
  }
  double gauss() {
    double u1 = uniform(), u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
  }
};

// Waegezelle + HX711: ganzzahlige Rohwerte, Rauschen in Gramm, optional eine
// Vibration, die von Sample zu Sample zwischen +vib und -vib springt.
struct Cell {
  double factor = 708.0, offset = 8e6, load = 0.0, noise = 0.3, vib = 0.0;
  uint32_t k = 0;
  Rng rng;
  explicit Cell(uint32_t seed) : rng(seed) {}
  float raw() {
    double v = (k++ & 1) ? vib : -vib;
    return (float)std::llround(offset +
                               factor * (load + v + noise * rng.gauss()));
  }
};

// Festes Sample-Raster (10 oder 80 SPS), Zeit intern in us (80 SPS = 12,5 ms).
struct Sim {
  Core core;
  Cell cell;
  uint32_t start;
  uint64_t us = 0, nextUs;
  uint32_t periodUs;
  bool sensorOn = true;

  Sim(int sps, uint32_t startMs, uint32_t seed)
      : cell(seed), start(startMs), nextUs(1000000u / sps),
        periodUs(1000000u / sps) {
    core.begin(708.0f, 8e6f);
  }
  uint32_t now() const { return start + (uint32_t)(us / 1000); }
  // Springt genau zum naechsten Sample und speist es ein.
  uint32_t sample() {
    us = nextUs;
    nextUs += periodUs;
    core.addSample(cell.raw(), now());
    return now();
  }
  void samplesFor(uint32_t ms) {
    uint64_t end = us + (uint64_t)ms * 1000;
    while (nextUs <= end)
      sample();
    us = end;
  }
  // Loop-Schritt: faellige Samples bekommen den Zeitstempel des Schritts.
  void tick(uint32_t ms) {
    us += (uint64_t)ms * 1000;
    while (nextUs <= us) {
      if (sensorOn)
        core.addSample(cell.raw(), now());
      nextUs += periodUs;
    }
  }
};

// Handgefuetterte Samples ohne Rauschen.
struct Feed {
  Core core;
  uint32_t t;
  explicit Feed(uint32_t start = 10000) : t(start) { core.begin(708.0f, 8e6f); }
  void add(float grams, uint32_t dt = 100) {
    t += dt;
    core.addSample(8e6f + 708.0f * grams, t);
  }
  Reading r() const { return core.reading(t); }
};

} // namespace

// ── Grundlagen ────────────────────────────────────────────────────────────────

static void testBasics() {
  Core c;
  Reading r = c.reading(0);
  CHECK(!r.valid && r.grams == 0.0f && r.spread == 0.0f && !r.stable &&
        r.sps == 0.0f);
  CHECK(c.factor() == 708.0f && c.offset() == 0.0f && c.stableSpread() == 2.0f);
  CHECK(!c.taring());

  c.begin(708.0f, 8e6f);
  c.addSample(8e6f + 708.0f * 100.0f, 1000);
  r = c.reading(1000);
  CHECK(r.valid);
  CHECK(near(r.grams, 100.0, 1e-3));
  CHECK(!r.stable);
  CHECK(r.sps == 0.0f); // ein Sample: Rate unbekannt
  CHECK(r.spread == 0.0f);

  // Negativer Faktor (Zelle verkehrt herum montiert)
  c.begin(-708.0f, 8e6f);
  CHECK(!c.reading(2000).valid); // begin leert den Puffer
  c.addSample(8e6f - 708.0f * 50.0f, 2000);
  CHECK(near(c.reading(2000).grams, 50.0, 1e-3));
  CHECK(c.factor() == -708.0f);
}

static void testSetters() {
  Core c;
  c.begin(708.0f, 8e6f);
  c.setFactor(0.5f);
  c.setFactor(-0.99f);
  c.setFactor(NaN);
  c.setFactor(INF);
  c.setFactor(-INF);
  c.setFactor(0.0f);
  CHECK(c.factor() == 708.0f);
  c.setFactor(-1.0f);
  CHECK(c.factor() == -1.0f);
  c.setFactor(1234.5f);
  CHECK(c.factor() == 1234.5f);

  c.begin(0.0f, 5.0f); // ungueltiger Faktor bleibt beim alten
  CHECK(c.factor() == 1234.5f && c.offset() == 5.0f);
  c.begin(NaN, NaN);
  CHECK(c.factor() == 1234.5f && c.offset() == 5.0f);
  c.setOffset(INF);
  CHECK(c.offset() == 5.0f);
  c.setOffset(-3.0e6f);
  CHECK(c.offset() == -3.0e6f);

  c.setStableSpread(0.5f);
  CHECK(c.stableSpread() == STABLE_SPREAD_MIN_G);
  c.setStableSpread(4.0f);
  CHECK(c.stableSpread() == 4.0f);
  c.setStableSpread(NaN);
  CHECK(c.stableSpread() == STABLE_SPREAD_MIN_G);
  c.setStableSpread(-3.0f);
  CHECK(c.stableSpread() == STABLE_SPREAD_MIN_G);
  c.setStableSpread(1.0f);
  CHECK(c.stableSpread() == 1.0f);

  // Nicht endliche Rohwerte werden ignoriert
  Core d;
  d.begin(708.0f, 8e6f);
  d.addSample(NaN, 100);
  d.addSample(INF, 200);
  d.addSample(-INF, 300);
  CHECK(!d.reading(300).valid);
  d.addSample(8e6f, 400);
  CHECK(d.reading(400).valid && d.reading(400).grams == 0.0f);
  d.addSample(NaN, 500);
  CHECK(d.reading(500).grams == 0.0f);
}

// ── Adaptive Glaettung ────────────────────────────────────────────────────────

static void testFilterAverage() {
  // Mittel seit dem letzten Neustart, begrenzt auf DISPLAY_MS (10 Samples)
  Feed f;
  for (int i = 0; i < 30; i++)
    f.add(i % 2 ? 1.0f : 0.0f);
  CHECK(near(f.r().grams, 0.5, 1e-4));

  // 1,5 g weicht < STEP_G ab: kein Neustart, die Anzeige gleitet
  f.add(1.5f);
  CHECK(near(f.r().grams, 0.65, 1e-4));
  for (int i = 0; i < 4; i++)
    f.add(1.5f);
  CHECK(near(f.r().grams, 1.05, 1e-4));
  for (int i = 0; i < 5; i++)
    f.add(1.5f);
  CHECK(
      near(f.r().grams, 1.5, 1e-4)); // alte Samples sind aelter als DISPLAY_MS

  // Anzeige bezieht sich auf das neueste Sample, nicht auf now
  CHECK(near(f.core.reading(f.t + 5000).grams, 1.5, 1e-4));
  CHECK(!f.core.reading(f.t + 5000).stable);
}

static void testFilterSteps() {
  // Sprung > STEP_G: sofort der neue Wert
  Feed a;
  for (int i = 0; i < 20; i++)
    a.add(0.0f);
  a.add(2.5f);
  CHECK(near(a.r().grams, 2.5, 1e-4));
  a.add(2.5f);
  CHECK(near(a.r().grams, 2.5, 1e-4));
  a.add(-0.1f); // zurueck: wieder Neustart
  CHECK(near(a.r().grams, -0.1, 1e-3));

  // Sprung < STEP_G: gleitend ueber DISPLAY_MS
  Feed b;
  for (int i = 0; i < 20; i++)
    b.add(0.0f);
  b.add(1.9f);
  CHECK(near(b.r().grams, 0.19, 1e-3));

  // Glas wird ueber 300 ms aufgestellt: Anzeige folgt jedem Schritt
  Feed c;
  for (int i = 0; i < 20; i++)
    c.add(0.0f);
  c.add(33.0f);
  CHECK(near(c.r().grams, 33.0, 1e-3));
  c.add(66.0f);
  CHECK(near(c.r().grams, 66.0, 1e-3));
  c.add(100.0f);
  CHECK(near(c.r().grams, 100.0, 1e-3));
  c.add(100.5f);
  CHECK(near(c.r().grams, 100.25, 1e-3));

  // Einzelner Ausreisser: nur dieses eine Sample sichtbar
  Feed d;
  for (int i = 0; i < 20; i++)
    d.add(0.0f);
  d.add(50.0f);
  CHECK(near(d.r().grams, 50.0, 1e-3));
  d.add(0.0f);
  CHECK(near(d.r().grams, 0.0, 1e-3));

  // Nach langer Pause beginnt die Glaettung neu (alte Samples zaehlen nicht)
  Feed e;
  for (int i = 0; i < 20; i++)
    e.add(1.0f);
  e.add(0.0f, 3000);
  CHECK(near(e.r().grams, 0.0, 1e-4));
  e.add(0.2f);
  CHECK(near(e.r().grams, 0.1, 1e-3));
}

static void testFilterNoise(int sps) {
  Sim s(sps, 1000, 7);
  s.cell.load = 50.0;
  s.samplesFor(2000);
  double sq = 0.0, worst = 0.0;
  int n = 0;
  for (int i = 0; i < 10 * sps; i++) {
    uint32_t t = s.sample();
    Reading r = s.core.reading(t);
    double e = r.grams - 50.0;
    sq += e * e;
    if (std::fabs(e) > worst)
      worst = std::fabs(e);
    n++;
    CHECK(r.valid);
  }
  double rms = std::sqrt(sq / n);
  CHECK(rms <
        (sps == 10 ? 0.2 : 0.1)); // Rohrauschen 0,3 g, Anzeige deutlich ruhiger
  CHECK(worst < 0.6);
}

// ── Reaktion auf Glas und Stabilitaet ─────────────────────────────────────────

static void testStepReaction(int sps, uint32_t start, uint32_t warmMs) {
  Sim s(sps, start, 11);
  s.samplesFor(warmMs);
  Reading r = s.core.reading(s.now());
  CHECK(r.valid && r.stable);
  CHECK(std::fabs(r.grams) < 0.5f);
  CHECK(near(r.sps, sps, 0.5));

  // Glas aufgestellt: das erste Sample zeigt schon ~100 g
  s.cell.load = 100.0;
  uint32_t tStep = s.sample();
  r = s.core.reading(tStep);
  CHECK(near(r.grams, 100.0, 1.5));
  CHECK(!r.stable);
  CHECK(r.spread >
        (sps == 10 ? 90.0f : 10.0f)); // 80 SPS: 1 von 8 Samples im Mittel

  // Stabil, sobald das Fenster nur noch Samples mit Glas enthaelt
  uint32_t tStable = 0;
  bool found = false;
  while (elapsed(tStep, s.now()) < 1000) {
    uint32_t t = s.sample();
    if (s.core.reading(t).stable) {
      tStable = t;
      found = true;
      break;
    }
  }
  CHECK(found);
  uint32_t d = elapsed(tStep, tStable);
  if (sps == 10)
    CHECK(d == 400);
  else
    CHECK(d >= 475 && d <= 500);
  r = s.core.reading(tStable);
  CHECK(near(r.grams, 100.0, 0.6));
  CHECK(r.spread <= s.core.stableSpread());

  // Bleibt stabil (Rauschen: ein 5-Sample-Fenster ueberschreitet 2 g nur bei
  // ~6,7 Sigma; ein solcher Ausreisser bleibt 5 Samples im Fenster)
  int unstable = 0;
  for (int i = 0; i < 3 * sps; i++) {
    uint32_t t = s.sample();
    if (!s.core.reading(t).stable)
      unstable++;
  }
  CHECK(unstable <= 5);

  // Glas abgehoben: sofort ~0
  s.cell.load = 0.0;
  uint32_t t = s.sample();
  r = s.core.reading(t);
  CHECK(near(r.grams, 0.0, 1.5));
  CHECK(!r.stable);
}

static void testCoverage() {
  // Frischer Puffer: 10 SPS braucht 5 Samples (400 ms + 1,5 Intervalle)
  {
    Feed f;
    for (int i = 1; i <= 4; i++) {
      f.add(0.0f);
      CHECK(!f.r().stable);
    }
    f.add(0.0f);
    CHECK(f.r().stable);
    CHECK(f.core.reading(f.t + 50).stable); // auch zwischen den Samples
  }
  // 80 SPS braucht 40 Samples
  {
    Sim s(80, 1000, 3);
    s.cell.noise = 0.0;
    s.samplesFor(1000);
    s.core.clear();
    CHECK(!s.core.reading(s.now()).valid);
    int k = 0;
    while (k < 100) {
      uint32_t t = s.sample();
      k++;
      if (s.core.reading(t).stable)
        break;
    }
    CHECK(k == 40);
  }
  // Weniger als 3 Samples im Fenster (4 SPS): nie stabil
  {
    Sim s(4, 1000, 3);
    s.cell.noise = 0.0;
    bool any = false;
    for (int i = 0; i < 20; i++) {
      uint32_t t = s.sample();
      if (s.core.reading(t).stable)
        any = true;
    }
    CHECK(!any);
  }
  // Sensor liefert nichts mehr: nach 300 ms (nur noch 2 Samples) nicht stabil
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    CHECK(f.r().stable);
    CHECK(f.core.reading(f.t + 250).stable);
    CHECK(!f.core.reading(f.t + 300).stable);
    CHECK(!f.core.reading(f.t + 5000).stable);
    CHECK(f.core.reading(f.t + 5000).valid); // Daten sind da, nur nicht frisch
  }
  // Aussetzer ohne clear(): Luecke zaehlt hoechstens STABLE_MS / 2
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    f.add(0.0f, 3000);
    f.add(0.0f);
    f.add(0.0f);
    CHECK(!f.r().stable); // 3 Samples, 200 ms
    f.add(0.0f);
    CHECK(f.r().stable); // 300 ms + 250 ms Luecke
  }
  // Zeitstempel-Jitter und verlorene Samples duerfen nicht flattern
  {
    Rng rng(99);
    Feed f;
    for (int i = 0; i < 300; i++) {
      uint32_t dt = 100 + (uint32_t)(rng.next() % 61) - 30; // 70..130 ms
      if (i % 7 == 3)
        dt += 100; // Sample verpasst
      f.add(0.05f * (float)(rng.next() % 5), dt);
      if (i >= 10) {
        CHECK(f.r().stable);
        CHECK(f.core.reading(f.t + 20).stable);
      }
    }
  }
  {
    Rng rng(98);
    Feed f;
    for (int i = 0; i < 1000; i++) {
      uint32_t dt = 12 + (i & 1);
      if (i % 5 == 2)
        dt = 25; // Loop kurz blockiert
      if (i % 13 == 6)
        dt = 37; // zwei verpasst
      f.add(0.05f * (float)(rng.next() % 5), dt);
      if (i >= 60)
        CHECK(f.r().stable);
    }
  }
}

static void testSpreadAndSps() {
  Feed f;
  const float g[5] = {0.0f, 0.5f, -0.25f, 1.0f, 0.2f};
  for (float v : g)
    f.add(v);
  Reading r = f.r();
  CHECK(near(r.spread, 1.25, 1e-3));
  CHECK(near(r.sps, 10.0, 1e-3));
  CHECK(r.stable); // 1,25 g <= 2 g
  f.core.setStableSpread(1.0f);
  CHECK(!f.r().stable);
  f.core.setStableSpread(2.0f);

  // Nur die letzten STABLE_MS zaehlen
  for (int i = 0; i < 5; i++)
    f.add(0.0f);
  CHECK(near(f.r().spread, 0.0, 1e-4));

  // Gemessene Rate
  Sim s10(10, 1000, 5);
  s10.samplesFor(1500);
  CHECK(near(s10.core.reading(s10.now()).sps, 10.0, 0.05));
  Sim s80(80, 1000, 5);
  s80.samplesFor(1500);
  CHECK(near(s80.core.reading(s80.now()).sps, 80.0, 0.5));
}

static void testSpreadRateIndependent() {
  // 80 SPS, Vibration +-0,9 g von Sample zu Sample (1,8 g < STEP_G, die
  // Glaettung laeuft durch): mittelt sich in 100 ms weg
  Feed a;
  for (int i = 0; i < 120; i++)
    a.add(i % 2 ? 0.9f : -0.9f, 12 + (i & 1));
  Reading r = a.r();
  CHECK(r.spread < 0.3f);
  CHECK(r.stable);
  CHECK(std::fabs(r.grams) < 0.1f);
  CHECK(near(r.sps, 80.0, 0.5));
  float mean = 0.0f, spread = 0.0f;
  int n = 0;
  CHECK(a.core.rawWindow(a.t, &mean, &spread, &n));
  CHECK(near(spread, r.spread * 708.0, 1.0));
  CHECK(n == 40);
  a.core.setStableSpread(STABLE_SPREAD_MIN_G);
  CHECK(a.r().stable);

  // +-1,5 g: jedes Sample weicht > STEP_G vom vorigen ab, die Glaettung
  // startet jedes Mal neu und die Anzeige springt um 3 g. Die Spanne zeigt
  // das (Mittel seit dem Neustart = Einzelsample), bleibt aber unter 2 g.
  Feed v;
  for (int i = 0; i < 120; i++)
    v.add(i % 2 ? 1.5f : -1.5f, 12 + (i & 1));
  r = v.r();
  CHECK(near(r.grams, 1.5, 1e-3));
  CHECK(near(r.spread, 1.5, 0.01));
  CHECK(r.stable);
  v.core.setStableSpread(STABLE_SPREAD_MIN_G);
  CHECK(!v.r().stable);

  // Dieselbe Vibration bei 10 SPS ist echte Unruhe
  Feed b;
  for (int i = 0; i < 20; i++)
    b.add(i % 2 ? 1.5f : -1.5f);
  CHECK(near(b.r().spread, 3.0, 1e-3));
  CHECK(!b.r().stable);

  // Langsame Drift bei 80 SPS (3 g in 500 ms) bleibt sichtbar
  Feed c;
  for (int i = 0; i < 80; i++)
    c.add(0.0f, 12 + (i & 1));
  for (int i = 0; i < 40; i++)
    c.add(0.075f * (float)i, 12 + (i & 1));
  CHECK(c.r().spread > 2.0f);
  CHECK(!c.r().stable);
}

// Kleiner Sprung (> STEP_G): ab dem ersten Sample nicht stabil, bei 10 und 80
// SPS gleich. Die gleitenden 100-ms-Mittel duerfen ihn bei 80 SPS nicht
// verduennen (vorher: 5 g zaehlten als 0,6 g, 3 Samples lang "stabil").
static void testSmallStep(int sps, uint32_t start) {
  const float steps[] = {2.5f, 3.0f, 5.0f, 10.0f, 15.0f, -5.0f};
  for (float S : steps) {
    Feed f(start);
    auto dt = [&](int i) -> uint32_t {
      return sps == 80 ? 12u + (uint32_t)(i & 1) : 100u;
    };
    for (int i = 0; i < 2 * sps; i++)
      f.add(0.0f, dt(i));
    CHECK(f.r().stable);

    f.add(S, dt(0));
    const uint32_t tStep = f.t;
    Reading r = f.r();
    CHECK(near(r.grams, S, 1e-3)); // Glaettung neu gestartet
    CHECK(!r.stable);
    CHECK(r.spread >= std::fabs(S) - 0.01f);
    float mean = 0.0f, spread = 0.0f;
    int n = 0;
    f.core.rawWindow(f.t, &mean, &spread, &n);
    CHECK(spread >= 708.0f * (std::fabs(S) - 0.01f));
    float off = f.core.offset();
    CHECK(!f.core.zeroFromWindow(INF, 2.0f, f.t)); // Sprung im Fenster
    CHECK(f.core.offset() == off);

    // Unruhig, bis das Fenster (fast) nur noch den neuen Wert enthaelt. Bei
    // 80 SPS verduennt das aelteste 100-ms-Mittel die letzten alten Samples:
    // bei 2,5 g ist das bis zu 7 Samples frueher stabil.
    uint32_t tStable = 0;
    for (int i = 1; i < 3 * sps; i++) {
      f.add(S, dt(i));
      Reading q = f.r();
      CHECK(near(q.grams, S, 1e-3));
      if (q.stable) {
        tStable = f.t;
        break;
      }
    }
    CHECK(tStable != 0);
    uint32_t d = elapsed(tStep, tStable);
    if (sps == 10)
      CHECK(d == 400);
    else
      CHECK(d >= 400 && d <= 500);
  }

  // Groessere stableSpread (4 g): 3 g Sprung startet die Glaettung neu, gilt
  // aber bei beiden Raten weiter als stabil (Spanne 3 g <= 4 g)
  Feed g(start);
  g.core.setStableSpread(4.0f);
  for (int i = 0; i < 2 * sps; i++)
    g.add(0.0f, sps == 80 ? 12u + (uint32_t)(i & 1) : 100u);
  g.add(3.0f, sps == 80 ? 12u : 100u);
  CHECK(near(g.r().grams, 3.0, 1e-3));
  CHECK(near(g.r().spread, 3.0, 0.01));
  CHECK(g.r().stable);
}

// Dasselbe mit Rauschen (0,3 g) bei 80 SPS: kein "stabil" kurz nach 5 g
static void testSmallStepNoise() {
  for (uint32_t seed = 1; seed <= 20; seed++) {
    Sim s(80, 1000, 700 + seed);
    s.samplesFor(2000);
    CHECK(s.core.reading(s.now()).stable);
    s.cell.load = 5.0;
    uint32_t tStep = s.sample();
    int early = 0;
    uint32_t tStable = 0;
    while (elapsed(tStep, s.now()) < 1000) {
      uint32_t t = s.now();
      Reading r = s.core.reading(t);
      if (r.stable) {
        if (elapsed(tStep, t) < 400)
          early++;
        else if (!tStable)
          tStable = t;
      }
      s.sample();
    }
    CHECK(early == 0);
    CHECK(tStable != 0 && elapsed(tStep, tStable) <= 520);
    CHECK(near(s.core.reading(s.now()).grams, 5.0, 0.3));
  }
}

// ── Tara ──────────────────────────────────────────────────────────────────────

static void testTare(int sps, uint32_t start, uint32_t warmMs) {
  Sim s(sps, start, 21);
  s.cell.load = 37.5;
  s.samplesFor(warmMs);
  uint32_t T = s.now() + 3; // zwischen zwei Samples
  s.core.startTare(T);
  CHECK(s.core.taring());
  CHECK(!s.core.reading(T).valid);

  uint32_t tDone = 0, t0 = s.now();
  while (s.core.taring() && elapsed(t0, s.now()) < 3000) {
    tDone = s.sample();
    if (s.core.taring())
      CHECK(!s.core.reading(tDone).valid);
  }
  CHECK(!s.core.taring());
  uint32_t d = elapsed(T, tDone);
  if (sps == 10)
    CHECK(d >= 500 && d <= 600); // ~0,6 s
  else
    CHECK(d >= TARE_DISCARD_MS + 4 * 12 && d <= 200);
  CHECK(near(s.core.offset(), 8e6 + 708.0 * 37.5, 708.0 * 0.6)); // Mittel aus 5

  // Anzeige sofort auf 0, bei 10 SPS auch sofort stabil
  Reading r = s.core.reading(tDone);
  CHECK(r.valid);
  CHECK(std::fabs(r.grams) < 0.01f);
  if (sps == 10)
    CHECK(r.stable);

  int late = 0, lateUnstable = 0;
  for (int i = 0; i < 2 * sps; i++) {
    uint32_t t = s.sample();
    r = s.core.reading(t);
    CHECK(std::fabs(r.grams) < 0.8f);
    if (elapsed(tDone, t) > 520) {
      late++;
      if (!r.stable)
        lateUnstable++;
    }
  }
  CHECK(late > 0 && lateUnstable <= 5);
}

static void testTareDiscard() {
  Feed f(20000);
  for (int i = 0; i < 20; i++)
    f.add(0.0f);
  uint32_t T = f.t;
  f.core.startTare(T);
  f.core.addSample(8e6f + 708.0f * 500.0f, T - 5); // vor dem Start gestempelt
  for (uint32_t dt = 12; dt < TARE_DISCARD_MS; dt += 12) {
    f.core.addSample(8e6f + 708.0f * 50.0f, T + dt); // Hand noch auf der Waage
  }
  // Ab genau TARE_DISCARD_MS wird gesammelt
  const uint32_t ts[5] = {100, 112, 125, 137, 150};
  for (int i = 0; i < 5; i++) {
    CHECK(f.core.taring());
    f.core.addSample(8e6f + 708.0f * 0.25f, T + ts[i]);
  }
  CHECK(!f.core.taring());
  CHECK(near(f.core.offset(), 8e6 + 708.0 * 0.25, 1.0));
  CHECK(std::fabs(f.core.reading(T + 150).grams) < 0.001f);
}

static void testTareTimeout(int sps, uint32_t start, uint32_t warmMs) {
  // Unruhig (Vibration +-3 g): nach TARE_MAX_MS mit dem Mittel von allem
  Sim s(sps, start, 31);
  s.cell.load = 20.0;
  s.cell.vib = 3.0;
  s.samplesFor(warmMs);
  uint32_t T = s.now();
  s.core.startTare(T);
  uint32_t tDone = 0;
  while (s.core.taring() && elapsed(T, s.now()) < 5000)
    tDone = s.sample();
  CHECK(!s.core.taring());
  uint32_t d = elapsed(T, tDone);
  CHECK(d >= TARE_MAX_MS && d <= TARE_MAX_MS + 1000 / (uint32_t)sps + 1);
  CHECK(near(s.core.offset(), 8e6 + 708.0 * 20.0, 708.0 * 0.5));
  CHECK(s.core.reading(tDone).valid);
  // 10 SPS: unruhig. 80 SPS: die Wechsel von Sample zu Sample mitteln sich in
  // der Spanne (100-ms-Mittel) weg, die Tara prueft aber 5 Einzelsamples.
  if (sps == 10)
    CHECK(!s.core.reading(tDone).stable);

  // Groessere stableSpread: dieselbe Unruhe gilt als stabil
  Sim q(sps, start, 31);
  q.cell.load = 20.0;
  q.cell.vib = 3.0;
  q.core.setStableSpread(30.0f);
  q.samplesFor(warmMs);
  T = q.now();
  q.core.startTare(T);
  while (q.core.taring() && elapsed(T, q.now()) < 5000)
    tDone = q.sample();
  CHECK(elapsed(T, tDone) <= 600);
}

static void testTareDeadline() {
  // Unruhig (+-3 g abwechselnd): Sample bei 1999 ms wird noch gesammelt,
  // bei 2000 ms ist Schluss, Offset = Mittel der 20 Samples
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    uint32_t T = f.t;
    f.core.startTare(T);
    int k = 0;
    for (uint32_t dt = 100; dt <= 1900; dt += 100, k++) {
      f.core.addSample(8e6f + 708.0f * (k % 2 ? -3.0f : 3.0f), T + dt);
    }
    f.core.addSample(8e6f + 708.0f * -3.0f, T + 1999);
    CHECK(f.core.taring());
    f.core.addSample(8e6f + 708.0f * 7.0f, T + 2000);
    CHECK(!f.core.taring());
    CHECK(near(f.core.offset(), 8e6, 1.0));
    // Das ausloesende Sample landet normal im Puffer (Sprung: Neustart)
    CHECK(near(f.core.reading(T + 2000).grams, 7.0, 1e-3));
  }
  // Bis zur Frist kein Sample (Loop blockiert, Sensor spaet): die Frist
  // beginnt mit dem ersten spaeteren Sample neu, das schon zaehlt
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    uint32_t T = f.t;
    f.core.setOffset(7.5e6f);
    f.core.startTare(T);
    f.core.addSample(8e6f, T + 50); // nur verworfen
    CHECK(!f.core.reading(T + 3000).valid);
    for (uint32_t dt = 2500; dt < 2900; dt += 100) {
      f.core.addSample(8e6f + 708.0f * 5.0f, T + dt);
      CHECK(f.core.taring());
      CHECK(f.core.offset() == 7.5e6f);
    }
    f.core.addSample(8e6f + 708.0f * 5.0f, T + 2900); // fuenftes stabiles
    CHECK(!f.core.taring());
    CHECK(near(f.core.offset(), 8e6 + 708.0 * 5.0, 1.0));
    Reading r = f.core.reading(T + 2900);
    CHECK(r.valid && r.stable);
    CHECK(std::fabs(r.grams) < 0.001f);
  }
  // Unruhig nach spaetem Start: TARE_MAX_MS ab dem ersten spaeten Sample
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    uint32_t T = f.t;
    f.core.startTare(T);
    int k = 0;
    for (uint32_t dt = 3000; dt <= 4800; dt += 100, k++) {
      f.core.addSample(8e6f + 708.0f * (k % 2 ? -3.0f : 3.0f), T + dt);
      CHECK(f.core.taring());
    }
    f.core.addSample(8e6f, T + 4900);
    CHECK(!f.core.taring());
    CHECK(
        near(f.core.offset(), 8e6 + 708.0 * 3.0 / 19.0, 1.0)); // 10x +3, 9x -3
  }
  // Boot: begin() mit Offset 0, Tara, dann blockiert der Loop 3 s (AP-Start)
  {
    Core c;
    c.begin(708.0f, 0.0f);
    c.startTare(100);
    for (uint32_t t = 3100; t <= 3500; t += 100)
      c.addSample(8e6f + 708.0f * 0.5f, t);
    CHECK(!c.taring());
    CHECK(near(c.offset(), 8e6 + 354.0, 1.0));
    CHECK(std::fabs(c.reading(3500).grams) < 0.001f);
  }
  // clear() nach der Frist (Sensorfehler): wieder ab dem ersten neuen Sample
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    uint32_t T = f.t;
    f.core.startTare(T);
    f.core.addSample(8e6f + 708.0f * 40.0f, T + 200);
    f.core.clear();
    CHECK(f.core.taring());
    for (uint32_t dt = 5000; dt <= 5400; dt += 100)
      f.core.addSample(8e6f, T + dt);
    CHECK(!f.core.taring());
    CHECK(near(f.core.offset(), 8e6, 1.0));
  }
}

static void testTareAbortRestart() {
  // clear(): Tara laeuft weiter, sammelt aber neu
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    f.core.startTare(f.t);
    for (int i = 0; i < 5; i++)
      f.add(0.0f); // 100..500 ms: alle 5 gesammelt
    CHECK(!f.core.taring());
  }
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    f.core.startTare(f.t);
    for (int i = 0; i < 4; i++)
      f.add(0.0f);
    f.core.clear();
    CHECK(f.core.taring());
    CHECK(!f.r().valid);
    for (int i = 0; i < 4; i++)
      f.add(0.0f);
    CHECK(f.core.taring()); // ohne clear() waere sie hier fertig
    f.add(0.0f);
    CHECK(!f.core.taring());
    CHECK(near(f.core.offset(), 8e6, 1.0));
  }
  // setOffset() bricht ab, der gesetzte Offset bleibt
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    f.core.startTare(f.t);
    f.add(0.0f);
    f.add(0.0f);
    f.core.setOffset(7.9e6f);
    CHECK(!f.core.taring());
    for (int i = 0; i < 20; i++)
      f.add(0.0f);
    CHECK(f.core.offset() == 7.9e6f);
    CHECK(f.r().valid);
    CHECK(near(f.r().grams, 100000.0 / 708.0, 1e-2));
  }
  // begin() bricht ab und leert
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    f.core.startTare(f.t);
    f.core.begin(708.0f, 8e6f);
    CHECK(!f.core.taring());
    CHECK(!f.r().valid);
  }
  // Erneuter startTare() beginnt von vorn
  {
    Feed f;
    for (int i = 0; i < 10; i++)
      f.add(0.0f);
    uint32_t T = f.t;
    f.core.startTare(T);
    for (int i = 0; i < 4; i++)
      f.add(0.0f); // T+100..T+400
    f.core.startTare(T + 450);
    f.add(0.0f); // T+500: verworfen
    for (int i = 0; i < 4; i++)
      f.add(0.0f);
    CHECK(f.core.taring());
    f.add(0.0f); // T+1000: fuenftes gesammeltes
    CHECK(!f.core.taring());
  }
}

// ── Nullung aus dem Fenster ───────────────────────────────────────────────────

static void testZeroFromWindow() {
  Feed f;
  for (int i = 0; i < 20; i++)
    f.add(0.4f);
  float off = f.core.offset();
  CHECK(!f.core.zeroFromWindow(0.3f, 2.0f, f.t)); // |Mittel| zu gross
  CHECK(!f.core.zeroFromWindow(NaN, 2.0f, f.t));
  CHECK(!f.core.zeroFromWindow(0.5f, NaN, f.t));
  CHECK(
      !f.core.zeroFromWindow(0.5f, 2.0f, f.t + 1000)); // keine frischen Samples
  CHECK(f.core.offset() == off);

  CHECK(f.core.zeroFromWindow(0.5f, 2.0f, f.t));
  CHECK(near(f.core.offset(), 8e6 + 708.0 * 0.4, 1.0));
  Reading r = f.r();
  CHECK(r.valid && r.stable); // Puffer bleibt: sofort stabil
  CHECK(std::fabs(r.grams) < 0.002f);
  CHECK(near(r.sps, 10.0, 1e-3));
  f.add(0.4f); // naechstes Sample passt nahtlos
  CHECK(std::fabs(f.r().grams) < 0.002f);

  // Spanne zu gross
  Feed g;
  for (int i = 0; i < 20; i++)
    g.add(i % 2 ? 1.0f : 0.0f);
  CHECK(!g.core.zeroFromWindow(5.0f, 0.8f, g.t));
  CHECK(g.core.zeroFromWindow(5.0f, 1.0f, g.t));
  CHECK(std::fabs(g.r().grams) < 0.002f);

  // Fenster nicht abgedeckt (frischer Puffer)
  Feed h;
  for (int i = 0; i < 4; i++)
    h.add(0.3f);
  CHECK(!h.core.zeroFromWindow(1.0f, 2.0f, h.t));
  h.add(0.3f);
  CHECK(h.core.zeroFromWindow(1.0f, 2.0f, h.t));

  // Waehrend der Tara nie
  Feed k;
  for (int i = 0; i < 20; i++)
    k.add(0.3f);
  k.core.startTare(k.t);
  CHECK(!k.core.zeroFromWindow(1.0f, 2.0f, k.t));

  // NegZero: mit Glas tariert, Glas abgehoben → unbegrenzt nullen
  Feed n;
  for (int i = 0; i < 20; i++)
    n.add(0.0f);
  n.add(-100.0f);
  CHECK(!n.core.zeroFromWindow(INF, 2.0f, n.t)); // Sprung im Fenster
  for (int i = 0; i < 4; i++)
    n.add(-100.0f);
  CHECK(n.core.zeroFromWindow(INF, 2.0f, n.t));
  CHECK(std::fabs(n.r().grams) < 0.002f);
  CHECK(near(n.core.offset(), 8e6 - 70800.0, 1.0));

  // Mit Rauschen: Anzeige danach um 0
  Sim s(80, 1000, 41);
  s.cell.load = 0.8;
  s.samplesFor(2000);
  CHECK(s.core.zeroFromWindow(1.0f, 2.0f, s.now()));
  CHECK(std::fabs(s.core.reading(s.now()).grams) < 0.01f);
  for (int i = 0; i < 80; i++) {
    uint32_t t = s.sample();
    CHECK(std::fabs(s.core.reading(t).grams) < 0.3f);
  }
}

static void testRawWindow() {
  Core c;
  float mean = 1.0f, spread = 1.0f;
  int n = 7;
  CHECK(!c.rawWindow(0, &mean, &spread, &n));
  CHECK(mean == 0.0f && spread == 0.0f && n == 0);

  c.begin(708.0f, 8e6f);
  const float d[5] = {0.0f, 100.0f, -50.0f, 200.0f, 50.0f};
  uint32_t t = 500;
  for (float v : d) {
    t += 100;
    c.addSample(8e6f + v, t);
  }
  CHECK(c.rawWindow(t, &mean, &spread, &n));
  CHECK(mean == 8e6f + 60.0f);
  CHECK(spread == 250.0f);
  CHECK(n == 5);
  CHECK(c.rawWindow(t, nullptr, nullptr, nullptr));
  CHECK(!c.rawWindow(t + 300, &mean, &spread, &n)); // nur noch 2 Samples
  CHECK(n == 2 && mean == 8e6f + 125.0f && spread == 150.0f);
}

static void testBufferWrap() {
  // Lange bei 80 SPS: Ringpuffer laeuft vielfach ueber
  Sim s(80, 1000, 51);
  s.cell.load = 12.0;
  s.samplesFor(20000);
  Reading r = s.core.reading(s.now());
  CHECK(r.valid && r.stable);
  CHECK(near(r.grams, 12.0, 0.2));
  CHECK(near(r.sps, 80.0, 0.5));
}

// ── Kalibrierung ──────────────────────────────────────────────────────────────

namespace {

struct CalSim {
  Sim s;
  Calibrator cal;
  float tol = 5.0f;
  CalSim(int sps, uint32_t start, uint32_t seed) : s(sps, start, seed) {
    s.core.begin(500.0f, 7.9e6f); // alter, falscher Stand
    s.samplesFor(1000);
  }
  void tick() {
    s.tick(5);
    cal.update(s.core, s.now(), tol);
  }
  void runFor(uint32_t ms) {
    uint32_t t0 = s.now();
    while (elapsed(t0, s.now()) < ms)
      tick();
  }
  // Laeuft bis zum Zustand st; liefert die Dauer (oder maxMs + 1).
  uint32_t runUntil(CalState st, uint32_t maxMs) {
    uint32_t t0 = s.now();
    while (cal.state() != st) {
      if (elapsed(t0, s.now()) > maxMs)
        return maxMs + 1;
      tick();
    }
    return elapsed(t0, s.now());
  }
  void toWaitWeight() {
    cal.start(s.core, s.now());
    runUntil(CalState::WaitWeight, CAL_PREPARE_MS + 3000);
  }
  bool restored() const {
    return s.core.factor() == 500.0f && s.core.offset() == 7.9e6f &&
           !s.core.taring();
  }
};

} // namespace

static void testCalHappy(int sps, uint32_t start, double cellFactor,
                         float knownG, double maxErr) {
  CalSim c(sps, start, 61);
  c.s.cell.factor = cellFactor;
  c.s.samplesFor(500);
  CHECK(!c.cal.active());
  CHECK(c.cal.liveDeltaCounts(c.s.core, c.s.now()) !=
        0.0f); // alter Offset passt nicht

  uint32_t t0 = c.s.now();
  c.cal.start(c.s.core, t0);
  CHECK(c.cal.state() == CalState::Prepare);
  CHECK(c.cal.active());
  CHECK(c.cal.oldFactor() == 500.0f);
  CHECK(!c.cal.measure(knownG, t0)); // falscher Schritt
  CHECK(c.cal.error() == CalError::None);

  c.runFor(CAL_PREPARE_MS - 10);
  CHECK(c.cal.state() == CalState::Prepare);
  CHECK(!c.s.core.taring());
  c.runFor(10);
  CHECK(c.cal.state() == CalState::Taring);
  CHECK(c.s.core.taring());

  // Tara mit altem Faktor 500: Rauschen wirkt 1,4-mal groesser
  uint32_t d = c.runUntil(CalState::WaitWeight, 3000);
  CHECK(d <= 1000);
  CHECK(near(c.s.core.offset(), 8e6, std::fabs(cellFactor) * 0.6));
  CHECK(c.s.core.factor() == 500.0f); // Faktor noch alt

  // Gewicht auflegen, Live-Delta folgt sofort
  c.s.cell.load = knownG;
  c.runFor(1000);
  double expect = cellFactor * knownG;
  CHECK(near(c.cal.liveDeltaCounts(c.s.core, c.s.now()), expect,
             std::fabs(expect) * 0.02));
  CHECK(c.cal.measure(knownG, c.s.now()));
  CHECK(c.cal.state() == CalState::Measuring);
  CHECK(!c.cal.measure(knownG, c.s.now())); // nur einmal

  d = c.runUntil(CalState::Done, CAL_MEASURE_MAX_MS + 1000);
  CHECK(d <= 300); // lag schon ruhig: sofort (selten ein paar Samples mehr)
  CHECK(std::fabs(c.cal.newFactor() / cellFactor - 1.0) < maxErr);
  CHECK(c.s.core.factor() == c.cal.newFactor());
  float f = 0.0f;
  CHECK(c.cal.takeNewFactor(&f));
  CHECK(f == c.cal.newFactor());
  CHECK(!c.cal.takeNewFactor(&f));

  // Done genau ein update(), dann RemoveWeight (Gewicht liegt noch)
  c.tick();
  CHECK(c.cal.state() == CalState::RemoveWeight);
  c.runFor(2000);
  CHECK(c.cal.state() == CalState::RemoveWeight);
  CHECK(near(c.s.core.reading(c.s.now()).grams, knownG, knownG * maxErr + 0.5));

  c.s.cell.load = 0.0;
  d = c.runUntil(CalState::Off, 3000);
  CHECK(d >= CAL_REMOVE_MS && d <= CAL_REMOVE_MS + 1000 / (uint32_t)sps + 20);
  CHECK(!c.cal.active());
  CHECK(c.cal.error() == CalError::None);
  CHECK(c.s.core.factor() == f);
  CHECK(near(c.s.core.offset(), 8e6, std::fabs(cellFactor) * 0.6));
  CHECK(std::fabs(c.s.core.reading(c.s.now()).grams) < 0.8f);

  // Neue Kalibrierung merkt sich den neuen Stand
  c.cal.start(c.s.core, c.s.now());
  CHECK(c.cal.oldFactor() == f);
  CHECK(!c.cal.takeNewFactor(&f));
  c.cal.cancel(c.s.core);
  CHECK(c.s.core.factor() == f);
}

static void testCalMeasureBeforeWeight() {
  // Messung gestartet, Gewicht erst danach aufgelegt: wartet auf Stabilitaet
  CalSim c(10, 1000, 71);
  c.toWaitWeight();
  CHECK(c.cal.state() == CalState::WaitWeight);
  CHECK(c.cal.measure(200.0f, c.s.now()));
  c.runFor(1000);
  CHECK(c.cal.state() == CalState::Measuring);
  c.s.cell.load = 200.0;
  uint32_t d = c.runUntil(CalState::Done, 3000);
  CHECK(d >= 400 && d <= 600);
  CHECK(std::fabs(c.cal.newFactor() / 708.0 - 1.0) < 0.005);
}

static void testCalNoWeight() {
  // Gewicht lag schon waehrend der Tara: kein Delta → NoWeight, alter Stand
  CalSim c(10, 1000, 81);
  c.s.cell.load = 300.0;
  c.s.samplesFor(1000);
  c.toWaitWeight();
  CHECK(c.cal.state() == CalState::WaitWeight);
  CHECK(std::fabs(c.cal.liveDeltaCounts(c.s.core, c.s.now())) < 708.0f);
  CHECK(c.cal.measure(300.0f, c.s.now()));
  uint32_t d = c.runUntil(CalState::Error, CAL_MEASURE_MAX_MS + 1000);
  CHECK(d >= CAL_MEASURE_MAX_MS && d <= CAL_MEASURE_MAX_MS + 10);
  CHECK(c.cal.error() == CalError::NoWeight);
  CHECK(c.restored());
  CHECK(c.cal.active());
  float f;
  CHECK(!c.cal.takeNewFactor(&f));
  c.runFor(1000);
  CHECK(c.cal.state() == CalState::Error); // bleibt bis zur Quittung
  c.cal.acknowledge();
  CHECK(c.cal.state() == CalState::Off);
  CHECK(c.cal.error() == CalError::None);
  CHECK(!c.cal.active());
}

static void testCalBadWeight() {
  CalSim c(10, 1000, 91);
  c.toWaitWeight();
  const float bad[] = {0.0f, -1.0f, NaN, 6000.0f, INF, -INF, 0.49f, 5000.5f};
  for (float w : bad) {
    CHECK(!c.cal.measure(w, c.s.now()));
    CHECK(c.cal.state() == CalState::WaitWeight);
    CHECK(c.cal.error() == CalError::BadWeight);
  }
  // Neuer Versuch mit gueltigem Gewicht (Grenzen inklusive)
  CHECK(c.cal.measure(CAL_WEIGHT_MAX, c.s.now()));
  CHECK(c.cal.state() == CalState::Measuring);
  CHECK(c.cal.error() == CalError::None);
  c.cal.cancel(c.s.core);
  CHECK(c.restored());

  CalSim e(10, 1000, 92);
  e.toWaitWeight();
  CHECK(e.cal.measure(CAL_WEIGHT_MIN, e.s.now()));

  // Falscher Schritt: kein BadWeight
  Calibrator off;
  CHECK(!off.measure(100.0f, 0));
  CHECK(off.error() == CalError::None);
  CHECK(off.state() == CalState::Off);
}

static void testCalBadFactor() {
  // 3 g aufgelegt, 5000 g angegeben: Faktor ~0,42 < 1
  CalSim c(10, 1000, 101);
  c.toWaitWeight();
  c.s.cell.load = 3.0;
  c.runFor(1000);
  CHECK(c.cal.measure(5000.0f, c.s.now()));
  uint32_t d = c.runUntil(CalState::Error, CAL_MEASURE_MAX_MS + 1000);
  CHECK(d <= CAL_MEASURE_MAX_MS + 10);
  CHECK(c.cal.error() == CalError::BadFactor);
  CHECK(c.restored());
}

static void testCalTimeouts(uint32_t start) {
  // WaitWeight ohne Messung
  {
    CalSim c(10, start, 111);
    c.toWaitWeight();
    uint32_t d = c.runUntil(CalState::Error, CAL_TIMEOUT_MS + 1000);
    CHECK(d >= CAL_TIMEOUT_MS - 5 && d <= CAL_TIMEOUT_MS + 5);
    CHECK(c.cal.error() == CalError::Timeout);
    CHECK(c.restored());
  }
  // Measuring ohne Samples (Sensor tot): kein Mittel, also Timeout
  {
    CalSim c(10, start, 112);
    c.toWaitWeight();
    c.s.sensorOn = false;
    c.runFor(1000);
    CHECK(c.cal.measure(100.0f, c.s.now()));
    c.runFor(CAL_MEASURE_MAX_MS + 1000);
    CHECK(c.cal.state() == CalState::Measuring);
    uint32_t d = c.runUntil(CalState::Error, CAL_TIMEOUT_MS);
    CHECK(d >= CAL_TIMEOUT_MS - CAL_MEASURE_MAX_MS - 1000 - 5 &&
          d <= CAL_TIMEOUT_MS);
    CHECK(c.cal.error() == CalError::Timeout);
    CHECK(c.restored());
  }
  // Taring ohne Samples
  {
    CalSim c(10, start, 113);
    c.s.sensorOn = false;
    c.cal.start(c.s.core, c.s.now());
    c.runUntil(CalState::Taring, CAL_PREPARE_MS + 100);
    CHECK(c.cal.state() == CalState::Taring);
    uint32_t d = c.runUntil(CalState::Error, CAL_TIMEOUT_MS + 1000);
    CHECK(d >= CAL_TIMEOUT_MS - 5 && d <= CAL_TIMEOUT_MS + 5);
    CHECK(c.cal.error() == CalError::Timeout);
    CHECK(c.restored());
  }
}

static void testCalCancel() {
  const CalState targets[] = {CalState::Prepare, CalState::Taring,
                              CalState::WaitWeight, CalState::Measuring};
  for (CalState st : targets) {
    CalSim c(10, 1000, 121);
    c.cal.start(c.s.core, c.s.now());
    if (st != CalState::Prepare)
      c.runUntil(st == CalState::Measuring ? CalState::WaitWeight : st, 5000);
    if (st == CalState::Taring)
      CHECK(c.s.core.taring());
    if (st == CalState::Measuring) {
      CHECK(c.cal.measure(100.0f, c.s.now())); // kein Gewicht: wartet
      c.runFor(500);
    }
    CHECK(c.cal.state() == st);
    c.cal.cancel(c.s.core);
    CHECK(c.cal.state() == CalState::Off);
    CHECK(c.cal.error() == CalError::None);
    CHECK(c.restored());
    c.runFor(3000); // die abgebrochene Tara darf nichts mehr aendern
    CHECK(c.restored());
    CHECK(c.cal.state() == CalState::Off);
  }

  // Abbruch in RemoveWeight: Kalibrierung bleibt gueltig
  {
    CalSim c(10, 1000, 122);
    c.toWaitWeight();
    c.s.cell.load = 250.0;
    c.runFor(800);
    CHECK(c.cal.measure(250.0f, c.s.now()));
    c.runUntil(CalState::RemoveWeight, 6000);
    CHECK(c.cal.state() == CalState::RemoveWeight);
    float nf = c.cal.newFactor();
    c.cal.cancel(c.s.core);
    CHECK(c.cal.state() == CalState::Off);
    CHECK(c.s.core.factor() == nf);
    CHECK(c.cal.takeNewFactor(nullptr)); // noch nicht abgeholt
    CHECK(!c.cal.takeNewFactor(nullptr));
  }

  // Abbruch in Error und Off
  {
    CalSim c(10, 1000, 123);
    c.toWaitWeight();
    CHECK(c.cal.measure(100.0f, c.s.now()));
    c.runUntil(CalState::Error, CAL_MEASURE_MAX_MS + 100);
    CHECK(c.cal.state() == CalState::Error);
    c.cal.cancel(c.s.core);
    CHECK(c.cal.state() == CalState::Off && c.cal.error() == CalError::None);
    c.cal.cancel(c.s.core);
    c.cal.acknowledge();
    CHECK(c.cal.state() == CalState::Off);
    CHECK(c.restored());
  }

  // acknowledge() nur im Fehlerzustand
  {
    CalSim c(10, 1000, 124);
    c.toWaitWeight();
    c.cal.acknowledge();
    CHECK(c.cal.state() == CalState::WaitWeight);
  }

  // Neustart mitten drin: zuerst alter Stand zurueck, dann neu merken
  {
    CalSim c(10, 1000, 125);
    c.toWaitWeight();
    CHECK(c.s.core.offset() != 7.9e6f); // Kalibrier-Tara
    c.cal.start(c.s.core, c.s.now());
    CHECK(c.cal.state() == CalState::Prepare);
    CHECK(c.restored());
    CHECK(c.cal.oldFactor() == 500.0f);
    c.cal.cancel(c.s.core);
    CHECK(c.restored());
  }
}

static void testCalRemoveWeight() {
  CalSim c(10, 1000, 131);
  c.toWaitWeight();
  c.s.cell.load = 400.0;
  c.runFor(800);
  CHECK(c.cal.measure(400.0f, c.s.now()));
  c.runUntil(CalState::RemoveWeight, 6000);
  CHECK(c.cal.state() == CalState::RemoveWeight);

  // Kurz weg, wieder drauf: Zeit beginnt neu
  c.s.cell.load = 0.0;
  c.runFor(600);
  c.s.cell.load = 100.0;
  c.runFor(300);
  CHECK(c.cal.state() == CalState::RemoveWeight);
  c.s.cell.load = 0.0;
  c.runFor(900);
  CHECK(c.cal.state() == CalState::RemoveWeight);
  c.runFor(300);
  CHECK(c.cal.state() == CalState::Off);

  // Ungueltige Toleranz: stableSpread als Ersatz
  CalSim e(10, 1000, 132);
  e.tol = NaN;
  e.toWaitWeight();
  e.s.cell.load = 400.0;
  e.runFor(800);
  CHECK(e.cal.measure(400.0f, e.s.now()));
  e.runUntil(CalState::RemoveWeight, 6000);
  e.s.cell.load = 0.0;
  CHECK(e.runUntil(CalState::Off, 2000) <= 1200);
}

// update() mit einem now kurz vor dem Zeitstempel von start()/measure()
// (z. B. millis() im Web-Handler spaeter gelesen als das now des Loops):
// zaehlt als 0 ms, nicht als ~49 Tage.
static void testCalClockSkew() {
  // Prepare wird nicht uebersprungen
  {
    CalSim c(10, WRAP_START, 141);
    uint32_t t = c.s.now();
    c.cal.start(c.s.core, t + 5);
    c.cal.update(c.s.core, t, c.tol);
    CHECK(c.cal.state() == CalState::Prepare);
    CHECK(!c.s.core.taring());
    c.cal.update(c.s.core, t + 5 + CAL_PREPARE_MS - 1, c.tol);
    CHECK(c.cal.state() == CalState::Prepare);
    c.cal.update(c.s.core, t + 5 + CAL_PREPARE_MS,
                 c.tol); // ueber den Ueberlauf
    CHECK(c.cal.state() == CalState::Taring);
    c.cal.cancel(c.s.core);
    CHECK(c.restored());
  }
  // Measuring ohne Samples: kein sofortiger Timeout
  {
    CalSim c(10, 1000, 142);
    c.toWaitWeight();
    c.s.sensorOn = false;
    c.runFor(1000);
    uint32_t t = c.s.now();
    CHECK(c.cal.measure(100.0f, t + 3));
    c.cal.update(c.s.core, t, c.tol);
    CHECK(c.cal.state() == CalState::Measuring);
    CHECK(c.cal.error() == CalError::None);
    c.cal.cancel(c.s.core);
    CHECK(c.restored());
  }
  // Measuring mit frisch aufgelegtem Gewicht: kein sofortiges Ende mit dem
  // Mischmittel (das gaebe einen falschen Faktor), sondern stabil abwarten
  {
    CalSim c(10, 1000, 143);
    c.toWaitWeight();
    c.s.cell.load = 200.0;
    c.s.samplesFor(150);
    uint32_t t = c.s.now();
    CHECK(c.cal.measure(200.0f, t + 2));
    c.cal.update(c.s.core, t, c.tol);
    CHECK(c.cal.state() == CalState::Measuring);
    CHECK(c.runUntil(CalState::Done, 2000) <= 1000);
    CHECK(std::fabs(c.cal.newFactor() / 708.0 - 1.0) < 0.005);
  }
  // RemoveWeight: die Haltezeit beginnt nicht von vorn und endet nicht sofort
  {
    CalSim c(10, WRAP_START, 144);
    c.toWaitWeight();
    c.s.cell.load = 300.0;
    c.runFor(800);
    CHECK(c.cal.measure(300.0f, c.s.now()));
    c.runUntil(CalState::RemoveWeight, 6000);
    CHECK(c.cal.state() == CalState::RemoveWeight);
    c.s.cell.load = 0.0;
    c.s.samplesFor(300);
    uint32_t t = c.s.now();
    c.cal.update(c.s.core, t, c.tol); // Haltezeit beginnt bei t
    CHECK(c.cal.state() == CalState::RemoveWeight);
    c.cal.update(c.s.core, t - 2, c.tol);
    CHECK(c.cal.state() == CalState::RemoveWeight);
    c.cal.update(c.s.core, t + CAL_REMOVE_MS - 1, c.tol);
    CHECK(c.cal.state() == CalState::RemoveWeight);
    c.cal.update(c.s.core, t + CAL_REMOVE_MS, c.tol);
    CHECK(c.cal.state() == CalState::Off);
  }
}

// Erfolg, aber start() vor takeNewFactor(): der neue Faktor gilt im Core
// weiter und muss trotzdem einmal zum Speichern abholbar bleiben
static void testCalPendingFactor() {
  CalSim c(10, 1000, 151);
  c.toWaitWeight();
  c.s.cell.load = 250.0;
  c.runFor(800);
  CHECK(c.cal.measure(250.0f, c.s.now()));
  CHECK(c.runUntil(CalState::Done, 6000) <= 6000);
  float nf = c.cal.newFactor();
  CHECK(std::fabs(nf / 708.0 - 1.0) < 0.005);

  c.cal.start(c.s.core, c.s.now());
  CHECK(c.cal.state() == CalState::Prepare);
  CHECK(c.cal.oldFactor() == nf);
  CHECK(c.s.core.factor() == nf);
  float f = 0.0f;
  CHECK(c.cal.takeNewFactor(&f));
  CHECK(f == nf);
  CHECK(!c.cal.takeNewFactor(&f));
  c.cal.cancel(c.s.core);
  CHECK(c.s.core.factor() == nf);

  // Zweite Kalibrierung vor dem Abholen: der zuletzt gesetzte Faktor zaehlt
  CalSim d(10, 1000, 152);
  d.toWaitWeight();
  d.s.cell.load = 250.0;
  d.runFor(800);
  CHECK(d.cal.measure(250.0f, d.s.now()));
  d.runUntil(CalState::Done, 6000);
  CHECK(d.cal.state() == CalState::Done);
  d.s.cell.load = 0.0;
  d.s.cell.factor = 720.0; // z. B. Zelle getauscht
  d.s.samplesFor(1000);
  d.toWaitWeight();
  CHECK(d.cal.state() == CalState::WaitWeight);
  d.s.cell.load = 250.0;
  d.runFor(800);
  CHECK(d.cal.measure(250.0f, d.s.now()));
  d.runUntil(CalState::Done, 6000);
  CHECK(d.cal.state() == CalState::Done);
  CHECK(d.cal.takeNewFactor(&f));
  CHECK(std::fabs(f / 720.0 - 1.0) < 0.005);
  CHECK(f == d.s.core.factor());
  CHECK(!d.cal.takeNewFactor(&f));

  // Nach dem Abholen setzt start() newFactor() zurueck
  d.cal.start(d.s.core, d.s.now());
  CHECK(d.cal.newFactor() == 0.0f);
  CHECK(!d.cal.takeNewFactor(&f));
  d.cal.cancel(d.s.core);
}

// Ungueltiger Offset wird ganz ignoriert: eine laufende Tara laeuft weiter.
// begin() bricht dagegen immer ab (frischer Start).
static void testInvalidOffsetDuringTare() {
  Feed f;
  for (int i = 0; i < 10; i++)
    f.add(0.0f);
  f.core.setOffset(7.9e6f);
  f.core.startTare(f.t);
  f.add(0.0f);
  f.core.setOffset(NaN);
  CHECK(f.core.taring());
  f.core.setOffset(INF);
  f.core.setOffset(-INF);
  CHECK(f.core.taring());
  CHECK(f.core.offset() == 7.9e6f);
  for (int i = 0; i < 4; i++)
    f.add(0.0f);
  CHECK(!f.core.taring());
  CHECK(near(f.core.offset(), 8e6, 1.0));

  Feed g;
  for (int i = 0; i < 10; i++)
    g.add(0.0f);
  g.core.startTare(g.t);
  g.add(0.0f);
  g.core.begin(NaN, NaN);
  CHECK(!g.core.taring());
  CHECK(!g.r().valid);
  CHECK(g.core.factor() == 708.0f && g.core.offset() == 8e6f);
}

// Zufaellige Aufruffolgen (auch ueber den Ueberlauf, Zeitstempel teils
// rueckwaerts): nie ungueltige Werte, Puffergrenzen halten (ASan).
static void testRandomOps() {
  Rng rng(4242);
  int bad = 0;
  for (int run = 0; run < 100; run++) {
    Core c;
    c.begin(run % 2 ? 708.0f : -708.0f, 8e6f);
    Calibrator cal;
    uint32_t t = WRAP_START + rng.next() % 8192;
    for (int i = 0; i < 2000; i++) {
      uint32_t op = rng.next() % 100;
      if (op < 70) {
        t += rng.next() % 30;
        if (rng.next() % 50 == 0)
          t -= rng.next() % 40;
        c.addSample(8e6f + (float)((int)(rng.next() % 200000) - 100000), t);
      } else if (op < 73) {
        c.startTare(t + rng.next() % 5 - 2);
      } else if (op < 75) {
        c.clear();
      } else if (op < 77) {
        c.zeroFromWindow((float)(rng.next() % 50), (float)(rng.next() % 5), t);
      } else if (op < 78) {
        c.setOffset((float)(rng.next() % 16000000));
      } else if (op < 79) {
        c.setStableSpread((float)(rng.next() % 10));
      } else if (op < 81) {
        cal.start(c, t);
      } else if (op < 83) {
        cal.measure((float)(rng.next() % 6000) - 100.0f, t);
      } else if (op < 84) {
        cal.cancel(c);
      } else if (op < 85) {
        cal.acknowledge();
      } else if (op < 95) {
        cal.update(c, t + rng.next() % 3000, 5.0f);
      } else {
        t += rng.next() % 3000;
      }
      Reading r = c.reading(t + rng.next() % 700);
      float m = 0.0f, sp = 0.0f;
      int n = 0;
      c.rawWindow(t, &m, &sp, &n);
      bool ok = std::isfinite(r.grams) && std::isfinite(r.spread) &&
                r.spread >= 0.0f && std::isfinite(r.sps) && r.sps >= 0.0f &&
                std::isfinite(c.offset()) && std::fabs(c.factor()) >= 1.0f &&
                std::isfinite(m) && sp >= 0.0f && n >= 0 && n <= BUF_MAX &&
                !(r.valid && c.taring()) &&
                std::isfinite(cal.liveDeltaCounts(c, t));
      if (!ok)
        bad++;
    }
  }
  CHECK(bad == 0);
}

static void testCalMisc() {
  Calibrator cal;
  Core c;
  CHECK(cal.liveDeltaCounts(c, 0) == 0.0f);
  CHECK(!cal.active());
  CHECK(cal.state() == CalState::Off && cal.error() == CalError::None);
  float f = 1.0f;
  CHECK(!cal.takeNewFactor(&f));
  CHECK(f == 1.0f);
  cal.update(c, 1000, 5.0f); // Off: nichts
  CHECK(cal.state() == CalState::Off);
  cal.cancel(c);
  CHECK(cal.state() == CalState::Off);
}

int main() {
  testBasics();
  testSetters();
  testFilterAverage();
  testFilterSteps();
  testFilterNoise(10);
  testFilterNoise(80);
  testStepReaction(10, 1000, 2000);
  testStepReaction(80, 1000, 2000);
  testStepReaction(10, WRAP_START, 3900); // Glas kurz vor dem Ueberlauf
  testStepReaction(80, WRAP_START, 3900);
  testCoverage();
  testSpreadAndSps();
  testSpreadRateIndependent();
  testSmallStep(10, 1000);
  testSmallStep(80, 1000);
  testSmallStep(80, WRAP_START + 2000); // Sprung ~100 ms vor dem Ueberlauf
  testSmallStep(10, WRAP_START + 2000);
  testSmallStepNoise();
  testTare(10, 1000, 1000);
  testTare(80, 1000, 1000);
  testTare(10, WRAP_START, 3700); // Tara ueber den Ueberlauf
  testTare(80, WRAP_START, 4000);
  testTareDiscard();
  testTareTimeout(10, 1000, 1000);
  testTareTimeout(80, 1000, 1000);
  testTareTimeout(10, WRAP_START, 3000);
  testTareDeadline();
  testTareAbortRestart();
  testZeroFromWindow();
  testRawWindow();
  testBufferWrap();
  // Faktor auf 0,1 %. Fehler je Sigma (0,3 g Rauschen, Tara aus 5 Samples):
  // 1000 g ~0,02 %, 500 g ~0,04 %, 100 g ~0,15 %; Grenzen >= 5 Sigma.
  testCalHappy(10, 1000, 708.0, 1000.0f, 0.001);
  testCalHappy(80, 1000, 708.0, 1000.0f, 0.001);
  testCalHappy(80, 1000, -708.0, 1000.0f, 0.001); // Zelle verkehrt herum
  testCalHappy(10, WRAP_START, 708.0, 1000.0f, 0.001);
  testCalHappy(10, 1000, 708.0, 500.0f, 0.002);
  testCalHappy(80, 1000, 1234.5, 100.0f, 0.008);
  testCalMeasureBeforeWeight();
  testCalNoWeight();
  testCalBadWeight();
  testCalBadFactor();
  testCalTimeouts(1000);
  testCalTimeouts(WRAP_START);
  testCalCancel();
  testCalRemoveWeight();
  testCalClockSkew();
  testCalPendingFactor();
  testInvalidOffsetDuringTare();
  testRandomOps();
  testCalMisc();
  return finish("scale_core_test");
}
