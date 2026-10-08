#pragma once
#include <stdint.h>

// ── Taster (rein, ohne Arduino) ───────────────────────────────────────────────
// Wird in jedem Loop mit dem rohen Pegel gepollt (HIGH = gedrueckt).
// Entprellung: ein Pegel gilt erst, wenn er DEBOUNCE_MS stabil anliegt.
// Beim Loslassen entscheidet die Haltedauer ueber die Aktion.

namespace button {

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t MIN_PRESS_MS = 50; // kuerzere Druecke loesen nichts aus
constexpr uint32_t OVERLAY_MS =
    300; // ab hier zeigt das Display den Haltebalken
// Zonengrenzen in der Standard-Einstellung (Tara 0,75 s, je Stufe 1,25 s).
// Die wirksamen Grenzen kommen aus Timing (konfigurierbar).
constexpr uint32_t MODE_MS = 750;     // Moduswechsel
constexpr uint32_t RADIO_MS = 2000;   // AP an/aus
constexpr uint32_t CLASSIC_MS = 3250; // Klassik an / zurueck
constexpr uint32_t CANCEL_MS = 4500;  // Abbruch, nichts tun

enum class Zone : uint8_t {
  None,    // nicht gedrueckt
  Short,   // < modeMs: Reset + Tara
  Mode,    // modeMs..radioMs: naechster Modus (Game → Duell → Standard)
  Radio,   // radioMs..classicMs: AP an/aus
  Classic, // classicMs..cancelMs: Klassik an / zurueck
  Cancel
}; // >= cancelMs: nichts

// Wirksame Zonengrenzen [ms], aufsteigend.
struct Timing {
  uint32_t modeMs = MODE_MS;
  uint32_t radioMs = RADIO_MS;
  uint32_t classicMs = CLASSIC_MS;
  uint32_t cancelMs = CANCEL_MS;
};

// Grenzen aus der Tara-Zeit (Ende von Short) und der Breite jeder weiteren
// Zone (Mode, Radio, Classic). taraMs wird auf >= MIN_PRESS_MS + 1, stepMs auf
// >= 1 angehoben, damit die Zonen immer aufsteigen.
Timing makeTiming(uint32_t taraMs, uint32_t stepMs);

// Zone einer Haltedauer (ab MIN_PRESS_MS; kuerzer liefert ebenfalls Short).
Zone zoneFor(uint32_t heldMs, const Timing &t);

class Button {
public:
  // levelAtBoot = HIGH (z. B. der Weck-Druck aus dem Deep-Sleep): dieser Druck
  // wird ignoriert, bis der Taster losgelassen wurde.
  void begin(bool levelAtBoot, uint32_t now);
  // Zonengrenzen setzen (gilt sofort, auch fuer einen laufenden Druck).
  void setTiming(const Timing &t) { timing_ = t; }
  const Timing &timing() const { return timing_; }

  // Liefert beim Loslassen die Zone der Haltedauer (Short..Cancel),
  // sonst Zone::None. Druecke < MIN_PRESS_MS liefern None.
  Zone update(bool level, uint32_t now);

  bool pressed() const { return pressed_; } // entprellt
  uint32_t heldMs(uint32_t now) const;      // 0 wenn nicht gedrueckt
  Zone zone(uint32_t now) const;    // aktuelle Zone waehrend des Haltens
  bool overlay(uint32_t now) const; // gedrueckt und >= OVERLAY_MS
  bool takeEdge(); // true einmal nach jeder entprellten Flanke (Aktivitaet)

private:
  bool raw_ = false, pressed_ = false, ignore_ = false, edge_ = false;
  uint32_t rawSince_ = 0, pressSince_ = 0;
  Timing timing_;
};

} // namespace button
