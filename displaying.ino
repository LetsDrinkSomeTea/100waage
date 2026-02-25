const String trinksprueche[] = {
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

void drawBatteryIcon(int x, int y, int percent) {
  // Body: 12 wide × 7 tall
  display.drawRect(x, y, 12, 7, SSD1306_WHITE);
  // Positive nub: 2 wide × 3 tall on the right
  display.fillRect(x + 12, y + 2, 2, 3, SSD1306_WHITE);
  // Fill bar: max 10 px wide inside the body
  int fill = map(constrain(percent, 0, 100), 0, 100, 0, 10);
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, 5, SSD1306_WHITE);
}


void drawWifiIcon(int x, int y) {
  // Compact WiFi symbol: 11×8 px, arcs open upward
  int xc = x + 5, yc = y + 7;  // anchor at bottom-centre
  display.fillRect(xc - 1, yc - 1, 2, 2, SSD1306_WHITE);                  // dot
  display.drawCircleHelper(xc, yc, 3, 0x03, SSD1306_WHITE);               // inner arc
  display.drawCircleHelper(xc, yc, 5, 0x03, SSD1306_WHITE);               // outer arc
}


void printCenteredText(String text, int lineNumber, int totalLines) {
  int16_t tx1, ty1;
  uint16_t width, height;
  display.getTextBounds(text, 0, 0, &tx1, &ty1, &width, &height);
  display.setCursor((SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT / totalLines) * lineNumber + ((SCREEN_HEIGHT / totalLines) - height) / 2);
  display.print(text);
}

void displayLines(String line, String line2, String line3, boolean border) {
  String lines[3] = { line, line2, line3 };

  // Count non-empty lines and find the longest one
  int numLines = 0;
  int maxLen   = 0;
  for (int i = 0; i < 3; i++) {
    if (lines[i] != "") {
      numLines = i + 1;
      if ((int)lines[i].length() > maxLen) maxLen = lines[i].length();
    }
  }
  if (numLines == 0) return;

  // textSize(2): 12 px/char → max ~10 chars in 128 px, 16 px tall (2 lines fill 32 px exactly)
  // textSize(1):  6 px/char → max ~21 chars in 128 px,  8 px tall
  // Use large text only for 1–2 short lines; anything longer/more falls back to small text.
  if (numLines <= 2 && maxLen <= 10) {
    display.setTextSize(2);
  } else {
    display.setTextSize(1);
    // If a single line is too long for size-1, try word-wrapping it
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
      // Second line still too long? try wrapping again
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
  for (int i = 0; i < numLines; i++) {
    printCenteredText(lines[i], i, numLines);
  }
  if (border) {
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  }
  display.display();
}

void displayText(String line, boolean border) {
  displayLines(line, "", "", border);
}

String getTrinkspruch() {
  int index = random(0, sizeof(trinksprueche) / sizeof(trinksprueche[0]));
  return trinksprueche[index];
}

void displayLoadingAnimation(int frame) {
  const int numCircles = 5;
  const int radius = 5;
  const int spacing = radius * 3;
  const int startX = (SCREEN_WIDTH - (numCircles - 1) * spacing) / 2;
  const int centerY = SCREEN_HEIGHT / 2;

  int filledCircle = frame % numCircles;

  display.clearDisplay();
  for (int i = 0; i < numCircles; i++) {
    int x = startX + i * spacing;
    if (i == filledCircle) {
      display.fillCircle(x, centerY, radius, SSD1306_WHITE);
    } else {
      display.drawCircle(x, centerY, radius, SSD1306_WHITE);
    }
  }
  display.display();
}
