#include "display.h"
#include <Wire.h>

constexpr uint8_t OLED_RESET   = -1;
constexpr uint8_t SCREEN_ADDR  = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void initDisplay(uint8_t rotation) {
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR);
  display.setRotation(rotation);
  display.setTextColor(SSD1306_WHITE);
  display.cp437();
}

// ── Icons ─────────────────────────────────────────────────────────────────────

void drawBatteryIcon(int x, int y, int percent) {
  display.drawRect(x, y, 12, 7, SSD1306_WHITE);
  display.fillRect(x + 12, y + 2, 2, 3, SSD1306_WHITE);
  int fill = map(constrain(percent, 0, 100), 0, 100, 0, 10);
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, 5, SSD1306_WHITE);
}

void drawWifiIcon(int x, int y) {
  int xc = x + 5, yc = y + 7;
  display.fillRect(xc - 1, yc - 1, 2, 2, SSD1306_WHITE);
  display.drawCircleHelper(xc, yc, 3, 0x03, SSD1306_WHITE);
  display.drawCircleHelper(xc, yc, 5, 0x03, SSD1306_WHITE);
}

// ── Text helpers ──────────────────────────────────────────────────────────────

static void printCentered(const String& text, int lineIndex, int totalLines) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int slotH = SCREEN_HEIGHT / totalLines;
  display.setCursor((SCREEN_WIDTH - w) / 2, slotH * lineIndex + (slotH - h) / 2);
  display.print(text);
}

void displayLines(const String& l1, const String& l2, const String& l3, bool border) {
  String lines[3] = { l1, l2, l3 };

  int numLines = 0;
  int maxLen   = 0;
  for (int i = 0; i < 3; i++) {
    if (lines[i].length() > 0) {
      numLines = i + 1;
      if ((int)lines[i].length() > maxLen) maxLen = lines[i].length();
    }
  }
  if (numLines == 0) return;

  if (numLines <= 2 && maxLen <= 10) {
    display.setTextSize(2);
  } else {
    display.setTextSize(1);
    if (numLines == 1 && maxLen > 21) {
      for (int i = 21; i > 0; i--) {
        if (lines[0].charAt(i) == ' ') {
          lines[1] = lines[0].substring(i + 1);
          lines[0] = lines[0].substring(0, i);
          numLines  = 2;
          maxLen    = max((int)lines[0].length(), (int)lines[1].length());
          break;
        }
      }
      if (numLines == 2 && (int)lines[1].length() > 21) {
        for (int i = 21; i > 0; i--) {
          if (lines[1].charAt(i) == ' ') {
            lines[2] = lines[1].substring(i + 1);
            lines[1] = lines[1].substring(0, i);
            numLines  = 3;
            break;
          }
        }
      }
    }
  }

  display.clearDisplay();
  for (int i = 0; i < numLines; i++) printCentered(lines[i], i, numLines);
  if (border) display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();
}

void displayText(const String& line, bool border) {
  displayLines(line, "", "", border);
}

// ── Loading animation ─────────────────────────────────────────────────────────

void displayLoadingAnimation(int frame) {
  constexpr int NUM_CIRCLES = 5;
  constexpr int RADIUS      = 5;
  constexpr int SPACING     = RADIUS * 3;
  constexpr int START_X     = (SCREEN_WIDTH - (NUM_CIRCLES - 1) * SPACING) / 2;
  constexpr int CENTER_Y    = SCREEN_HEIGHT / 2;

  int filled = frame % NUM_CIRCLES;
  display.clearDisplay();
  for (int i = 0; i < NUM_CIRCLES; i++) {
    int x = START_X + i * SPACING;
    if (i == filled) display.fillCircle(x, CENTER_Y, RADIUS, SSD1306_WHITE);
    else             display.drawCircle(x, CENTER_Y, RADIUS, SSD1306_WHITE);
  }
  display.display();
}

// ── Trinksprüche ──────────────────────────────────────────────────────────────

static const char* const trinksprueche[] = {
  "Prost! Auf alles, was uns heute noch erwartet",
  "Zum Wohl und auf einen gelungenen Abend",
  "Hoch die Glaeser, tief die Hemmungen",
  "Jetzt wird nicht geredet, jetzt wird getrunken",
  "Ein Schluck fuer den Durst, zwei fuer die Stimmung",
  "Auf uns, auf euch und auf den Rest im Glas",
  "Auf dich! Ohne dich waer es nur halb so lustig",
  "Zack zack, der Pegel wartet nicht",
  "Hopp hopp, das Getraenk wird sonst warm",
  "Abfahrt! Der Abend hat gerade erst begonnen",
  "Nicht zoegern, das Glas schaut schon traurig",
  "Keine Ausreden, wir sind hier nicht zum Nippen",
  "Einer geht noch, sagen alle und haben recht",
  "Feuer frei! Die Leber ist ein Muskel",
  "Nicht reden, das Glas will Aufmerksamkeit",
  "Zieh durch, wir glauben fest an dich",
  "Hau weg, das Getraenk hat keine Gefuehle",
  "Ziel trinken statt ziellos nippen",
  "Gleich nochmal, zur Sicherheit",
  "Durst loeschen auf professionelle Art",
  "Beweis es, das Glas zweifelt an dir",
  "Das Glas ist voll, tu etwas dagegen",
  "Zeit fuer einen mutigen Schluck",
  "Wer zaehlt schon mit, wir nicht",
  "Leber sagt nein, wir sagen ja",
  "Der Pegel muss stimmen",
  "Trinken ist auch Teamarbeit",
  "Das Glas fuehlt sich unbeachtet",
  "Auf alles, was wir morgen vergessen",
  "Jetzt wird Ernst gemacht",
  "Zeit den Fuellstand zu aendern",
  "Das ist keine Bitte, und auch kein Vorschlag: Trink!",
  "Der Abend verlangt Opfer",
  "Ein Schluck fuer den Mut",
  "Wer langsam trinkt, trinkt zweimal",
  "Nicht diskutieren, demonstrieren",
  "Prost, weil wir es koennen",
  "Nicht nachdenken, ansetzen",
  "Ein Schluck fuer den guten Zweck",
  "Jetzt ist keine Zeit fuer Vernunft",
  "Ein Schluck fuer alle Anwesenden",
  "Nicht schuechtern sein",
  "Das Glas hat es verdient",
  "Jetzt oder nie",
  "Die Runde zaehlt auf dich",
  "Einmal ansetzen, bitte",
};

String getRandomTrinkspruch() {
  constexpr int count = sizeof(trinksprueche) / sizeof(trinksprueche[0]);
  return String(trinksprueche[random(0, count)]);
}
