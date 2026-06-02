#pragma once
#include <Adafruit_SSD1306.h>

constexpr int SCREEN_WIDTH  = 128;
constexpr int SCREEN_HEIGHT = 32;

extern Adafruit_SSD1306 display;

void initDisplay(uint8_t rotation);

void displayText(const String& line, bool border = false);
void displayLines(const String& l1, const String& l2 = "", const String& l3 = "", bool border = false);
void displayLoadingAnimation(int frame);

void drawBatteryIcon(int x, int y, int percent);
void drawWifiIcon(int x, int y);
void drawDuellIcon(int x, int y, int peers);

String getRandomTrinkspruch();
