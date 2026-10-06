// Unit-Tests fuer die Texte (UTF-8 -> CP437, Layout, Zahlenformate, Trinksprueche).
#include "check.h"
#include "text_core.h"
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace text;

// ── Hilfen ────────────────────────────────────────────────────────────────────

static std::string cp(const char *utf8) {
  char buf[512];
  size_t n = toCp437(utf8, buf, sizeof(buf));
  return std::string(buf, n);
}

// CP437 der Umlaute
static const char AE = (char)0x84, OE = (char)0x94, UE = (char)0x81;
static const char AE_U = (char)0x8E, OE_U = (char)0x99, UE_U = (char)0x9A, SZ = (char)0xE1;

// Deterministischer Zufall (xorshift32)
static uint32_t g_rng = 0x12345678u;
static uint32_t rnd() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rnd(uint32_t n) {
  return rnd() % n;
}

// ── toCp437 ───────────────────────────────────────────────────────────────────

static void testUmlauts() {
  CHECK(cp("ä") == std::string(1, AE));
  CHECK(cp("ö") == std::string(1, OE));
  CHECK(cp("ü") == std::string(1, UE));
  CHECK(cp("Ä") == std::string(1, AE_U));
  CHECK(cp("Ö") == std::string(1, OE_U));
  CHECK(cp("Ü") == std::string(1, UE_U));
  CHECK(cp("ß") == std::string(1, SZ));

  // Konkrete Bytewerte laut Font
  CHECK((unsigned char)cp("ä")[0] == 0x84);
  CHECK((unsigned char)cp("ö")[0] == 0x94);
  CHECK((unsigned char)cp("ü")[0] == 0x81);
  CHECK((unsigned char)cp("Ä")[0] == 0x8E);
  CHECK((unsigned char)cp("Ö")[0] == 0x99);
  CHECK((unsigned char)cp("Ü")[0] == 0x9A);
  CHECK((unsigned char)cp("ß")[0] == 0xE1);

  char buf[32];
  CHECK(toCp437("äöüÄÖÜß", buf, sizeof(buf)) == 7);
  CHECK(strlen(buf) == 7);
  CHECK(std::string(buf) == std::string({ AE, OE, UE, AE_U, OE_U, UE_U, SZ }));

  CHECK(cp("Gläser") == std::string("Gl") + AE + "ser");
  CHECK(cp("Größe") == std::string("Gr") + OE + SZ + "e");
  CHECK(cp("Übermut") == std::string(1, UE_U) + "bermut");
  CHECK(toCp437("Schüchtern", buf, sizeof(buf)) == 10);

  // Alle Zweibyte-Folgen hinter 0xC3: genau die 7 Umlaute, sonst '?'
  int mapped = 0;
  for (int b = 0x80; b <= 0xBF; b++) {
    char in[3] = { (char)0xC3, (char)b, 0 };
    size_t n = toCp437(in, buf, sizeof(buf));
    CHECK(n == 1);
    if (buf[0] != '?') mapped++;
  }
  CHECK(mapped == 7);
}

static void testAscii() {
  // Alle ASCII-Zeichen 0x01..0x7F unveraendert
  char in[128], out[128];
  for (int i = 1; i < 128; i++) in[i - 1] = (char)i;
  in[127] = 0;
  CHECK(toCp437(in, out, sizeof(out)) == 127);
  CHECK(memcmp(in, out, 128) == 0);

  CHECK(cp("Tara...") == "Tara...");
  CHECK(cp("100.00g") == "100.00g");
  CHECK(cp("") == "");
  CHECK(cp(nullptr) == "");
}

static void testUnknownAndBroken() {
  // Andere gueltige Mehrbyte-Zeichen: genau ein '?'
  CHECK(cp("é") == "?");             // C3 A9
  CHECK(cp("°C") == "?C");           // C2 B0
  CHECK(cp("5 €") == "5 ?");         // E2 82 AC
  CHECK(cp("Bier 🍺!") == "Bier ?!");  // F0 9F 8D BA
  CHECK(cp("\xF4\x8F\xBF\xBF") == "?");  // U+10FFFF
  CHECK(cp("\xEF\xBF\xBF") == "?");      // U+FFFF

  // Abgeschnittene Sequenzen: ein '?', folgendes Zeichen bleibt erhalten
  CHECK(cp("\xC3") == "?");
  CHECK(cp("a\xC3") == "a?");
  CHECK(cp("\xC3" "b") == "?b");
  CHECK(cp("\xE2\x82") == "?");
  CHECK(cp("\xE2\x82x") == "?x");
  CHECK(cp("\xE2x") == "?x");
  CHECK(cp("\xF0\x9F\x8D") == "?");
  CHECK(cp("\xF0\x9F\x8Dz") == "?z");
  CHECK(cp("\xF0\x9F") == "?");
  CHECK(cp("\xC3\xC3\xA4") == std::string("?") + AE);   // neuer Start nach Abbruch
  CHECK(cp("\xE2\x82\xC3\xBC") == std::string("?") + UE);

  // Einzelne Folgebytes und ungueltige Startbytes: je ein '?'
  CHECK(cp("\x80") == "?");
  CHECK(cp("\x80\x80") == "??");
  CHECK(cp("\xBF" "a") == "?a");
  CHECK(cp("\xC0") == "?");
  CHECK(cp("\xC1") == "?");
  for (int b = 0xF5; b <= 0xFF; b++) {
    char in[3] = { (char)b, 'x', 0 };
    CHECK(cp(in) == "?x");
  }

  // Latin-1 statt UTF-8
  CHECK(cp("M\xFCller") == "M?ller");
  CHECK(cp("Gl\xE4ser") == "Gl?ser");

  // Overlongs, Surrogates, > U+10FFFF sind ungueltig (kein Umlaut daraus)
  CHECK(cp("\xC0\xA4") == "??");
  CHECK(cp("\xC1\xBF") == "??");
  CHECK(cp("\xE0\x83\xA4") == "???");
  CHECK(cp("\xE0\x9F\xBF") == "???");
  CHECK(cp("\xED\xA0\x80") == "???");
  CHECK(cp("\xF0\x80\x83\xA4") == "????");
  CHECK(cp("\xF4\x90\x80\x80") == "????");
  // Grenzen des gueltigen 2. Bytes
  CHECK(cp("\xE0\xA0\x80") == "?");
  CHECK(cp("\xED\x9F\xBF") == "?");
  CHECK(cp("\xF0\x90\x80\x80") == "?");
  CHECK(cp("\xF4\x8F\x80\x80") == "?");
  CHECK(cp("\xC2\x80") == "?");
  CHECK(cp("\xDF\xBF") == "?");
}

static void testCp437Limits() {
  char buf[16];

  // outSize 0 und nullptr: nichts schreiben
  memset(buf, 'X', sizeof(buf));
  CHECK(toCp437("abc", buf, 0) == 0);
  CHECK(buf[0] == 'X');
  CHECK(toCp437("abc", nullptr, 10) == 0);
  CHECK(toCp437(nullptr, nullptr, 0) == 0);

  // outSize 1: nur NUL
  memset(buf, 'X', sizeof(buf));
  CHECK(toCp437("abc", buf, 1) == 0);
  CHECK(buf[0] == 0 && buf[1] == 'X');
  memset(buf, 'X', sizeof(buf));
  CHECK(toCp437(nullptr, buf, 5) == 0);
  CHECK(buf[0] == 0 && buf[1] == 'X');

  // Kuerzen zaehlt Glyphen, nicht Bytes
  memset(buf, 'X', sizeof(buf));
  CHECK(toCp437("äöü", buf, 3) == 2);
  CHECK(buf[0] == AE && buf[1] == OE && buf[2] == 0 && buf[3] == 'X');
  CHECK(toCp437("ab€cd", buf, 4) == 3);
  CHECK(std::string(buf) == "ab?");

  // Alle Groessen 0..N: nie ueber outSize hinaus, immer terminiert
  const char *samples[] = { "Hallo Welt", "äöüÄÖÜß", "\xF0\x9F\x8D\xBA\xE2\x82\xAC\xC3",
                            "a\x80" "b\xE2\x82" "c", "" };
  for (const char *s : samples) {
    for (size_t sz = 0; sz < 20; sz++) {
      std::vector<char> out(sz + 8, 'Z');
      size_t n = toCp437(s, out.data(), sz);
      if (sz == 0) {
        CHECK(n == 0 && out[0] == 'Z');
      } else {
        CHECK(n < sz);
        CHECK(out[n] == 0);
        CHECK(strlen(out.data()) == n);
        // Praefix des ungekuerzten Ergebnisses
        CHECK(cp(s).compare(0, n, out.data(), n) == 0);
      }
      for (size_t i = sz; i < out.size(); i++) CHECK(out[i] == 'Z');
    }
  }
}

// Unabhaengige Referenz: Codepunkt dekodieren ("maximal subpart" nach Unicode
// Tabelle 3-7), dann auf CP437 abbilden
static std::string refDecode(const std::string &in) {
  std::string out;
  size_t i = 0;
  while (i < in.size()) {
    unsigned c = (unsigned char)in[i];
    if (c < 0x80) {
      out += (char)c;
      i++;
      continue;
    }
    int need = 0;
    unsigned lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) need = 1;
    else if (c == 0xE0) need = 2, lo = 0xA0;
    else if (c == 0xED) need = 2, hi = 0x9F;
    else if (c >= 0xE1 && c <= 0xEF) need = 2;
    else if (c == 0xF0) need = 3, lo = 0x90;
    else if (c == 0xF4) need = 3, hi = 0x8F;
    else if (c >= 0xF1 && c <= 0xF3) need = 3;
    if (need == 0) {
      out += '?';
      i++;
      continue;
    }
    uint32_t code = c & (0x7Fu >> (need + 1));
    size_t j = i + 1;
    int got = 0;
    while (got < need && j < in.size()) {
      unsigned d = (unsigned char)in[j];
      if (d < (got == 0 ? lo : 0x80u) || d > (got == 0 ? hi : 0xBFu)) break;
      code = (code << 6) | (d & 0x3F);
      got++;
      j++;
    }
    i = j;
    if (got < need) {
      out += '?';
      continue;
    }
    switch (code) {
      case 0xE4: out += AE; break;
      case 0xF6: out += OE; break;
      case 0xFC: out += UE; break;
      case 0xC4: out += AE_U; break;
      case 0xD6: out += OE_U; break;
      case 0xDC: out += UE_U; break;
      case 0xDF: out += SZ; break;
      default: out += '?'; break;
    }
  }
  return out;
}

static void testCp437Reference() {
  int bad = 0;
  // Alle Folgen aus 1 und 2 Bytes
  for (int a = 1; a < 256; a++) {
    std::string s1(1, (char)a);
    if (cp(s1.c_str()) != refDecode(s1)) bad++;
    for (int b = 1; b < 256; b++) {
      std::string s2 = s1 + (char)b;
      if (cp(s2.c_str()) != refDecode(s2)) bad++;
    }
  }
  CHECK(bad == 0);

  // 3 bis 5 Bytes aus den Grenzwerten der Bereiche
  const int edge[] = { 0x01, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xA4, 0xBC, 0xBF, 0xC0, 0xC1,
                       0xC2, 0xC3, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF4, 0xF5 };
  bad = 0;
  int cases = 0;
  for (int a : edge)
    for (int b : edge)
      for (int c : edge) {
        std::string s3 = { (char)a, (char)b, (char)c };
        cases++;
        if (cp(s3.c_str()) != refDecode(s3)) bad++;
        for (int d : edge) {
          std::string s4 = s3 + (char)d;
          cases++;
          if (cp(s4.c_str()) != refDecode(s4)) bad++;
        }
      }
  for (int iter = 0; iter < 20000; iter++) {
    std::string s;
    size_t len = 1 + rnd(12);
    for (size_t i = 0; i < len; i++) s += (char)edge[rnd(sizeof(edge) / sizeof(edge[0]))];
    cases++;
    if (cp(s.c_str()) != refDecode(s)) bad++;
  }
  CHECK(bad == 0);
  CHECK(cases > 400000);
}

// Zufaellige Bytefolgen in exakt grossen Heap-Puffern (ASan erkennt Ueberlesen)
static void testCp437Fuzz() {
  int bad = 0;
  for (int iter = 0; iter < 20000; iter++) {
    size_t len = rnd(24);
    std::vector<char> in(len + 1);
    for (size_t i = 0; i < len; i++) {
      // Viele Bytes aus dem Mehrbyte-Bereich, kein NUL
      uint32_t r = rnd(4);
      in[i] = (char)(r == 0 ? 0x20 + rnd(0x5F) : 0x80 + rnd(0x80));
    }
    in[len] = 0;
    size_t sz = 1 + rnd(30);
    std::vector<char> out(sz);
    size_t n = toCp437(in.data(), out.data(), sz);
    if (n >= sz || out[n] != 0 || strlen(out.data()) != n || n > len) bad++;
    // Jede Glyphe ist ASCII aus der Eingabe, ein Umlaut oder '?'
    for (size_t i = 0; i < n; i++) {
      unsigned char c = (unsigned char)out[i];
      bool ok = c < 0x80 || c == 0x84 || c == 0x94 || c == 0x81 || c == 0x8E || c == 0x99 ||
                c == 0x9A || c == 0xE1;
      if (!ok) bad++;
    }
  }
  CHECK(bad == 0);

  // Verkettung gueltiger Zeichen: Glyphenzahl = Anzahl Zeichen
  const char *chars[] = { "a", "ä", "ß", "€", "🍺", "é", " ", "Ü" };
  for (int iter = 0; iter < 2000; iter++) {
    std::string s;
    int count = (int)rnd(40);
    for (int i = 0; i < count; i++) s += chars[rnd(8)];
    char out[64];
    if (toCp437(s.c_str(), out, sizeof(out)) != (size_t)count) bad++;
  }
  CHECK(bad == 0);
}

// ── Layout ────────────────────────────────────────────────────────────────────

struct Ref {
  int lines;
  int size;
  std::string line[3];
};

// Bisheriger Algorithmus aus displayLines() (Arduino-String, Laenge in Bytes)
static Ref refOld(const std::string &a, const std::string &b, const std::string &c) {
  std::string lines[3] = { a, b, c };
  int numLines = 0;
  int maxLen = 0;
  for (int i = 0; i < 3; i++) {
    if (lines[i].length() > 0) {
      numLines = i + 1;
      if ((int)lines[i].length() > maxLen) maxLen = (int)lines[i].length();
    }
  }
  int size;
  if (numLines <= 2 && maxLen <= 10) {
    size = 2;
  } else {
    size = 1;
    if (numLines == 1 && maxLen > 21) {
      for (int i = 21; i > 0; i--) {
        if (lines[0][i] == ' ') {
          lines[1] = lines[0].substr(i + 1);
          lines[0] = lines[0].substr(0, i);
          numLines = 2;
          break;
        }
      }
      if (numLines == 2 && (int)lines[1].length() > 21) {
        for (int i = 21; i > 0; i--) {
          if (lines[1][i] == ' ') {
            lines[2] = lines[1].substr(i + 1);
            lines[1] = lines[1].substr(0, i);
            numLines = 3;
            break;
          }
        }
      }
    }
  }
  Ref r;
  r.lines = numLines;
  r.size = size;
  for (int i = 0; i < 3; i++) r.line[i] = i < numLines ? lines[i] : "";
  return r;
}

// Neue Regeln: wie oben, aber harter Schnitt ohne Leerzeichen, Kuerzen auf 21,
// leere Zeilen am Ende zaehlen nicht.
static Ref refNew(const std::string &a, const std::string &b, const std::string &c) {
  std::string lines[3] = { a, b, c };
  int numLines = 0;
  size_t maxLen = 0;
  for (int i = 0; i < 3; i++) {
    if (!lines[i].empty()) numLines = i + 1;
    maxLen = std::max(maxLen, lines[i].length());
  }
  Ref r;
  r.size = (numLines <= 2 && maxLen <= 10) ? 2 : 1;
  auto split = [](std::string &from, std::string &to) {
    for (int i = 21; i > 0; i--) {
      if (from[i] == ' ') {
        to = from.substr(i + 1);
        from = from.substr(0, i);
        return;
      }
    }
    to = from.substr(21);
    from = from.substr(0, 21);
  };
  if (numLines == 1 && lines[0].length() > 21) {
    split(lines[0], lines[1]);
    if (lines[1].length() > 21) split(lines[1], lines[2]);
    numLines = 3;
    while (numLines > 0 && lines[numLines - 1].empty()) numLines--;
  }
  r.lines = numLines;
  for (int i = 0; i < 3; i++) r.line[i] = i < numLines ? lines[i].substr(0, 21) : "";
  return r;
}

static bool wellFormed(const Layout &l) {
  if (l.lines > MAX_LINES) return false;
  if (l.size != 1 && l.size != 2) return false;
  for (int i = 0; i < MAX_LINES; i++) {
    if (memchr(l.line[i], 0, LINE_CHARS + 1) == nullptr) return false;
    if (i >= l.lines && l.line[i][0] != 0) return false;
    // Hinter dem NUL nur Nullen (sameFrame vergleicht per memcmp)
    for (size_t k = strlen(l.line[i]); k <= (size_t)LINE_CHARS; k++)
      if (l.line[i][k] != 0) return false;
  }
  if (l.lines > 0 && l.line[l.lines - 1][0] == 0) return false;
  return true;
}

static bool sameAs(const Layout &l, const Ref &r) {
  if (!wellFormed(l)) return false;
  if (l.lines != r.lines || l.size != r.size) return false;
  for (int i = 0; i < MAX_LINES; i++)
    if (std::string(l.line[i]) != r.line[i]) return false;
  return true;
}

static bool fitsOld(const Ref &r) {
  for (int i = 0; i < 3; i++)
    if (r.line[i].length() > 21) return false;
  return r.lines == 0 || !r.line[r.lines - 1].empty();
}

static const char *nz(const std::string &s) {
  return s.empty() ? nullptr : s.c_str();
}

// Vergleicht layout() mit der Referenz; UTF-8 wird vorher nach CP437 gewandelt
static bool matches(const std::string &a, const std::string &b = "", const std::string &c = "") {
  Layout l;
  memset(&l, 0x5A, sizeof(l));
  layout(a.c_str(), b.c_str(), c.c_str(), l);
  bool ok = sameAs(l, refNew(cp(a.c_str()), cp(b.c_str()), cp(c.c_str())));
  // nullptr statt "" ergibt dasselbe
  Layout l2;
  memset(&l2, 0xA5, sizeof(l2));
  layout(nz(a), nz(b), nz(c), l2);
  return ok && memcmp(&l, &l2, sizeof(Layout)) == 0;
}

// Die alten ASCII-Trinksprueche aus display.cpp, gute Beispieltexte
static const char *const OLD_SPRUECHE[] = {
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
constexpr int OLD_COUNT = (int)(sizeof(OLD_SPRUECHE) / sizeof(OLD_SPRUECHE[0]));

static void testLayoutEquivalence() {
  // Beispieltexte: UI-Texte, alte Trinksprueche, Grenzfaelle um 10/21/22/43/44
  std::vector<std::string> samples = {
    "", "Tara...", "Bereit?", "Warte...", "WiFi AUS", "WiFi AN", "Game Mode", "Standard",
    "Reset? Halten...", "Reset OK!", "Funk + AP an", "Standard-Modus", "Abbrechen",
    "123.4g?", "100.00g", "12.34s", "Perfekt!", "1234567890", "12345678901",
    "abcdefghijklmnopqrstu", "abcdefghijklmnopqrstuv", "abcdefghij klmnopqrstu",
    "abcdefghijklmnopqrstu vwxyz", "abcdefghijklmnopqrst uvwxyz", " abcdefghijklmnopqrstuvwxyz",
    "a bcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz",
    "aaaaaaaaaaaaaaaaaaaaa ", "aaaaaaaaaaaaaaaaaaaaa  ", "aaaaaaaaaaaaaaaaaaaa  b",
    "aaaaaaaaaa aaaaaaaaaa bbbbbbbbbb bbbbbbbbbb cccccccccc cccccccccc dddd",
    "aaaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbbb ccccccccccccccccccccc ddd",
    "aaaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbbb ", "a  b  c  d  e  f  g  h  i  j  k  l  m",
    "x                                                    y",
    "Hoch die Gläser, tief die Hemmungen", "Übergrößenträgerhöschen sind schön",
    "äääääääääääääääääääää ööööööööööööööööööööö üüüüüüüüüüüüüüüüüüüüü ßßß",
    "Bier 🍺 Bier 🍺 Bier 🍺 Bier 🍺 Bier 🍺 Bier 🍺",
    std::string(300, 'x'), std::string(300, ' '),
  };
  for (int i = 0; i < OLD_COUNT; i++) samples.push_back(OLD_SPRUECHE[i]);

  int failsNew = 0, failsOld = 0, oldCompared = 0;
  for (const std::string &s : samples) {
    if (!matches(s)) failsNew++;
    for (const std::string &t : { std::string(""), std::string("Ziel"), std::string("123.45g") }) {
      if (!matches(t, s)) failsNew++;
      if (!matches(s, t)) failsNew++;
      if (!matches(s, "", t)) failsNew++;
      if (!matches("", t, s)) failsNew++;
      if (!matches(t, t, s)) failsNew++;
    }
    // Wo der alte Algorithmus auf das Display passte, ist das Ergebnis gleich
    bool ascii = true;
    for (unsigned char ch : s) ascii = ascii && ch < 0x80;
    if (ascii) {
      Ref old = refOld(s, "", "");
      if (fitsOld(old)) {
        oldCompared++;
        Layout l;
        layout(s.c_str(), nullptr, nullptr, l);
        if (!sameAs(l, old)) failsOld++;
      }
    }
  }
  CHECK(failsNew == 0);
  CHECK(failsOld == 0);
  CHECK(oldCompared > 50);

  // Alle alten Trinksprueche liefen schon frueher sauber durch den Umbruch
  for (int i = 0; i < OLD_COUNT; i++) {
    Ref old = refOld(OLD_SPRUECHE[i], "", "");
    CHECK(fitsOld(old));
    Layout l;
    layout(OLD_SPRUECHE[i], nullptr, nullptr, l);
    CHECK(sameAs(l, old));
  }

  // Zufaellige Woerter (ASCII und Umlaute), auch Doppel- und Randleerzeichen
  const char *letters[] = { "a", "b", "z", "Wort", "ä", "ß", "Ü", "€", "🍺", "\xC3" };
  const char *spaces[] = { " ", " ", "  " };
  int fuzzFails = 0, fuzzOld = 0, fuzzOldFails = 0;
  for (int iter = 0; iter < 30000; iter++) {
    std::string parts[3];
    int nParts = (int)rnd(3);  // 0: nur l1, 1: l1+l2, 2: l1+l2+l3
    bool asciiOnly = rnd(2) == 0;
    for (int p = 0; p <= nParts; p++) {
      size_t target = rnd(p == 0 ? 90 : 30);
      while (parts[p].size() < target) {
        if (rnd(10) < 3) parts[p] += spaces[rnd(3)];
        else parts[p] += letters[rnd(asciiOnly ? 4 : 10)];
      }
    }
    // Leere Zeilen mittendrin zulassen
    if (rnd(8) == 0) parts[0].clear();
    if (!matches(parts[0], parts[1], parts[2])) fuzzFails++;
    if (asciiOnly) {
      Ref old = refOld(parts[0], parts[1], parts[2]);
      if (fitsOld(old)) {
        fuzzOld++;
        Layout l;
        layout(parts[0].c_str(), parts[1].c_str(), parts[2].c_str(), l);
        if (!sameAs(l, old)) fuzzOldFails++;
      }
    }
  }
  CHECK(fuzzFails == 0);
  CHECK(fuzzOldFails == 0);
  CHECK(fuzzOld > 1000);
}

// Unabhaengig formulierte Referenz (Glyphen, find_last_of statt Schleife)
static Ref refWrap(const std::string &a, const std::string &b, const std::string &c) {
  std::string g[3] = { refDecode(a), refDecode(b), refDecode(c) };
  Ref r;
  int num = 0;
  size_t longest = 0;
  for (int i = 0; i < 3; i++) {
    if (!g[i].empty()) num = i + 1;
    longest = std::max(longest, g[i].size());
  }
  r.size = (num <= 2 && longest <= (size_t)BIG_CHARS) ? 2 : 1;
  if (num == 1 && g[0].size() > (size_t)LINE_CHARS) {
    std::string rest = g[0];
    for (int k = 0; k < 3; k++) {
      if (k == 2 || rest.size() <= (size_t)LINE_CHARS) {
        g[k] = rest;
        break;
      }
      size_t sp = rest.substr(0, LINE_CHARS + 1).find_last_of(' ');
      if (sp == std::string::npos || sp == 0) {
        g[k] = rest.substr(0, LINE_CHARS);
        rest = rest.substr(LINE_CHARS);
      } else {
        g[k] = rest.substr(0, sp);
        rest = rest.substr(sp + 1);
      }
    }
    num = 3;
    while (num > 0 && g[num - 1].empty()) num--;
  }
  r.lines = num;
  for (int k = 0; k < 3; k++) r.line[k] = k < num ? g[k].substr(0, LINE_CHARS) : "";
  return r;
}

// layout() mit exakt grossen Heap-Puffern (ASan erkennt Ueberlesen)
static bool matchesHeap(const std::string &a, const std::string &b, const std::string &c) {
  std::vector<char> pa(a.c_str(), a.c_str() + a.size() + 1);
  std::vector<char> pb(b.c_str(), b.c_str() + b.size() + 1);
  std::vector<char> pc(c.c_str(), c.c_str() + c.size() + 1);
  Layout l;
  memset(&l, 0xEE, sizeof(l));
  layout(pa.data(), pb.data(), pc.data(), l);
  return sameAs(l, refWrap(a, b, c));
}

// Bis zu zwei Leerzeichen an jeder Position, Laengen um die Umbruchgrenzen
// 21/22, 43/44 und das Ende von Zeile 3 (Arbeitspuffer 66 Glyphen)
static void testLayoutSweep() {
  int bad = 0, cases = 0;
  const int lens[] = { 20, 21, 22, 23, 24, 42, 43, 44, 45, 46, 63, 64, 65, 66, 67, 68, 70, 90 };
  for (int len : lens) {
    for (int p = -1; p < len; p++) {
      for (int q = p; q < len; q++) {
        std::string s((size_t)len, 'a');
        if (p >= 0) s[(size_t)p] = ' ';
        if (q >= 0) s[(size_t)q] = ' ';
        cases++;
        if (!matchesHeap(s, "", "")) bad++;
        // Gleiche Struktur mit Zweibyte-Glyphen (Bytes != Glyphen)
        std::string u;
        for (char ch : s) u += ch == ' ' ? " " : "ü";
        cases++;
        if (!matchesHeap(u, "", "")) bad++;
      }
    }
  }
  CHECK(bad == 0);
  CHECK(cases > 50000);

  // Gemischte Zufallstexte gegen die unabhaengige Referenz
  const char *pieces[] = { "a", "Wort", " ", "  ", "ä", "ß", "€", "\xC3", "\x80", "🍺" };
  bad = 0;
  for (int iter = 0; iter < 5000; iter++) {
    std::string parts[3];
    int nParts = (int)rnd(3);
    for (int p = 0; p <= nParts; p++) {
      size_t target = rnd(p == 0 ? 100 : 40);
      while (parts[p].size() < target) parts[p] += pieces[rnd(10)];
    }
    if (!matchesHeap(parts[0], parts[1], parts[2])) bad++;
  }
  CHECK(bad == 0);
}

// Zeilen 2 und 3 werden nach Glyphen gekuerzt, nicht nach Bytes
static void testLayoutUmlautLines() {
  Layout l;
  std::string u21, u22, u10, u11;
  for (int i = 0; i < 21; i++) u21 += "ü";
  u22 = u21 + "ü";
  for (int i = 0; i < 10; i++) u10 += "ö";
  u11 = u10 + "ö";

  layout("x", u21.c_str(), nullptr, l);
  CHECK(l.lines == 2 && l.size == 1 && wellFormed(l));
  CHECK(std::string(l.line[1]) == std::string(21, UE));
  layout("x", u22.c_str(), u22.c_str(), l);
  CHECK(l.lines == 3 && l.size == 1 && wellFormed(l));
  CHECK(std::string(l.line[1]) == std::string(21, UE));
  CHECK(std::string(l.line[2]) == std::string(21, UE));
  // Groesse nach Glyphen auch in Zeile 2: 10 Umlaute (20 Bytes) noch gross
  layout("Ziel", u10.c_str(), nullptr, l);
  CHECK(l.lines == 2 && l.size == 2);
  CHECK(std::string(l.line[1]) == std::string(10, OE));
  layout("Ziel", u11.c_str(), nullptr, l);
  CHECK(l.lines == 2 && l.size == 1);
  layout(nullptr, u10.c_str(), nullptr, l);
  CHECK(l.lines == 2 && l.size == 2 && l.line[0][0] == 0);
  // Mehrbyte-Zeichen genau auf der Kuerzungsgrenze von Zeile 2
  std::string edge = std::string(20, 'a') + "ß" + "€";
  layout("x", edge.c_str(), nullptr, l);
  CHECK(std::string(l.line[1]) == std::string(20, 'a') + SZ);
  edge = std::string(20, 'a') + "\xE2\x82";  // abgeschnittenes Euro am Ende
  layout("x", edge.c_str(), nullptr, l);
  CHECK(std::string(l.line[1]) == std::string(20, 'a') + "?");
}

static void testLayoutRules() {
  Layout l;

  // Leer: 0 Zeilen
  layout(nullptr, nullptr, nullptr, l);
  CHECK(l.lines == 0);
  CHECK(wellFormed(l));
  layout("", "", "", l);
  CHECK(l.lines == 0);

  // Kurz: Groesse 2
  layout("Tara...", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 2 && std::string(l.line[0]) == "Tara...");
  layout("Ziel", "123.4g", nullptr, l);
  CHECK(l.lines == 2 && l.size == 2);
  CHECK(std::string(l.line[0]) == "Ziel" && std::string(l.line[1]) == "123.4g");

  // Genau 10 Glyphen: Groesse 2, 11: Groesse 1
  layout("1234567890", "1234567890", nullptr, l);
  CHECK(l.lines == 2 && l.size == 2);
  layout("1234567890", "12345678901", nullptr, l);
  CHECK(l.lines == 2 && l.size == 1);
  layout("12345678901", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 1);

  // Glyphen zaehlen, nicht Bytes: "Schüchtern" (11 Bytes, 10 Glyphen)
  CHECK(strlen("Schüchtern") == 11);
  layout("Schüchtern", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 2);
  CHECK(std::string(l.line[0]) == std::string("Sch") + UE + "chtern");
  layout("Größe", "Schüchtern", nullptr, l);
  CHECK(l.lines == 2 && l.size == 2);
  layout("Schüüchtern", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 1);
  // 21 Glyphen, 22 Bytes: kein Umbruch
  CHECK(strlen("Nicht schüchtern sein") == 22);
  layout("Nicht schüchtern sein", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 1);
  CHECK(cp("Nicht schüchtern sein") == l.line[0]);

  // Drei Zeilen: immer Groesse 1, auch wenn kurz
  layout("a", "b", "c", l);
  CHECK(l.lines == 3 && l.size == 1);
  layout(nullptr, nullptr, "c", l);
  CHECK(l.lines == 3 && l.size == 1);
  CHECK(l.line[0][0] == 0 && l.line[1][0] == 0 && std::string(l.line[2]) == "c");
  // Leere erste Zeile zaehlt mit
  layout(nullptr, "x", nullptr, l);
  CHECK(l.lines == 2 && l.size == 2 && l.line[0][0] == 0);
  // Ein einzelnes Leerzeichen ist nicht leer
  layout(" ", nullptr, nullptr, l);
  CHECK(l.lines == 1 && std::string(l.line[0]) == " ");

  // 21 Zeichen passen ohne Umbruch
  layout("abcdefghij klmnopqrst", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 1);
  // 22: Umbruch am letzten Leerzeichen bis Position 21
  layout("abcdefghij klmnopqrstu", nullptr, nullptr, l);
  CHECK(l.lines == 2 && l.size == 1);
  CHECK(std::string(l.line[0]) == "abcdefghij" && std::string(l.line[1]) == "klmnopqrstu");
  // Leerzeichen genau an Position 21
  layout("abcdefghijklmnopqrstu vwx", nullptr, nullptr, l);
  CHECK(l.lines == 2);
  CHECK(std::string(l.line[0]) == "abcdefghijklmnopqrstu" && std::string(l.line[1]) == "vwx");
  // Leerzeichen an Position 22 zaehlt nicht mehr: davor umbrechen
  CHECK(std::string("abc defghijklmnopqrstu wx")[22] == ' ');
  layout("abc defghijklmnopqrstu wx", nullptr, nullptr, l);
  CHECK(l.lines == 2);
  CHECK(std::string(l.line[0]) == "abc" && std::string(l.line[1]) == "defghijklmnopqrstu wx");
  // Kein Leerzeichen (Position 0 zaehlt nicht): harter Schnitt bei 21
  layout(" bcdefghijklmnopqrstuvwxyz", nullptr, nullptr, l);
  CHECK(l.lines == 2);
  CHECK(std::string(l.line[0]) == " bcdefghijklmnopqrstu" && std::string(l.line[1]) == "vwxyz");
  // Endet mit dem Trenn-Leerzeichen: nur eine Zeile
  layout("aaaaaaaaaaaaaaaaaaaaa ", nullptr, nullptr, l);
  CHECK(l.lines == 1 && l.size == 1 && std::string(l.line[0]) == "aaaaaaaaaaaaaaaaaaaaa");
  layout("aaaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbbb ", nullptr, nullptr, l);
  CHECK(l.lines == 2 && l.size == 1);
  CHECK(std::string(l.line[1]) == "bbbbbbbbbbbbbbbbbbbbb");

  // Zweiter Umbruch in Zeile 3
  layout("Prost! Auf alles, was uns heute noch erwartet", nullptr, nullptr, l);
  CHECK(l.lines == 3 && l.size == 1);
  CHECK(std::string(l.line[0]) == "Prost! Auf alles, was");
  CHECK(std::string(l.line[1]) == "uns heute noch");
  CHECK(std::string(l.line[2]) == "erwartet");
  // Zeile 2 ohne Leerzeichen: harter Schnitt
  layout("ab cdefghijklmnopqrstuvwxyz0123456789", nullptr, nullptr, l);
  CHECK(l.lines == 3);
  CHECK(std::string(l.line[0]) == "ab");
  CHECK(std::string(l.line[1]) == "cdefghijklmnopqrstuvw");
  CHECK(std::string(l.line[2]) == "xyz0123456789");

  // Umbruch nach Glyphen: Umlaute verschieben die Position nicht
  layout("äääääääääää ööööööööö üüü", nullptr, nullptr, l);
  CHECK(l.lines == 2);
  CHECK(std::string(l.line[0]) == std::string(11, AE) + " " + std::string(9, OE));
  CHECK(std::string(l.line[1]) == std::string(3, UE));

  // Zeile 3 und sehr lange Texte werden auf 21 gekuerzt
  std::string longText;
  for (int i = 0; i < 40; i++) longText += "Wort ";
  layout(longText.c_str(), nullptr, nullptr, l);
  CHECK(l.lines == 3 && wellFormed(l));
  for (int i = 0; i < 3; i++) CHECK(strlen(l.line[i]) <= (size_t)LINE_CHARS);
  std::string huge(5000, 'x');
  layout(huge.c_str(), huge.c_str(), huge.c_str(), l);
  CHECK(l.lines == 3 && l.size == 1 && wellFormed(l));
  for (int i = 0; i < 3; i++) CHECK(std::string(l.line[i]) == std::string(21, 'x'));
  layout(huge.c_str(), nullptr, nullptr, l);
  CHECK(l.lines == 3 && wellFormed(l));
  for (int i = 0; i < 3; i++) CHECK(std::string(l.line[i]) == std::string(21, 'x'));
  std::string hugeUml;
  for (int i = 0; i < 1000; i++) hugeUml += "ü";
  layout(hugeUml.c_str(), nullptr, nullptr, l);
  CHECK(l.lines == 3 && wellFormed(l));
  for (int i = 0; i < 3; i++) CHECK(std::string(l.line[i]) == std::string(21, UE));

  // Lange erste Zeile mit weiteren Zeilen: kein Umbruch, nur kuerzen
  layout("abcdefghijklmnopqrstuvwxyz", "zwei", nullptr, l);
  CHECK(l.lines == 2 && l.size == 1);
  CHECK(std::string(l.line[0]) == "abcdefghijklmnopqrstu" && std::string(l.line[1]) == "zwei");
  layout("eins", nullptr, "abcdefghijklmnopqrstuvwxyz", l);
  CHECK(l.lines == 3 && std::string(l.line[2]) == "abcdefghijklmnopqrstu");

  // Kaputtes UTF-8 erscheint als '?'
  layout("Bier \xF0\x9F\x8D\xBA", "\xC3", nullptr, l);
  CHECK(l.lines == 2 && l.size == 2);
  CHECK(std::string(l.line[0]) == "Bier ?" && std::string(l.line[1]) == "?");

  // Ganze Struktur deterministisch (Frames werden per memcmp verglichen)
  Layout a, b;
  memset(&a, 0xAA, sizeof(a));
  memset(&b, 0x55, sizeof(b));
  layout("Hallo", nullptr, nullptr, a);
  layout("Hallo", nullptr, nullptr, b);
  CHECK(memcmp(&a, &b, sizeof(Layout)) == 0);
  memset(&a, 0xAA, sizeof(a));
  memset(&b, 0x55, sizeof(b));
  layout("Prost! Auf alles, was uns heute noch erwartet", nullptr, nullptr, a);
  layout("Prost! Auf alles, was uns heute noch erwartet", nullptr, nullptr, b);
  CHECK(memcmp(&a, &b, sizeof(Layout)) == 0);
  // Wiederverwendung: alte Inhalte verschwinden
  layout("x", nullptr, nullptr, a);
  Layout fresh;
  memset(&fresh, 0, sizeof(fresh));
  layout("x", nullptr, nullptr, fresh);
  CHECK(memcmp(&a, &fresh, sizeof(Layout)) == 0);
}

// ── Zahlenformate ─────────────────────────────────────────────────────────────

static std::string fc(int32_t cg) {
  char buf[32];
  fmtCentigrams(cg, buf, sizeof(buf));
  return buf;
}

static std::string fg(float g) {
  char buf[32];
  fmtGrams1(g, buf, sizeof(buf));
  return buf;
}

static std::string refCg(int64_t v) {
  std::string s = v < 0 ? "-" : "";
  int64_t m = v < 0 ? -v : v;
  std::string frac = std::to_string(m % 100);
  if (frac.size() < 2) frac = "0" + frac;
  return s + std::to_string(m / 100) + "." + frac;
}

static std::string refDg(int64_t v) {
  std::string s = v < 0 ? "-" : "";
  int64_t m = v < 0 ? -v : v;
  return s + std::to_string(m / 10) + "." + std::to_string(m % 10);
}

static void testFmtCentigrams() {
  CHECK(fc(10000) == "100.00");
  CHECK(fc(5) == "0.05");
  CHECK(fc(-5) == "-0.05");
  CHECK(fc(-1230) == "-12.30");
  CHECK(fc(0) == "0.00");
  CHECK(fc(1) == "0.01");
  CHECK(fc(-1) == "-0.01");
  CHECK(fc(99) == "0.99");
  CHECK(fc(-99) == "-0.99");
  CHECK(fc(100) == "1.00");
  CHECK(fc(-100) == "-1.00");
  CHECK(fc(12345) == "123.45");
  CHECK(fc(INT32_MAX) == "21474836.47");
  CHECK(fc(INT32_MIN) == "-21474836.48");

  int bad = 0;
  for (int32_t v = -30000; v <= 30000; v++)
    if (fc(v) != refCg(v)) bad++;
  for (int i = 0; i < 20000; i++) {
    int32_t v = (int32_t)rnd();
    if (fc(v) != refCg(v)) bad++;
  }
  CHECK(bad == 0);

  // Puffergrenzen: nie ueber n hinaus, immer terminiert
  char buf[16];
  memset(buf, 'X', sizeof(buf));
  fmtCentigrams(10000, buf, 0);
  CHECK(buf[0] == 'X');
  fmtCentigrams(10000, nullptr, 10);
  fmtCentigrams(10000, buf, 1);
  CHECK(buf[0] == 0 && buf[1] == 'X');
  for (size_t n = 1; n < 14; n++) {
    memset(buf, 'X', sizeof(buf));
    fmtCentigrams(INT32_MIN, buf, n);
    CHECK(strlen(buf) == std::min(n - 1, strlen("-21474836.48")));
    CHECK(std::string("-21474836.48").compare(0, strlen(buf), buf) == 0);
    for (size_t i = n; i < sizeof(buf); i++) CHECK(buf[i] == 'X');
  }
  memset(buf, 'X', sizeof(buf));
  fmtCentigrams(10000, buf, 4);
  CHECK(std::string(buf) == "100" && buf[4] == 'X');
}

static void testFmtGrams1() {
  CHECK(fg(0.0f) == "0.0");
  CHECK(fg(-0.0f) == "0.0");
  CHECK(fg(100.0f) == "100.0");
  CHECK(fg(12.34f) == "12.3");
  CHECK(fg(12.36f) == "12.4");
  CHECK(fg(-12.34f) == "-12.3");
  CHECK(fg(-12.36f) == "-12.4");
  // Exakt halbe Dezigramm: weg von 0
  CHECK(fg(0.25f) == "0.3");
  CHECK(fg(0.75f) == "0.8");
  CHECK(fg(1.25f) == "1.3");
  CHECK(fg(12.25f) == "12.3");
  CHECK(fg(-0.25f) == "-0.3");
  CHECK(fg(-0.75f) == "-0.8");
  CHECK(fg(-12.25f) == "-12.3");
  CHECK(fg(0.05f) == "0.1");
  CHECK(fg(-0.05f) == "-0.1");
  // Nie "-0.0"
  CHECK(fg(-0.04f) == "0.0");
  CHECK(fg(-0.049f) == "0.0");
  CHECK(fg(-1e-7f) == "0.0");
  CHECK(fg(-std::numeric_limits<float>::denorm_min()) == "0.0");
  CHECK(fg(0.04f) == "0.0");
  CHECK(fg(0.96f) == "1.0");
  CHECK(fg(-0.96f) == "-1.0");
  CHECK(fg(9.95f) == "9.9");   // 9.95f liegt knapp unter 9.95
  CHECK(fg(9.96f) == "10.0");
  CHECK(fg(-99.99f) == "-100.0");
  CHECK(fg(1234.5f) == "1234.5");

  // Nicht endlich und riesig: definiert, ohne UB
  CHECK(fg(std::numeric_limits<float>::quiet_NaN()) == "nan");
  CHECK(fg(std::numeric_limits<float>::infinity()) == "inf");
  CHECK(fg(-std::numeric_limits<float>::infinity()) == "-inf");
  CHECK(fg(1e30f) == "214748364.7");
  CHECK(fg(-1e30f) == "-214748364.7");
  CHECK(fg(std::numeric_limits<float>::max()) == "214748364.7");
  CHECK(fg(-std::numeric_limits<float>::max()) == "-214748364.7");

  // Ganze Dezigramm k/10 ergeben genau k
  int bad = 0;
  for (int k = -50000; k <= 50000; k++)
    if (fg((float)k / 10.0f) != refDg(k)) bad++;
  CHECK(bad == 0);

  // Rundung gegen Referenz in long double; nie "-0.0"
  bad = 0;
  int negZero = 0;
  for (int i = 0; i < 50000; i++) {
    float g = ((float)(int32_t)rnd(2000001) - 1000000.0f) / 997.0f;
    long double d = (long double)g * 10.0L;
    long double r = d < 0 ? -std::floor(-d + 0.5L) : std::floor(d + 0.5L);
    if (fg(g) != refDg((int64_t)r)) bad++;
    if (fg(g) == "-0.0") negZero++;
  }
  for (float g = -1.0f; g <= 1.0f; g += 0.001f)
    if (fg(g) == "-0.0") negZero++;
  CHECK(bad == 0);
  CHECK(negZero == 0);

  // Puffergrenzen
  char buf[16];
  memset(buf, 'X', sizeof(buf));
  fmtGrams1(123.4f, buf, 0);
  CHECK(buf[0] == 'X');
  fmtGrams1(123.4f, nullptr, 10);
  fmtGrams1(123.4f, buf, 1);
  CHECK(buf[0] == 0 && buf[1] == 'X');
  for (size_t n = 1; n < 14; n++) {
    memset(buf, 'X', sizeof(buf));
    fmtGrams1(-1e30f, buf, n);
    CHECK(strlen(buf) == std::min(n - 1, strlen("-214748364.7")));
    CHECK(std::string("-214748364.7").compare(0, strlen(buf), buf) == 0);
    for (size_t i = n; i < sizeof(buf); i++) CHECK(buf[i] == 'X');
    memset(buf, 'X', sizeof(buf));
    fmtGrams1(std::numeric_limits<float>::quiet_NaN(), buf, n);
    CHECK(strlen(buf) == std::min(n - 1, (size_t)3));
    for (size_t i = n; i < sizeof(buf); i++) CHECK(buf[i] == 'X');
  }
}

// Jede Rundungsgrenze k + 0.5 Dezigramm und ihre Nachbar-Floats
static void testFmtGrams1Boundaries() {
  int bad = 0, negZero = 0, cases = 0;
  for (int k = -20000; k <= 20000; k++) {
    float f = (float)(((long double)k + 0.5L) / 10.0L);
    float g = std::nextafter(std::nextafter(f, -INFINITY), -INFINITY);
    for (int u = 0; u < 5; u++, g = std::nextafter(g, INFINITY)) {
      long double d = (long double)g * 10.0L;  // exakt
      long double r = d < 0 ? -std::floor(-d + 0.5L) : std::floor(d + 0.5L);
      int64_t q = (int64_t)r;
      std::string got = fg(g);
      cases++;
      if (got != refDg(q)) bad++;
      if (got == "-0.0") negZero++;
    }
  }
  CHECK(bad == 0);
  CHECK(negZero == 0);
  CHECK(cases == 200005);

  // Exakte Haelften als Float (m/4, m ungerade): immer weg von 0
  CHECK(fg(2.25f) == "2.3");
  CHECK(fg(-2.25f) == "-2.3");
  CHECK(fg(99.75f) == "99.8");
  CHECK(fg(-99.75f) == "-99.8");
  CHECK(fg(1000.25f) == "1000.3");
  CHECK(fg(-1000.25f) == "-1000.3");
  // Knapp unter/ueber -0.05: -0.0 darf nie entstehen
  CHECK(fg(std::nextafter(-0.05f, 0.0f)) == "0.0");   // -0.04999999702
  CHECK(fg(-0.05f) == "-0.1");                          // -0.05000000075
  CHECK(fg(-0.0499999f) == "0.0");
  CHECK(fg(-0.0500001f) == "-0.1");
  // Negatives NaN ohne Vorzeichen
  CHECK(fg(-std::numeric_limits<float>::quiet_NaN()) == "nan");
}

// Kein Zeitbezug im Modul; ui_model formatiert aber Dauern (uint32 ms) als
// Hundertstel. Werte nahe dem Uhrueberlauf bleiben korrekt formatiert.
static void testClockNearWrap() {
  const uint32_t nearWrap = 0xFFFFF000u;
  CHECK(fc((int32_t)(nearWrap / 10)) == "4294963.20");
  CHECK(fc((int32_t)((nearWrap + 5) / 10)) == "4294963.20");
  CHECK(fc((int32_t)(0xFFFFFFFFu / 10)) == "4294967.29");
  // Differenz ueber den Ueberlauf hinweg (now - since), wrap-sicher
  uint32_t since = nearWrap, now = 0x00000123u;
  uint32_t dur = (uint32_t)(now - since);
  CHECK(dur == 0x1123u);
  CHECK(fc((int32_t)((dur + 5) / 10)) == "4.39");
  // Als int32 interpretiert wird der Wert negativ, aber korrekt formatiert
  CHECK(fc((int32_t)nearWrap) == "-40.96");
}

// ── Trinksprueche ─────────────────────────────────────────────────────────────

// Umlaute zurueck in die alte Umschrift
static std::string transcribe(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
    if ((unsigned char)s[i] == 0xC3 && i + 1 < s.size()) {
      switch ((unsigned char)s[i + 1]) {
        case 0xA4: r += "ae"; i++; continue;
        case 0xB6: r += "oe"; i++; continue;
        case 0xBC: r += "ue"; i++; continue;
        case 0x84: r += "Ae"; i++; continue;
        case 0x96: r += "Oe"; i++; continue;
        case 0x9C: r += "Ue"; i++; continue;
        case 0x9F: r += "ss"; i++; continue;
        default: break;
      }
    }
    r += s[i];
  }
  return r;
}

static void testTrinksprueche() {
  const int n = trinkspruchCount();
  CHECK(n == 46);
  CHECK(n == OLD_COUNT);

  std::set<std::string> seen;
  int umlautCount = 0;
  for (int i = 0; i < n; i++) {
    const char *s = trinkspruch(i);
    CHECK(s != nullptr);
    if (!s) continue;
    CHECK(strlen(s) > 0);
    seen.insert(s);

    // Gleicher Inhalt wie bisher, nur mit echten Umlauten
    CHECK(transcribe(s) == OLD_SPRUECHE[i]);
    if (transcribe(s) != s) umlautCount++;

    // Gueltiges UTF-8, nur darstellbare Zeichen
    std::string c = cp(s);
    CHECK(c.find('?') == std::string::npos);

    // Passt nach Umbruch in 3 x 21, ohne Kuerzen und ohne harten Schnitt
    Layout l;
    layout(s, nullptr, nullptr, l);
    CHECK(wellFormed(l));
    CHECK(l.lines >= 1 && l.lines <= 3);
    std::string joined;
    for (int k = 0; k < l.lines; k++) {
      CHECK(strlen(l.line[k]) <= (size_t)LINE_CHARS);
      if (k) joined += " ";
      joined += l.line[k];
    }
    CHECK(joined == c);
  }
  CHECK((int)seen.size() == n);
  CHECK(umlautCount == 18);

  // Beispiele aus der Vorgabe
  std::set<std::string> expect = {
    "Hoch die Gläser, tief die Hemmungen",
    "Hopp hopp, das Getränk wird sonst warm",
    "Nicht zögern, das Glas schaut schon traurig",
    "Leber sagt nein, wir sagen ja",
    "Durst löschen auf professionelle Art",
    "Zeit den Füllstand zu ändern",
    "Das Glas fühlt sich unbeachtet",
    "Prost, weil wir es können",
    "Nicht schüchtern sein",
    "Auf dich! Ohne dich wär es nur halb so lustig",
  };
  for (const std::string &e : expect) CHECK(seen.count(e) == 1);

  // Keine alte Umschrift mehr in den Texten
  const char *old[] = { "Glaeser", "fuer", "waer", "raenk", "zoeg", "fuehl", "loesch",
                        "zaehl", "Fuell", "aender", "koenn", "schuech", "Gefuehl" };
  for (int i = 0; i < n; i++)
    for (const char *o : old) CHECK(strstr(trinkspruch(i), o) == nullptr);

  // Index modulo Anzahl, auch negativ und an den int-Grenzen
  CHECK(trinkspruch(n) == trinkspruch(0));
  CHECK(trinkspruch(n + 5) == trinkspruch(5));
  CHECK(trinkspruch(-1) == trinkspruch(n - 1));
  CHECK(trinkspruch(-n) == trinkspruch(0));
  CHECK(trinkspruch(-n - 3) == trinkspruch(n - 3));
  CHECK(trinkspruch(INT_MAX) == trinkspruch(INT_MAX % n));
  CHECK(trinkspruch(INT_MIN) == trinkspruch(((INT_MIN % n) + n) % n));
  CHECK(trinkspruch(INT_MIN) != nullptr);
}

int main() {
  testUmlauts();
  testAscii();
  testUnknownAndBroken();
  testCp437Limits();
  testCp437Reference();
  testCp437Fuzz();
  testLayoutEquivalence();
  testLayoutSweep();
  testLayoutUmlautLines();
  testLayoutRules();
  testFmtCentigrams();
  testFmtGrams1();
  testFmtGrams1Boundaries();
  testClockNearWrap();
  testTrinksprueche();
  return finish("text_core_test");
}
