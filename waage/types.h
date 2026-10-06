#pragma once
#include <stdint.h>

constexpr uint8_t CONFIG_MAGIC = 0xCD;

enum class State     { Idle, Tare, Drinking, Result };
enum class ScaleMode { Game, Standard };
// Duell-Ablauf aus Sicht der State-Machine. Live = eigenes Ergebnis gemeldet,
// Rang wird live angezeigt und ist final, sobald alle fertig sind.
enum class MultiplayerState { Offline, WaitReady, WaitStart, Live };


struct WaageConfig {
  uint8_t magic;              // CONFIG_MAGIC wenn gültig

  char    apSSID[64];
  float   scaleFactor;
  float   goal;
  float   tolerance;
  uint8_t displayRotation;    // 0 = normal, 2 = 180°

  char    adminPassword[32];
  uint8_t wifiTimeout;        // Minuten, 0 = nie
  uint8_t sleepTimeout;       // Minuten, 0 = nie
  float   battDividerRatio;
  uint8_t scaleMode;          // 0 = Game, 1 = Standard
  uint8_t autoResetRange;     // Prozent

  bool    autoZeroEnabled;
  float   autoZeroThreshold;  // Gramm
  uint8_t autoZeroDelay;      // Sekunden

  bool    randomModeEnabled;
  float   randomMin;          // Gramm
};
