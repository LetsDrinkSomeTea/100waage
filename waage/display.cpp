#include "display.h"
#include <Wire.h>

constexpr int8_t OLED_RESET = -1; // kein Reset-Pin
constexpr uint8_t SCREEN_ADDR = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void initDisplay(uint8_t rotation) {
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR);
  display.setRotation(rotation);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.cp437(true);
}

// ── Text ──────────────────────────────────────────────────────────────────────

void drawCentered(const char *cp437, int textSize, int y, int h) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, th;
  display.getTextBounds(cp437, 0, 0, &x1, &y1, &w, &th);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, y + (h - (int)th) / 2);
  display.print(cp437);
}

void drawLayout(const text::Layout &l) {
  if (l.lines == 0)
    return;
  int slotH = SCREEN_HEIGHT / l.lines;
  for (int i = 0; i < l.lines; i++)
    drawCentered(l.line[i], l.size, slotH * i, slotH);
}

// ── Animation und Haltebalken ─────────────────────────────────────────────────

void drawLoadingAnimation(int frame) {
  constexpr int NUM_CIRCLES = 5;
  constexpr int RADIUS = 5;
  constexpr int SPACING = RADIUS * 3;
  constexpr int START_X = (SCREEN_WIDTH - (NUM_CIRCLES - 1) * SPACING) / 2;
  constexpr int CENTER_Y = SCREEN_HEIGHT / 2;

  int filled = frame % NUM_CIRCLES;
  for (int i = 0; i < NUM_CIRCLES; i++) {
    int x = START_X + i * SPACING;
    if (i == filled)
      display.fillCircle(x, CENTER_Y, RADIUS, SSD1306_WHITE);
    else
      display.drawCircle(x, CENTER_Y, RADIUS, SSD1306_WHITE);
  }
}

// Balken unten (y 24..31) mit Markierungen bei tickA/tickB
void drawHoldBar(int filledPx, int tickA, int tickB) {
  constexpr int Y = 24, H = 8;
  display.drawRect(0, Y, SCREEN_WIDTH, H, SSD1306_WHITE);
  if (filledPx > 0)
    display.fillRect(0, Y, filledPx, H, SSD1306_WHITE);
  for (int x : {tickA, tickB}) {
    display.drawFastVLine(x, Y - 3, 3, SSD1306_WHITE);
    // In der Fuellung invertiert, damit die Marke sichtbar bleibt
    display.drawFastVLine(x, Y + 1, H - 2,
                          x < filledPx ? SSD1306_BLACK : SSD1306_WHITE);
  }
}

// ── Icons ─────────────────────────────────────────────────────────────────────

void drawBatteryIcon(int16_t x, int16_t y, int percent) {
  display.drawRect(x, y, 12, 7, SSD1306_WHITE);
  display.fillRect(x + 12, y + 2, 2, 3, SSD1306_WHITE);
  int fill = map(constrain(percent, 0, 100), 0, 100, 0, 10);
  if (fill > 0)
    display.fillRect(x + 1, y + 1, fill, 5, SSD1306_WHITE);
}

// Leeres Akkusymbol mit Ausrufezeichen auf schwarzem Grund (Warnung < 10 %)
void drawLowBatteryIcon(int16_t x, int16_t y) {
  display.fillRect(x - 1, y, 16, 9, SSD1306_BLACK);
  display.drawRect(x, y + 1, 12, 7, SSD1306_WHITE);
  display.fillRect(x + 12, y + 3, 2, 3, SSD1306_WHITE);
  display.drawFastVLine(x + 6, y + 2, 3, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 6, SSD1306_WHITE);
}

void drawWifiIcon(int16_t x, int16_t y) {
  int xc = x + 5, yc = y + 7;
  display.fillRect(xc - 1, yc - 1, 2, 2, SSD1306_WHITE);
  display.drawCircleHelper(xc, yc, 3, 0x03, SSD1306_WHITE);
  display.drawCircleHelper(xc, yc, 5, 0x03, SSD1306_WHITE);
}

void drawDuellIcon(int16_t x, int16_t y, int peers) {
  display.setTextSize(1);
  display.setCursor(x, y);
  display.print("Vs");
  display.setCursor(x + 14, y);
  display.print(peers);
}

void drawApBadge(int16_t x, int16_t y) {
  display.setTextSize(1);
  display.setCursor(x, y);
  display.print("AP");
}

void drawShuffleIcon(int16_t x, int16_t y) {
  // Kreuzende Pfeile
  display.drawLine(x, y + 2, x + 8, y + 10, SSD1306_WHITE);
  display.drawLine(x + 8, y + 10, x + 8, y + 7, SSD1306_WHITE);
  display.drawLine(x + 8, y + 10, x + 5, y + 10, SSD1306_WHITE);
  display.drawLine(x, y + 10, x + 8, y + 2, SSD1306_WHITE);
  display.drawLine(x + 8, y + 2, x + 5, y + 2, SSD1306_WHITE);
  display.drawLine(x + 8, y + 2, x + 8, y + 5, SSD1306_WHITE);
}
