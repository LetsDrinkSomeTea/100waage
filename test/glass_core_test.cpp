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
  CHECK(d.lastId() == 1);
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
  CHECK(d.lastId() == 1);
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
  CHECK(d.lastId() == 0);
  // Gedaechtnis bleibt bei unbekannt
  d.place(l, 920.0f, TOL);
  r = d.place(l, 2000.0f, TOL);
  CHECK(r.id == 0);
  CHECK(d.lastId() == 2 && d.lastRefG() == 920.0f);
  // Leere Liste
  List e;
  e.begin(nullptr, 0, 1);
  Detector x;
  CHECK(x.place(e, 500.0f, TOL).id == 0);
  // Grenzen 70 % / 115 % (Euro 0,5: 350..575 g Inhalt). Das zweite Glas
  // ist bei allen Gewichten moeglich, aber nie Kandidat (sonst griffe
  // "nur ein Glas passt")
  Detector b;
  List two;
  static const Glass TWO[] = {{3, "Euro 0,5", 370.0f, 500.0f},
                              {9, "Maß", 650.0f, 1000.0f}};
  two.begin(TWO, 2, 10);
  CHECK(b.place(two, 370.0f + 350.0f, TOL).id == 3);
  Detector c;
  CHECK(c.place(two, 370.0f + 349.0f, TOL).id == 0);
  Detector f;
  CHECK(f.place(two, 370.0f + 575.0f, TOL).id == 3);
  Detector g;
  CHECK(g.place(two, 370.0f + 576.0f, TOL).id == 9); // nur noch Maß moeglich
  // Ein einziges Glas in der Liste gilt immer, solange es nicht ueberlaeuft
  List only;
  only.begin(TWO, 1, 4);
  Detector k;
  CHECK(k.place(only, 370.0f + 100.0f, TOL).id == 3);
  Detector n;
  CHECK(n.place(only, 370.0f + 576.0f, TOL).id == 0);
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

// Nur ein Glas passt ueberhaupt: alle anderen zu schwer oder ueberlaufend
static void testOnlyPossible() {
  // Fall aus dem Web: Gläsle 249,1 g, Krügle 623,4 g, 378 g auf der Waage
  static const Glass TWO[] = {{1, "Gläsle", 249.1f, 300.0f},
                              {2, "Krügle", 623.4f, 400.0f}};
  List l;
  l.begin(TWO, 2, 3);
  Detector d;
  Detection r = d.place(l, 378.0f, TOL); // 43 %: kein Kandidat
  CHECK(r.id == 1 && r.source == Source::Auto);
  CHECK(d.lastId() == 1);

  // Andere Gläser wuerden ueberlaufen (Schnapsglas)
  static const Glass SMALL[] = {{1, "Schnaps", 50.0f, 20.0f},
                                {2, "Tulpe 0,3", 270.0f, 300.0f}};
  List s;
  s.begin(SMALL, 2, 3);
  Detector e;
  CHECK(e.place(s, 400.0f, TOL).id == 2);

  // Zwei moegliche Glaeser: unbekannt
  List all;
  fresh(all);
  Detector f;
  CHECK(f.place(all, 400.0f, TOL).id == 0); // Tulpe 43 %, Euro 33 42 %
  // Ohne Inhalt ueber tol: unbekannt
  Detector g;
  CHECK(g.place(l, 255.0f, TOL).id == 1); // leer (Regel 2)
}

// ── Szenarien am Tisch (Tauschzeit, mehrere Glaeser) ──────────────────────────

static const Glass TABLE[] = {
    {1, "Gläsle", 249.1f, 300.0f},
    {2, "Krügle", 623.4f, 400.0f},
    {3, "Euro 0,5", 370.0f, 500.0f},
    {4, "Euro 0,33", 260.0f, 330.0f},
};
constexpr uint32_t SWAP = 5 * 60000u;   // Standard-Tauschzeit
constexpr uint32_t QUICK = 60000u;      // direkt hintereinander
constexpr uint32_t PAUSE = 10 * 60000u; // Pause, dazwischen getrunken

struct Table {
  List l;
  Detector d;
  uint32_t now = 1000;
  uint32_t swap;
  explicit Table(uint32_t swapMs = SWAP) : swap(swapMs) {
    l.begin(TABLE, 4, 5);
  }
  // Glas aufstellen (nach dt ms), Runde spielen, Endgewicht rest
  uint16_t put(float w, uint32_t dt = QUICK) {
    now += dt;
    return d.place(l, w, TOL, now, swap).id;
  }
  void round(float rest) {
    now += 20000;
    d.settle(rest, now);
  }
};

static void testScenarioAlone() {
  // Allein mit dem Gläsle, Runde fuer Runde leerer, dann nachgefuellt
  Table t;
  CHECK(t.put(549) == 1);
  t.round(460);
  CHECK(t.put(460) == 1);
  t.round(380);
  CHECK(t.put(380) == 1);
  t.round(262);
  CHECK(t.put(262) == 1); // fast leer
  CHECK(t.put(545) == 1); // nachgefuellt
  // Allein mit dem Krügle bis fast leer (wiegt dann wie ein volles Gläsle)
  Table k;
  CHECK(k.put(1023) == 2);
  k.round(750);
  CHECK(k.put(750) == 2);
  k.round(640);
  CHECK(k.put(640) == 2);
  CHECK(k.put(1015) == 2);
}

static void testScenarioTwoAlternating() {
  // Zu zweit an einer Waage: A Gläsle, B Krügle, abwechselnd. Halbes Krügle
  // (900 g) wiegt wie eine volle Euro 0,5: das gemerkte Krügle gewinnt.
  Table t;
  CHECK(t.put(549) == 1);
  t.round(460);
  CHECK(t.put(1023) == 2);
  t.round(900);
  CHECK(t.put(460) == 1);
  t.round(380);
  CHECK(t.put(900) == 2);
  t.round(780);
  CHECK(t.put(380) == 1);
  t.round(300);
  CHECK(t.put(780) == 2);
  // Zwei gleiche Gläsle: egal, wer welches hat
  Table g;
  CHECK(g.put(549) == 1);
  g.round(450);
  CHECK(g.put(551) == 1);
  g.round(470);
  CHECK(g.put(450) == 1);
  CHECK(g.put(470) == 1);
}

static void testScenarioThree() {
  // Zu dritt reihum, direkt hintereinander: die volle Euro 0,5 (870 g) nach
  // dem Krügle (910 g) ist ein neues Glas, kein abgetrunkenes Krügle
  Table t;
  CHECK(t.put(549) == 1);
  t.round(470);
  CHECK(t.put(1023) == 2);
  t.round(910);
  CHECK(t.put(870) == 3);
  t.round(760);
  CHECK(t.put(470) == 1);
  t.round(390);
  CHECK(t.put(910) == 2);
  t.round(800);
  CHECK(t.put(760) == 3);
  t.round(650);
  CHECK(t.put(390) == 1);
  CHECK(t.put(800) == 2);
  CHECK(t.put(650) == 3);
}

static void testScenarioSwapVsPause() {
  // Direkt hintereinander: leichter als zuletzt = anderes Glas
  Table t;
  CHECK(t.put(1023) == 2);
  t.round(1000);
  CHECK(t.put(870) == 3); // volle Euro 0,5
  Table e;
  CHECK(e.put(870) == 3);
  e.round(800);
  CHECK(e.put(590) == 4); // volle Euro 0,33
  // Nach der Pause: leichter = dazwischen getrunken, dasselbe Glas
  Table p;
  CHECK(p.put(870) == 3);
  p.round(760);
  CHECK(p.put(550, PAUSE) == 3); // wiegt wie ein volles Gläsle
  Table q;
  CHECK(q.put(870) == 3);
  q.round(760);
  CHECK(q.put(600, PAUSE) == 3); // wiegt wie eine volle Euro 0,33
  // Innerhalb der Tauschzeit dazwischen getrunken: springt (bekannte Grenze)
  Table r;
  CHECK(r.put(870) == 3);
  r.round(760);
  CHECK(r.put(550) == 1);
  // Grenze der Tauschzeit
  Table b;
  CHECK(b.put(870) == 3);
  b.round(760);
  CHECK(b.put(550, SWAP - 1) == 1);
  Table c;
  CHECK(c.put(870) == 3);
  c.round(760);
  CHECK(c.put(550, SWAP) == 3);
  // Tauschzeit 0 = aus: immer wie nach einer Pause
  Table o(0);
  CHECK(o.put(870) == 3);
  o.round(760);
  CHECK(o.put(550) == 3);
  // Deep-Sleep zaehlt als Pause
  Table s;
  CHECK(s.put(870) == 3);
  s.round(760);
  s.d.pause();
  CHECK(s.put(550, 1000) == 3);
  // Direkt: gleiches Glas mit fast gleichem Gewicht bleibt
  Table k;
  CHECK(k.put(1023) == 2);
  k.round(900);
  CHECK(k.put(892) == 2);
}

static void testScenarioOthers() {
  // Krügle halb, dann jemand mit vollem Gläsle
  Table t;
  CHECK(t.put(1023) == 2);
  t.round(800);
  CHECK(t.put(549) == 1);
  // Leeres Glas auflegen, dann eingeschenkt
  Table e;
  CHECK(e.put(1023) == 2);
  e.round(1000);
  CHECK(e.put(370) == 3);
  CHECK(e.put(870) == 3);
  // Nicht aufloesbar: halb volles Krügle, das die Waage noch nie voll gesehen
  // hat, wiegt wie eine volle Euro 0,5
  Table h;
  CHECK(h.put(549) == 1);
  h.round(450);
  CHECK(h.put(830) == 3);
}

static void testRecentMemory() {
  // Fuenftes Glas verdraengt das aelteste
  static const Glass FIVE[] = {
      {1, "A", 100.0f, 300.0f},  {2, "B", 500.0f, 300.0f},
      {3, "C", 900.0f, 300.0f},  {4, "D", 1300.0f, 300.0f},
      {5, "E", 1700.0f, 300.0f},
  };
  List l;
  l.begin(FIVE, 5, 6);
  Detector d;
  for (int i = 0; i < 5; i++)
    CHECK(d.place(l, FIVE[i].emptyG + 300.0f, TOL).id == FIVE[i].id);
  CHECK(d.lastId() == 5);
  CHECK(d.memory().recent[3].id == 2); // A ist herausgefallen
  // Wieder benutzt → nach vorn, ohne Doppel
  CHECK(d.place(l, 500.0f + 300.0f, TOL).id == 2);
  CHECK(d.lastId() == 2 && d.memory().recent[1].id == 5);
  int twos = 0;
  for (int i = 0; i < RECENT; i++)
    twos += d.memory().recent[i].id == 2;
  CHECK(twos == 1);
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
  testOnlyPossible();
  testScenarioAlone();
  testScenarioTwoAlternating();
  testScenarioThree();
  testScenarioSwapVsPause();
  testScenarioOthers();
  testRecentMemory();
  return finish("glass_core_test");
}
