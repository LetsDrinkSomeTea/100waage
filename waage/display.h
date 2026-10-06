#pragma once
#include <Adafruit_SSD1306.h>
#include "text_core.h"

// ── OLED-Primitive (SSD1306 128x32, CP437-Font) ───────────────────────────────

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 32;

extern Adafruit_SSD1306 display;

void initDisplay(uint8_t rotation);

// Zeilen aus text::layout() zentriert zeichnen (ohne clear/display()).
void drawLayout(const text::Layout &l);
// Einzelne CP437-Zeile zentriert im Bereich y..y+h zeichnen.
void drawCentered(const char *cp437, int textSize, int y, int h);

void drawLoadingAnimation(int frame);
void drawHoldBar(int filledPx, int tickA, int tickB);

void drawBatteryIcon(int16_t x, int16_t y, int percentage);
void drawLowBatteryIcon(int16_t x, int16_t y);
void drawWifiIcon(int16_t x, int16_t y);
void drawDuellIcon(int16_t x, int16_t y, int peerCount);
void drawApBadge(int16_t x, int16_t y);
void drawShuffleIcon(int16_t x, int16_t y);
