#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Texte fuer das OLED (rein, ohne Arduino) ──────────────────────────────────
// Quelltexte sind UTF-8. Das Display nutzt den Adafruit-GFX-Font im CP437-Modus
// (display.cp437(true)): ä 0x84, ö 0x94, ü 0x81, Ä 0x8E, Ö 0x99, Ü 0x9A,
// ß 0xE1. Unbekannte oder kaputte UTF-8-Sequenzen werden zu '?'.

namespace text {

constexpr int LINE_CHARS = 21;   // Zeichen pro Zeile bei Textgroesse 1 (128 px)
constexpr int BIG_CHARS = 10;    // Zeichen pro Zeile bei Textgroesse 2
constexpr int MAX_LINES = 3;

// Wandelt UTF-8 nach CP437, schreibt hoechstens outSize-1 Zeichen + NUL.
// Liefert die Anzahl Zeichen (Glyphen) in out.
size_t toCp437(const char *utf8, char *out, size_t outSize);

// Layout wie bisher displayLines(): bis zu 3 Zeilen; Groesse 2, wenn hoechstens
// 2 nicht leere Zeilen mit je hoechstens BIG_CHARS Glyphen, sonst Groesse 1.
// Eine einzelne Zeile > LINE_CHARS wird an Leerzeichen auf bis zu 3 Zeilen
// umbrochen. Eingaben UTF-8 (nullptr oder "" = leer), Ausgabe CP437.
struct Layout {
  uint8_t lines;     // 0..3, letzte nicht leere Zeile + 1
  uint8_t size;      // 1 oder 2
  char line[MAX_LINES][LINE_CHARS + 1];
};
void layout(const char *l1, const char *l2, const char *l3, Layout &out);

// Zahlenformate (ASCII): Centigramm mit 2 Nachkommastellen ("100.00",
// "-0.05"), Gramm mit 1 Nachkommastelle ohne "-0.0".
void fmtCentigrams(int32_t cg, char *out, size_t n);
void fmtGrams1(float g, char *out, size_t n);

// Trinksprueche (UTF-8 mit Umlauten), alle passen nach Umbruch in 3 x 21.
int trinkspruchCount();
const char *trinkspruch(int index);  // index wird modulo Anzahl genommen

}  // namespace text
