#pragma once
#include "text_core.h"
#include <Adafruit_SSD1306.h>

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

// low: Ausrufezeichen statt Fuellung (Akku-Warnung)
void drawBatteryIcon(int16_t x, int16_t y, int percentage, bool low);
void drawWifiIcon(int16_t x, int16_t y);
void drawDuellIcon(int16_t x, int16_t y, int peerCount);
// n Punkte mittig um y = 17 (zwischen Text und Haltebalken), Punkt filled
// gefuellt.
void drawModeDots(int filled, int n);
void drawShuffleIcon(int16_t x, int16_t y);
