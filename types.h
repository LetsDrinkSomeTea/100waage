#ifndef TYPES_H
#define TYPES_H

struct WaageConfig {
  // EEPROM 0–81 (legacy fields, unchanged)
  char    apSSID[64];        // 0–63
  float   scaleFactor;       // 64–67
  long    tareOffset;        // 68–71
  float   goal;              // 72–75
  float   tolerance;         // 76–79
  uint8_t displayRotation;   // 80
  bool    valid;             // flag only, stored at 81

  // EEPROM 82–122 (extended, guarded by 0xBB at 122)
  char    adminPassword[32]; // 82–113
  uint8_t wifiTimeout;       // 114  minutes (0 = never)
  uint8_t sleepTimeout;      // 115  minutes (0 = never)
  float   battDividerRatio;  // 116–119
  uint8_t scaleMode;         // 120  0=Game, 1=Standard
  uint8_t autoResetRange;    // 121  percent
};

#endif
