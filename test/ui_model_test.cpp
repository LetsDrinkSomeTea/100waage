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

static void testLowBattBlink() {
  Model m;
  Status s = status();
  s.battLow = true;
  bool on = false, off = false;
  for (uint32_t t = 0; t < 2000; t += 100) {
    Frame f = m.build(idleView(), s, NO_HOLD, nullptr, t);
    if (f.lowBatt)
      on = true;
    else
      off = true;
    CHECK(f.lowBatt == ((t / BLINK_MS) % 2 == 0));
  }
  CHECK(on && off);
  // auch ueber anderen Bildschirmen
  game::View v = idleView();
  v.screen = game::Screen::Drinking;
  CHECK(m.build(v, s, NO_HOLD, nullptr, 0).lowBatt);
  s.battLow = false;
  CHECK(!m.build(v, s, NO_HOLD, nullptr, 0).lowBatt);
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
  v.screen = game::Screen::Taring;
  CHECK(textIs(m.build(v, status(), NO_HOLD, nullptr, 6000), "Tara..."));
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

int main() {
  testLabels();
  testLayers();
  testToastWrap();
  testHoldBar();
  testIcons();
  testModeDots();
  testLowBattBlink();
  testReadyAndDrinking();
  testResultSoloAlternates();
  testResultDuel();
  testSoloToast();
  testSameFrame();
  return finish("ui_model_test");
}
