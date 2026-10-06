#include "battery_core.h"
#include <cmath>

namespace batt {

namespace {

// Ruhespannungskurve, aufsteigend nach Spannung.
struct Point {
  float v;
  float pct;
};
constexpr Point OCV[] = {
  { 3.35f, 0.0f },  { 3.50f, 5.0f },  { 3.62f, 10.0f }, { 3.70f, 20.0f },
  { 3.75f, 30.0f }, { 3.79f, 40.0f }, { 3.82f, 50.0f }, { 3.87f, 60.0f },
  { 3.92f, 70.0f }, { 4.00f, 79.0f }, { 4.10f, 90.0f }, { 4.20f, 100.0f },
};
constexpr int OCV_N = sizeof(OCV) / sizeof(OCV[0]);
static_assert(OCV[0].v == EMPTY_V && OCV[OCV_N - 1].v == FULL_V, "OCV-Tabelle passt nicht zu EMPTY_V/FULL_V");

constexpr int DOWN_COUNT = 2;  // so viele Messungen in Folge, bevor die Anzeige faellt

// Kalibriergrenzen
constexpr float CAL_V_MIN = 2.5f, CAL_V_MAX = 4.5f;
constexpr float CAL_PIN_MIN = 300.0f, CAL_PIN_MAX = 2500.0f;
constexpr float CAL_RATIO_MIN = 1.0f, CAL_RATIO_MAX = 6.0f;

bool inRange(float x, float lo, float hi) {
  return x >= lo && x <= hi;  // NaN → false
}

// Prozent (0..100) kaufmaennisch auf ganze Zahl
int toInt(float pct) {
  int p = (int)(pct + 0.5f);
  if (p < 0) return 0;
  if (p > 100) return 100;
  return p;
}

}  // namespace

float percentFromVoltage(float volts) {
  if (!(volts > OCV[0].v)) return 0.0f;  // auch NaN
  if (volts >= OCV[OCV_N - 1].v) return 100.0f;
  int i = 1;
  while (volts > OCV[i].v) i++;
  const Point &a = OCV[i - 1];
  const Point &b = OCV[i];
  float t = (volts - a.v) / (b.v - a.v);  // 0..1, an Stuetzpunkten exakt
  return a.pct + t * (b.pct - a.pct);
}

float trimmedMean(uint16_t *mv, size_t n) {
  if (mv == nullptr || n == 0) return 0.0f;
  // Insertion-Sort: n ist klein (typ. 16..64), kein Heap noetig
  for (size_t i = 1; i < n; i++) {
    uint16_t x = mv[i];
    size_t j = i;
    while (j > 0 && mv[j - 1] > x) {
      mv[j] = mv[j - 1];
      j--;
    }
    mv[j] = x;
  }
  size_t cut = n / 8;  // n < 8 → 0, also normales Mittel
  size_t keep = n - 2 * cut;
  uint64_t sum = 0;
  for (size_t i = cut; i < cut + keep; i++) sum += mv[i];
  return (float)((double)sum / (double)keep);
}

bool calibrateRatio(float measuredV, float pinMv, float *ratio, const char **err) {
  const char *msg = nullptr;
  float r = 0.0f;
  if (!inRange(measuredV, CAL_V_MIN, CAL_V_MAX)) {
    msg = "Akkuspannung muss zwischen 2,5 und 4,5 V liegen";
  } else if (!inRange(pinMv, CAL_PIN_MIN, CAL_PIN_MAX)) {
    msg = "Pin-Spannung unplausibel (300–2500 mV erwartet)";
  } else {
    r = measuredV * 1000.0f / pinMv;
    if (!inRange(r, CAL_RATIO_MIN, CAL_RATIO_MAX)) msg = "Spannungsteiler wäre außerhalb 1–6 – Messung prüfen";
  }
  if (err) *err = msg;
  if (msg) return false;
  if (ratio) *ratio = r;
  return true;
}

void Gauge::reset() {
  *this = Gauge();
}

void Gauge::update(float cellVolts) {
  if (!std::isfinite(cellVolts)) return;  // kaputte Messung verwerfen, EMA nicht vergiften
  if (cellVolts == 0.0f) cellVolts = 0.0f;  // -0,0 → +0,0 (sonst "-0.00 V" in der Ausgabe)

  if (!valid_) {
    // Erste Messung: Start- und Anzeigewert direkt setzen
    valid_ = true;
    ema_ = cellVolts;
    shown_ = toInt(percentFromVoltage(ema_));
    downCount_ = 0;
    low_ = shown_ < LOW_ON;
    return;
  }

  // Absurde, aber endliche Werte koennen ueberlaufen (inf, danach NaN fuer immer):
  // solche Messungen verwerfen
  float next = ema_ + EMA_ALPHA * (cellVolts - ema_);
  if (!std::isfinite(next)) return;
  ema_ = next;
  int p = toInt(percentFromVoltage(ema_));

  if (p <= shown_ - DOWN_STEP) {
    // Fallen erst, wenn es sich bestaetigt (Lastspitzen ignorieren)
    if (++downCount_ >= DOWN_COUNT) {
      shown_ = p;
      downCount_ = 0;
    }
  } else {
    downCount_ = 0;
    if (p >= shown_ + UP_STEP) shown_ = p;  // Steigen nur deutlich (Laden)
  }
  // Endwerte direkt: sonst bliebe die Anzeige z. B. auf 97 % haengen,
  // weil 100 nie 5 Punkte darueber liegt
  if (p == 100 || (p == 0 && shown_ <= DOWN_STEP)) {
    shown_ = p;
    downCount_ = 0;
  }

  // Warnung mit Hysterese
  if (!low_ && shown_ < LOW_ON) low_ = true;
  else if (low_ && shown_ >= LOW_OFF) low_ = false;
}

}  // namespace batt
