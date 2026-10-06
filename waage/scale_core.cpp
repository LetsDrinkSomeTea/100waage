#include "scale_core.h"
#include <cmath>

namespace scale {

namespace {

// Die Spanne wird ueber gleitende Mittel von ~SPREAD_BLOCK_MS gebildet: bei
// 10 SPS sind das Einzelsamples, bei 80 SPS je 8 Samples. Sonst waechst die
// Spanne mit der Samplezahl (40 statt 5 Extremwerte) und dem hoeheren
// Rauschen schneller Raten, und 80 SPS waere kaum je stabil.
constexpr uint32_t SPREAD_BLOCK_MS = 100;

// Alter eines Zeitstempels relativ zu ref (wrap-sicher). Zeitstempel aus der
// "Zukunft" (ref etwas zu frueh gelesen) zaehlen als 0 statt als uralt.
uint32_t ageOf(uint32_t ref, uint32_t t) {
  uint32_t d = ref - t;
  return (int32_t)d < 0 ? 0 : d;
}

bool validFactor(float f) {
  return std::isfinite(f) && std::fabs(f) >= 1.0f;
}

}  // namespace

// Statistik der Samples mit Alter < STABLE_MS (relativ zu now). Rohwerte
// werden relativ zum neuesten Sample summiert, damit float bei ~8e6
// Zaehlschritten nicht an Genauigkeit verliert.
struct Core::Window {
  int n;            // Samples im Fenster
  float ref;        // Bezugswert (neuestes Sample)
  float meanD;      // Mittel - ref [Zaehlschritte]
  float spreadRaw;  // max - min der ~100-ms-Mittel [Zaehlschritte]
  bool covered;     // STABLE_MS abgedeckt
  float iv;         // mittleres Sample-Intervall [ms], 0 = unbekannt
};

// ── Puffer ────────────────────────────────────────────────────────────────────

const Core::Sample &Core::at(int i) const {
  int idx = head_ - 1 - i;
  if (idx < 0) idx += BUF_MAX;
  return buf_[idx];
}

// Glaettung: Mittel der Samples seit dem letzten Neustart, aber nur die
// letzten DISPLAY_MS (relativ zu ref). Ohne Samples 0.
float Core::filterMean(uint32_t ref) const {
  int lim = filterN_ < count_ ? filterN_ : count_;
  if (lim < 1 && count_ > 0) lim = 1;
  if (lim < 1) return 0.0f;
  float base = at(0).raw;
  float sum = 0.0f;
  int n = 0;
  for (int i = 0; i < lim; i++) {
    const Sample &s = at(i);
    if (i > 0 && ageOf(ref, s.t) >= DISPLAY_MS) break;
    sum += s.raw - base;
    n++;
  }
  return ((base - offset_) + sum / (float)n) / factor_;
}

// Neues Sample anhaengen. Weicht es um mehr als STEP_G vom bisherigen
// geglaetteten Wert ab (Glas aufgestellt/abgehoben), beginnt die Glaettung
// bei diesem Sample neu: sofortige Reaktion, ruhig im Stillstand.
void Core::push(float raw, uint32_t t) {
  int keep = 0;
  if (count_ > 0 && filterN_ > 0) {
    // Nur Samples der letzten DISPLAY_MS vor t zaehlen; ist keins mehr
    // uebrig (lange Pause), beginnt die Glaettung ebenfalls neu.
    bool fresh = ageOf(t, at(0).t) < DISPLAY_MS;
    float g = (raw - offset_) / factor_;
    if (fresh && std::fabs(g - filterMean(t)) <= STEP_G) keep = filterN_;
  }
  buf_[head_] = { t, raw };
  head_ = (head_ + 1) % BUF_MAX;
  if (count_ < BUF_MAX) count_++;
  filterN_ = keep + 1 < count_ ? keep + 1 : count_;
}

// Mittleres Sample-Intervall [ms] aus den Zeitstempeln der letzten
// DISPLAY_MS (mindestens die zwei neuesten Samples); 0 = unbekannt.
float Core::meanInterval() const {
  if (count_ < 2) return 0.0f;
  uint32_t t0 = at(0).t;
  uint32_t span = 0;
  int k = 1;
  for (int i = 1; i < count_; i++) {
    uint32_t a = ageOf(t0, at(i).t);
    if (i >= 2 && a >= DISPLAY_MS) break;
    span = a;
    k = i + 1;
  }
  return span > 0 ? (float)span / (float)(k - 1) : 0.0f;
}

// Abgedeckt heisst: mindestens 3 Samples im Fenster und das aelteste reicht
// mit seinem Wandlungsintervall bis STABLE_MS zurueck (Alter + Intervall >=
// STABLE_MS). Intervall = Abstand zum naechstaelteren Sample im Puffer
// (Luecken hoechstens STABLE_MS / 2 angerechnet); gibt es keins (frischer
// Puffer nach clear()/Tara), 1,5 mittlere Intervalle (halbes Intervall
// Reserve fuer Jitter). 10 SPS: 5 Samples, 80 SPS: 40 Samples.
void Core::window(uint32_t now, Window *w) const {
  w->n = 0;
  w->ref = count_ > 0 ? at(0).raw : 0.0f;
  w->meanD = 0.0f;
  w->spreadRaw = 0.0f;
  w->covered = false;
  w->iv = meanInterval();
  float sum = 0.0f;
  uint32_t oldest = 0;
  int n = 0;
  for (; n < count_; n++) {
    const Sample &s = at(n);
    uint32_t a = ageOf(now, s.t);
    if (a >= STABLE_MS) break;
    sum += s.raw - w->ref;
    oldest = a;
  }
  if (n == 0) return;
  w->n = n;
  w->meanD = sum / (float)n;

  // Spanne der gleitenden Mittel aus m Samples (mindestens 3 Mittel)
  float iv = w->iv;
  int m = iv > 0.0f ? (int)((float)SPREAD_BLOCK_MS / iv + 0.5f) : 1;
  if (m > n / 3) m = n / 3;
  if (m < 1) m = 1;
  float lo = 0.0f, hi = 0.0f;
  for (int j = 0; j + m <= n; j++) {
    float b = 0.0f;
    for (int i = j; i < j + m; i++) b += at(i).raw - w->ref;
    if (j == 0 || b < lo) lo = b;
    if (j == 0 || b > hi) hi = b;
  }
  // Glaettung vor weniger als m Samples neu gestartet (Sprung > STEP_G): die
  // Samples seit dem Sprung bilden ein eigenes Mittel. Sonst verduennt das
  // gleitende Mittel den Sprung (80 SPS: 5 g zaehlten erst als 0,6 g), und
  // ein frischer Einzelwert galt bis zu m - 1 Samples lang als stabil.
  if (filterN_ > 0 && filterN_ < m) {
    float b = 0.0f;
    for (int i = 0; i < filterN_; i++) b += at(i).raw - w->ref;
    b = b * (float)m / (float)filterN_;
    if (b < lo) lo = b;
    if (b > hi) hi = b;
  }
  w->spreadRaw = (hi - lo) / (float)m;
  if (n < 3) return;
  if (n < count_) {
    uint32_t gap = ageOf(now, at(n).t) - oldest;  // > 0, da ausserhalb des Fensters
    if (gap > STABLE_MS / 2) gap = STABLE_MS / 2;
    w->covered = oldest + gap >= STABLE_MS;
  } else {
    w->covered = (float)oldest + 1.5f * iv >= (float)STABLE_MS;
  }
}

// ── Einstellungen ─────────────────────────────────────────────────────────────

void Core::begin(float factor, float offset) {
  setFactor(factor);
  setOffset(offset);
  taring_ = false;  // frischer Start, auch wenn der Offset ungueltig war
  clear();
}

void Core::setFactor(float factor) {
  if (validFactor(factor)) factor_ = factor;
}

// Ungueltiger Offset: Aufruf wird ganz ignoriert (eine laufende Tara laeuft
// weiter, es gibt ja keinen gesetzten Offset, den sie ueberschreiben koennte).
void Core::setOffset(float offset) {
  if (!std::isfinite(offset)) return;
  offset_ = offset;
  taring_ = false;
  tareCount_ = 0;
}

void Core::setStableSpread(float grams) {
  stableSpread_ = grams >= STABLE_SPREAD_MIN_G ? grams : STABLE_SPREAD_MIN_G;
}

void Core::clear() {
  head_ = 0;
  count_ = 0;
  filterN_ = 0;
  tareCount_ = 0;
}

// ── Samples und Anzeige ───────────────────────────────────────────────────────

void Core::addSample(float raw, uint32_t now) {
  if (!std::isfinite(raw)) return;
  if (taring_) {
    uint32_t before = tareStart_ - now;
    if (before != 0 && before <= TARE_MAX_MS) return;  // vor dem Tara-Start gestempelt
    uint32_t el = now - tareStart_;
    if (el >= TARE_MAX_MS && tareCount_ == 0) {
      // Bis zur Frist kein Sample (Loop blockiert, Sensor spaet): neu anlaufen,
      // dieses Sample zaehlt schon. Sonst bliebe z. B. nach dem Boot Offset 0.
      tareStart_ = now - TARE_DISCARD_MS;
      el = TARE_DISCARD_MS;
    }
    if (el < TARE_DISCARD_MS) return;  // Einschwingen nach dem Tastendruck
    if (el >= TARE_MAX_MS) {
      finishTare(tareCount_);  // Zeit um: Mittel von allem, was da ist
    } else {
      push(raw, now);
      if (tareCount_ < BUF_MAX) tareCount_++;
      if (tareCount_ >= TARE_MIN_SAMPLES) {
        float lo = at(0).raw, hi = lo;
        for (int i = 1; i < TARE_MIN_SAMPLES; i++) {
          float r = at(i).raw;
          if (r < lo) lo = r;
          if (r > hi) hi = r;
        }
        if ((hi - lo) / std::fabs(factor_) <= stableSpread_) finishTare(TARE_MIN_SAMPLES);
      }
      return;
    }
  }
  push(raw, now);
}

// Tara abschliessen: Offset = Mittel der neuesten k Samples; genau diese
// bleiben im Puffer und bilden die Glaettung (Anzeige sofort 0).
void Core::finishTare(int k) {
  if (k > count_) k = count_;
  if (k > 0) {
    float base = at(0).raw;
    float sum = 0.0f;
    for (int i = 0; i < k; i++) sum += at(i).raw - base;
    offset_ = base + sum / (float)k;
  }
  count_ = k;
  filterN_ = k;
  taring_ = false;
  tareCount_ = 0;
}

Reading Core::reading(uint32_t now) const {
  Reading r = {};
  r.valid = count_ > 0 && !taring_;
  if (count_ == 0) return r;
  r.grams = filterMean(at(0).t);
  Window w;
  window(now, &w);
  r.spread = w.spreadRaw / std::fabs(factor_);
  r.stable = w.covered && r.spread <= stableSpread_;
  r.sps = w.iv > 0.0f ? 1000.0f / w.iv : 0.0f;
  return r;
}

// ── Tara und Nullung ──────────────────────────────────────────────────────────

void Core::startTare(uint32_t now) {
  taring_ = true;
  tareStart_ = now;
  tareCount_ = 0;
}

bool Core::zeroFromWindow(float maxAbsG, float maxSpreadG, uint32_t now) {
  if (taring_) return false;
  Window w;
  window(now, &w);
  if (!w.covered) return false;
  float meanG = ((w.ref - offset_) + w.meanD) / factor_;
  float spreadG = w.spreadRaw / std::fabs(factor_);
  if (!(std::fabs(meanG) <= maxAbsG) || !(spreadG <= maxSpreadG)) return false;
  // Offset = Rohmittel des Fensters (= Verschiebung um meanG * factor);
  // die Glaettung beginnt bei den Fenster-Samples, die Anzeige steht auf 0.
  offset_ = w.ref + w.meanD;
  filterN_ = w.n;
  return true;
}

bool Core::rawWindow(uint32_t now, float *mean, float *spread, int *n) const {
  Window w;
  window(now, &w);
  if (mean) *mean = w.n > 0 ? w.ref + w.meanD : 0.0f;
  if (spread) *spread = w.spreadRaw;
  if (n) *n = w.n;
  return w.covered;
}

// ── Kalibrierung ──────────────────────────────────────────────────────────────

namespace {

// Zustaende, in denen Offset/Faktor noch auf den alten Stand zurueck muessen.
bool inProgress(CalState s) {
  return s == CalState::Prepare || s == CalState::Taring || s == CalState::WaitWeight ||
         s == CalState::Measuring;
}

}  // namespace

void Calibrator::restore(Core &s) const {
  s.setFactor(oldFactor_);
  s.setOffset(oldOffset_);  // bricht auch die Kalibrier-Tara ab
}

void Calibrator::fail(Core &s, CalError e) {
  restore(s);
  state_ = CalState::Error;
  error_ = e;
  removeTiming_ = false;
}

void Calibrator::start(Core &s, uint32_t now) {
  if (inProgress(state_)) restore(s);
  oldFactor_ = s.factor();
  oldOffset_ = s.offset();
  // Ein noch nicht abgeholter Faktor (Done/RemoveWeight ohne takeNewFactor)
  // gilt im Core weiter und muss weiterhin gespeichert werden koennen.
  if (!newFactorPending_) newFactor_ = 0.0f;
  knownG_ = 0.0f;
  error_ = CalError::None;
  removeTiming_ = false;
  state_ = CalState::Prepare;
  since_ = now;
}

bool Calibrator::measure(float knownG, uint32_t now) {
  if (state_ != CalState::WaitWeight) return false;
  if (!(knownG >= CAL_WEIGHT_MIN && knownG <= CAL_WEIGHT_MAX)) {  // auch NaN
    error_ = CalError::BadWeight;
    return false;
  }
  knownG_ = knownG;
  error_ = CalError::None;
  state_ = CalState::Measuring;
  since_ = now;
  return true;
}

void Calibrator::cancel(Core &s) {
  if (inProgress(state_)) restore(s);
  state_ = CalState::Off;
  error_ = CalError::None;
  removeTiming_ = false;
}

void Calibrator::acknowledge() {
  if (state_ != CalState::Error) return;
  state_ = CalState::Off;
  error_ = CalError::None;
}

bool Calibrator::takeNewFactor(float *f) {
  if (!newFactorPending_) return false;
  newFactorPending_ = false;
  if (f) *f = newFactor_;
  return true;
}

float Calibrator::liveDeltaCounts(const Core &s, uint32_t now) const {
  return s.reading(now).grams * s.factor();
}

// Messung auswerten: delta = Rohmittel - Offset (Offset aus der Tara der
// leeren Waage), Faktor = delta / bekanntes Gewicht.
void Calibrator::finish(Core &s, float delta) {
  if (!(std::fabs(delta) >= CAL_MIN_DELTA_COUNTS)) {
    fail(s, CalError::NoWeight);
    return;
  }
  float f = delta / knownG_;
  if (!validFactor(f)) {
    fail(s, CalError::BadFactor);
    return;
  }
  s.setFactor(f);
  newFactor_ = f;
  newFactorPending_ = true;
  state_ = CalState::Done;
}

void Calibrator::update(Core &s, uint32_t now, float removeTolG) {
  // Wrap-sicher; ein now kurz vor since_ (start()/measure() mit spaeterem
  // millis() als dieses update) zaehlt als 0 statt als ~49 Tage.
  uint32_t el = ageOf(now, since_);
  switch (state_) {
    case CalState::Prepare:
      if (el >= CAL_PREPARE_MS) {
        s.startTare(now);
        state_ = CalState::Taring;
        since_ = now;
      }
      break;
    case CalState::Taring:
      if (!s.taring()) {
        state_ = CalState::WaitWeight;
        since_ = now;
      } else if (el >= CAL_TIMEOUT_MS) {
        fail(s, CalError::Timeout);
      }
      break;
    case CalState::WaitWeight:
      if (el >= CAL_TIMEOUT_MS) fail(s, CalError::Timeout);
      break;
    case CalState::Measuring: {
      float mean = 0.0f, spread = 0.0f;
      int n = 0;
      bool covered = !s.taring() && s.rawWindow(now, &mean, &spread, &n);
      float delta = mean - s.offset();
      // Stabil in Gramm des neuen Faktors (|delta| / knownG), damit ein
      // falscher alter Faktor die Pruefung nicht verfaelscht.
      float ref = std::fabs(delta) > CAL_MIN_DELTA_COUNTS ? std::fabs(delta) : CAL_MIN_DELTA_COUNTS;
      bool stable = covered && spread * knownG_ <= s.stableSpread() * ref;
      if (n > 0 && (stable || el >= CAL_MEASURE_MAX_MS)) {
        finish(s, delta);
      } else if (el >= CAL_TIMEOUT_MS) {
        fail(s, CalError::Timeout);
      }
      break;
    }
    case CalState::Done:
      state_ = CalState::RemoveWeight;
      removeTiming_ = false;
      break;
    case CalState::RemoveWeight: {
      float tol = removeTolG > 0.0f ? removeTolG : s.stableSpread();  // auch NaN
      Reading r = s.reading(now);
      if (r.valid && std::fabs(r.grams) < tol) {
        if (!removeTiming_) {
          removeTiming_ = true;
          removeSince_ = now;
        } else if (ageOf(now, removeSince_) >= CAL_REMOVE_MS) {
          state_ = CalState::Off;
          removeTiming_ = false;
        }
      } else {
        removeTiming_ = false;
      }
      break;
    }
    case CalState::Off:
    case CalState::Error:
      break;
  }
}

}  // namespace scale
