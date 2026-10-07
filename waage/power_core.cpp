#include "power_core.h"
#include <cmath>

namespace power {

void SleepPolicy::reset(uint32_t now) {
  started_ = true;
  lastActivity_ = now;
  lastCheck_ = now;
  refValid_ = false; // naechstes gueltiges Gewicht wird neue Referenz
  refWeight_ = 0.0f;
}

void SleepPolicy::activity(uint32_t now) {
  if (!started_)
    reset(now);
  lastActivity_ = now;
}

bool SleepPolicy::update(uint32_t now, float weight, bool weightValid,
                         const Blockers &b, uint8_t sleepTimeoutMin) {
  if (!started_)
    reset(now); // ohne reset() nicht sofort schlafen

  // Aktivitaet "in der Zukunft" (activity() mit spaeter gelesenem millis() als
  // dieses now, oder Blocker-Abfrage mit groesserem now): sonst saehe
  // (now - lastActivity_) wie ~49 Tage aus → sofort schlafen. Zaehlt als jetzt.
  if ((uint32_t)(now - lastActivity_) >= 0x80000000u)
    lastActivity_ = now;

  if (sleepTimeoutMin == 0) {
    // Nie schlafen; Timer frisch halten, damit ein spaeter gesetztes
    // Timeout ab jetzt zaehlt und nicht sofort ausloest.
    lastActivity_ = now;
    return false;
  }

  if (weightValid && std::isfinite(weight)) {
    if (!refValid_) {
      refWeight_ = weight;
      refValid_ = true;
      lastCheck_ = now;
    } else if (elapsed(now, lastCheck_, WEIGHT_CHECK_MS)) {
      lastCheck_ = now;
      if (std::fabs(weight - refWeight_) > ACTIVITY_G) {
        lastActivity_ = now;
        refWeight_ = weight;
      }
    }
  }

  if (b.apOn || b.ownRoundOpen || b.buttonBusy || b.calibrating ||
      b.actionPending) {
    lastActivity_ = now;
    return false;
  }

  return elapsed(now, lastActivity_, (uint32_t)sleepTimeoutMin * 60000UL);
}

} // namespace power
