#ifndef TYPES_H
#define TYPES_H

// Konfigurationsdaten
struct WaageConfig {
  char apSSID[64];
  float scaleFactor;
  long tareOffset;
  float goal;
  float tolerance;
  bool valid;
};

#endif
