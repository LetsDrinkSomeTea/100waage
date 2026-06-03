#include "config.h"
#include <EEPROM.h>
#include <string.h>

constexpr int EEPROM_SIZE = 512;

WaageConfig defaultConfig() {
  WaageConfig cfg{};
  cfg.magic            = CONFIG_MAGIC;
  strncpy(cfg.apSSID, "100-Waage-Config", sizeof(cfg.apSSID));
  cfg.scaleFactor      = 708.0f;
  cfg.goal             = 100.0f;
  cfg.tolerance        = 10.0f;
  cfg.displayRotation  = 0;
  strncpy(cfg.adminPassword, "admin", sizeof(cfg.adminPassword));
  cfg.wifiTimeout      = 10;
  cfg.sleepTimeout     = 5;
  cfg.battDividerRatio = 2.0f;
  cfg.scaleMode        = 0;
  cfg.autoResetRange   = 10;
  cfg.autoZeroEnabled  = true;
  cfg.autoZeroThreshold = 2.0f;
  cfg.autoZeroDelay    = 5;
  cfg.randomModeEnabled = false;
  cfg.randomMin        = 20.0f;
  return cfg;
}

bool loadConfig(WaageConfig& cfg) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  EEPROM.end();

  if (cfg.magic != CONFIG_MAGIC) {
    cfg = defaultConfig();
    saveConfig(cfg);
    return false;
  }
  return true;
}

void saveConfig(const WaageConfig& cfg) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, cfg);
  EEPROM.commit();
  EEPROM.end();
}

void clearConfig() {
  WaageConfig empty{};
  empty.magic = 0x00;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, empty);
  EEPROM.commit();
  EEPROM.end();
}
