#include "ui.h"
#include "display.h"

constexpr uint32_t MIN_FRAME_MS = 50; // max. 20 Bilder pro Sekunde

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
  haveLast = false; // neu zeichnen
}

static void drawIcons(const ui::Frame &f) {
  if (f.shuffle)
    drawShuffleIcon(0, 0);
  // von rechts nach links: Akku, "Vs n", WLAN-Bogen (AP)
  int x = SCREEN_WIDTH;
  if (f.battIcon) {
    x -= 14;
    drawBatteryIcon(x, 0, f.battPercent);
  }
  if (f.duelIcon) {
    x -= f.peers > 9 ? 28 : 22;
    drawDuellIcon(x, 0, f.peers);
  }
  if (f.apIcon)
    drawWifiIcon(x - 13, 0);
}

// Text oben in Groesse 1, Modus-Punkte darunter
static void drawModeText(const ui::Frame &f) {
  if (f.text.lines > 0)
    drawCentered(f.text.line[0], 1, 0, 12);
  drawModeDots(f.modeDots - 1, ui::MODE_DOTS);
}

static void draw(const ui::Frame &f) {
  display.clearDisplay();
  switch (f.kind) {
  case ui::Kind::Text:
    if (f.modeDots)
      drawModeText(f);
    else
      drawLayout(f.text);
    if (f.border)
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    if (f.icons)
      drawIcons(f);
    break;
  case ui::Kind::Hold:
    if (f.modeDots)
      drawModeText(f);
    else if (f.text.lines > 0)
      drawCentered(f.text.line[0], f.text.size, 0, 22);
    drawHoldBar(f.barPx, ui::TICK_MODE_PX, ui::TICK_RADIO_PX);
    break;
  case ui::Kind::Anim:
    drawLoadingAnimation(f.animFrame);
    break;
  }
  // Akku-Warnung auf jedem Bildschirm; im Idle neben dem Zufallssymbol
  if (f.lowBatt)
    drawLowBatteryIcon((f.icons && f.shuffle) ? 12 : 1, 0);
  display.display();
}

void ui_render(const ui::Frame &f, uint32_t now) {
  if (haveLast && ui::sameFrame(f, last))
    return;
  if (haveLast && (uint32_t)(now - lastDraw) < MIN_FRAME_MS)
    return;
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

void ui_off() { display.ssd1306_command(SSD1306_DISPLAYOFF); }
