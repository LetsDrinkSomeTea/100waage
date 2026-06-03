#pragma once
#include "types.h"

void  initScale(float scaleFactor);
void  updateWeight();
float getCurrentWeight();
float calibrateScale(float knownWeight);  // blocks ~10s, returns new scaleFactor

void resetState(const WaageConfig& cfg);
void updateState(const WaageConfig& cfg, bool wifiActive, int batteryPercent);

State     getCurrentState();
ScaleMode getCurrentScaleMode();
void      setScaleMode(ScaleMode mode);

float getLocalGameGoal();
