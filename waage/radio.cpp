#include "radio.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "battery.h"
#include "duell.h"
#include "power_core.h"
#include "web.h"

constexpr char AP_PASSWORD[] = "";
constexpr int AP_MAX_CLIENTS = 4;
constexpr uint32_t AP_AFTER_BOOT_MAGIC = 0xA9B0071Du;

static bool radioOn = false;
static bool apOn = false;
static char apName[cfg::SSID_MAX + 1] = "";

static bool apRestartPending = false, rebootPending = false, apAfterBoot = false;
static uint32_t apRestartAt = 0, rebootAt = 0;

// Ueberlebt den Software-Reset (nicht den Deep-Sleep): AP nach OTA wieder an
static RTC_NOINIT_ATTR uint32_t apAfterBootFlag;

bool radio_isOn() {
  return radioOn;
}

bool radio_apOn() {
  return apOn;
}

const char *radio_apName() {
  return apName;
}

void radio_start() {
  if (radioOn) return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();     // nie mit einem gespeicherten Netz verbinden (Kanalwechsel)
  WiFi.setSleep(false);  // Modem-Sleep wuerde ESP-NOW-Pakete verschlucken
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_channel(DUELL_CHANNEL, WIFI_SECOND_CHAN_NONE);
  duell_init();
  radioOn = true;
  battery_noteRadioToggle(millis());
}

void radio_stopAP() {
  if (!apOn) return;
  apOn = false;
  web_stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(DUELL_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

void radio_stop() {
  if (!radioOn) return;
  radio_stopAP();
  duell_deinit();  // verlaesst eine laufende Runde und sendet das vorher
  WiFi.mode(WIFI_OFF);
  radioOn = false;
  battery_noteRadioToggle(millis());
}

void radio_startAP(const cfg::Config &c) {
  if (apOn) return;
  radio_start();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  cfg::effectiveApName(c, mac, apName);
  // Funk (STA + ESP-NOW) laeuft bereits auf DUELL_CHANNEL, AP kommt dazu
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(100);
  WiFi.softAP(apName, AP_PASSWORD, DUELL_CHANNEL, 0, AP_MAX_CLIENTS);
  delay(500);
  Serial.print("AP: ");
  Serial.print(apName);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
  web_start();
  apOn = true;
  battery_noteRadioToggle(millis());
}

void radio_requestApRestart(uint32_t delayMs) {
  apRestartPending = true;
  apRestartAt = millis() + delayMs;
}

void radio_requestReboot(uint32_t delayMs, bool apAfter) {
  rebootPending = true;
  rebootAt = millis() + delayMs;
  apAfterBoot = apAfter;
}

bool radio_actionPending() {
  return apRestartPending || rebootPending;
}

bool radio_takeApAfterBoot() {
  bool r = apAfterBootFlag == AP_AFTER_BOOT_MAGIC;
  apAfterBootFlag = 0;
  return r;
}

RadioEvent radio_loop(const cfg::Config &c, uint32_t now) {
  RadioEvent ev = RadioEvent::None;
  if (apOn) {
    web_handle();
    now = millis();  // Anfragen koennen gedauert haben
    if (power::apTimedOut(now, web_lastActivity(), c.wifiTimeout)) {
      radio_stopAP();
      ev = RadioEvent::ApTimedOut;
    }
  }
  if (apRestartPending && (int32_t)(now - apRestartAt) >= 0) {
    apRestartPending = false;
    if (apOn) {
      radio_stopAP();
      radio_startAP(c);
      ev = RadioEvent::ApRestarted;
    }
  }
  if (rebootPending && (int32_t)(now - rebootAt) >= 0) {
    rebootPending = false;
    radio_stop();  // Runde sauber verlassen
    apAfterBootFlag = apAfterBoot ? AP_AFTER_BOOT_MAGIC : 0;
    Serial.flush();
    ESP.restart();
  }
  return ev;
}
