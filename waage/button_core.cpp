#include "button_core.h"

namespace button {

static_assert(DEBOUNCE_MS < MIN_PRESS_MS,
              "Mindestdruck muss laenger als die Entprellung sein");
static_assert(MIN_PRESS_MS < MODE_MS && MODE_MS < RADIO_MS &&
                  RADIO_MS < CLASSIC_MS && CLASSIC_MS < CANCEL_MS,
              "Zonen muessen aufsteigend sein");

namespace {

// Vergangene Zeit seit t, ueberlaufsicher. Ein veralteter Zeitstempel (now
// liegt vor t) zaehlt als 0 statt als riesige Dauer.
uint32_t since(uint32_t now, uint32_t t) {
  uint32_t d = now - t;
  return d > 0x7FFFFFFFu ? 0 : d;
}

} // namespace

Timing makeTiming(uint32_t taraMs, uint32_t stepMs) {
  if (taraMs <= MIN_PRESS_MS)
    taraMs = MIN_PRESS_MS + 1;
  if (stepMs < 1)
    stepMs = 1;
  Timing t;
  t.modeMs = taraMs;
  t.radioMs = taraMs + stepMs;
  t.classicMs = taraMs + 2 * stepMs;
  t.cancelMs = taraMs + 3 * stepMs;
  return t;
}

Zone zoneFor(uint32_t held, const Timing &t) {
  if (held < t.modeMs)
    return Zone::Short;
  if (held < t.radioMs)
    return Zone::Mode;
  if (held < t.classicMs)
    return Zone::Radio;
  if (held < t.cancelMs)
    return Zone::Classic;
  return Zone::Cancel;
}

void Button::begin(bool levelAtBoot, uint32_t now) {
  // Ein beim Boot gehaltener Taster gilt sofort als gedrueckt, aber ignoriert.
  raw_ = levelAtBoot;
  pressed_ = levelAtBoot;
  ignore_ = levelAtBoot;
  edge_ = false;
  rawSince_ = now;
  pressSince_ = now;
}

Zone Button::update(bool level, uint32_t now) {
  if (level != raw_) {
    raw_ = level;
    rawSince_ = now;
  }
  if (raw_ == pressed_ || since(now, rawSince_) < DEBOUNCE_MS)
    return Zone::None;

  // Entprellte Flanke. Als Zeitpunkt zaehlt der Beginn des stabilen Pegels:
  // Haltedauer, Overlay und Zonen hinken so nicht um DEBOUNCE_MS hinterher,
  // und die Entprellzeit kuerzt sich bei der Haltedauer heraus.
  pressed_ = raw_;
  edge_ = true;
  if (pressed_) {
    pressSince_ = rawSince_;
    return Zone::None;
  }
  if (ignore_) { // Weck-Druck vom Boot: nur Aktivitaet, keine Aktion
    ignore_ = false;
    return Zone::None;
  }
  uint32_t held = since(rawSince_, pressSince_);
  return held < MIN_PRESS_MS ? Zone::None : zoneFor(held, timing_);
}

uint32_t Button::heldMs(uint32_t now) const {
  if (!pressed_ || ignore_)
    return 0;
  uint32_t held = since(now, pressSince_);
  // Loslassen erkannt, aber noch nicht entprellt: Dauer einfrieren, damit die
  // Anzeige bis zum Ereignis dieselbe Zone zeigt, die das Ereignis liefert.
  if (!raw_) {
    uint32_t upTo = since(rawSince_, pressSince_);
    if (upTo < held)
      held = upTo;
  }
  return held;
}

Zone Button::zone(uint32_t now) const {
  if (!pressed_ || ignore_)
    return Zone::None;
  return zoneFor(heldMs(now), timing_);
}

bool Button::overlay(uint32_t now) const {
  return pressed_ && !ignore_ && heldMs(now) >= OVERLAY_MS;
}

bool Button::takeEdge() {
  bool e = edge_;
  edge_ = false;
  return e;
}

} // namespace button
