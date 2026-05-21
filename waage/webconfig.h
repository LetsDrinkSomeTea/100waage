#pragma once
#include "types.h"

void startWebServer(const WaageConfig& cfg);
void stopWebServer();
void handleWebRequests();
bool isWebServerRunning();
unsigned long getLastHttpActivity();
void setLiveBatteryPercent(int percent);
