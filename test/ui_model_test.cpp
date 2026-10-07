// Anzeige-Modell: Ebenen, Haltebalken, Toasts, Symbole, Texte.
#include "check.h"
#include "ui_model.h"
#include <cstring>

using namespace ui;

static game::View idleView() {
  game::View v = {};
  v.screen = game::Screen::IdleGame;
  v.goal = 100.0f;
  return v;
}

static Status status() {
  Status s = {};
  s.battShown = true;
  s.battPercent = 80;
  s.mode = cfg::ScaleMode::Game;
  return s;
}

static const Hold NO_HOLD = {};

// Frame-Text entspricht dem Layout dieser UTF-8-Zeilen?
static bool textIs(const Frame &f, const char *a, const char *b = nullptr,
                   const char *c = nullptr) {
  text::Layout l;
  text::layout(a, b, c, l);
  return memcmp(&l, &f.text, sizeof(l)) == 0;
}

static void testLabels() {
  using button::Zone;
  using cfg::ScaleMode;
  CHECK(!strcmp(holdLabel(Zone::Short, ScaleMode::Game, false), "Tara"));
  // Zielmodus: Game → Duell → Standard → Game
  CHECK(!strcmp(holdLabel(Zone::Mode, ScaleMode::Game, false), "Duell-Modus"));
  CHECK(
      !strcmp(holdLabel(Zone::Mode, ScaleMode::Duel, false), "Standard-Modus"));
  CHECK(
      !strcmp(holdLabel(Zone::Mode, ScaleMode::Standard, false), "Game-Modus"));
  CHECK(!strcmp(holdLabel(Zone::Radio, ScaleMode::Game, false), "AP an"));
  CHECK(!strcmp(holdLabel(Zone::Radio, ScaleMode::Duel, true), "AP aus"));
  CHECK(!strcmp(holdLabel(Zone::Cancel, ScaleMode::Game, true), "Abbrechen"));
  CHECK(!strcmp(ratingText(game::Rating::Shy), "Schüchtern"));
  CHECK(!strcmp(ratingText(game::Rating::Perfect), "Perfekt!"));
}

static void testLayers() {
  Model m;
  game::View v = idleView();
  Status s = status();
  const char *sys[3] = {"Kalibrierung", "Waage leeren", nullptr};
  uint32_t now = 1000;

  Frame f = m.build(v, s, NO_HOLD, nullptr, now);
  CHECK(f.kind == Kind::Text);
  CHECK(textIs(f, "100.0g?"));

  m.toast("Hallo", now);
  f = m.build(v, s, NO_HOLD, nullptr, now + 100);
  CHECK(textIs(f, "Hallo"));
  CHECK(!f.icons);
  f = m.build(v, s, NO_HOLD, sys, now + 200); // System vor Toast
  CHECK(textIs(f, "Kalibrierung", "Waage leeren"));

  Hold h = {true, button::Zone::Mode, button::MODE_MS + 200};
  f = m.build(v, s, h, sys, now + 300); // Haltebalken vor allem
  CHECK(f.kind == Kind::Hold);
  CHECK(!strcmp(f.text.line[0], "Duell-Modus"));

  f = m.build(v, s, NO_HOLD, nullptr, now + TOAST_MS - 1);
  CHECK(textIs(f, "Hallo"));
  f = m.build(v, s, NO_HOLD, nullptr, now + TOAST_MS);
  CHECK(textIs(f, "100.0g?"));
}

static void testToastWrap() {
  Model m;
  game::View v = idleView();
  uint32_t now = 0xFFFFFF00u;
  m.toast("Funk aus", now, 2000);
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, now + 1000),
               "Funk aus")); // nach Ueberlauf
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, now + 2000), "100.0g?"));
  // lange UTF-8-Meldung wird nicht mitten im Zeichen gekuerzt
  char longMsg[200] = {};
  for (int i = 0; i < 40; i++)
    strcat(longMsg, "ü");
  m.toast(longMsg, now + 3000);
  Frame f = m.build(v, status(), NO_HOLD, nullptr, now + 3001);
  for (int l = 0; l < f.text.lines; l++)
    CHECK(strchr(f.text.line[l], '?') == nullptr);
}

static void testHoldBar() {
  Model m;
  game::View v = idleView();
  struct {
    uint32_t ms;
    int px;
  } cases[] = {{0, 0},
               {button::MODE_MS, TICK_MODE_PX},
               {button::RADIO_MS, TICK_RADIO_PX},
               {button::CANCEL_MS, BAR_W},
               {button::CANCEL_MS * 4, BAR_W}};
  for (auto &c : cases) {
    Hold h = {true, button::Zone::Short, c.ms};
    Frame f = m.build(v, status(), h, nullptr, 1000);
    CHECK(f.barPx == c.px);
    CHECK(f.modeDots == 0);
  }
  CHECK(0 < TICK_MODE_PX && TICK_MODE_PX < TICK_RADIO_PX &&
        TICK_RADIO_PX < BAR_W);
}

static void testIcons() {
  Model m;
  game::View v = idleView();
  Status s = status();
  Frame f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.icons && f.battIcon && f.battPercent == 80);
  CHECK(!f.border && !f.shuffle && !f.apIcon && !f.duelIcon);

  v.glassOn = true;
  v.randomMode = true;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.border && f.shuffle);

  // Game-Modus: Gegner werden nie angezeigt
  s.peers = 2;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(!f.duelIcon && f.peers == 0);

  // Duell-Modus: "Vs n" auch ohne Gegner, Akku bleibt sichtbar
  s.mode = cfg::ScaleMode::Duel;
  s.peers = 0;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.duelIcon && f.peers == 0 && f.battIcon);
  s.peers = 2;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.duelIcon && f.peers == 2 && f.battIcon && !f.apIcon);
  s.peers = 150;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.peers == 99);

  // AP: WLAN-Bogen, unabhaengig vom Modus
  s.apOn = true;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.apIcon && f.duelIcon && f.battIcon);
  s.mode = cfg::ScaleMode::Standard;
  v.screen = game::Screen::IdleStandard;
  v.weight = 12.34f;
  f = m.build(v, s, NO_HOLD, nullptr, 1000);
  CHECK(f.apIcon && !f.duelIcon && f.battIcon);
  CHECK(textIs(f, "12.3g"));

  s = status();
  s.battShown = false;
  f = m.build(idleView(), s, NO_HOLD, nullptr, 1000);
  CHECK(!f.battIcon && !f.duelIcon && !f.apIcon);
  s = status();
  s.battPercent = 150;
  f = m.build(idleView(), s, NO_HOLD, nullptr, 1000);
  CHECK(f.battPercent == 100);
}

// Moduswechsel: Haltebalken und Toast zeigen Name + Punkte (Zielmodus gefuellt)
static void testModeDots() {
  using cfg::ScaleMode;
  game::View v = idleView();
  const struct {
    ScaleMode from;
    const char *label;
    uint8_t dots;
  } cases[] = {{ScaleMode::Game, "Duell-Modus", 2},
               {ScaleMode::Duel, "Standard-Modus", 3},
               {ScaleMode::Standard, "Game-Modus", 1}};
  for (auto &c : cases) {
    Model m;
    Status s = status();
    s.mode = c.from;
    Hold h = {true, button::Zone::Mode, button::MODE_MS};
    Frame f = m.build(v, s, h, nullptr, 1000);
    CHECK(f.kind == Kind::Hold && f.modeDots == c.dots);
    CHECK(f.text.size == 1 && f.text.lines == 1);
    CHECK(!strcmp(f.text.line[0], c.label));

    // Toast nach dem Wechsel: gleicher Name, gleiche Punkte
    s.mode = cfg::nextMode(c.from);
    m.modeToast(s.mode, 2000);
    f = m.build(v, s, NO_HOLD, nullptr, 2001);
    CHECK(f.kind == Kind::Text && f.modeDots == c.dots);
    CHECK(f.text.size == 1 && !strcmp(f.text.line[0], c.label));
    CHECK(!f.icons);
    // normaler Toast danach ohne Punkte
    m.toast("Tara", 2100);
    f = m.build(v, s, NO_HOLD, nullptr, 2101);
    CHECK(f.modeDots == 0 && textIs(f, "Tara"));
    f = m.build(v, s, NO_HOLD, nullptr, 2100 + TOAST_MS);
    CHECK(f.modeDots == 0);
  }
  // Andere Zonen ohne Punkte
  Model m;
  Hold h = {true, button::Zone::Radio, button::RADIO_MS};
  Frame f = m.build(v, status(), h, nullptr, 1000);
  CHECK(f.modeDots == 0 && textIs(f, "AP an"));
}

static void testLowBatt() {
  Model m;
  Status s = status();
  s.battLow = true;
  // Akkusymbol mit "!", dauerhaft (kein Blinken)
  for (uint32_t t = 0; t < 2000; t += 100) {
    Frame f = m.build(idleView(), s, NO_HOLD, nullptr, t);
    CHECK(f.battIcon && f.lowBatt);
  }
  // nur in der Statusleiste: nicht ueber anderen Bildschirmen
  game::View v = idleView();
  v.screen = game::Screen::Drinking;
  CHECK(!m.build(v, s, NO_HOLD, nullptr, 0).lowBatt);
  // ohne Akku keine Warnung
  s.battShown = false;
  CHECK(!m.build(idleView(), s, NO_HOLD, nullptr, 0).lowBatt);
  s = status();
  CHECK(!m.build(idleView(), s, NO_HOLD, nullptr, 0).lowBatt);
}

static void testReadyAndDrinking() {
  Model m;
  game::View v = {};
  v.screen = game::Screen::Ready;
  v.screenSince = 5000;
  v.toastIdx = 3;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 5000), "Bereit?"));
  CHECK(
      textIs(m.build(v, status(), NO_HOLD, nullptr, 5000 + READY_PROMPT_MS - 1),
             "Bereit?"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 5000 + READY_PROMPT_MS),
               text::trinkspruch(3)));

  v.screen = game::Screen::Drinking;
  for (int i = 0; i < 10; i++) {
    Frame f =
        m.build(v, status(), NO_HOLD, nullptr, 5000 + (uint32_t)i * ANIM_MS);
    CHECK(f.kind == Kind::Anim);
    CHECK(f.animFrame == i % 5);
    CHECK(!f.icons);
  }

  v.screen = game::Screen::WaitDuel;
  v.ready = 2;
  v.readyTotal = 3;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 6000), "Warte...",
               "2/3 bereit"));
  v.screen = game::Screen::DuelStart;
  v.goal = 73.0f;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 6000), "Ziel", "73.0g"));
  // Tara: kein eigener Hinweis, sondern Idle ohne veraltetes Gewicht
  v.screen = game::Screen::Taring;
  v.weight = 250.0f;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 6000), "73.0g?"));
  Status st = status();
  st.mode = cfg::ScaleMode::Duel;
  CHECK(textIs(m.build(v, st, NO_HOLD, nullptr, 6000), "73.0g?"));
  st.mode = cfg::ScaleMode::Standard;
  CHECK(textIs(m.build(v, st, NO_HOLD, nullptr, 6000), "0.0g"));
}

static void testResultSoloAlternates() {
  Model m;
  game::View v = {};
  v.screen = game::Screen::ResultSolo;
  v.screenSince = 10000;
  v.drankCg = 9995;
  v.durationMs = 4567;
  v.rating = game::Rating::Shy;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 10000), "99.95g",
               "Schüchtern"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 12999), "99.95g",
               "Schüchtern"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 13000), "4.57s",
               "Schüchtern"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 16000), "99.95g",
               "Schüchtern"));
  // Umlaut als CP437 0x81
  Frame f = m.build(v, status(), NO_HOLD, nullptr, 16001);
  CHECK(strchr(f.text.line[1], (char)0x81) != nullptr);

  // neues Ergebnis → beginnt wieder mit dem Wert
  m.build(v, status(), NO_HOLD, nullptr, 19000); // jetzt Zeit
  v.screenSince = 19100;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 19100), "99.95g",
               "Schüchtern"));
}

static void testResultDuel() {
  Model m;
  game::View v = {};
  v.screen = game::Screen::ResultDuel;
  v.screenSince = 1000;
  v.drankCg = 8012;
  v.durationMs = 3000;
  v.rank = 2;
  v.settled = 2;
  v.total = 3;
  v.resultSig = 1;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 1000), "80.12g",
               "~2. Platz"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 4000), "2/3 fertig",
               "3.00s"));
  // neuer Duell-Stand → Wechsel startet neu
  v.isFinal = true;
  v.settled = 3;
  v.resultSig = 2;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 4100), "80.12g",
               "2. Platz!"));
  CHECK(
      textIs(m.build(v, status(), NO_HOLD, nullptr, 7100), "80.12g", "3.00s"));
  v.forfeit = true;
  v.resultSig = 3;
  Frame f = m.build(v, status(), NO_HOLD, nullptr, 7200);
  CHECK(textIs(f, "80.12g", "Zu spät!"));
  CHECK(strchr(f.text.line[1], (char)0x84) != nullptr);
}

static void testSoloToast() {
  Model m;
  game::View v = idleView();
  v.soloFallbackSeq = 5; // Startwert loest nichts aus
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 1000), "100.0g?"));
  v.soloFallbackSeq = 6;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 1100), "Solo!"));
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 1100 + TOAST_MS),
               "100.0g?"));
}

static void testSameFrame() {
  Model a, b;
  game::View v = idleView();
  Frame f1 = a.build(v, status(), NO_HOLD, nullptr, 1000);
  Frame f2 = b.build(v, status(), NO_HOLD, nullptr, 1050);
  CHECK(sameFrame(f1, f2));
  v.glassOn = true;
  Frame f3 = a.build(v, status(), NO_HOLD, nullptr, 1100);
  CHECK(!sameFrame(f1, f3));
  // Idle aendert sich ohne Eingaben nicht → kein Neuzeichnen
  Frame f4 = a.build(v, status(), NO_HOLD, nullptr, 60000);
  CHECK(sameFrame(f3, f4));
}

// ── Statistik ─────────────────────────────────────────────────────────────────

static stats::Tracker tracker() {
  stats::Tracker t;
  t.record({10003, 10000, 4210, false}); // Rekord 0.03, schnellste 4.21
  t.record({9900, 10000, 3870, true});   // schnellste 3.87, Ganz ok
  t.duelFinal(1, 2, false);
  t.record({7000, 5000, 9000, false}); // weit daneben
  return t;
}

static void testAchievementInResult() {
  stats::Tracker t;
  t.record({10003, 10000, 4210, false});
  Model m;
  game::View v = {};
  v.screen = game::Screen::ResultSolo;
  v.screenSince = 1000;
  v.drankCg = 10003;
  v.durationMs = 4210;
  v.rating = game::Rating::NotBad;
  v.roundSeq = 7;
  Status s = status();
  s.stats = &t;
  s.ach = stats::Achievement::Record;
  s.achSeq = 7;
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, 1000), "100.03g", "Not Bad!"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, 4000), "4.21s", "Not Bad!"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, 7000), "Neuer Rekord!",
               "0.03g daneben"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, 10000), "100.03g", "Not Bad!"));

  // Erfolg einer anderen Runde: nur Wert/Zeit
  Model m2;
  s.achSeq = 6;
  m2.build(v, s, NO_HOLD, nullptr, 1000);
  m2.build(v, s, NO_HOLD, nullptr, 4000);
  CHECK(textIs(m2.build(v, s, NO_HOLD, nullptr, 7000), "100.03g", "Not Bad!"));

  // schnellste Zeit
  Model m3;
  s.achSeq = 7;
  s.ach = stats::Achievement::Fastest;
  m3.build(v, s, NO_HOLD, nullptr, 1000);
  m3.build(v, s, NO_HOLD, nullptr, 4000);
  CHECK(textIs(m3.build(v, s, NO_HOLD, nullptr, 7000), "Schnellste Zeit!",
               "4.21s"));

  // Duell: erst final, nicht bei Aufgabe
  Model m4;
  s.ach = stats::Achievement::Record;
  v.screen = game::Screen::ResultDuel;
  v.rank = 1;
  v.settled = 1;
  v.total = 2;
  v.resultSig = 1;
  m4.build(v, s, NO_HOLD, nullptr, 1000);
  m4.build(v, s, NO_HOLD, nullptr, 4000);
  CHECK(textIs(m4.build(v, s, NO_HOLD, nullptr, 7000), "100.03g", "~1. Platz"));
  v.isFinal = true;
  v.settled = 2;
  v.resultSig = 2;
  m4.build(v, s, NO_HOLD, nullptr, 7100);
  m4.build(v, s, NO_HOLD, nullptr, 10100);
  CHECK(textIs(m4.build(v, s, NO_HOLD, nullptr, 13100), "Neuer Rekord!",
               "0.03g daneben"));
  v.forfeit = true;
  v.resultSig = 3;
  for (uint32_t t2 = 13200; t2 < 25000; t2 += 1000)
    CHECK(!textIs(m4.build(v, s, NO_HOLD, nullptr, t2), "Neuer Rekord!",
                  "0.03g daneben"));
}

static void testStatsRotation() {
  stats::Tracker t = tracker();
  Model m;
  game::View v = idleView();
  v.screenSince = 1000;
  Status s = status();
  s.stats = &t;
  s.statsRotation = true;
  const uint32_t T0 = 1000, A = STATS_AFTER_MS, S = STATS_STEP_MS;

  Frame f = m.build(v, s, NO_HOLD, nullptr, T0);
  CHECK(textIs(f, "100.0g?") && f.icons);
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A - 1);
  CHECK(textIs(f, "100.0g?"));
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A); // erster Schritt: Ziel
  CHECK(textIs(f, "100.0g?") && f.icons);
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + S);
  CHECK(textIs(f, "Bester Treffer", "0.03g daneben", "Ziel 100.0g, 4.21s"));
  CHECK(!f.icons); // ohne Statusleiste
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + 2 * S);
  CHECK(textIs(f, "Schnellste Zeit", "3.87s", "1.00g daneben"));
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + 3 * S);
  CHECK(textIs(f, "3 Runden", "1 Sieg, 1 Duell"));
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + 4 * S);
  CHECK(textIs(f, "Perfekt 0", "Not Bad 1", "Ganz ok 1"));
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + 5 * S);
  CHECK(textIs(f, "Letzte Runden", "+20.00g  -1.00g", "+0.03g"));
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + 6 * S); // wieder Ziel
  CHECK(textIs(f, "100.0g?") && f.icons);

  // Glas aufgestellt: sofort Ziel, Wartezeit beginnt neu
  m.build(v, s, NO_HOLD, nullptr, T0 + A + S);
  v.glassOn = true;
  f = m.build(v, s, NO_HOLD, nullptr, T0 + A + S + 100);
  CHECK(textIs(f, "100.0g?") && f.border);
  v.glassOn = false;
  const uint32_t T1 = T0 + A + S + 200; // erstes Bild ohne Glas
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, T1), "100.0g?"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, T1 + A + S - 1), "100.0g?"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, T1 + A + S), "Bester Treffer",
               "0.03g daneben", "Ziel 100.0g, 4.21s"));

  // Tara/Reset (neuer Bildschirm-Beginn) startet neu
  v.screenSince = T1 + A + S + 50;
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, T1 + A + S + 100), "100.0g?"));

  // Taster gehalten startet neu
  Model m2;
  m2.build(v, s, NO_HOLD, nullptr, T0);
  Hold h = {true, button::Zone::Short, 500};
  m2.build(v, s, h, nullptr, T0 + A);
  CHECK(textIs(m2.build(v, s, NO_HOLD, nullptr, T0 + A + S), "100.0g?"));

  // aus, ohne Statistik oder in anderen Modi: nie
  Model m3;
  s.statsRotation = false;
  m3.build(v, s, NO_HOLD, nullptr, T0);
  CHECK(textIs(m3.build(v, s, NO_HOLD, nullptr, T0 + A + S), "100.0g?"));
  s.statsRotation = true;
  s.stats = nullptr;
  CHECK(textIs(m3.build(v, s, NO_HOLD, nullptr, T0 + 2 * A + S), "100.0g?"));
}

static void testStatsEmpty() {
  stats::Tracker t; // noch keine Runde: rotiert trotzdem
  Model m;
  game::View v = idleView();
  Status s = status();
  s.stats = &t;
  s.statsRotation = true;
  const uint32_t A = STATS_AFTER_MS, S = STATS_STEP_MS;
  m.build(v, s, NO_HOLD, nullptr, 0);
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, A + S), "Bester Treffer",
               "noch keiner"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, A + 2 * S), "Schnellste Zeit",
               "noch keine"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, A + 3 * S), "0 Runden"));
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, A + 4 * S), "Perfekt 0",
               "Not Bad 0", "Ganz ok 0"));
  // ohne Verlauf kein "Letzte Runden"
  CHECK(textIs(m.build(v, s, NO_HOLD, nullptr, A + 5 * S), "100.0g?"));
}

int main() {
  testLabels();
  testLayers();
  testToastWrap();
  testHoldBar();
  testIcons();
  testModeDots();
  testLowBatt();
  testReadyAndDrinking();
  testResultSoloAlternates();
  testResultDuel();
  testSoloToast();
  testSameFrame();
  testAchievementInResult();
  testStatsRotation();
  testStatsEmpty();
  return finish("ui_model_test");
}
