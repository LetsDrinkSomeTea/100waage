#pragma once
#include "config_core.h"
#include <stdint.h>

// ── Funk und Config-AP ────────────────────────────────────────────────────────
// Funk = WLAN im STA-Modus fuer ESP-NOW (Duell) auf DUELL_CHANNEL.
// AP = zusaetzlich der Config-Access-Point mit Webserver (WIFI_AP_STA).
// Der AP-Timeout schaltet nur den AP ab; ob der Funk danach weiterlaeuft,
// entscheidet app.cpp (an genau im Duell-Modus oder solange der AP laeuft).
// AP-Neustart und Reboot werden nie im Web-Handler ausgefuehrt, sondern
// angefordert und danach im Loop erledigt.

enum class RadioEvent : uint8_t {
  None,
  ApTimedOut, // AP per Timeout aus
  ApRestarted
}; // AP mit neuem Namen neu gestartet

bool radio_isOn();
bool radio_apOn();
const char *radio_apName();

void radio_start(); // nur Funk (ESP-NOW)
void radio_stop();  // alles aus; laufende Runde wird vorher verlassen
void radio_startAP(const cfg::Config &c); // Funk + AP + Webserver
void radio_stopAP();                      // nur AP + Webserver

void radio_requestApRestart(uint32_t delayMs);
void radio_requestReboot(uint32_t delayMs, bool apAfterBoot);
bool radio_actionPending();
bool radio_takeApAfterBoot(); // einmal true nach angefordertem Reboot (z. B.
                              // OTA)

RadioEvent radio_loop(const cfg::Config &c, uint32_t now);
