// Unit-Tests fuer die reine Akku-Logik (OCV-Kurve, Mittelung, Kalibrierung, Anzeige).
#include "battery_core.h"
#include "check.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace batt;

static bool near(float a, float b, float eps = 0.01f) {
  return std::fabs(a - b) <= eps;
}

// ── percentFromVoltage ────────────────────────────────────────────────────────

static void testPercentTable() {
  struct {
    float v, pct;
  } pts[] = {
    { 4.20f, 100 }, { 4.10f, 90 }, { 4.00f, 79 }, { 3.92f, 70 }, { 3.87f, 60 }, { 3.82f, 50 },
    { 3.79f, 40 },  { 3.75f, 30 }, { 3.70f, 20 }, { 3.62f, 10 }, { 3.50f, 5 },  { 3.35f, 0 },
  };
  for (auto &p : pts) CHECK(near(percentFromVoltage(p.v), p.pct, 1e-3f));
  // Stuetzpunkte exakt
  CHECK(percentFromVoltage(3.70f) == 20.0f);
  CHECK(percentFromVoltage(4.00f) == 79.0f);
  CHECK(percentFromVoltage(EMPTY_V) == 0.0f);
  CHECK(percentFromVoltage(FULL_V) == 100.0f);
}

static void testPercentInterpolation() {
  // Mitte zwischen zwei Stuetzpunkten
  CHECK(near(percentFromVoltage(3.66f), 15.0f));     // 3,62→10 .. 3,70→20
  CHECK(near(percentFromVoltage(3.96f), 74.5f));     // 3,92→70 .. 4,00→79
  CHECK(near(percentFromVoltage(4.15f), 95.0f));     // 4,10→90 .. 4,20→100
  CHECK(near(percentFromVoltage(3.425f), 2.5f));     // 3,35→0 .. 3,50→5
  CHECK(near(percentFromVoltage(3.56f), 7.5f));      // 3,50→5 .. 3,62→10
  CHECK(near(percentFromVoltage(3.845f), 55.0f));    // 3,82→50 .. 3,87→60
  // Viertel
  CHECK(near(percentFromVoltage(3.7125f), 22.5f));   // 3,70→20 .. 3,75→30
}

static void testPercentClamp() {
  CHECK(percentFromVoltage(0.0f) == 0.0f);
  CHECK(percentFromVoltage(-1.0f) == 0.0f);
  CHECK(percentFromVoltage(3.0f) == 0.0f);
  CHECK(percentFromVoltage(3.3499f) == 0.0f);
  CHECK(percentFromVoltage(4.2001f) == 100.0f);
  CHECK(percentFromVoltage(5.0f) == 100.0f);
  CHECK(percentFromVoltage(1e9f) == 100.0f);
  CHECK(percentFromVoltage(std::numeric_limits<float>::infinity()) == 100.0f);
  CHECK(percentFromVoltage(-std::numeric_limits<float>::infinity()) == 0.0f);
  CHECK(percentFromVoltage(std::nanf("")) == 0.0f);
  // knapp innerhalb: > 0 bzw. < 100
  float lo = percentFromVoltage(3.351f);
  float hi = percentFromVoltage(4.199f);
  CHECK(lo > 0.0f && lo < 0.1f);
  CHECK(hi < 100.0f && hi > 99.8f);
}

static void testPercentMonotonic() {
  float prev = -1.0f;
  bool mono = true, inRange = true;
  for (int mv = 3000; mv <= 4500; mv++) {
    float p = percentFromVoltage(mv / 1000.0f);
    if (p < prev) mono = false;
    if (p < 0.0f || p > 100.0f) inRange = false;
    prev = p;
  }
  CHECK(mono);
  CHECK(inRange);
  // feiner um jeden Stuetzpunkt herum (Float-Nachbarn)
  const float knots[] = { 3.35f, 3.50f, 3.62f, 3.70f, 3.75f, 3.79f, 3.82f, 3.87f, 3.92f, 4.00f, 4.10f, 4.20f };
  bool knotMono = true;
  for (float k : knots) {
    float v = k;
    for (int i = 0; i < 64; i++) v = std::nextafter(v, 0.0f);
    float pv = percentFromVoltage(v);
    for (int i = 0; i < 128; i++) {
      v = std::nextafter(v, 10.0f);
      float p = percentFromVoltage(v);
      if (p < pv) knotMono = false;
      pv = p;
    }
  }
  CHECK(knotMono);
}

// ── trimmedMean ───────────────────────────────────────────────────────────────

static void testTrimmedMeanSmall() {
  CHECK(trimmedMean(nullptr, 0) == 0.0f);
  uint16_t one[1] = { 1234 };
  CHECK(trimmedMean(one, 0) == 0.0f);  // n == 0 → 0, Daten egal
  CHECK(trimmedMean(one, 1) == 1234.0f);

  // n < 8: normales Mittel, Ausreisser bleiben drin
  uint16_t seven[7] = { 1000, 1000, 1000, 1000, 1000, 1000, 8000 };
  CHECK(trimmedMean(seven, 7) == 2000.0f);
  uint16_t two[2] = { 1, 2 };
  CHECK(trimmedMean(two, 2) == 1.5f);
}

static void testTrimmedMeanTrims() {
  // n == 8: je 1 Wert oben/unten weg
  uint16_t a[8] = { 5000, 10, 1000, 1002, 998, 1001, 999, 1000 };
  CHECK(trimmedMean(a, 8) == 1000.0f);
  // sortiert in place
  const uint16_t sortedA[8] = { 10, 998, 999, 1000, 1000, 1001, 1002, 5000 };
  CHECK(memcmp(a, sortedA, sizeof(a)) == 0);

  // n == 15: 15/8 == 1 → je 1 weg
  uint16_t b[15];
  for (int i = 0; i < 15; i++) b[i] = (uint16_t)(100 * (15 - i));  // 1500..100 absteigend
  // ohne 100 und 1500: Mittel von 200..1400 = 800
  CHECK(trimmedMean(b, 15) == 800.0f);
  bool sorted = true;
  for (int i = 1; i < 15; i++)
    if (b[i - 1] > b[i]) sorted = false;
  CHECK(sorted);

  // n == 16: je 2 weg; zwei Ausreisser auf jeder Seite verschwinden
  uint16_t c[16] = { 0, 65535, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1, 65000 };
  CHECK(trimmedMean(c, 16) == 1500.0f);

  // n == 64: je 8 weg
  uint16_t d[64];
  for (int i = 0; i < 64; i++) d[i] = (uint16_t)(i < 8 ? 0 : (i >= 56 ? 4095 : 2000 + (i % 2)));
  CHECK(trimmedMean(d, 64) == 2000.5f);
}

static void testTrimmedMeanLarge() {
  // keine Ueberlaeufe bei vielen grossen Werten
  std::vector<uint16_t> v(100000, 65535);
  CHECK(trimmedMean(v.data(), v.size()) == 65535.0f);

  // nicht ganzzahliges Mittel
  uint16_t e[3] = { 1, 1, 2 };
  CHECK(near(trimmedMean(e, 3), 4.0f / 3.0f, 1e-6f));

  // gleiche Werte, Duplikate
  uint16_t f[9] = { 7, 7, 7, 7, 7, 7, 7, 7, 7 };
  CHECK(trimmedMean(f, 9) == 7.0f);
}

// ── calibrateRatio ────────────────────────────────────────────────────────────

static bool calOk(float v, float pin, float *r) {
  const char *err = "unveraendert";
  bool ok = calibrateRatio(v, pin, r, &err);
  if (ok) CHECK(err == nullptr);
  return ok;
}

static std::string calErr(float v, float pin) {
  float r = -42.0f;
  const char *err = nullptr;
  bool ok = calibrateRatio(v, pin, &r, &err);
  CHECK(!ok);
  CHECK(r == -42.0f);  // bei Fehler unveraendert
  CHECK(err != nullptr);
  return err ? std::string(err) : std::string();
}

static void testCalibrateOk() {
  float r = 0;
  CHECK(calOk(4.0f, 2000.0f, &r));
  CHECK(near(r, 2.0f, 1e-6f));
  CHECK(calOk(3.7f, 1233.333f, &r));
  CHECK(near(r, 3.0f, 1e-3f));
  // Grenzen inklusive
  CHECK(calOk(2.5f, 2500.0f, &r));
  CHECK(r == 1.0f);
  CHECK(calOk(4.5f, 750.0f, &r));
  CHECK(near(r, 6.0f, 1e-6f));
  CHECK(calOk(2.5f, 500.0f, &r));
  CHECK(near(r, 5.0f, 1e-6f));
  CHECK(calOk(3.0f, 1000.0f, &r));
  CHECK(near(r, 3.0f, 1e-6f));
  // err == nullptr ist erlaubt
  r = 0;
  CHECK(calibrateRatio(4.2f, 2100.0f, &r, nullptr));
  CHECK(near(r, 2.0f, 1e-6f));
}

static void testCalibrateErrors() {
  // Zellspannung ausserhalb 2,5..4,5 V
  std::string e1 = calErr(2.49f, 1000.0f);
  CHECK(e1.find("Akkuspannung") != std::string::npos);
  CHECK(calErr(4.51f, 1000.0f) == e1);
  CHECK(calErr(0.0f, 1000.0f) == e1);
  CHECK(calErr(-3.7f, 1000.0f) == e1);
  CHECK(calErr(3700.0f, 1850.0f) == e1);  // mV statt V eingegeben
  CHECK(calErr(std::nanf(""), 1000.0f) == e1);
  CHECK(calErr(std::numeric_limits<float>::infinity(), 1000.0f) == e1);

  // Pin-Spannung ausserhalb 300..2500 mV
  std::string e2 = calErr(3.7f, 299.0f);
  CHECK(e2.find("Pin-Spannung") != std::string::npos);
  CHECK(e2 != e1);
  CHECK(calErr(3.7f, 2501.0f) == e2);
  CHECK(calErr(3.7f, 0.0f) == e2);  // kein Akku → Pin 0 mV, keine Division durch 0
  CHECK(calErr(3.7f, std::nanf("")) == e2);
  CHECK(calErr(3.7f, -1000.0f) == e2);

  // Ergebnis ausserhalb 1..6
  std::string e3 = calErr(4.5f, 300.0f);  // 15
  CHECK(e3.find("1–6") != std::string::npos);
  CHECK(e3 != e1 && e3 != e2);
  CHECK(calErr(3.7f, 600.0f) == e3);   // 6,17
  CHECK(calErr(4.2f, 699.0f) == e3);   // 6,008

  // Spannung wird zuerst geprueft
  CHECK(calErr(1.0f, 1.0f) == e1);

  // Meldungen sind UTF-8 mit echten Umlauten (Ratio-Meldung)
  CHECK(e3.find("ä") != std::string::npos || e3.find("ü") != std::string::npos || e3.find("ß") != std::string::npos);

  // ratio == nullptr darf nicht abstuerzen
  const char *err = nullptr;
  CHECK(calibrateRatio(4.0f, 2000.0f, nullptr, &err));
  CHECK(err == nullptr);
  CHECK(!calibrateRatio(9.0f, 2000.0f, nullptr, &err));
  CHECK(err != nullptr);
  CHECK(!calibrateRatio(9.0f, 2000.0f, nullptr, nullptr));
}

// ── Gauge ─────────────────────────────────────────────────────────────────────

// Spannung zu einem Prozentwert (Umkehrung der Tabelle, fuer konstante Eingaben)
static float voltsFor(float pct) {
  float lo = EMPTY_V, hi = FULL_V;
  for (int i = 0; i < 60; i++) {
    float mid = 0.5f * (lo + hi);
    if (percentFromVoltage(mid) < pct) lo = mid;
    else hi = mid;
  }
  return hi;
}

static void testGaugeInitial() {
  Gauge g;
  CHECK(!g.valid());
  CHECK(!g.low());
  CHECK(g.percent() == 0);

  g.update(3.82f);
  CHECK(g.valid());
  CHECK(g.voltage() == 3.82f);  // erster Wert ohne Glaettung
  CHECK(g.percent() == 50);
  CHECK(!g.low());

  // Rundung auf ganze Prozent
  Gauge h;
  h.update(3.96f);  // 74,5
  CHECK(h.percent() == 74 || h.percent() == 75);
  Gauge k;
  k.update(3.71f);  // 22
  CHECK(k.percent() == 22);
  Gauge up;
  up.update(3.7135f);  // 22,7 → 23 (gerundet, nicht abgeschnitten)
  CHECK(up.percent() == 23);
  Gauge dn;
  dn.update(3.7115f);  // 22,3 → 22
  CHECK(dn.percent() == 22);

  // Klemmen
  Gauge full;
  full.update(4.35f);
  CHECK(full.percent() == 100);
  CHECK(full.voltage() == 4.35f);
  Gauge empty;
  empty.update(2.9f);
  CHECK(empty.percent() == 0);
  CHECK(empty.low());
}

static void testGaugeEma() {
  Gauge g;
  g.update(4.0f);
  g.update(3.5f);
  CHECK(near(g.voltage(), 4.0f + EMA_ALPHA * (3.5f - 4.0f), 1e-5f));  // 3,9
  float e = g.voltage();
  g.update(3.5f);
  CHECK(near(g.voltage(), e + EMA_ALPHA * (3.5f - e), 1e-5f));
  // konvergiert gegen konstante Eingabe
  for (int i = 0; i < 200; i++) g.update(3.75f);
  CHECK(near(g.voltage(), 3.75f, 1e-4f));
  CHECK(g.percent() == 30);
}

static void testGaugeSmallChangesIgnored() {
  Gauge g;
  g.update(voltsFor(50.0f));
  CHECK(g.percent() == 50);
  // 1 Punkt tiefer: Anzeige bleibt, auch nach vielen Messungen
  for (int i = 0; i < 100; i++) g.update(voltsFor(49.0f));
  CHECK(g.percent() == 50);
  // bis 4 Punkte hoeher: bleibt
  for (int i = 0; i < 100; i++) g.update(voltsFor(54.0f));
  CHECK(g.percent() == 50);
}

// Eingabe so waehlen, dass die EMA nach dieser Messung genau auf pct liegt
// (EMA-Formel rueckgerechnet). Damit sind die Schritte der Anzeige exakt steuerbar.
static void steer(Gauge &g, float pct) {
  float e = g.voltage();
  float target = voltsFor(pct);
  g.update(e + (target - e) / EMA_ALPHA);
}

static int emaPct(const Gauge &g) {
  return (int)(percentFromVoltage(g.voltage()) + 0.5f);
}

static void testGaugeSteerHelper() {
  Gauge g;
  g.update(voltsFor(60.0f));
  for (int p = 0; p <= 100; p++) {
    steer(g, (float)p);
    CHECK(emaPct(g) == p);
  }
}

static void testGaugeDownNeedsTwo() {
  Gauge g;
  g.update(voltsFor(60.0f));
  CHECK(g.percent() == 60);

  // 2 Punkte tiefer, aber nur einmal → bleibt
  steer(g, 58);
  CHECK(g.percent() == 60);
  // zweites Mal in Folge → faellt auf den aktuellen Wert
  steer(g, 58);
  CHECK(g.percent() == 58);

  // nur 1 Punkt tiefer, beliebig oft → bleibt
  for (int i = 0; i < 10; i++) steer(g, 57);
  CHECK(g.percent() == 58);

  // zweite Messung tiefer als die erste → neuer Wert ist der aktuelle
  steer(g, 56);
  CHECK(g.percent() == 58);
  steer(g, 40);
  CHECK(g.percent() == 40);

  // starker Einbruch ueber echte Eingaben: erst die zweite Messung zaehlt
  Gauge h;
  h.update(voltsFor(60.0f));
  h.update(voltsFor(20.0f));
  CHECK(emaPct(h) <= 58);
  CHECK(h.percent() == 60);
  h.update(voltsFor(20.0f));
  CHECK(h.percent() == emaPct(h));
  CHECK(h.percent() < 58);
}

static void testGaugeDownCounterResets() {
  Gauge g;
  g.update(voltsFor(60.0f));
  // tief, nicht tief, tief → kein Abfall (nicht 2x in Folge)
  steer(g, 58);
  steer(g, 59);
  steer(g, 58);
  CHECK(g.percent() == 60);
  steer(g, 58);  // jetzt 2x in Folge
  CHECK(g.percent() == 58);

  // Zaehler wird auch durch einen Anstieg zurueckgesetzt
  steer(g, 50);
  CHECK(g.percent() == 58);  // 1x tief
  steer(g, 63);              // +5 → steigt sofort, Zaehler weg
  CHECK(g.percent() == 63);
  steer(g, 61);              // 1x tief (bezogen auf 63)
  CHECK(g.percent() == 63);
  steer(g, 62);              // nicht tief genug → Zaehler weg
  steer(g, 61);
  CHECK(g.percent() == 63);
  steer(g, 61);
  CHECK(g.percent() == 61);

  // Zaehler bleibt ueber unveraenderte Werte nicht stehen
  steer(g, 59);  // 1x
  steer(g, 61);  // gleich → zurueck
  steer(g, 59);  // 1x
  CHECK(g.percent() == 61);
}

static void testGaugeUp() {
  Gauge g;
  g.update(voltsFor(30.0f));
  CHECK(g.percent() == 30);
  steer(g, 34);  // +4 → bleibt
  CHECK(g.percent() == 30);
  steer(g, 34);
  CHECK(g.percent() == 30);
  steer(g, 35);  // +5 → sofort, ohne zweite Messung
  CHECK(g.percent() == 35);
  steer(g, 100);
  CHECK(g.percent() == 100);
  steer(g, 99);  // 1 Punkt tiefer → bleibt
  CHECK(g.percent() == 100);

  // Laden mit echten Eingaben: Anzeige steigt nur in Schritten >= UP_STEP
  Gauge h;
  h.update(voltsFor(30.0f));
  float v = voltsFor(80.0f);
  int last = h.percent();
  bool smallJump = false, fell = false;
  for (int i = 0; i < 100; i++) {
    h.update(v);
    int now = h.percent();
    if (now != last && now - last < UP_STEP) smallJump = true;
    if (now < last) fell = true;
    last = now;
  }
  CHECK(!smallJump);
  CHECK(!fell);
  CHECK(h.percent() > 80 - UP_STEP);
  CHECK(h.percent() <= 80);
}

static void testGaugeLowHysteresis() {
  Gauge g;
  g.update(voltsFor(12.0f));
  CHECK(g.percent() == 12);
  CHECK(!g.low());
  steer(g, 10);
  steer(g, 10);
  CHECK(g.percent() == 10);
  CHECK(!g.low());  // nur < 10 warnt
  steer(g, 8);
  steer(g, 8);
  CHECK(g.percent() == 8);
  CHECK(g.low());
  steer(g, 12);     // +4 → Anzeige bleibt 8
  CHECK(g.percent() == 8);
  CHECK(g.low());
  steer(g, 13);     // +5 → 13 → Warnung aus (>= 13)
  CHECK(g.percent() == 13);
  CHECK(!g.low());

  // Bereich 10..12 haelt den Zustand
  Gauge h;
  h.update(voltsFor(7.0f));
  CHECK(h.low());
  steer(h, 12);
  CHECK(h.percent() == 12);
  CHECK(h.low());   // 12 < 13 → bleibt an
  steer(h, 10);
  steer(h, 10);
  CHECK(h.percent() == 10);
  CHECK(h.low());
  steer(h, 15);
  CHECK(h.percent() == 15);
  CHECK(!h.low());
  steer(h, 12);
  steer(h, 12);
  CHECK(h.percent() == 12);
  CHECK(!h.low());  // 12 >= 10 → bleibt aus
  steer(h, 10);
  steer(h, 10);
  CHECK(h.percent() == 10);
  CHECK(!h.low());
  steer(h, 8);
  steer(h, 8);
  CHECK(h.low());

  // Startwerte
  Gauge s;
  s.update(voltsFor(11.0f));
  CHECK(s.percent() == 11);
  CHECK(!s.low());
  Gauge t;
  t.update(voltsFor(10.0f));
  CHECK(t.percent() == 10);
  CHECK(!t.low());
  Gauge u;
  u.update(voltsFor(9.0f));
  CHECK(u.percent() == 9);
  CHECK(u.low());
}

static void testGaugeReset() {
  Gauge g;
  g.update(4.2f);
  CHECK(g.percent() == 100);
  g.reset();
  CHECK(!g.valid());
  g.update(3.70f);  // neuer Startwert ohne Glaettung, ohne Hysterese
  CHECK(g.valid());
  CHECK(g.voltage() == 3.70f);
  CHECK(g.percent() == 20);
  CHECK(!g.low());
  g.reset();
  g.update(3.50f);
  CHECK(g.percent() == 5);
  CHECK(g.low());

  // reset loescht auch einen halben Abwaerts-Zaehler
  Gauge h;
  h.update(voltsFor(60.0f));
  h.update(voltsFor(20.0f));  // 1x tiefer
  CHECK(h.percent() == 60);
  h.reset();
  h.update(voltsFor(60.0f));
  CHECK(h.percent() == 60);
  h.update(voltsFor(20.0f));  // wieder nur 1x tiefer
  CHECK(h.percent() == 60);
}

static void testGaugeInvalidInput() {
  Gauge g;
  g.update(std::nanf(""));  // vor dem Start: ignoriert
  CHECK(!g.valid());
  g.update(std::numeric_limits<float>::infinity());
  CHECK(!g.valid());
  g.update(3.82f);
  CHECK(g.valid());
  CHECK(g.percent() == 50);
  g.update(std::nanf(""));  // danach: EMA unveraendert
  CHECK(g.voltage() == 3.82f);
  CHECK(g.percent() == 50);
  g.update(-std::numeric_limits<float>::infinity());
  CHECK(g.voltage() == 3.82f);
  g.update(3.82f);
  CHECK(g.voltage() == 3.82f);

  // Kein Akku: 0 V ist eine gueltige Messung
  Gauge z;
  z.update(0.0f);
  CHECK(z.valid());
  CHECK(z.percent() == 0);
  CHECK(z.low());
}

static void testGaugeDischargeCurve() {
  // Langsame Entladung (4,2 → 3,35 V): Anzeige faellt monoton, Warnung geht
  // genau einmal an und bleibt an.
  Gauge g;
  int prev = 1000;
  bool mono = true;
  int lowOnCount = 0;
  bool prevLow = false;
  for (int i = 0; i <= 1700; i++) {
    float v = 4.2f - i * 0.0005f;
    g.update(v);
    if (g.percent() > prev) mono = false;
    prev = g.percent();
    if (g.low() && !prevLow) lowOnCount++;
    prevLow = g.low();
  }
  for (int i = 0; i < 100; i++) g.update(3.35f);
  CHECK(mono);
  CHECK(lowOnCount == 1);
  CHECK(g.low());
  CHECK(g.percent() <= 1);
}

static void testGaugeNoise() {
  // Rauschen +-10 mV um 3,8 V (ca. 43 %): Anzeige bleibt exakt stehen.
  Gauge g;
  g.update(3.80f);
  int start = g.percent();
  bool stable = true;
  for (int i = 0; i < 500; i++) {
    float noise = ((i * 7919) % 21 - 10) / 1000.0f;
    g.update(3.80f + noise);
    if (g.percent() != start) stable = false;
  }
  CHECK(stable);

  // +-20 mV: Anzeige bleibt in einem schmalen Band um den Startwert
  Gauge h;
  h.update(3.80f);
  start = h.percent();
  int minP = start, maxP = start;
  for (int i = 0; i < 500; i++) {
    float noise = ((i * 7919) % 41 - 20) / 1000.0f;
    h.update(3.80f + noise);
    if (h.percent() < minP) minP = h.percent();
    if (h.percent() > maxP) maxP = h.percent();
  }
  CHECK(maxP - start <= UP_STEP);
  CHECK(start - minP <= UP_STEP);
}

// ── Adversarial: Grenzfaelle ─────────────────────────────────────────────────

static void testPercentSignedZero() {
  CHECK(percentFromVoltage(-0.0f) == 0.0f);
  CHECK(!std::signbit(percentFromVoltage(-0.0f)));
  CHECK(!std::signbit(percentFromVoltage(-5.0f)));
  CHECK(!std::signbit(percentFromVoltage(std::nanf(""))));
}

static void testTrimmedMeanBounds() {
  // liest und schreibt nichts hinter mv[n-1]
  uint16_t a[10] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0xBEEF };
  CHECK(trimmedMean(a, 9) == 5.0f);  // 9/8 == 1 → ohne 1 und 9: 2..8
  CHECK(a[9] == 0xBEEF);
  CHECK(a[0] == 1 && a[8] == 9);
  uint16_t b[5] = { 3, 1, 2, 0xFFFF, 0xFFFF };
  CHECK(trimmedMean(b, 3) == 2.0f);
  CHECK(b[0] == 1 && b[1] == 2 && b[2] == 3 && b[3] == 0xFFFF && b[4] == 0xFFFF);

  // Kappgrenzen: 23 → je 2, 24 → je 3
  uint16_t c[23];
  for (int i = 0; i < 23; i++) c[i] = 100;
  c[0] = c[1] = 0;
  c[21] = c[22] = 60000;
  CHECK(trimmedMean(c, 23) == 100.0f);
  uint16_t d[24];
  for (int i = 0; i < 24; i++) d[i] = 100;
  d[0] = d[1] = d[2] = 0;
  d[21] = d[22] = d[23] = 60000;
  CHECK(trimmedMean(d, 24) == 100.0f);
  // 23 mit drei Ausreissern pro Seite: der dritte bleibt drin
  uint16_t e[23];
  for (int i = 0; i < 23; i++) e[i] = 100;
  e[0] = e[1] = e[2] = 0;
  e[20] = e[21] = e[22] = 1000;
  CHECK(near(trimmedMean(e, 23), (17 * 100 + 0 + 1000) / 19.0f, 1e-3f));
}

static void testCalibrateOrderAtPinLimit() {
  // Pin 300 mV ist gueltig (inklusive), das Verhaeltnis aber nie (>= 8,3):
  // also Teiler-Meldung, nicht Pin-Meldung
  std::string ePin = calErr(3.0f, 299.99f);
  std::string eRatio = calErr(3.0f, 300.0f);
  CHECK(ePin != eRatio);
  CHECK(ePin.find("Pin-Spannung") != std::string::npos);
  CHECK(eRatio.find("1–6") != std::string::npos);
  // Vorzeichen-Null und Unendlich am Pin
  CHECK(calErr(3.7f, -0.0f) == ePin);
  CHECK(calErr(3.7f, std::numeric_limits<float>::infinity()) == ePin);
  CHECK(calErr(-0.0f, 1000.0f).find("Akkuspannung") != std::string::npos);
  // gerade noch 1..6 (inklusive), knapp darueber nicht
  float r = 0;
  CHECK(calOk(3.0f, 3000.0f / 6.0f, &r));
  CHECK(r <= 6.0f && r > 5.999f);
  CHECK(calErr(3.0f, std::nextafter(500.0f, 0.0f)) == eRatio);
}

static void testGaugeExtremeFinite() {
  // Endliche, aber absurde Werte duerfen die EMA nicht dauerhaft auf inf/NaN setzen
  // (vorher: -3e38 dann 3e38 → inf, danach NaN fuer immer, Anzeige 0 % + Warnung).
  const float big = std::numeric_limits<float>::max();
  Gauge g;
  g.update(3.8f);
  g.update(-big);
  g.update(big);
  CHECK(std::isfinite(g.voltage()));
  bool finite = true;
  for (int i = 0; i < 50; i++) {
    g.update(i % 2 ? big : -big);
    if (!std::isfinite(g.voltage())) finite = false;
  }
  CHECK(finite);
  for (int i = 0; i < 3000; i++) g.update(3.8f);
  CHECK(std::isfinite(g.voltage()));
  CHECK(near(g.voltage(), 3.8f, 1e-3f));
  CHECK(g.percent() > 43 - UP_STEP && g.percent() <= 43);
  CHECK(!g.low());

  // absurder Startwert
  Gauge h;
  h.update(big);
  CHECK(h.percent() == 100);
  h.update(-big);
  CHECK(std::isfinite(h.voltage()));
  for (int i = 0; i < 3000; i++) h.update(3.70f);
  CHECK(near(h.voltage(), 3.70f, 1e-3f));
  CHECK(h.percent() >= 20 && h.percent() < 20 + DOWN_STEP);  // Hysterese nach unten
}

static void testGaugeSignedZero() {
  Gauge g;
  g.update(-0.0f);
  CHECK(g.valid());
  CHECK(g.voltage() == 0.0f);
  CHECK(!std::signbit(g.voltage()));  // sonst "-0.00 V" in Web/Anzeige
  CHECK(g.percent() == 0);
  CHECK(g.low());
  g.update(-0.0f);
  CHECK(!std::signbit(g.voltage()));
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.2f", (double)g.voltage());
  CHECK(std::string(buf) == "0.00");
}

// Endwerte: 100 % bzw. 0 % werden erreicht, obwohl die Hysterese 5 Punkte
// nach oben verlangt (sonst bliebe die Anzeige z. B. bei 97 % haengen)
static void testGaugeReachesEnds() {
  Gauge g;
  g.update(4.17f);  // ~97 %
  CHECK(g.percent() > 90 && g.percent() < 100);
  for (int i = 0; i < 60; i++) g.update(4.20f);
  CHECK(g.percent() == 100);

  Gauge h;
  h.update(3.37f);  // ~1 %
  CHECK(h.percent() >= 0 && h.percent() <= DOWN_STEP);
  for (int i = 0; i < 60; i++) h.update(3.30f);
  CHECK(h.percent() == 0);
  CHECK(h.low());
}

int main() {
  testGaugeReachesEnds();
  testPercentTable();
  testPercentInterpolation();
  testPercentClamp();
  testPercentMonotonic();
  testTrimmedMeanSmall();
  testTrimmedMeanTrims();
  testTrimmedMeanLarge();
  testCalibrateOk();
  testCalibrateErrors();
  testGaugeInitial();
  testGaugeEma();
  testGaugeSmallChangesIgnored();
  testGaugeSteerHelper();
  testGaugeDownNeedsTwo();
  testGaugeDownCounterResets();
  testGaugeUp();
  testGaugeLowHysteresis();
  testGaugeReset();
  testGaugeInvalidInput();
  testGaugeDischargeCurve();
  testGaugeNoise();
  testPercentSignedZero();
  testTrimmedMeanBounds();
  testCalibrateOrderAtPinLimit();
  testGaugeExtremeFinite();
  testGaugeSignedZero();
  return finish("battery_core_test");
}
