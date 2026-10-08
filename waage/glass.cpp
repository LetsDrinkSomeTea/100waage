#include "glass.h"
#include "glasses_default.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_sleep.h>

static constexpr char NS[] = "glass";
static constexpr char K_DELTAS[] = "d";
static constexpr uint32_t RTC_MAGIC = 0x67A55002u;

static glass::List list;

struct RtcBlock {
  uint32_t magic;
  GlassRtc data;
};
RTC_DATA_ATTR static RtcBlock rtc;

void glass_begin() {
  list.begin(glass::DEFAULTS, glass::DEFAULT_COUNT, glass::DEFAULT_NEXT_ID);
  Preferences p;
  if (p.begin(NS, true)) { // Namespace fehlt beim ersten Start
    size_t n = p.getBytesLength(K_DELTAS);
    if (n > 0 && n <= glass::BLOB_MAX) {
      static uint8_t buf[glass::BLOB_MAX];
      if (p.getBytes(K_DELTAS, buf, n) == n && !list.load(buf, n))
        Serial.println("Glaeser: gespeicherte Liste ungueltig, Standard");
    }
    p.end();
  }
  if (list.pruned())
    glass_save(); // in die Firmware uebernommene Abweichungen entfernen
}

glass::List &glass_list() { return list; }

bool glass_save() {
  static uint8_t buf[glass::BLOB_MAX];
  size_t n = list.save(buf, sizeof buf);
  if (n == 0)
    return false;
  Preferences p;
  if (!p.begin(NS, false))
    return false;
  bool ok = p.putBytes(K_DELTAS, buf, n) == n;
  p.end();
  return ok;
}

bool glass_rtcRestore(GlassRtc *out) {
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ||
      rtc.magic != RTC_MAGIC)
    return false;
  *out = rtc.data;
  return true;
}

void glass_rtcStore(const GlassRtc &s) {
  rtc.data = s;
  rtc.magic = RTC_MAGIC;
}
