#pragma once
#include <stdint.h>

// ── Energie (rein, ohne Arduino) ──────────────────────────────────────────────
// Inaktivitaets-Timer fuer den Deep-Sleep, gilt in jedem Spielzustand.

namespace power {

// Ueberlaufsicher (millis-Wrap nach ~49 Tagen).
inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) {
  return (uint32_t)(now - since) >= ms;
}

constexpr float ACTIVITY_G = 2.0f;           // Gewichtsaenderung, die als Aktivitaet zaehlt
constexpr uint32_t WEIGHT_CHECK_MS = 2000;   // so oft wird das Gewicht verglichen

struct Blockers {
  bool apOn;           // Config-AP laeuft
  bool ownRoundOpen;   // eigene Duell-Runde noch nicht final
  bool buttonBusy;     // Taster gedrueckt
  bool calibrating;
  bool actionPending;  // AP-Neustart, Reboot o. ae. steht an
};

class SleepPolicy {
public:
  void reset(uint32_t now);
  void activity(uint32_t now);  // Tasterflanke, Web-Aktion, ...

  // true → jetzt schlafen. sleepTimeoutMin == 0 → nie. Blocker setzen den
  // Timer laufend zurueck. Gewicht zaehlt nur, wenn weightValid.
  bool update(uint32_t now, float weight, bool weightValid, const Blockers &b,
              uint8_t sleepTimeoutMin);

private:
  uint32_t lastActivity_ = 0, lastCheck_ = 0;
  float refWeight_ = 0.0f;
  bool refValid_ = false;
  bool started_ = false;  // reset() oder activity() schon gelaufen
};

// AP-Auto-Aus: timeoutMin == 0 → nie. Liegt lastHttp nach now (Anfrage wurde
// erst nach dem Zeitstempel des Loops bearbeitet), gilt das als frische
// Aktivitaet und nicht als ~49 Tage alt.
inline bool apTimedOut(uint32_t now, uint32_t lastHttp, uint8_t timeoutMin) {
  if (timeoutMin == 0) return false;
  int32_t age = (int32_t)(now - lastHttp);
  return age >= 0 && (uint32_t)age >= (uint32_t)timeoutMin * 60000UL;
}

}  // namespace power
