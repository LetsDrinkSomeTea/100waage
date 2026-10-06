#pragma once
#include "types.h"

void  initScale(float scaleFactor);
void  updateWeight();
float getCurrentWeight();
float calibrateScale(float knownWeight);  // blocks ~10s, returns new scaleFactor

// tare=false: Glas steht auf der Waage (neues Glas nach Duell) — nicht nullen
void resetState(const WaageConfig& cfg, bool tare = true);
void updateState(const WaageConfig& cfg, bool radioOn, int batteryPercent);

State     getCurrentState();
ScaleMode getCurrentScaleMode();
void      setScaleMode(ScaleMode mode);

float getLocalGameGoal();
