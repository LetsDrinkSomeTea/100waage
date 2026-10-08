#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>
#include <string.h>

// NVS-Namespace "waage", ein Schluessel pro Feld. "schema" wird zuletzt
// geschrieben und markiert eine vollstaendige Config. Neue Felder lesen
// einfach ihren Default, solange sie nicht gespeichert wurden.
static constexpr char NS[] = "waage";
static constexpr char K_SCHEMA[] = "schema";
static constexpr uint8_t SCHEMA = 1;

// Altes Abbild der Arduino-EEPROM-Emulation (Namespace und Key "eeprom").
static constexpr char LEGACY_NS[] = "eeprom";
static constexpr char LEGACY_KEY[] = "eeprom";

static cfg::Config current;
static cfg::ClassicBackup classicBak = {};

static void readClassic(Preferences &p) {
  classicBak = {};
  classicBak.valid = p.getBool("clsOn", false);
  if (!classicBak.valid)
    return;
  classicBak.scaleMode = (cfg::ScaleMode)p.getUChar("clsMode", 0);
  classicBak.goal = p.getFloat("clsGoal", 0.0f);
  classicBak.goalPercent = p.getBool("clsPctOn", false);
  classicBak.randomModeEnabled = p.getBool("clsRnd", false);
  classicBak.randomMin = p.getFloat("clsRndMin", 0.0f);
  // Ungueltige Werte faengt sanitize beim Zurueckspringen ab.
}

static void writeAll(Preferences &p, const cfg::Config &c, uint32_t mask) {
  if (mask & cfg::CH_SSID)
    p.putString("ssid", c.apSSID);
  if (mask & cfg::CH_PASSWORD)
    p.putString("pw", c.adminPassword);
  if (mask & cfg::CH_SCALE)
    p.putFloat("scale", c.scaleFactor);
  if (mask & cfg::CH_GOAL) {
    p.putFloat("goal", c.goal);
    p.putBool("goalPctOn", c.goalPercent);
    p.putUChar("goalPct", c.goalPct);
  }
  if (mask & cfg::CH_GAME) {
    p.putFloat("tol", c.tolerance);
    p.putUChar("arRange", c.autoResetRange);
    p.putUChar("swapMin", c.glassSwapMin);
  }
  if (mask & cfg::CH_ROTATION)
    p.putUChar("rot", c.displayRotation);
  if (mask & cfg::CH_TIMEOUTS) {
    p.putUChar("wifiTo", c.wifiTimeout);
    p.putUChar("sleepTo", c.sleepTimeout);
  }
  if (mask & cfg::CH_BATT) {
    p.putFloat("battDiv", c.battDividerRatio);
    p.putBool("battOn", c.batteryPresent);
  }
  if (mask & cfg::CH_MODE)
    p.putUChar("mode", (uint8_t)c.scaleMode);
  if (mask & cfg::CH_AUTOZERO) {
    p.putBool("azOn", c.autoZeroEnabled);
    p.putFloat("azThr", c.autoZeroThreshold);
    p.putUChar("azDelay", c.autoZeroDelay);
  }
  if (mask & cfg::CH_RANDOM) {
    p.putBool("rndOn", c.randomModeEnabled);
    p.putFloat("rndMin", c.randomMin);
    p.putUChar("rndMinPct", c.randomMinPct);
  }
  if (mask & cfg::CH_STATS) {
    p.putBool("statsRot", c.statsRotation);
    p.putUChar("statsAfter", c.statsAfterS);
    p.putUChar("statsGoal", c.statsGoalS);
    p.putUChar("statsStep", c.statsStepS);
  }
}

static constexpr uint32_t ALL = 0xFFFFFFFFu;

static void readAll(Preferences &p, cfg::Config &c) {
  const cfg::Config d = cfg::defaults();
  c = d;
  p.getString("ssid", c.apSSID, sizeof(c.apSSID));
  p.getString("pw", c.adminPassword, sizeof(c.adminPassword));
  if (!p.isKey("ssid"))
    memcpy(c.apSSID, d.apSSID, sizeof(c.apSSID));
  if (!p.isKey("pw"))
    memcpy(c.adminPassword, d.adminPassword, sizeof(c.adminPassword));
  c.scaleFactor = p.getFloat("scale", d.scaleFactor);
  c.goal = p.getFloat("goal", d.goal);
  c.tolerance = p.getFloat("tol", d.tolerance);
  c.autoResetRange = p.getUChar("arRange", d.autoResetRange);
  c.glassSwapMin = p.getUChar("swapMin", d.glassSwapMin);
  c.displayRotation = p.getUChar("rot", d.displayRotation);
  c.wifiTimeout = p.getUChar("wifiTo", d.wifiTimeout);
  c.sleepTimeout = p.getUChar("sleepTo", d.sleepTimeout);
  c.battDividerRatio = p.getFloat("battDiv", d.battDividerRatio);
  c.batteryPresent = p.getBool("battOn", d.batteryPresent);
  c.scaleMode = (cfg::ScaleMode)p.getUChar("mode", (uint8_t)d.scaleMode);
  c.autoZeroEnabled = p.getBool("azOn", d.autoZeroEnabled);
  c.autoZeroThreshold = p.getFloat("azThr", d.autoZeroThreshold);
  c.autoZeroDelay = p.getUChar("azDelay", d.autoZeroDelay);
  c.randomModeEnabled = p.getBool("rndOn", d.randomModeEnabled);
  c.randomMin = p.getFloat("rndMin", d.randomMin);
  c.goalPercent = p.getBool("goalPctOn", d.goalPercent);
  c.goalPct = p.getUChar("goalPct", d.goalPct);
  c.randomMinPct = p.getUChar("rndMinPct", d.randomMinPct);
  c.statsRotation = p.getBool("statsRot", d.statsRotation);
  c.statsAfterS = p.getUChar("statsAfter", d.statsAfterS);
  c.statsGoalS = p.getUChar("statsGoal", d.statsGoalS);
  c.statsStepS = p.getUChar("statsStep", d.statsStepS);
}

// Liest das alte EEPROM-Abbild nur lesend (legt nichts an, aendert nichts).
static bool readLegacy(cfg::Config &out) {
  nvs_handle_t h;
  if (nvs_open(LEGACY_NS, NVS_READONLY, &h) != ESP_OK)
    return false;
  uint8_t blob[512];
  size_t len = sizeof(blob);
  esp_err_t err = nvs_get_blob(h, LEGACY_KEY, blob, &len);
  nvs_close(h);
  if (err != ESP_OK)
    return false;
  return cfg::decodeLegacy(blob, len, out);
}

void config_begin() {
  Preferences p;
  p.begin(NS, false);
  if (p.getUChar(K_SCHEMA, 0) == SCHEMA) {
    readAll(p, current);
    cfg::Config loaded = current;
    if (cfg::sanitize(current))
      writeAll(p, current, cfg::diff(loaded, current));
  } else {
    if (readLegacy(current)) {
      cfg::sanitize(current);
      Serial.println("Config aus altem EEPROM-Abbild importiert");
    } else {
      current = cfg::defaults();
    }
    writeAll(p, current, ALL);
    p.putUChar(K_SCHEMA, SCHEMA);
  }
  readClassic(p);
  p.end();
}

const cfg::Config &config_get() { return current; }

uint32_t config_set(const cfg::Config &next) {
  cfg::Config c = next;
  cfg::sanitize(c);
  uint32_t mask = cfg::diff(current, c);
  if (mask) {
    Preferences p;
    p.begin(NS, false);
    writeAll(p, c, mask);
    p.end();
    current = c;
  }
  return mask;
}

void config_factoryReset() {
  Preferences p;
  p.begin(NS, false);
  current = cfg::defaults();
  writeAll(p, current, ALL);
  p.putUChar(K_SCHEMA, SCHEMA);
  p.putBool("clsOn", false);
  classicBak = {};
  p.end();
}

const cfg::ClassicBackup &config_classicBackup() { return classicBak; }

void config_setClassicBackup(const cfg::ClassicBackup &b) {
  if (!b.valid && !classicBak.valid)
    return;
  Preferences p;
  p.begin(NS, false);
  if (b.valid) {
    p.putUChar("clsMode", (uint8_t)b.scaleMode);
    p.putFloat("clsGoal", b.goal);
    p.putBool("clsPctOn", b.goalPercent);
    p.putBool("clsRnd", b.randomModeEnabled);
    p.putFloat("clsRndMin", b.randomMin);
  }
  p.putBool("clsOn", b.valid); // zuletzt: markiert einen vollstaendigen Satz
  p.end();
  classicBak = b.valid ? b : cfg::ClassicBackup{};
}
