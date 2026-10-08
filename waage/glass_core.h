#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Glaeser (rein, ohne Arduino) ──────────────────────────────────────────────
// Liste bekannter Glaeser/Flaschen (Leergewicht, Nennfuellung) und die
// Glasbestimmung beim Aufstellen. Gespeichert wird in glass.cpp (NVS + RTC).
//
// Liste: Die Standardglaeser stehen in der Firmware (glasses_default.h) und
// haben feste IDs. Im NVS liegen nur Abweichungen (Delta): eigene Glaeser,
// geaenderte und geloeschte Standardglaeser. Ein OTA-Update bringt so neue
// Standardglaeser mit, eigene Aenderungen bleiben. Abweichungen, die die neue
// Firmware schon enthaelt (z. B. nach Export und Flashen), fallen beim Laden
// weg (prune).
//
// Bestimmung beim Aufstellen (Glas steht ruhig, absolutes Gewicht W, Toleranz
// tol), Regeln in dieser Reihenfolge (siehe Specs.md):
//  0. Glas im Web festgelegt → dieses Glas.
//  1. W <= Referenz + tol und W >= Leergewicht(letztes Glas) - tol → dasselbe
//     Glas (nicht nachgefuellt, hoechstens weiter abgetrunken).
//  2. |W - Leergewicht| <= tol fuer ein Glas → leeres Glas erkannt.
//  3. Kandidaten: Inhalt W - Leergewicht in FILL_MIN_PCT..FILL_MAX_PCT % der
//     Nennfuellung. Bester Kandidat = kleinste Abweichung von der
//     Nennfuellung; das letzte Glas bleibt, solange es hoechstens
//     LAST_BONUS_PCT Prozentpunkte schlechter passt. Kein Kandidat: das
//     letzte Glas, wenn es mehr als tol und hoechstens FILL_MAX_PCT % Inhalt
//     haette (leer erkannt, dann nur halb eingeschenkt). Sonst das einzige
//     Glas, das ueberhaupt passt (mehr als tol und hoechstens FILL_MAX_PCT %
//     Inhalt; alle anderen leer schon zu schwer oder ueberlaufend).
//  4. Kein Kandidat → unbekannt (Gedaechtnis bleibt).
// Referenz = letztes Gewicht, mit dem das letzte Glas auf der Waage stand
// (beim Aufstellen, nach einer Runde das Endgewicht, siehe settle()).

namespace glass {

constexpr int NAME_GLYPHS = 12;        // Zeichen (Umlaute zaehlen einfach)
constexpr int NAME_BYTES = 24;         // UTF-8, Umlaute 2 Bytes
constexpr int MAX_GLASSES = 24;        // wirksame Liste
constexpr int MAX_DELTAS = 48;         // Abweichungen im NVS
constexpr uint16_t USER_ID_MIN = 1000; // eigene Glaeser; darunter Firmware
constexpr float EMPTY_MIN = 1.0f, EMPTY_MAX = 3000.0f;
constexpr float NOMINAL_MIN = 10.0f, NOMINAL_MAX = 3000.0f;
constexpr int FILL_MIN_PCT = 70, FILL_MAX_PCT = 115;
constexpr int LAST_BONUS_PCT = 10; // Vorsprung des letzten Glases (Regel 3)
constexpr size_t BLOB_MAX = 2 + MAX_DELTAS * (2 + 1 + 1 + NAME_BYTES + 8);

struct Glass {
  uint16_t id;
  char name[NAME_BYTES + 1]; // UTF-8
  float emptyG;              // Leergewicht [g]
  float nominalG;            // Nennfuellung [g]
};

// Herkunft eines Eintrags der wirksamen Liste
enum class Origin : uint8_t { Default, Modified, Custom };

struct Error {
  const char *field;   // nullptr = ok, sonst Feldname wie in der Web-API
  const char *message; // deutsch
};

// Name pruefen: 1..NAME_GLYPHS Zeichen, nur druckbares ASCII und ä ö ü Ä Ö Ü ß
// (Display, CP437), keine Leerzeichen am Rand.
bool validName(const char *utf8);

class List {
public:
  // Standardliste der Firmware (wird nicht kopiert, muss leben bleiben).
  // nextId: erste freie Firmware-ID fuer den Export.
  void begin(const Glass *defaults, int n, uint16_t nextId);

  int count() const { return n_; }
  const Glass &at(int i) const { return list_[i]; }
  Origin origin(int i) const { return origin_[i]; }
  const Glass *find(uint16_t id) const;

  // Geloeschte Standardglaeser (zum Wiederherstellen)
  int deletedCount() const;
  const Glass *deletedAt(int i) const;

  // Aendern. Name wird an den Raendern von Leerzeichen befreit.
  Error add(const char *name, float emptyG, float nominalG,
            uint16_t *newId = nullptr);
  Error update(uint16_t id, const char *name, float emptyG, float nominalG);
  bool remove(uint16_t id);
  bool restore(uint16_t id); // Standardglas auf Firmware-Stand (auch geloescht)
  void restoreAll();         // alle Abweichungen weg

  // Abweichungen als Blob (NVS). load() verwirft ungueltige Daten komplett
  // und liefert dann false; pruned() meldet, ob beim Laden etwas weggefallen
  // ist (dann neu speichern).
  size_t save(uint8_t *buf, size_t cap) const;
  bool load(const uint8_t *buf, size_t len);
  bool pruned() const { return pruned_; }

  // Wirksame Liste als glasses_default.h (eigene Glaeser bekommen neue
  // Firmware-IDs ab nextId). Liefert die Laenge, 0 = Puffer zu klein.
  size_t exportHeader(char *out, size_t cap) const;

private:
  enum Kind : uint8_t { K_CUSTOM = 1, K_MODIFIED = 2, K_DELETED = 3 };
  struct Delta {
    uint8_t kind;
    Glass g; // g.id: Firmware-ID (Modified/Deleted) oder eigene ID
  };
  const Glass *defaultById(uint16_t id) const;
  int deltaIndex(uint16_t id) const;
  bool pushDelta(const Delta &d);
  void eraseDelta(int i);
  void rebuild();
  void prune();

  const Glass *defs_ = nullptr;
  int defCount_ = 0;
  uint16_t nextId_ = 1;
  Delta deltas_[MAX_DELTAS] = {};
  int deltaCount_ = 0;
  Glass list_[MAX_GLASSES] = {};
  Origin origin_[MAX_GLASSES] = {};
  int n_ = 0;
  bool pruned_ = false;
};

// Woher das aktuelle Glas kommt
enum class Source : uint8_t {
  None,   // unbekannt (Regel 4) oder noch nichts bestimmt
  Manual, // im Web festgelegt (Regel 0)
  Same,   // dasselbe Glas wie zuletzt (Regel 1)
  Empty,  // leeres Glas erkannt (Regel 2)
  Auto    // neu bestimmt (Regel 3)
};

const char *sourceKey(Source s); // "none", "manual", "same", "empty", "auto"

// Gedaechtnis (ueberlebt den Deep-Sleep, nicht den Neustart)
struct Memory {
  uint16_t lastId;   // 0 = keins
  uint16_t manualId; // 0 = automatische Erkennung
  float refG;        // Referenzgewicht des letzten Glases
};

struct Detection {
  uint16_t id; // 0 = unbekannt
  Source source;
  float contentG; // W - Leergewicht (nur mit id)
};

class Detector {
public:
  void setMemory(const Memory &m) { mem_ = m; }
  const Memory &memory() const { return mem_; }

  // Im Web festlegen (0 = wieder automatisch)
  void setManual(uint16_t id) { mem_.manualId = id; }
  uint16_t manual() const { return mem_.manualId; }

  // Glas steht ruhig mit absolutem Gewicht absW: Regeln 0..4
  Detection place(const List &l, float absW, float tol);
  // Glas steht nach einer Runde wieder (absolutes Endgewicht): Referenz
  void settle(float absW);

private:
  Memory mem_ = {};
};

} // namespace glass
