// Glaeserliste (Standard + Abweichungen, Speichern, Export) und
// Glasbestimmung (Regeln 0..4, Faelle aus Issue #17).
#include "check.h"
#include "glass_core.h"
#include <cmath>
#include <cstring>

using namespace glass;

static const Glass DEFS[] = {
    {1, "Tulpe 0,3", 270.0f, 300.0f},
    {2, "Krug 0,4", 520.0f, 400.0f},
    {3, "Euro 0,5", 370.0f, 500.0f},
    {4, "Euro 0,33", 260.0f, 330.0f},
};
static const int NDEFS = 4;
constexpr float TOL = 10.0f;

static void fresh(List &l) { l.begin(DEFS, NDEFS, 5); }

// ── Namen ─────────────────────────────────────────────────────────────────────

static void testNames() {
  CHECK(validName("Tulpe 0,3"));
  CHECK(validName("Maß 1,0"));
  CHECK(validName("ÄÖÜäöüß"));
  CHECK(validName("123456789012"));   // 12 Zeichen
  CHECK(!validName("1234567890123")); // 13 Zeichen
  CHECK(validName("ääääääääääää"));   // 12 Umlaute = 24 Bytes
  CHECK(!validName("äääääääääääää")); // 13 Umlaute
  CHECK(!validName(""));
  CHECK(!validName(nullptr));
  CHECK(!validName(" Tulpe"));
  CHECK(!validName("Tulpe "));
  CHECK(!validName("Glas\x01"));
  CHECK(!validName("Glas…"));       // nicht im Display-Zeichensatz
  CHECK(!validName("\xC3"));        // kaputtes UTF-8
  CHECK(!validName("Caf\xC3\xA9")); // é nicht erlaubt
}

// ── Liste ─────────────────────────────────────────────────────────────────────

static void testDefaults() {
  List l;
  fresh(l);
  CHECK(l.count() == 4);
  CHECK(l.at(0).id == 1 && strcmp(l.at(0).name, "Tulpe 0,3") == 0);
  CHECK(l.origin(0) == Origin::Default);
  CHECK(l.find(3) && l.find(3)->nominalG == 500.0f);
  CHECK(!l.find(0));
  CHECK(!l.find(99));
  CHECK(l.deletedCount() == 0);
}

static void testAddUpdateRemove() {
  List l;
  fresh(l);
  uint16_t id = 0;
  Error e = l.add("  Maß 1,0 ", 1234.56f, 1000.0f, &id);
  CHECK(!e.field);
  CHECK(id == USER_ID_MIN);
  CHECK(l.count() == 5);
  const Glass *g = l.find(id);
  CHECK(g && strcmp(g->name, "Maß 1,0") == 0); // getrimmt
  CHECK(g && g->emptyG == 1234.6f);            // 0,1-g-Raster
  CHECK(l.origin(4) == Origin::Custom);

  uint16_t id2 = 0;
  CHECK(!l.add("Weizen", 450.0f, 500.0f, &id2).field);
  CHECK(id2 == USER_ID_MIN + 1);

  // Fehler mit Feldnamen
  CHECK(l.add("", 100, 300).field &&
        strcmp(l.add("", 100, 300).field, "name") == 0);
  CHECK(strcmp(l.add("X", 0.5f, 300).field, "empty") == 0);
  CHECK(strcmp(l.add("X", NAN, 300).field, "empty") == 0);
  CHECK(strcmp(l.add("X", 100, 5).field, "nominal") == 0);
  CHECK(strcmp(l.add("X", 100, 3001).field, "nominal") == 0);
  CHECK(l.count() == 6);

  // Standardglas aendern → Modified, zurueck auf den alten Stand → kein Delta
  CHECK(!l.update(1, "Tulpe 0,3", 275.0f, 300.0f).field);
  CHECK(l.origin(0) == Origin::Modified);
  CHECK(l.find(1)->emptyG == 275.0f);
  CHECK(!l.update(1, "Tulpe 0,3", 270.0f, 300.0f).field);
  CHECK(l.origin(0) == Origin::Default);
  uint8_t blob[BLOB_MAX];
  CHECK(l.save(blob, sizeof blob) > 2);
  CHECK(strcmp(l.update(77, "A", 100, 300).field, "id") == 0);

  // Eigenes Glas aendern bleibt Custom
  CHECK(!l.update(id, "Maß", 1200.0f, 1000.0f).field);
  CHECK(strcmp(l.find(id)->name, "Maß") == 0);

  // Standardglas loeschen → ausgeblendet, wiederherstellbar
  CHECK(l.remove(2));
  CHECK(!l.find(2));
  CHECK(l.count() == 5);
  CHECK(l.deletedCount() == 1);
  CHECK(l.deletedAt(0) && l.deletedAt(0)->id == 2);
  CHECK(!l.remove(2)); // schon weg
  CHECK(l.restore(2));
  CHECK(l.find(2) && l.find(2)->emptyG == 520.0f);
  CHECK(l.at(1).id == 2); // Reihenfolge wie in der Firmware

  // Geaendertes Standardglas loeschen und wiederherstellen → Firmware-Stand
  CHECK(!l.update(3, "Euro", 380.0f, 500.0f).field);
  CHECK(l.remove(3));
  CHECK(l.restore(3));
  CHECK(strcmp(l.find(3)->name, "Euro 0,5") == 0);

  // Eigenes Glas loeschen ist endgueltig
  CHECK(l.remove(id2));
  CHECK(!l.find(id2));
  CHECK(!l.restore(id2));

  l.restoreAll();
  CHECK(l.count() == 4);
  CHECK(l.deletedCount() == 0);
}

static void testFull() {
  List l;
  fresh(l);
  int added = 0;
  char name[16];
  for (int i = 0; i < 40; i++) {
    snprintf(name, sizeof name, "G%d", i);
    if (!l.add(name, 100.0f + i, 300.0f).field)
      added++;
  }
  CHECK(added == MAX_GLASSES - 4);
  CHECK(l.count() == MAX_GLASSES);
}

static void testSaveLoad() {
  List l;
  fresh(l);
  uint16_t id;
  l.add("Weizen", 450.0f, 500.0f, &id);
  l.update(1, "Tulpe", 271.5f, 300.0f);
  l.remove(4);
  uint8_t blob[BLOB_MAX];
  size_t n = l.save(blob, sizeof blob);
  CHECK(n > 2);

  List m;
  fresh(m);
  CHECK(m.load(blob, n));
  CHECK(!m.pruned());
  CHECK(m.count() == 4);
  CHECK(strcmp(m.find(1)->name, "Tulpe") == 0 && m.find(1)->emptyG == 271.5f);
  CHECK(!m.find(4));
  CHECK(m.find(id) && m.find(id)->nominalG == 500.0f);

  // Kaputte Daten: alles verwerfen, Standardliste bleibt
  for (size_t cut = 0; cut < n; cut++) {
    List k;
    fresh(k);
    CHECK(!k.load(blob, cut));
    CHECK(k.count() == 4);
  }
  uint8_t bad[BLOB_MAX];
  memcpy(bad, blob, n);
  bad[0] = 9; // Version
  List k;
  fresh(k);
  CHECK(!k.load(bad, n));
  CHECK(!k.load(nullptr, 0));
  CHECK(k.count() == 4);
  CHECK(l.save(blob, 3) == 0); // Puffer zu klein
}

static void testPruneAfterFirmwareUpdate() {
  // Nutzer hat ein Glas angelegt und ein Standardglas geaendert, exportiert;
  // die neue Firmware enthaelt beides
  List l;
  fresh(l);
  uint16_t id;
  l.add("Weizen", 450.0f, 500.0f, &id);
  l.update(2, "Krug 0,4", 530.0f, 400.0f);
  l.remove(4);
  uint8_t blob[BLOB_MAX];
  size_t n = l.save(blob, sizeof blob);

  static const Glass NEW[] = {
      {1, "Tulpe 0,3", 270.0f, 300.0f}, {2, "Krug 0,4", 530.0f, 400.0f},
      {3, "Euro 0,5", 370.0f, 500.0f},  {5, "Weizen", 450.0f, 500.0f},
      {6, "Neu", 300.0f, 300.0f},
  };
  List m;
  m.begin(NEW, 5, 7);
  CHECK(m.load(blob, n));
  CHECK(m.pruned()); // neu speichern
  CHECK(m.count() == 5);
  CHECK(!m.find(id));                    // Custom steckt jetzt als ID 5 drin
  CHECK(m.origin(1) == Origin::Default); // Krug = Firmware-Stand
  CHECK(m.deletedCount() == 0);          // ID 4 gibt es nicht mehr
  CHECK(m.find(6));                      // neues Standardglas kam dazu

  // Geaendertes Standardglas, das die Firmware nicht mehr kennt → eigenes
  List a;
  fresh(a);
  a.update(3, "Euro alt", 371.0f, 500.0f);
  n = a.save(blob, sizeof blob);
  static const Glass FEW[] = {{1, "Tulpe 0,3", 270.0f, 300.0f}};
  List b;
  b.begin(FEW, 1, 2);
  CHECK(b.load(blob, n));
  CHECK(b.pruned());
  CHECK(b.count() == 2);
  CHECK(b.origin(1) == Origin::Custom);
  CHECK(b.at(1).id >= USER_ID_MIN);
  CHECK(strcmp(b.at(1).name, "Euro alt") == 0);
}

static void testExport() {
  List l;
  fresh(l);
  l.add("Weizen \"W\"", 450.0f, 500.0f);
  l.remove(4);
  char out[2048];
  size_t n = l.exportHeader(out, sizeof out);
  CHECK(n > 0 && n == strlen(out));
  CHECK(strstr(out, "{1, \"Tulpe 0,3\", 270.0f, 300.0f},"));
  CHECK(!strstr(out, "Euro 0,33"));
  CHECK(strstr(out, "{5, \"Weizen \\\"W\\\"\", 450.0f, 500.0f},"));
  CHECK(strstr(out, "DEFAULT_NEXT_ID = 6;"));
  CHECK(l.exportHeader(out, 50) == 0);
  CHECK(out[0] == 0);
}

// ── Bestimmung ────────────────────────────────────────────────────────────────

static void testRuleZeroManual() {
  List l;
  fresh(l);
  Detector d;
  d.setManual(1);
  Detection r = d.place(l, 870.0f, TOL); // eigentlich volle Euro 0,5
  CHECK(r.id == 1 && r.source == Source::Manual);
  CHECK(r.contentG == 600.0f);
  CHECK(d.memory().lastId == 1);
  d.setManual(0);
  r = d.place(l, 870.0f, TOL);
  CHECK(r.id == 1 && r.source == Source::Same); // weiter von dort
  // Festgelegtes Glas geloescht → wieder automatisch
  d.setManual(4);
  l.remove(4);
  r = d.place(l, 870.0f, TOL);
  CHECK(d.manual() == 0);
  CHECK(r.source != Source::Manual);
}

static void testIssueCases() {
  List l;
  fresh(l);
  Detector d;

  // Krug voll (520 + 400)
  Detection r = d.place(l, 920.0f, TOL);
  CHECK(r.id == 2 && r.source == Source::Auto);
  CHECK(r.contentG == 400.0f);

  // Runde: halb ausgetrunken, Endgewicht 570 g = so schwer wie eine volle
  // Tulpe; wieder aufgestellt → weiter der Krug
  d.settle(570.0f);
  r = d.place(l, 570.0f, TOL);
  CHECK(r.id == 2 && r.source == Source::Same);
  CHECK(r.contentG == 50.0f);
  // Ohne Gedaechtnis waere es die Tulpe
  Detector fresh2;
  CHECK(fresh2.place(l, 570.0f, TOL).id == 1);

  // Zwischendurch ohne Runde angehoben und getrunken
  r = d.place(l, 560.0f, TOL);
  CHECK(r.id == 2 && r.source == Source::Same);

  // Nachgefuellt (910 g): Krug und Euro 0,5 sind Kandidaten, Krug war zuletzt
  r = d.place(l, 910.0f, TOL);
  CHECK(r.id == 2 && r.source == Source::Auto);

  // Leicht nachgefuellt (880 g: Krug 90 %, Euro 0,5 102 %): Vorsprung reicht
  d.settle(700.0f);
  r = d.place(l, 880.0f, TOL);
  CHECK(r.id == 2);

  // Krug weg, volle Euroflasche (870 g) nach einer Runde mit 700 g Rest:
  // Euro passt viel besser (100 % gegen 87,5 %)
  d.settle(700.0f);
  r = d.place(l, 870.0f, TOL);
  CHECK(r.id == 3 && r.source == Source::Auto);

  // Glas leichter als das leere letzte Glas → anderes Glas (Tulpe voll)
  d.settle(400.0f); // Euro fast leer
  r = d.place(l, 360.0f, TOL);
  CHECK(r.id == 3 && r.source == Source::Same); // 360 >= 370 - 10
  r = d.place(l, 300.0f, TOL);
  CHECK(r.id != 3); // leichter als leere Euroflasche
}

static void testEmptyGlass() {
  List l;
  fresh(l);
  Detector d;
  Detection r = d.place(l, 268.0f, TOL); // leere Tulpe (Euro 0,33: 260)
  CHECK(r.id == 1 && r.source == Source::Empty);
  CHECK(std::fabs(r.contentG + 2.0f) < 0.001f);
  CHECK(d.memory().lastId == 1);
  // Leere Euro 0,33
  Detector e;
  CHECK(e.place(l, 259.0f, TOL).id == 4);
  // Leeres Glas loest den Mehrdeutigkeitsfall: halb volle fremde Tulpe nach
  // dem Krug
  Detector k;
  k.place(l, 920.0f, TOL);                // Krug
  CHECK(k.place(l, 560.0f, TOL).id == 2); // nicht aufloesbar: gilt als Krug
  CHECK(k.place(l, 270.0f, TOL).id == 1); // leer aufgelegt: Tulpe
  CHECK(k.place(l, 560.0f, TOL).id == 1); // jetzt richtig
  // Leer erkannt, dann nur zu einem Drittel eingeschenkt (unter 70 %)
  Detector h;
  CHECK(h.place(l, 270.0f, TOL).id == 1);
  r = h.place(l, 400.0f, TOL);
  CHECK(r.id == 1 && r.source == Source::Auto && r.contentG == 130.0f);
  // ... aber nicht ohne Inhalt ueber tol und nicht uebervoll
  Detector m;
  m.place(l, 270.0f, TOL);
  m.settle(250.0f);
  CHECK(m.place(l, 275.0f, TOL).id == 1); // Regel 1/2: leer
  Detector o;
  o.place(l, 270.0f, TOL);
  CHECK(o.place(l, 270.0f + 346.0f, TOL).id != 1); // 115 % von 300 = 345
}

static void testUnknown() {
  List l;
  fresh(l);
  Detector d;
  Detection r = d.place(l, 2000.0f, TOL);
  CHECK(r.id == 0 && r.source == Source::None);
  CHECK(d.memory().lastId == 0);
  // Gedaechtnis bleibt bei unbekannt
  d.place(l, 920.0f, TOL);
  r = d.place(l, 2000.0f, TOL);
  CHECK(r.id == 0);
  CHECK(d.memory().lastId == 2 && d.memory().refG == 920.0f);
  // Leere Liste
  List e;
  e.begin(nullptr, 0, 1);
  Detector x;
  CHECK(x.place(e, 500.0f, TOL).id == 0);
  // Grenzen 70 % / 115 % (Euro 0,5: 350..575 g Inhalt)
  Detector b;
  List only;
  static const Glass ONE[] = {{3, "Euro 0,5", 370.0f, 500.0f}};
  only.begin(ONE, 1, 4);
  CHECK(b.place(only, 370.0f + 350.0f, TOL).id == 3);
  Detector c;
  CHECK(c.place(only, 370.0f + 349.0f, TOL).id == 0);
  Detector f;
  CHECK(f.place(only, 370.0f + 575.0f, TOL).id == 3);
  Detector g;
  CHECK(g.place(only, 370.0f + 576.0f, TOL).id == 0);
}

static void testLostGlass() {
  // Letztes Glas aus der Liste geloescht → wie ohne Gedaechtnis
  List l;
  fresh(l);
  Detector d;
  d.place(l, 920.0f, TOL);
  d.settle(570.0f);
  l.remove(2);
  CHECK(d.place(l, 570.0f, TOL).id == 1);
}

int main() {
  testNames();
  testDefaults();
  testAddUpdateRemove();
  testFull();
  testSaveLoad();
  testPruneAfterFirmwareUpdate();
  testExport();
  testRuleZeroManual();
  testIssueCases();
  testEmptyGlass();
  testUnknown();
  testLostGlass();
  return finish("glass_core_test");
}
