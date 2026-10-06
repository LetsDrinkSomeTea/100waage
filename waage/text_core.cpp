#include "text_core.h"
#include <cmath>
#include <stdio.h>
#include <string.h>

namespace text {

// ── UTF-8 nach CP437 ──────────────────────────────────────────────────────────

namespace {

// Zweites Byte nach 0xC3 (U+00C0..U+00FF) auf CP437, 0 = nicht im Font
char latin1Cp437(unsigned char second) {
  switch (second) {
    case 0xA4: return (char)0x84;  // ä
    case 0xB6: return (char)0x94;  // ö
    case 0xBC: return (char)0x81;  // ü
    case 0x84: return (char)0x8E;  // Ä
    case 0x96: return (char)0x99;  // Ö
    case 0x9C: return (char)0x9A;  // Ü
    case 0x9F: return (char)0xE1;  // ß
    default: return 0;
  }
}

// Liest ein Zeichen ab s, setzt *len auf die verbrauchten Bytes (>= 1) und
// liefert die CP437-Glyphe. Kaputte Sequenzen (fehlende/falsche Folgebytes,
// Overlongs, Surrogates, > U+10FFFF) verbrauchen nur das gueltige Praefix,
// damit ein folgendes ASCII-Zeichen erhalten bleibt.
char decodeOne(const unsigned char *s, size_t *len) {
  unsigned char c = s[0];
  *len = 1;
  if (c < 0x80) return (char)c;

  int need;
  unsigned char lo = 0x80, hi = 0xBF;  // erlaubter Bereich des 2. Bytes
  if (c >= 0xC2 && c <= 0xDF) {
    need = 1;
  } else if (c >= 0xE0 && c <= 0xEF) {
    need = 2;
    if (c == 0xE0) lo = 0xA0;        // keine Overlongs
    else if (c == 0xED) hi = 0x9F;   // keine Surrogates
  } else if (c >= 0xF0 && c <= 0xF4) {
    need = 3;
    if (c == 0xF0) lo = 0x90;        // keine Overlongs
    else if (c == 0xF4) hi = 0x8F;   // max. U+10FFFF
  } else {
    return '?';  // einzelnes Folgebyte oder ungueltiges Startbyte
  }

  // NUL ist nie ein Folgebyte, daher wird nicht ueber das Ende gelesen
  int got = 0;
  while (got < need) {
    unsigned char d = s[1 + got];
    bool ok = got == 0 ? (d >= lo && d <= hi) : (d >= 0x80 && d <= 0xBF);
    if (!ok) break;
    got++;
  }
  *len = 1 + (size_t)got;
  if (got < need) return '?';  // abgeschnittene Sequenz

  if (c == 0xC3) {
    char g = latin1Cp437(s[1]);
    if (g) return g;
  }
  return '?';
}

}  // namespace

size_t toCp437(const char *utf8, char *out, size_t outSize) {
  if (!out || outSize == 0) return 0;
  size_t n = 0;
  if (utf8) {
    const unsigned char *s = (const unsigned char *)utf8;
    while (*s && n + 1 < outSize) {
      size_t len;
      out[n++] = decodeOne(s, &len);
      s += len;
    }
  }
  out[n] = 0;
  return n;
}

// ── Layout ────────────────────────────────────────────────────────────────────

namespace {

// Puffer fuer eine umzubrechende Zeile: Zeile 3 beginnt spaetestens bei Index
// 2 * (LINE_CHARS + 1) und wird auf LINE_CHARS gekuerzt, mehr wird nie gelesen.
constexpr size_t WRAP_BUF = 3 * (LINE_CHARS + 1) + 1;

// Hoechstens LINE_CHARS Glyphen kopieren
void copyLine(char *dst, const char *src, size_t len) {
  if (len > (size_t)LINE_CHARS) len = LINE_CHARS;
  memcpy(dst, src, len);
  dst[len] = 0;
}

// Wie bisher: letztes Leerzeichen an Position 1..LINE_CHARS, sonst harter
// Schnitt bei LINE_CHARS. Liefert Laenge der ersten Zeile und Start des Rests.
void splitAt(const char *s, size_t *cut, size_t *rest) {
  for (int i = LINE_CHARS; i > 0; i--) {
    if (s[i] == ' ') {
      *cut = (size_t)i;
      *rest = (size_t)i + 1;
      return;
    }
  }
  *cut = LINE_CHARS;
  *rest = LINE_CHARS;
}

}  // namespace

void layout(const char *l1, const char *l2, const char *l3, Layout &out) {
  // Komplett nullen, damit memcmp-Vergleiche von Frames stabil sind
  memset(&out, 0, sizeof(out));

  char buf[WRAP_BUF];
  size_t len[MAX_LINES];
  len[0] = toCp437(l1, buf, sizeof(buf));
  len[1] = toCp437(l2, out.line[1], sizeof(out.line[1]));
  len[2] = toCp437(l3, out.line[2], sizeof(out.line[2]));

  int num = 0;
  size_t maxLen = 0;
  for (int i = 0; i < MAX_LINES; i++) {
    if (len[i] > 0) num = i + 1;
    if (len[i] > maxLen) maxLen = len[i];
  }
  out.lines = (uint8_t)num;
  out.size = (num <= 2 && maxLen <= (size_t)BIG_CHARS) ? 2 : 1;

  if (num == 1 && len[0] > (size_t)LINE_CHARS) {
    // Eine lange Zeile auf bis zu 3 Zeilen umbrechen
    size_t cut, rest;
    splitAt(buf, &cut, &rest);
    copyLine(out.line[0], buf, cut);
    const char *s2 = buf + rest;
    size_t len2 = len[0] - rest;
    if (len2 > (size_t)LINE_CHARS) {
      splitAt(s2, &cut, &rest);
      copyLine(out.line[1], s2, cut);
      copyLine(out.line[2], s2 + rest, len2 - rest);
    } else {
      copyLine(out.line[1], s2, len2);
    }
    // Endet der Text mit dem Trenn-Leerzeichen, bleibt der Rest leer
    num = MAX_LINES;
    while (num > 0 && out.line[num - 1][0] == 0) num--;
    out.lines = (uint8_t)num;
  } else {
    copyLine(out.line[0], buf, len[0]);
  }
}

// ── Zahlenformate ─────────────────────────────────────────────────────────────

void fmtCentigrams(int32_t cg, char *out, size_t n) {
  if (!out || n == 0) return;
  // Betrag als uint32_t, damit auch INT32_MIN korrekt bleibt
  uint32_t mag = cg < 0 ? 0u - (uint32_t)cg : (uint32_t)cg;
  snprintf(out, n, "%s%lu.%02lu", cg < 0 ? "-" : "", (unsigned long)(mag / 100),
           (unsigned long)(mag % 100));
}

void fmtGrams1(float g, char *out, size_t n) {
  if (!out || n == 0) return;
  if (!std::isfinite(g)) {
    snprintf(out, n, "%s", std::isnan(g) ? "nan" : (g < 0 ? "-inf" : "inf"));
    return;
  }
  // Dezigramm, kaufmaennisch gerundet (halb weg von 0), auf int32 begrenzt
  constexpr double LIMIT = 2147483647.0;
  double d = (double)g * 10.0;
  int32_t dg;
  if (d >= LIMIT) dg = INT32_MAX;
  else if (d <= -LIMIT) dg = -INT32_MAX;
  else dg = (int32_t)std::lround(d);

  uint32_t mag = dg < 0 ? (uint32_t)-dg : (uint32_t)dg;
  snprintf(out, n, "%s%lu.%lu", dg < 0 ? "-" : "", (unsigned long)(mag / 10),
           (unsigned long)(mag % 10));
}

// ── Trinksprueche ─────────────────────────────────────────────────────────────

namespace {

const char *const TRINKSPRUECHE[] = {
  "Prost! Auf alles, was uns heute noch erwartet",
  "Zum Wohl und auf einen gelungenen Abend",
  "Hoch die Gläser, tief die Hemmungen",
  "Jetzt wird nicht geredet, jetzt wird getrunken",
  "Ein Schluck für den Durst, zwei für die Stimmung",
  "Auf uns, auf euch und auf den Rest im Glas",
  "Auf dich! Ohne dich wär es nur halb so lustig",
  "Zack zack, der Pegel wartet nicht",
  "Hopp hopp, das Getränk wird sonst warm",
  "Abfahrt! Der Abend hat gerade erst begonnen",
  "Nicht zögern, das Glas schaut schon traurig",
  "Keine Ausreden, wir sind hier nicht zum Nippen",
  "Einer geht noch, sagen alle und haben recht",
  "Feuer frei! Die Leber ist ein Muskel",
  "Nicht reden, das Glas will Aufmerksamkeit",
  "Zieh durch, wir glauben fest an dich",
  "Hau weg, das Getränk hat keine Gefühle",
  "Ziel trinken statt ziellos nippen",
  "Gleich nochmal, zur Sicherheit",
  "Durst löschen auf professionelle Art",
  "Beweis es, das Glas zweifelt an dir",
  "Das Glas ist voll, tu etwas dagegen",
  "Zeit für einen mutigen Schluck",
  "Wer zählt schon mit, wir nicht",
  "Leber sagt nein, wir sagen ja",
  "Der Pegel muss stimmen",
  "Trinken ist auch Teamarbeit",
  "Das Glas fühlt sich unbeachtet",
  "Auf alles, was wir morgen vergessen",
  "Jetzt wird Ernst gemacht",
  "Zeit den Füllstand zu ändern",
  "Das ist keine Bitte, und auch kein Vorschlag: Trink!",
  "Der Abend verlangt Opfer",
  "Ein Schluck für den Mut",
  "Wer langsam trinkt, trinkt zweimal",
  "Nicht diskutieren, demonstrieren",
  "Prost, weil wir es können",
  "Nicht nachdenken, ansetzen",
  "Ein Schluck für den guten Zweck",
  "Jetzt ist keine Zeit für Vernunft",
  "Ein Schluck für alle Anwesenden",
  "Nicht schüchtern sein",
  "Das Glas hat es verdient",
  "Jetzt oder nie",
  "Die Runde zählt auf dich",
  "Einmal ansetzen, bitte",
};

constexpr int TRINKSPRUCH_COUNT = (int)(sizeof(TRINKSPRUECHE) / sizeof(TRINKSPRUECHE[0]));

}  // namespace

int trinkspruchCount() {
  return TRINKSPRUCH_COUNT;
}

const char *trinkspruch(int index) {
  int i = index % TRINKSPRUCH_COUNT;
  if (i < 0) i += TRINKSPRUCH_COUNT;
  return TRINKSPRUECHE[i];
}

}  // namespace text
