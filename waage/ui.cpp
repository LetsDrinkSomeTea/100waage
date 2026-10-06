#include "ui.h"
#include "display.h"

constexpr uint32_t MIN_FRAME_MS = 50;  // max. 20 Bilder pro Sekunde

static ui::Frame last;
static bool haveLast = false;
static uint32_t lastDraw = 0;

void ui_begin(uint8_t rotation) {
  initDisplay(rotation);
  display.clearDisplay();
  display.display();
}

void ui_setRotation(uint8_t rotation) {
  display.setRotation(rotation);
  haveLast = false;  // neu zeichnen
}

static void drawIcons(const ui::Frame &f) {
  if (f.shuffle) drawShuffleIcon(0, 0);
  int rightX = SCREEN_WIDTH;
  switch (f.right) {
    case ui::RightIcon::Battery:
      rightX = SCREEN_WIDTH - 14;
      drawBatteryIcon(rightX, 0, f.battPercent);
      break;
    case ui::RightIcon::Wifi:
      rightX = SCREEN_WIDTH - 14;
      drawWifiIcon(rightX, 0);
      break;
    case ui::RightIcon::Duel:
      rightX = SCREEN_WIDTH - 26;
      drawDuellIcon(rightX, 0, f.peers);
      break;
    case ui::RightIcon::None:
      break;
  }
  if (f.apBadge) drawApBadge(rightX - 14, 0);
}

static void draw(const ui::Frame &f) {
  display.clearDisplay();
  switch (f.kind) {
    case ui::Kind::Text:
      drawLayout(f.text);
      if (f.border) display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      if (f.icons) drawIcons(f);
      break;
    case ui::Kind::Hold:
      if (f.text.lines > 0) drawCentered(f.text.line[0], f.text.size, 0, 22);
      drawHoldBar(f.barPx, ui::TICK_MODE_PX, ui::TICK_RADIO_PX);
      break;
    case ui::Kind::Anim:
      drawLoadingAnimation(f.animFrame);
      break;
  }
  // Akku-Warnung auf jedem Bildschirm; im Idle neben dem Zufallssymbol
  if (f.lowBatt) drawLowBatteryIcon((f.icons && f.shuffle) ? 12 : 1, 0);
  display.display();
}

void ui_render(const ui::Frame &f, uint32_t now) {
  if (haveLast && ui::sameFrame(f, last)) return;
  if (haveLast && (uint32_t)(now - lastDraw) < MIN_FRAME_MS) return;
  draw(f);
  last = f;
  haveLast = true;
  lastDraw = now;
}

void ui_force(const char *l1, const char *l2) {
  text::Layout l;
  text::layout(l1, l2, nullptr, l);
  display.clearDisplay();
  drawLayout(l);
  display.display();
  haveLast = false;
}

void ui_off() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}
