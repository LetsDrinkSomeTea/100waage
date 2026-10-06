// Unit-Tests fuer die reine Konfiguration (Defaults, sanitize, validate,
// Parser, UTF-8, AP-Name, diff, Legacy-Import, Zufallsziel).
#include "check.h"
#include "config_core.h"
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace cfg;

static const float NAN_F = std::numeric_limits<float>::quiet_NaN();
static const float INF_F = std::numeric_limits<float>::infinity();

static uint32_t bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

static bool onGrid(float v) {
  return bits(roundf(v * 10.0f) / 10.0f) == bits(v);
}

// Deterministischer Zufall (xorshift32)
static uint32_t g_rng = 0x12345678u;
static uint32_t rnd() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static void setStr(char *dst, size_t size, const char *s) {
  memset(dst, 0, size);
  memcpy(dst, s, strlen(s));
}

// Feldweiser Vergleich (Padding ignoriert, Floats bitgenau, Strings komplett)
static bool same(const Config &a, const Config &b) {
  return memcmp(a.apSSID, b.apSSID, sizeof a.apSSID) == 0 &&
         memcmp(a.adminPassword, b.adminPassword, sizeof a.adminPassword) == 0 &&
         bits(a.scaleFactor) == bits(b.scaleFactor) && bits(a.goal) == bits(b.goal) &&
         bits(a.tolerance) == bits(b.tolerance) && a.displayRotation == b.displayRotation &&
         a.wifiTimeout == b.wifiTimeout && a.sleepTimeout == b.sleepTimeout &&
         bits(a.battDividerRatio) == bits(b.battDividerRatio) && a.scaleMode == b.scaleMode &&
         a.autoResetRange == b.autoResetRange && a.autoZeroEnabled == b.autoZeroEnabled &&
         bits(a.autoZeroThreshold) == bits(b.autoZeroThreshold) &&
         a.autoZeroDelay == b.autoZeroDelay && a.randomModeEnabled == b.randomModeEnabled &&
         bits(a.randomMin) == bits(b.randomMin);
}

static bool sanF(float Config::*f, float in, float &out) {
  Config c = defaults();
  c.*f = in;
  bool ch = sanitize(c);
  out = c.*f;
  return ch;
}

static bool sanU8(uint8_t Config::*f, uint8_t in, uint8_t &out) {
  Config c = defaults();
  c.*f = in;
  bool ch = sanitize(c);
  out = c.*f;
  return ch;
}

// SSID setzen, sanitize; true wenn danach die Default-SSID gesetzt ist
static bool ssidReplaced(const char *ssid) {
  Config c = defaults();
  setStr(c.apSSID, sizeof c.apSSID, ssid);
  bool ch = sanitize(c);
  return ch && strcmp(c.apSSID, DEFAULT_AP_SSID) == 0;
}

static bool ssidKept(const char *ssid) {
  Config c = defaults();
  setStr(c.apSSID, sizeof c.apSSID, ssid);
  return !sanitize(c) && strcmp(c.apSSID, ssid) == 0;
}

static bool rejects(const Config &in, const char *field) {
  Config c = in;
  Error e = validate(c);
  return e.field != nullptr && strcmp(e.field, field) == 0 && e.message != nullptr &&
         e.message[0] != '\0' && same(c, in);
}

static const char *messageFor(const Config &in) {
  Config c = in;
  return validate(c).message;
}

static bool accepts(Config &c) {
  Error e = validate(c);
  return e.field == nullptr && e.message == nullptr;
}

// ── Defaults ──────────────────────────────────────────────────────────────────

static void testDefaults() {
  Config c = defaults();
  CHECK(strcmp(c.apSSID, "100-Waage-Config") == 0);
  CHECK(strcmp(c.adminPassword, "admin") == 0);
  CHECK(c.scaleFactor == 708.0f);
  CHECK(c.goal == 100.0f);
  CHECK(c.tolerance == 10.0f);
  CHECK(c.displayRotation == 0);
  CHECK(c.wifiTimeout == 10);
  CHECK(c.sleepTimeout == 5);
  CHECK(c.battDividerRatio == 2.0f);
  CHECK(c.scaleMode == ScaleMode::Game);
  CHECK(c.autoResetRange == 10);
  CHECK(c.autoZeroEnabled == true);
  CHECK(c.autoZeroThreshold == 2.0f);
  CHECK(c.autoZeroDelay == 5);
  CHECK(c.randomModeEnabled == false);
  CHECK(c.randomMin == 20.0f);
  CHECK(c.scaleFactor == SCALE_FACTOR_DEFAULT && c.battDividerRatio == BATT_RATIO_DEFAULT);

  // String-Reste genullt (reproduzierbares Abbild)
  bool tailZero = true;
  for (size_t i = strlen(c.apSSID); i < sizeof c.apSSID; i++) tailZero &= c.apSSID[i] == 0;
  for (size_t i = strlen(c.adminPassword); i < sizeof c.adminPassword; i++)
    tailZero &= c.adminPassword[i] == 0;
  CHECK(tailZero);

  // Defaults sind in sich gueltig
  Config d = c;
  CHECK(!sanitize(d));
  CHECK(same(d, c));
  Config v = c;
  CHECK(accepts(v));
  CHECK(same(v, c));
  CHECK(diff(c, defaults()) == 0);
}

// ── sanitize: Zahlen ──────────────────────────────────────────────────────────

static void testSanitizeScaleFactor() {
  float o;
  CHECK(!sanF(&Config::scaleFactor, 712.34f, o) && bits(o) == bits(712.34f));
  CHECK(!sanF(&Config::scaleFactor, -1234.5f, o) && bits(o) == bits(-1234.5f));
  CHECK(!sanF(&Config::scaleFactor, 1.0f, o) && o == 1.0f);
  CHECK(!sanF(&Config::scaleFactor, -1.0f, o) && o == -1.0f);
  CHECK(!sanF(&Config::scaleFactor, 1e30f, o) && o == 1e30f);
  CHECK(sanF(&Config::scaleFactor, 0.999f, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, -0.5f, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, 0.0f, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, -0.0f, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, NAN_F, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, INF_F, o) && o == 708.0f);
  CHECK(sanF(&Config::scaleFactor, -INF_F, o) && o == 708.0f);
}

static void testSanitizeTolerance() {
  float o;
  CHECK(!sanF(&Config::tolerance, 7.25f, o) && o == 7.25f);  // kein Raster
  CHECK(!sanF(&Config::tolerance, 2.0f, o) && o == 2.0f);    // = autoZeroThreshold
  CHECK(sanF(&Config::tolerance, 0.49f, o) && o == 10.0f);
  CHECK(sanF(&Config::tolerance, 100.01f, o) && o == 10.0f);
  CHECK(sanF(&Config::tolerance, 0.0f, o) && o == 10.0f);
  CHECK(sanF(&Config::tolerance, -5.0f, o) && o == 10.0f);
  CHECK(sanF(&Config::tolerance, NAN_F, o) && o == 10.0f);
  CHECK(sanF(&Config::tolerance, INF_F, o) && o == 10.0f);

  // Untergrenze 0,5 (autoZeroThreshold muss darunter liegen)
  Config c = defaults();
  c.tolerance = 0.5f;
  c.autoZeroThreshold = 0.5f;
  CHECK(!sanitize(c));
  CHECK(c.tolerance == 0.5f);

  // Obergrenze 100: goal muss dann mindestens 101 sein
  c = defaults();
  c.tolerance = 100.0f;
  c.goal = 200.0f;
  c.randomMin = 150.0f;
  CHECK(!sanitize(c));
  CHECK(c.tolerance == 100.0f && c.goal == 200.0f);
  c.goal = 100.0f;
  CHECK(sanitize(c));
  CHECK(c.tolerance == 100.0f && c.goal == 101.0f && c.randomMin == 101.0f);
}

static void testSanitizeGoal() {
  float o;
  CHECK(!sanF(&Config::goal, 250.0f, o) && o == 250.0f);
  CHECK(!sanF(&Config::goal, 33.3f, o) && bits(o) == bits(33.3f));
  CHECK(!sanF(&Config::goal, 5000.0f, o) && o == 5000.0f);
  CHECK(!sanF(&Config::goal, 20.0f, o) && o == 20.0f);  // = randomMin
  // Raster 0,1 g
  CHECK(sanF(&Config::goal, 100.04f, o) && bits(o) == bits(100.0f));
  CHECK(sanF(&Config::goal, 100.06f, o) && bits(o) == bits(100.1f));
  CHECK(sanF(&Config::goal, 123.449f, o) && bits(o) == bits(123.4f));
  CHECK(sanF(&Config::goal, 4999.97f, o) && o == 5000.0f);
  // Klemmen
  CHECK(sanF(&Config::goal, 6000.0f, o) && o == 5000.0f);
  CHECK(sanF(&Config::goal, 1e30f, o) && o == 5000.0f);
  CHECK(sanF(&Config::goal, 0.0f, o) && o == 11.0f);  // 1 → mindestens tolerance + 1
  CHECK(sanF(&Config::goal, -50.0f, o) && o == 11.0f);
  CHECK(sanF(&Config::goal, 10.5f, o) && o == 11.0f);
  // Nicht endlich → Default
  CHECK(sanF(&Config::goal, NAN_F, o) && o == 100.0f);
  CHECK(sanF(&Config::goal, INF_F, o) && o == 100.0f);
  CHECK(sanF(&Config::goal, -INF_F, o) && o == 100.0f);

  // Genau tolerance + 1 bleibt
  Config c = defaults();
  c.goal = 11.0f;
  c.randomMin = 11.0f;
  CHECK(!sanitize(c));
  CHECK(c.goal == 11.0f);
  // goal unter randomMin: randomMin folgt
  c = defaults();
  c.goal = 11.0f;
  CHECK(sanitize(c));
  CHECK(c.goal == 11.0f && c.randomMin == 11.0f);

  // tolerance + 1 nicht im Raster → naechster Rasterwert darueber
  c = defaults();
  c.tolerance = 0.55f;
  c.goal = 1.0f;
  c.randomMin = 1.0f;
  c.autoZeroThreshold = 0.5f;
  CHECK(sanitize(c));
  CHECK(bits(c.goal) == bits(1.6f));
  CHECK(c.goal >= c.tolerance + 1.0f);
  CHECK(bits(c.randomMin) == bits(1.6f));

  c = defaults();
  c.tolerance = 7.25f;
  c.goal = 8.0f;
  CHECK(sanitize(c));
  CHECK(bits(c.goal) == bits(8.3f));
  CHECK(onGrid(c.goal));

  // Kleinstes moegliches Ziel: tolerance 0,5 → 1,5
  c = defaults();
  c.tolerance = 0.5f;
  c.goal = 1.0f;
  c.autoZeroThreshold = 0.2f;
  CHECK(sanitize(c));
  CHECK(c.goal == 1.5f);
}

static void testSanitizeRandomMin() {
  float o;
  CHECK(!sanF(&Config::randomMin, 20.0f, o) && o == 20.0f);
  CHECK(!sanF(&Config::randomMin, 11.0f, o) && o == 11.0f);    // tolerance + 1
  CHECK(!sanF(&Config::randomMin, 100.0f, o) && o == 100.0f);  // = goal
  CHECK(!sanF(&Config::randomMin, 55.5f, o) && bits(o) == bits(55.5f));
  CHECK(sanF(&Config::randomMin, 10.9f, o) && o == 11.0f);
  CHECK(sanF(&Config::randomMin, 5.0f, o) && o == 11.0f);
  CHECK(sanF(&Config::randomMin, -3.0f, o) && o == 11.0f);
  CHECK(sanF(&Config::randomMin, 150.0f, o) && o == 100.0f);
  CHECK(sanF(&Config::randomMin, 33.33f, o) && bits(o) == bits(33.3f));
  CHECK(sanF(&Config::randomMin, 33.36f, o) && bits(o) == bits(33.4f));
  CHECK(sanF(&Config::randomMin, NAN_F, o) && o == 20.0f);
  CHECK(sanF(&Config::randomMin, INF_F, o) && o == 20.0f);
  CHECK(sanF(&Config::randomMin, -INF_F, o) && o == 20.0f);

  // Obergrenze ist das (bereinigte) Ziel
  Config c = defaults();
  c.goal = 15.0f;
  CHECK(sanitize(c));
  CHECK(c.randomMin == 15.0f);
  c = defaults();
  c.goal = NAN_F;  // → 100
  c.randomMin = 150.0f;
  CHECK(sanitize(c));
  CHECK(c.goal == 100.0f && c.randomMin == 100.0f);
  // Untergrenze nicht im Raster
  c = defaults();
  c.tolerance = 7.25f;
  c.goal = 50.0f;
  c.randomMin = 1.0f;
  CHECK(sanitize(c));
  CHECK(bits(c.randomMin) == bits(8.3f));
}

static void testSanitizeAutoZero() {
  float o;
  CHECK(!sanF(&Config::autoZeroThreshold, 2.0f, o) && o == 2.0f);
  CHECK(!sanF(&Config::autoZeroThreshold, 0.1f, o) && bits(o) == bits(0.1f));
  CHECK(!sanF(&Config::autoZeroThreshold, 10.0f, o) && o == 10.0f);  // = tolerance
  CHECK(!sanF(&Config::autoZeroThreshold, 0.37f, o) && bits(o) == bits(0.37f));
  CHECK(sanF(&Config::autoZeroThreshold, 0.05f, o) && bits(o) == bits(0.1f));
  CHECK(sanF(&Config::autoZeroThreshold, -1.0f, o) && bits(o) == bits(0.1f));
  CHECK(sanF(&Config::autoZeroThreshold, 15.0f, o) && o == 10.0f);  // <= tolerance
  CHECK(sanF(&Config::autoZeroThreshold, NAN_F, o) && o == 2.0f);
  CHECK(sanF(&Config::autoZeroThreshold, INF_F, o) && o == 2.0f);

  Config c = defaults();
  c.tolerance = 50.0f;
  c.autoZeroThreshold = 25.0f;
  CHECK(sanitize(c));
  CHECK(c.autoZeroThreshold == 20.0f);
  c.autoZeroThreshold = 20.0f;
  CHECK(!sanitize(c));
  c = defaults();
  c.tolerance = 0.5f;  // Default 2 g > Toleranz
  CHECK(sanitize(c));
  CHECK(c.autoZeroThreshold == 0.5f);

  uint8_t u;
  CHECK(!sanU8(&Config::autoZeroDelay, 1, u) && u == 1);
  CHECK(!sanU8(&Config::autoZeroDelay, 60, u) && u == 60);
  CHECK(sanU8(&Config::autoZeroDelay, 0, u) && u == 5);
  CHECK(sanU8(&Config::autoZeroDelay, 61, u) && u == 5);
  CHECK(sanU8(&Config::autoZeroDelay, 255, u) && u == 5);
}

static void testSanitizeSmallFields() {
  uint8_t u;
  CHECK(!sanU8(&Config::autoResetRange, 0, u) && u == 0);
  CHECK(!sanU8(&Config::autoResetRange, 100, u) && u == 100);
  CHECK(sanU8(&Config::autoResetRange, 101, u) && u == 100);
  CHECK(sanU8(&Config::autoResetRange, 255, u) && u == 100);

  CHECK(!sanU8(&Config::displayRotation, 0, u) && u == 0);
  CHECK(!sanU8(&Config::displayRotation, 2, u) && u == 2);
  CHECK(sanU8(&Config::displayRotation, 1, u) && u == 0);
  CHECK(sanU8(&Config::displayRotation, 3, u) && u == 0);
  CHECK(sanU8(&Config::displayRotation, 180, u) && u == 0);

  // Timeouts: jeder Wert gueltig (0 = nie)
  CHECK(!sanU8(&Config::wifiTimeout, 0, u) && u == 0);
  CHECK(!sanU8(&Config::wifiTimeout, 255, u) && u == 255);
  CHECK(!sanU8(&Config::sleepTimeout, 0, u) && u == 0);
  CHECK(!sanU8(&Config::sleepTimeout, 255, u) && u == 255);

  float o;
  CHECK(!sanF(&Config::battDividerRatio, 1.0f, o) && o == 1.0f);
  CHECK(!sanF(&Config::battDividerRatio, 6.0f, o) && o == 6.0f);
  CHECK(!sanF(&Config::battDividerRatio, 3.3f, o) && bits(o) == bits(3.3f));
  CHECK(sanF(&Config::battDividerRatio, 0.99f, o) && o == 2.0f);
  CHECK(sanF(&Config::battDividerRatio, 6.01f, o) && o == 2.0f);
  CHECK(sanF(&Config::battDividerRatio, 0.0f, o) && o == 2.0f);
  CHECK(sanF(&Config::battDividerRatio, NAN_F, o) && o == 2.0f);
  CHECK(sanF(&Config::battDividerRatio, INF_F, o) && o == 2.0f);

  Config c = defaults();
  c.scaleMode = ScaleMode::Standard;
  CHECK(!sanitize(c) && c.scaleMode == ScaleMode::Standard);
  c.scaleMode = (ScaleMode)2;
  CHECK(sanitize(c) && c.scaleMode == ScaleMode::Game);
  c.scaleMode = (ScaleMode)255;
  CHECK(sanitize(c) && c.scaleMode == ScaleMode::Game);

  // bool aus Rohspeicher mit Muell-Byte
  c = defaults();
  uint8_t raw = 2;
  memcpy(&c.autoZeroEnabled, &raw, 1);
  raw = 0xFF;
  memcpy(&c.randomModeEnabled, &raw, 1);
  CHECK(sanitize(c));
  memcpy(&raw, &c.autoZeroEnabled, 1);
  CHECK(raw == 1);
  memcpy(&raw, &c.randomModeEnabled, 1);
  CHECK(raw == 1);
  c.autoZeroEnabled = false;
  c.randomModeEnabled = true;
  CHECK(!sanitize(c) && !c.autoZeroEnabled && c.randomModeEnabled);
}

// ── sanitize: Strings ─────────────────────────────────────────────────────────

static void testSanitizeStrings() {
  // Gueltige SSIDs bleiben (UTF-8 erlaubt)
  CHECK(ssidKept("Partywaage"));
  CHECK(ssidKept(" "));
  CHECK(ssidKept("B\xC3\xA4r"));                     // "Baer" mit ae als UTF-8
  CHECK(ssidKept("\xE2\x82\xAC" "uro"));             // Euro-Zeichen + "uro"
  CHECK(ssidKept("Bier \xF0\x9F\x8D\xBA"));          // Bier + Bierkrug U+1F37A
  CHECK(ssidKept("\xC2\xA0nbsp"));                   // U+00A0 ist druckbar
  CHECK(ssidKept("\xF4\x8F\xBF\xBF"));               // U+10FFFF
  CHECK(ssidKept("\xED\x9F\xBF"));                   // U+D7FF
  CHECK(ssidKept("12345678901234567890123456789012"));  // 32 Bytes

  // Leer oder nicht druckbar → Default
  CHECK(ssidReplaced(""));
  CHECK(ssidReplaced("\x01" "abc"));
  CHECK(ssidReplaced("ab\x7F"));
  CHECK(ssidReplaced("a\tb"));
  CHECK(ssidReplaced("a\nb"));
  CHECK(ssidReplaced("\xC2\x80"));          // C1-Steuerzeichen
  CHECK(ssidReplaced("x\xC2\x9F"));
  CHECK(ssidReplaced("\xFF"));
  CHECK(ssidReplaced("ab\x80"));            // einzelnes Folgebyte
  CHECK(ssidReplaced("ab\xC3"));            // abgeschnittene Sequenz
  CHECK(ssidReplaced("\xE2\x82"));
  CHECK(ssidReplaced("\xC3\x28"));          // falsches Folgebyte
  CHECK(ssidReplaced("\xC0\xAF"));          // Overlong
  CHECK(ssidReplaced("\xE0\x80\xAF"));      // Overlong
  CHECK(ssidReplaced("\xF0\x80\x80\xAF"));  // Overlong
  CHECK(ssidReplaced("\xED\xA0\x80"));      // Surrogate
  CHECK(ssidReplaced("\xF4\x90\x80\x80"));  // > U+10FFFF
  CHECK(ssidReplaced("\xF5\x80\x80\x80"));

  // Ersetzt: Rest des Arrays genullt
  Config c = defaults();
  memset(c.apSSID, 'x', sizeof c.apSSID);
  c.apSSID[0] = '\x01';
  CHECK(sanitize(c));
  CHECK(strcmp(c.apSSID, DEFAULT_AP_SSID) == 0);
  bool tailZero = true;
  for (size_t i = strlen(c.apSSID); i < sizeof c.apSSID; i++) tailZero &= c.apSSID[i] == 0;
  CHECK(tailZero);

  // Nicht terminiert → auf 32 Bytes gekuerzt
  c = defaults();
  memset(c.apSSID, 'x', sizeof c.apSSID);
  CHECK(sanitize(c));
  CHECK(strlen(c.apSSID) == SSID_MAX);
  CHECK(std::string(c.apSSID) == std::string(32, 'x'));
  // ... ohne ein Zeichen zu zerschneiden (ae auf Byte 31/32)
  c = defaults();
  memset(c.apSSID, 'x', 31);
  c.apSSID[31] = '\xC3';
  c.apSSID[32] = '\xA4';
  CHECK(sanitize(c));
  CHECK(std::string(c.apSSID) == std::string(31, 'x'));
  // 4-Byte-Zeichen ab Byte 30
  c = defaults();
  memset(c.apSSID, 'y', 30);
  memcpy(c.apSSID + 30, "\xF0\x9F\x8D", 3);
  CHECK(sanitize(c));
  CHECK(std::string(c.apSSID) == std::string(30, 'y'));

  // Passwort: leer → admin, kurze alte bleiben, Inhalt sonst unangetastet
  c = defaults();
  setStr(c.adminPassword, sizeof c.adminPassword, "");
  CHECK(sanitize(c));
  CHECK(strcmp(c.adminPassword, "admin") == 0);
  c = defaults();
  setStr(c.adminPassword, sizeof c.adminPassword, "abc");
  CHECK(!sanitize(c));
  CHECK(strcmp(c.adminPassword, "abc") == 0);
  c = defaults();
  setStr(c.adminPassword, sizeof c.adminPassword, "p\xC3\xA4ss\x01");
  CHECK(!sanitize(c));
  CHECK(strcmp(c.adminPassword, "p\xC3\xA4ss\x01") == 0);
  c = defaults();
  memset(c.adminPassword, 'p', sizeof c.adminPassword);
  CHECK(sanitize(c));
  CHECK(std::string(c.adminPassword) == std::string(31, 'p'));
  c = defaults();
  memset(c.adminPassword, 'p', 30);
  c.adminPassword[30] = '\xC3';
  c.adminPassword[31] = '\xBC';
  CHECK(sanitize(c));
  CHECK(std::string(c.adminPassword) == std::string(30, 'p'));
}

// Wahrt ein bereinigter Config alle Bereichsregeln?
static bool inRanges(const Config &c) {
  size_t n = strnlen(c.apSSID, sizeof c.apSSID);
  size_t p = strnlen(c.adminPassword, sizeof c.adminPassword);
  return n >= 1 && n <= SSID_MAX && p >= 1 && p <= PASSWORD_MAX &&
         std::isfinite(c.scaleFactor) && std::fabs(c.scaleFactor) >= 1.0f &&
         c.tolerance >= 0.5f && c.tolerance <= 100.0f &&
         c.goal >= 1.0f && c.goal <= 5000.0f && onGrid(c.goal) &&
         c.goal >= c.tolerance + 1.0f &&
         c.randomMin <= c.goal && c.randomMin >= std::fmin(c.tolerance + 1.0f, c.goal) &&
         onGrid(c.randomMin) &&
         c.autoZeroThreshold >= 0.1f && c.autoZeroThreshold <= 20.0f &&
         c.autoZeroThreshold <= c.tolerance &&
         c.autoZeroDelay >= 1 && c.autoZeroDelay <= 60 && c.autoResetRange <= 100 &&
         (c.displayRotation == 0 || c.displayRotation == 2) &&
         c.battDividerRatio >= 1.0f && c.battDividerRatio <= 6.0f &&
         (c.scaleMode == ScaleMode::Game || c.scaleMode == ScaleMode::Standard);
}

static float specialFloat() {
  static const float v[] = {NAN_F, INF_F, -INF_F, 0.0f, -0.0f, 1e-40f, 0.05f, 0.1f, 0.5f,
                            0.55f, 1.0f, 1.5f, 2.0f, 6.0f, 10.0f, 20.0f, 100.0f, 100.05f,
                            101.0f, 5000.0f, 5000.04f, 5000.06f, -1.0f, 3e38f, -3e38f};
  return v[rnd() % (sizeof v / sizeof v[0])];
}

// Rohspeicher mit Muell: sanitize scheitert nie, ist idempotent und das
// Ergebnis besteht validate unveraendert.
static void testSanitizeFuzz() {
  int bad = 0;
  for (int iter = 0; iter < 20000; iter++) {
    uint8_t raw[sizeof(Config)];
    for (size_t i = 0; i < sizeof raw; i++) raw[i] = (uint8_t)rnd();
    Config c;
    memcpy(&c, raw, sizeof c);
    // Sonderwerte gezielt einstreuen
    float Config::*floats[] = {&Config::scaleFactor, &Config::goal, &Config::tolerance,
                               &Config::battDividerRatio, &Config::autoZeroThreshold,
                               &Config::randomMin};
    for (auto f : floats)
      if (rnd() % 2) c.*f = specialFloat();
    if (rnd() % 4 == 0) c.apSSID[rnd() % sizeof c.apSSID] = '\0';
    if (rnd() % 4 == 0) c.adminPassword[rnd() % sizeof c.adminPassword] = '\0';

    sanitize(c);
    if (!inRanges(c)) bad++;
    Config d = c;
    if (sanitize(d) || !same(c, d)) bad++;
    Config v = c;
    if (!accepts(v) || !same(v, c)) bad++;
  }
  CHECK(bad == 0);
}

// ── validate ──────────────────────────────────────────────────────────────────

static void testValidateFields() {
  Config base = defaults();
  Config c;

  // apSSID
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "");
  CHECK(rejects(c, "apSSID"));
  c = base;
  memset(c.apSSID, 'a', sizeof c.apSSID);  // 33 Bytes ohne NUL
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "a\x01");
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "a\tb");
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "a\x7F");
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "\xC2\x85");  // C1
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "ab\xFF");
  CHECK(rejects(c, "apSSID"));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "12345678901234567890123456789012");
  CHECK(accepts(c));
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "B\xC3\xA4r \xF0\x9F\x8D\xBA");
  CHECK(accepts(c));

  // tolerance
  c = base;
  c.tolerance = 0.5f;
  c.autoZeroThreshold = 0.5f;
  CHECK(accepts(c));
  c = base;
  c.tolerance = 0.49f;
  CHECK(rejects(c, "tolerance"));
  c = base;
  c.tolerance = 100.0f;
  c.goal = 101.0f;
  CHECK(accepts(c));
  c = base;
  c.tolerance = 100.01f;
  c.goal = 200.0f;
  CHECK(rejects(c, "tolerance"));
  c = base;
  c.tolerance = NAN_F;
  CHECK(rejects(c, "tolerance"));
  c = base;
  c.tolerance = INF_F;
  CHECK(rejects(c, "tolerance"));

  // goal: Bereich
  c = base;
  c.goal = 5000.0f;
  CHECK(accepts(c));
  c = base;
  c.goal = 5000.1f;
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = 5000.06f;  // gerundet 5000,1
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = 5000.04f;  // gerundet 5000,0
  CHECK(accepts(c));
  CHECK(c.goal == 5000.0f);
  c = base;
  c.goal = 0.5f;
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = -1.0f;
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = NAN_F;
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = INF_F;
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = 3e38f;
  CHECK(rejects(c, "goal"));
  const char *rangeMsg = messageFor(c);

  // goal >= tolerance + 1
  c = base;
  c.goal = 11.0f;
  CHECK(accepts(c));
  c = base;
  c.goal = 10.9f;
  CHECK(rejects(c, "goal"));
  const char *relMsg = messageFor(c);
  CHECK(rangeMsg && relMsg && strcmp(rangeMsg, relMsg) != 0);
  CHECK(relMsg && strstr(relMsg, "Toleranz") != nullptr);
  c = base;
  c.goal = 10.96f;  // gerundet 11,0
  CHECK(accepts(c));
  CHECK(bits(c.goal) == bits(11.0f));
  c = base;
  c.tolerance = 0.5f;
  c.autoZeroThreshold = 0.5f;
  c.goal = 1.5f;
  CHECK(accepts(c));
  c.goal = 1.44f;  // gerundet 1,4 < 1,5
  CHECK(rejects(c, "goal"));
  c.goal = 1.46f;  // gerundet 1,5
  CHECK(accepts(c));
  CHECK(c.goal == 1.5f);

  // goal wird bei Erfolg aufs Raster gerundet
  c = base;
  c.goal = 123.44f;
  CHECK(accepts(c));
  CHECK(bits(c.goal) == bits(123.4f));
  c = base;
  c.goal = 123.46f;
  CHECK(accepts(c));
  CHECK(bits(c.goal) == bits(123.5f));

  // randomMin: geklemmt statt abgelehnt
  c = base;
  c.randomMin = 5.0f;
  CHECK(accepts(c));
  CHECK(c.randomMin == 11.0f);
  c = base;
  c.randomMin = 150.0f;
  CHECK(accepts(c));
  CHECK(c.randomMin == 100.0f);
  c = base;
  c.randomMin = 33.33f;
  CHECK(accepts(c));
  CHECK(bits(c.randomMin) == bits(33.3f));
  c = base;
  c.randomMin = 1e30f;
  CHECK(accepts(c));
  CHECK(c.randomMin == 100.0f);
  c = base;
  c.randomMin = -1e30f;
  CHECK(accepts(c));
  CHECK(c.randomMin == 11.0f);
  c = base;
  c.goal = 30.0f;  // Ziel unter bisherigem Minimum
  c.randomMin = 50.0f;
  CHECK(accepts(c));
  CHECK(c.randomMin == 30.0f);
  c = base;
  c.goal = 50.04f;
  c.randomMin = 50.04f;
  CHECK(accepts(c));
  CHECK(c.goal == 50.0f && c.randomMin == 50.0f);
  c = base;
  c.tolerance = 7.25f;
  c.randomMin = 2.0f;
  CHECK(accepts(c));
  CHECK(bits(c.randomMin) == bits(8.3f));
  c = base;
  c.randomMin = NAN_F;
  CHECK(rejects(c, "randomMin"));
  c = base;
  c.randomMin = -INF_F;
  CHECK(rejects(c, "randomMin"));

  // autoResetRange
  c = base;
  c.autoResetRange = 100;
  CHECK(accepts(c));
  c.autoResetRange = 0;
  CHECK(accepts(c));
  c.autoResetRange = 101;
  CHECK(rejects(c, "autoResetRange"));
  c.autoResetRange = 255;
  CHECK(rejects(c, "autoResetRange"));

  // Timeouts: ganzer uint8_t-Bereich
  c = base;
  c.wifiTimeout = 0;
  c.sleepTimeout = 255;
  CHECK(accepts(c));
  c.wifiTimeout = 255;
  c.sleepTimeout = 0;
  CHECK(accepts(c));

  // autoZeroThreshold
  c = base;
  c.autoZeroThreshold = 0.1f;
  CHECK(accepts(c));
  c.autoZeroThreshold = 0.09f;
  CHECK(rejects(c, "autoZeroThreshold"));
  c.autoZeroThreshold = 10.0f;  // = tolerance
  CHECK(accepts(c));
  c.autoZeroThreshold = 10.01f;
  CHECK(rejects(c, "autoZeroThreshold"));
  const char *azRelMsg = messageFor(c);
  c.autoZeroThreshold = NAN_F;
  CHECK(rejects(c, "autoZeroThreshold"));
  const char *azRangeMsg = messageFor(c);
  CHECK(azRelMsg && azRangeMsg && strcmp(azRelMsg, azRangeMsg) != 0);
  c = base;
  c.tolerance = 50.0f;
  c.autoZeroThreshold = 20.0f;
  CHECK(accepts(c));
  c.autoZeroThreshold = 20.01f;
  CHECK(rejects(c, "autoZeroThreshold"));

  // autoZeroDelay
  c = base;
  c.autoZeroDelay = 1;
  CHECK(accepts(c));
  c.autoZeroDelay = 60;
  CHECK(accepts(c));
  c.autoZeroDelay = 0;
  CHECK(rejects(c, "autoZeroDelay"));
  c.autoZeroDelay = 61;
  CHECK(rejects(c, "autoZeroDelay"));

  // displayRotation
  c = base;
  c.displayRotation = 2;
  CHECK(accepts(c));
  c.displayRotation = 1;
  CHECK(rejects(c, "displayRotation"));
  c.displayRotation = 3;
  CHECK(rejects(c, "displayRotation"));

  // battDividerRatio
  c = base;
  c.battDividerRatio = 1.0f;
  CHECK(accepts(c));
  c.battDividerRatio = 6.0f;
  CHECK(accepts(c));
  c.battDividerRatio = 0.99f;
  CHECK(rejects(c, "battDividerRatio"));
  c.battDividerRatio = 6.01f;
  CHECK(rejects(c, "battDividerRatio"));
  c.battDividerRatio = NAN_F;
  CHECK(rejects(c, "battDividerRatio"));

  // scaleMode
  c = base;
  c.scaleMode = ScaleMode::Standard;
  CHECK(accepts(c));
  c.scaleMode = (ScaleMode)2;
  CHECK(rejects(c, "scaleMode"));

  // scaleFactor (Kalibrierung)
  c = base;
  c.scaleFactor = -1.0f;
  CHECK(accepts(c));
  c.scaleFactor = 0.5f;
  CHECK(rejects(c, "scaleFactor"));
  c.scaleFactor = NAN_F;
  CHECK(rejects(c, "scaleFactor"));

  // Erster Fehler gewinnt; bei Fehler bleibt alles unveraendert
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "");
  c.tolerance = -1.0f;
  CHECK(rejects(c, "apSSID"));
  c = base;
  c.goal = 100.04f;
  c.randomMin = 5.0f;
  c.displayRotation = 1;
  CHECK(rejects(c, "displayRotation"));  // goal/randomMin nicht angefasst

  // Meldungen deutsch mit echten Umlauten (UTF-8)
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "a\x01");
  const char *m = messageFor(c);
  CHECK(m && strstr(m, "ung\xC3\xBC" "ltig") != nullptr);  // "ungueltig"
  c = base;
  c.autoZeroDelay = 0;
  m = messageFor(c);
  CHECK(m && strstr(m, "Verz\xC3\xB6gerung") != nullptr);  // "Verzoegerung"
  CHECK(azRelMsg && strstr(azRelMsg, "gr\xC3\xB6\xC3\x9F" "er") != nullptr);  // "groesser"
}

// Validierte Configs sind stabil: sanitize aendert nichts mehr.
static void testValidateMatchesSanitize() {
  int accepted = 0, bad = 0;
  for (int iter = 0; iter < 20000; iter++) {
    Config c = defaults();
    c.tolerance = (float)(rnd() % 1100) / 10.0f;               // 0..110
    c.goal = (float)(rnd() % 520000) / 100.0f;                 // 0..5200, 0,01 g
    c.randomMin = (float)(rnd() % 600000) / 100.0f - 100.0f;
    c.autoZeroThreshold = (float)(rnd() % 2500) / 100.0f;      // 0..25
    c.autoZeroDelay = (uint8_t)(rnd() % 70);
    c.autoResetRange = (uint8_t)(rnd() % 120);
    c.displayRotation = (uint8_t)(rnd() % 4);
    c.battDividerRatio = (float)(rnd() % 800) / 100.0f;
    c.scaleMode = (ScaleMode)(rnd() % 3);
    c.wifiTimeout = (uint8_t)rnd();
    c.sleepTimeout = (uint8_t)rnd();
    if (rnd() % 10 == 0) c.goal = specialFloat();
    if (rnd() % 10 == 0) c.randomMin = specialFloat();
    Config before = c;
    if (!accepts(c)) {
      if (!same(c, before)) bad++;  // Ablehnen aendert nichts
      continue;
    }
    accepted++;
    if (!inRanges(c)) bad++;
    Config s = c;
    if (sanitize(s) || !same(s, c)) bad++;
    Config again = c;
    if (!accepts(again) || !same(again, c)) bad++;  // idempotent
  }
  CHECK(bad == 0);
  CHECK(accepted > 500);
}

static void testValidNewPassword() {
  CHECK(!validNewPassword(nullptr));
  CHECK(!validNewPassword(""));
  CHECK(!validNewPassword("abc"));
  CHECK(validNewPassword("abcd"));
  CHECK(validNewPassword(" sp "));
  CHECK(validNewPassword("p\xC3\xA4ss"));  // "paess", 5 Bytes
  CHECK(validNewPassword(std::string(31, 'x').c_str()));
  CHECK(!validNewPassword(std::string(32, 'x').c_str()));
  CHECK(!validNewPassword(std::string(200, 'x').c_str()));
  CHECK(!validNewPassword("abc\x01" "d"));
  CHECK(!validNewPassword("abcd\x7F"));
  CHECK(!validNewPassword("abcd\n"));
  CHECK(!validNewPassword("\tabcd"));
}

// ── Parser ────────────────────────────────────────────────────────────────────

static bool pf(const char *s, float expect) {
  float v = -777.0f;
  return parseFloat(s, &v) && bits(v) == bits(expect);
}

static bool pfFails(const char *s) {
  float v = -777.0f;
  return !parseFloat(s, &v) && v == -777.0f;  // out bleibt unangetastet
}

static void testParseFloat() {
  CHECK(pf("12.5", 12.5f));
  CHECK(pf(" 12,5 ", 12.5f));
  CHECK(pf("12,5", 12.5f));
  CHECK(pf("\t-0,25\r\n", -0.25f));
  CHECK(pf("0.1", 0.1f));
  CHECK(pf("100", 100.0f));
  CHECK(pf("+3", 3.0f));
  CHECK(pf("-3", -3.0f));
  CHECK(pf("-0", -0.0f));
  CHECK(pf(".5", 0.5f));
  CHECK(pf(",5", 0.5f));
  CHECK(pf("5.", 5.0f));
  CHECK(pf("1e3", 1000.0f));
  CHECK(pf("1,5E2", 150.0f));
  CHECK(pf("2.5e-1", 0.25f));
  CHECK(pf("1e+2", 100.0f));
  CHECK(pf("007", 7.0f));
  CHECK(pf("712.34", 712.34f));
  CHECK(pf("3.4028235e38", 3.4028235e38f));
  CHECK(pf("1e-50", 0.0f));  // Unterlauf ist endlich

  CHECK(pfFails(""));
  CHECK(pfFails("   "));
  CHECK(pfFails("abc"));
  CHECK(pfFails("12x"));
  CHECK(pfFails("x12"));
  CHECK(pfFails("1 2"));
  CHECK(pfFails("12 ,5"));
  CHECK(pfFails("1e40"));
  CHECK(pfFails("-1e40"));
  CHECK(pfFails("3.4028236e38"));
  CHECK(pfFails("nan"));
  CHECK(pfFails("NaN"));
  CHECK(pfFails("inf"));
  CHECK(pfFails("-inf"));
  CHECK(pfFails("Infinity"));
  CHECK(pfFails("0x10"));
  CHECK(pfFails("0x1p3"));
  CHECK(pfFails("1.2.3"));
  CHECK(pfFails("1,2,3"));
  CHECK(pfFails("1,2.3"));
  CHECK(pfFails("."));
  CHECK(pfFails(","));
  CHECK(pfFails("-"));
  CHECK(pfFails("+"));
  CHECK(pfFails("--1"));
  CHECK(pfFails("+-1"));
  CHECK(pfFails("e5"));
  CHECK(pfFails("1e"));
  CHECK(pfFails("1e+"));
  CHECK(pfFails("1e5.5"));
  CHECK(pfFails("1_000"));
  CHECK(pfFails("12g"));
  CHECK(pfFails(nullptr));
  CHECK(!parseFloat("1", nullptr));

  // Laengengrenze: 47 Zeichen ok, 48 abgelehnt (Leerzeichen zaehlen nicht)
  std::string s47 = std::string(45, '0') + "12";
  CHECK(pf(s47.c_str(), 12.0f));
  CHECK(pf(("  " + s47 + "  ").c_str(), 12.0f));
  CHECK(pfFails(("0" + s47).c_str()));
}

static bool pu(const char *s, uint32_t maxValue, uint32_t expect) {
  uint32_t v = 777;
  return parseUint(s, maxValue, &v) && v == expect;
}

static bool puFails(const char *s, uint32_t maxValue) {
  uint32_t v = 777;
  return !parseUint(s, maxValue, &v) && v == 777;
}

static void testParseUint() {
  CHECK(pu("0", 255, 0));
  CHECK(pu("42", 255, 42));
  CHECK(pu(" 42 ", 255, 42));
  CHECK(pu("\t7\r\n", 255, 7));
  CHECK(pu("255", 255, 255));
  CHECK(pu("007", 255, 7));
  CHECK(pu("000000000000000000000000000042", 255, 42));
  CHECK(pu("0", 0, 0));
  CHECK(pu("4294967295", 0xFFFFFFFFu, 0xFFFFFFFFu));
  CHECK(pu("60", 60, 60));

  CHECK(puFails("256", 255));      // nicht auf 0 abgeschnitten
  CHECK(puFails("300", 255));      // nicht 44
  CHECK(puFails("65536", 65535));
  CHECK(puFails("1", 0));
  CHECK(puFails("61", 60));
  CHECK(puFails("4294967296", 0xFFFFFFFFu));
  CHECK(puFails("18446744073709551617", 0xFFFFFFFFu));  // 2^64 + 1
  CHECK(puFails("99999999999999999999999999999", 0xFFFFFFFFu));
  CHECK(puFails("-1", 255));
  CHECK(puFails("-0", 255));
  CHECK(puFails("+5", 255));
  CHECK(puFails("", 255));
  CHECK(puFails("   ", 255));
  CHECK(puFails("abc", 255));
  CHECK(puFails("12abc", 255));
  CHECK(puFails("1 2", 255));
  CHECK(puFails("12.0", 255));
  CHECK(puFails("12,0", 255));
  CHECK(puFails("1e3", 0xFFFFFFFFu));
  CHECK(puFails("0x10", 255));
  CHECK(puFails(nullptr, 255));
  CHECK(!parseUint("1", 255, nullptr));
}

// ── UTF-8 ─────────────────────────────────────────────────────────────────────

static std::string cu(const char *src, size_t maxBytes) {
  char buf[80];
  memset(buf, 'Z', sizeof buf);
  copyUtf8(buf, src, maxBytes);
  // Kein Schreiben hinter maxBytes + 1
  for (size_t i = maxBytes + 1; i < sizeof buf; i++)
    if (buf[i] != 'Z') return "<overflow>";
  return std::string(buf);
}

static void testCopyUtf8() {
  CHECK(cu("abc", 10) == "abc");
  CHECK(cu("abc", 3) == "abc");
  CHECK(cu("abcdef", 3) == "abc");
  CHECK(cu("abc", 0) == "");
  CHECK(cu("", 5) == "");
  CHECK(cu(nullptr, 5) == "");

  // ae = C3 A4
  CHECK(cu("a\xC3\xA4", 1) == "a");
  CHECK(cu("a\xC3\xA4", 2) == "a");
  CHECK(cu("a\xC3\xA4", 3) == "a\xC3\xA4");
  CHECK(cu("\xC3\xA4\xC3\xA4", 3) == "\xC3\xA4");
  CHECK(cu("\xC3\xA4", 1) == "");
  // Euro = E2 82 AC
  CHECK(cu("a\xE2\x82\xAC", 2) == "a");
  CHECK(cu("a\xE2\x82\xAC", 3) == "a");
  CHECK(cu("a\xE2\x82\xAC", 4) == "a\xE2\x82\xAC");
  CHECK(cu("a\xE2\x82\xAC" "b", 4) == "a\xE2\x82\xAC");
  // U+1F600 = F0 9F 98 80
  CHECK(cu("a\xF0\x9F\x98\x80", 2) == "a");
  CHECK(cu("a\xF0\x9F\x98\x80", 3) == "a");
  CHECK(cu("a\xF0\x9F\x98\x80", 4) == "a");
  CHECK(cu("a\xF0\x9F\x98\x80", 5) == "a\xF0\x9F\x98\x80");
  // Kaputte Eingaben: nie mehr als maxBytes, immer terminiert
  CHECK(cu("\x80\x80\x80\x80\x80", 3) == "\x80\x80\x80");
  CHECK(cu("ab\xC3", 2) == "ab");
  CHECK(cu("\xFF\xFE\xFD", 2) == "\xFF\xFE");
  CHECK(cu("a\xC3\xA4\xA4\xA4", 3) == "a\xC3\xA4");  // streunende Folgebytes

  // In-place (dst == src)
  char inplace[8] = "a\xC3\xA4" "bc";
  copyUtf8(inplace, inplace, 2);
  CHECK(std::string(inplace) == "a");

  // Eigenschaft: laengster Praefix an einer Zeichengrenze
  static const char *pool[] = {"a", "\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x8D\xBA"};
  int bad = 0;
  for (int iter = 0; iter < 2000; iter++) {
    std::string s;
    std::vector<size_t> bounds = {0};
    int chars = (int)(rnd() % 20);
    for (int k = 0; k < chars; k++) {
      s += pool[rnd() % 4];
      bounds.push_back(s.size());
    }
    for (size_t maxBytes = 0; maxBytes <= s.size() + 1 && maxBytes < 79; maxBytes++) {
      size_t expect = 0;
      for (size_t b : bounds)
        if (b <= maxBytes) expect = b;
      if (cu(s.c_str(), maxBytes) != s.substr(0, expect)) bad++;
    }
  }
  CHECK(bad == 0);
}

// ── AP-Name ───────────────────────────────────────────────────────────────────

static std::string apName(const Config &c, const uint8_t mac[6]) {
  char out[SSID_MAX + 1];
  memset(out, 'Z', sizeof out);
  effectiveApName(c, mac, out);
  if (strnlen(out, sizeof out) > SSID_MAX) return "<unterminated>";
  return std::string(out);
}

static void testEffectiveApName() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x11, 0xA1, 0xB2};
  const uint8_t mac2[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x05};
  Config c = defaults();
  CHECK(apName(c, mac) == "100-Waage-A1B2");
  CHECK(apName(c, mac2) == "100-Waage-0A05");

  setStr(c.apSSID, sizeof c.apSSID, "Meine Waage");
  CHECK(apName(c, mac) == "Meine Waage");
  setStr(c.apSSID, sizeof c.apSSID, "100-Waage-Config2");
  CHECK(apName(c, mac) == "100-Waage-Config2");
  setStr(c.apSSID, sizeof c.apSSID, "100-waage-config");
  CHECK(apName(c, mac) == "100-waage-config");
  setStr(c.apSSID, sizeof c.apSSID, "100-Waage-Confi");
  CHECK(apName(c, mac) == "100-Waage-Confi");
  setStr(c.apSSID, sizeof c.apSSID, "B\xC3\xA4r");
  CHECK(apName(c, mac) == "B\xC3\xA4r");
  std::string s32(32, 'q');
  setStr(c.apSSID, sizeof c.apSSID, s32.c_str());
  CHECK(apName(c, mac) == s32);
  // Leer (unbereinigt): trotzdem ein brauchbarer Name
  setStr(c.apSSID, sizeof c.apSSID, "");
  CHECK(apName(c, mac) == "100-Waage-A1B2");
  // Nicht terminiert (unbereinigt): begrenzt, an Zeichengrenze
  memset(c.apSSID, 'r', 31);
  c.apSSID[31] = '\xC3';
  c.apSSID[32] = '\xA4';
  CHECK(apName(c, mac) == std::string(31, 'r'));
}

// ── diff ──────────────────────────────────────────────────────────────────────

static void testDiff() {
  const Config a = defaults();
  Config b;

  b = a;
  CHECK(diff(a, b) == 0);
  b = a;
  setStr(b.apSSID, sizeof b.apSSID, "Andere");
  CHECK(diff(a, b) == CH_SSID);
  b = a;
  setStr(b.adminPassword, sizeof b.adminPassword, "geheim");
  CHECK(diff(a, b) == CH_PASSWORD);
  b = a;
  b.scaleFactor = 712.34f;
  CHECK(diff(a, b) == CH_SCALE);
  b = a;
  b.goal = 150.0f;
  CHECK(diff(a, b) == CH_GOAL);
  b = a;
  b.randomModeEnabled = true;
  CHECK(diff(a, b) == CH_RANDOM);
  b = a;
  b.randomMin = 30.0f;
  CHECK(diff(a, b) == CH_RANDOM);
  b = a;
  b.displayRotation = 2;
  CHECK(diff(a, b) == CH_ROTATION);
  b = a;
  b.scaleMode = ScaleMode::Standard;
  CHECK(diff(a, b) == CH_MODE);
  b = a;
  b.wifiTimeout = 0;
  CHECK(diff(a, b) == CH_TIMEOUTS);
  b = a;
  b.sleepTimeout = 0;
  CHECK(diff(a, b) == CH_TIMEOUTS);
  b = a;
  b.tolerance = 5.0f;
  CHECK(diff(a, b) == CH_GAME);
  b = a;
  b.autoResetRange = 50;
  CHECK(diff(a, b) == CH_GAME);
  b = a;
  b.autoZeroEnabled = false;
  CHECK(diff(a, b) == CH_AUTOZERO);
  b = a;
  b.autoZeroThreshold = 1.0f;
  CHECK(diff(a, b) == CH_AUTOZERO);
  b = a;
  b.autoZeroDelay = 10;
  CHECK(diff(a, b) == CH_AUTOZERO);
  b = a;
  b.battDividerRatio = 3.0f;
  CHECK(diff(a, b) == CH_BATT);

  // Kombination und Symmetrie
  b = a;
  b.goal = 150.0f;
  b.displayRotation = 2;
  b.battDividerRatio = 3.0f;
  CHECK(diff(a, b) == (CH_GOAL | CH_ROTATION | CH_BATT));
  CHECK(diff(b, a) == diff(a, b));

  // Alles anders → alle Bits
  b.scaleFactor = 1.0f;
  setStr(b.apSSID, sizeof b.apSSID, "x");
  setStr(b.adminPassword, sizeof b.adminPassword, "y");
  b.randomMin = 50.0f;
  b.scaleMode = ScaleMode::Standard;
  b.wifiTimeout = 1;
  b.tolerance = 1.0f;
  b.autoZeroDelay = 9;
  CHECK(diff(a, b) == 0x7FFu);

  // Bytes hinter dem NUL zaehlen nicht
  b = a;
  b.apSSID[sizeof b.apSSID - 1] = 'x';
  b.adminPassword[sizeof b.adminPassword - 1] = 'y';
  CHECK(diff(a, b) == 0);
}

// ── Altes EEPROM-Abbild ───────────────────────────────────────────────────────

// Kopie von WaageConfig aus types.h (Magic 0xCD), wie per EEPROM.put abgelegt.
struct OldConfig {
  uint8_t magic;
  char apSSID[64];
  float scaleFactor;
  float goal;
  float tolerance;
  uint8_t displayRotation;
  char adminPassword[32];
  uint8_t wifiTimeout;
  uint8_t sleepTimeout;
  float battDividerRatio;
  uint8_t scaleMode;
  uint8_t autoResetRange;
  bool autoZeroEnabled;
  float autoZeroThreshold;
  uint8_t autoZeroDelay;
  bool randomModeEnabled;
  float randomMin;
};
static_assert(offsetof(OldConfig, magic) == 0, "magic");
static_assert(offsetof(OldConfig, apSSID) == 1, "apSSID");
static_assert(offsetof(OldConfig, scaleFactor) == 68, "scaleFactor");
static_assert(offsetof(OldConfig, goal) == 72, "goal");
static_assert(offsetof(OldConfig, tolerance) == 76, "tolerance");
static_assert(offsetof(OldConfig, displayRotation) == 80, "displayRotation");
static_assert(offsetof(OldConfig, adminPassword) == 81, "adminPassword");
static_assert(offsetof(OldConfig, wifiTimeout) == 113, "wifiTimeout");
static_assert(offsetof(OldConfig, sleepTimeout) == 114, "sleepTimeout");
static_assert(offsetof(OldConfig, battDividerRatio) == 116, "battDividerRatio");
static_assert(offsetof(OldConfig, scaleMode) == 120, "scaleMode");
static_assert(offsetof(OldConfig, autoResetRange) == 121, "autoResetRange");
static_assert(offsetof(OldConfig, autoZeroEnabled) == 122, "autoZeroEnabled");
static_assert(offsetof(OldConfig, autoZeroThreshold) == 124, "autoZeroThreshold");
static_assert(offsetof(OldConfig, autoZeroDelay) == 128, "autoZeroDelay");
static_assert(offsetof(OldConfig, randomModeEnabled) == 129, "randomModeEnabled");
static_assert(offsetof(OldConfig, randomMin) == 132, "randomMin");
static_assert(sizeof(OldConfig) == 136, "OldConfig size");

// Aelteres Layout (Magic 0xCC) ohne Zufallsfelder
struct OldConfigCC {
  uint8_t magic;
  char apSSID[64];
  float scaleFactor;
  float goal;
  float tolerance;
  uint8_t displayRotation;
  char adminPassword[32];
  uint8_t wifiTimeout;
  uint8_t sleepTimeout;
  float battDividerRatio;
  uint8_t scaleMode;
  uint8_t autoResetRange;
  bool autoZeroEnabled;
  float autoZeroThreshold;
  uint8_t autoZeroDelay;
};
static_assert(offsetof(OldConfigCC, autoZeroDelay) == 128, "autoZeroDelay");
static_assert(sizeof(OldConfigCC) == 132, "OldConfigCC size");

// String samt NUL schreiben, Rest des Feldes bleibt Muell
static void putOldStr(char *field, const char *s) {
  memcpy(field, s, strlen(s) + 1);
}

static OldConfig oldSample() {
  OldConfig o;
  memset(&o, 0xEE, sizeof o);  // Padding und String-Reste mit Muell
  o.magic = 0xCD;
  putOldStr(o.apSSID, "Partywaage");
  o.scaleFactor = 712.34f;
  o.goal = 250.0f;
  o.tolerance = 7.5f;
  o.displayRotation = 2;
  putOldStr(o.adminPassword, "geheim42");
  o.wifiTimeout = 0;
  o.sleepTimeout = 30;
  o.battDividerRatio = 2.5f;
  o.scaleMode = 1;
  o.autoResetRange = 25;
  o.autoZeroEnabled = false;
  o.autoZeroThreshold = 1.5f;
  o.autoZeroDelay = 10;
  o.randomModeEnabled = true;
  o.randomMin = 150.0f;
  return o;
}

// Abbild exakt passender Groesse auf dem Heap (ASan erkennt Ueberlesen)
template <typename T>
static std::vector<uint8_t> image(const T &o, size_t len = sizeof(T)) {
  std::vector<uint8_t> v(len, 0xFF);
  memcpy(v.data(), &o, len < sizeof(T) ? len : sizeof(T));
  return v;
}

static Config sentinel() {
  Config s = defaults();
  s.goal = 777.0f;
  setStr(s.apSSID, sizeof s.apSSID, "Sentinel");
  return s;
}

static void testLegacyRoundtrip() {
  OldConfig o = oldSample();
  std::vector<uint8_t> img = image(o);
  Config c = sentinel();
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(strcmp(c.apSSID, "Partywaage") == 0);
  CHECK(bits(c.scaleFactor) == bits(712.34f));
  CHECK(c.goal == 250.0f);
  CHECK(c.tolerance == 7.5f);
  CHECK(c.displayRotation == 2);
  CHECK(strcmp(c.adminPassword, "geheim42") == 0);
  CHECK(c.wifiTimeout == 0);
  CHECK(c.sleepTimeout == 30);
  CHECK(c.battDividerRatio == 2.5f);
  CHECK(c.scaleMode == ScaleMode::Standard);
  CHECK(c.autoResetRange == 25);
  CHECK(c.autoZeroEnabled == false);
  CHECK(c.autoZeroThreshold == 1.5f);
  CHECK(c.autoZeroDelay == 10);
  CHECK(c.randomModeEnabled == true);
  CHECK(c.randomMin == 150.0f);
  Config s = c;
  CHECK(!sanitize(s));  // bereits bereinigt

  // Negativer Faktor bitgenau, Gegenwerte der bools
  o.scaleFactor = -1234.5f;
  o.autoZeroEnabled = true;
  o.randomModeEnabled = false;
  o.scaleMode = 0;
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(bits(c.scaleFactor) == bits(-1234.5f));
  CHECK(c.autoZeroEnabled == true && c.randomModeEnabled == false);
  CHECK(c.scaleMode == ScaleMode::Game);

  // Explizite Little-Endian-Bytes (unabhaengig vom Struct)
  img = image(oldSample());
  const uint8_t f712[4] = {0xC3, 0x15, 0x32, 0x44};  // 712.34f = 0x443215C3
  CHECK(memcmp(img.data() + 68, f712, 4) == 0);
  img[72] = 0x00, img[73] = 0x00, img[74] = 0x48, img[75] = 0x43;  // 200.0f
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.goal == 200.0f);

  // Alte Default-SSID → AP-Name aus der MAC
  o = oldSample();
  putOldStr(o.apSSID, "100-Waage-Config");
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  const uint8_t mac[6] = {1, 2, 3, 4, 0xA1, 0xB2};
  CHECK(apName(c, mac) == "100-Waage-A1B2");

  // Laengerer Blob (ganzer EEPROM-Bereich) ist ok
  img = image(oldSample(), 512);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(strcmp(c.apSSID, "Partywaage") == 0);
}

static void testLegacyCC() {
  OldConfigCC o;
  memset(&o, 0xEE, sizeof o);
  o.magic = 0xCC;
  putOldStr(o.apSSID, "Alt");
  o.scaleFactor = -712.34f;
  o.goal = 300.0f;
  o.tolerance = 12.0f;
  o.displayRotation = 0;
  putOldStr(o.adminPassword, "pw12");
  o.wifiTimeout = 15;
  o.sleepTimeout = 0;
  o.battDividerRatio = 1.5f;
  o.scaleMode = 0;
  o.autoResetRange = 100;
  o.autoZeroEnabled = true;
  o.autoZeroThreshold = 3.0f;
  o.autoZeroDelay = 60;
  std::vector<uint8_t> img = image(o);  // genau 132 Bytes, Padding 129..131 = 0xEE
  CHECK(img.size() == 132);
  Config c = sentinel();
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(strcmp(c.apSSID, "Alt") == 0);
  CHECK(bits(c.scaleFactor) == bits(-712.34f));
  CHECK(c.goal == 300.0f && c.tolerance == 12.0f && c.displayRotation == 0);
  CHECK(strcmp(c.adminPassword, "pw12") == 0);
  CHECK(c.wifiTimeout == 15 && c.sleepTimeout == 0);
  CHECK(c.battDividerRatio == 1.5f && c.scaleMode == ScaleMode::Game);
  CHECK(c.autoResetRange == 100 && c.autoZeroEnabled);
  CHECK(c.autoZeroThreshold == 3.0f && c.autoZeroDelay == 60);
  CHECK(c.randomModeEnabled == false);  // Default, nicht Padding 0xEE
  CHECK(c.randomMin == 20.0f);

  // Default-randomMin wird trotzdem ans Ziel geklemmt
  o.goal = 15.0f;
  o.tolerance = 10.0f;
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.goal == 15.0f && c.randomMin == 15.0f);
}

static void testLegacyRejects() {
  const Config s = sentinel();
  Config c = s;
  std::vector<uint8_t> img = image(oldSample());

  // Unbekanntes Magic
  const uint8_t magics[] = {0x00, 0xFF, 0xCE, 0xCB, 0xDC, 0xEE};
  for (uint8_t m : magics) {
    img[0] = m;
    c = s;
    CHECK(!decodeLegacy(img.data(), img.size(), c));
    CHECK(same(c, s));
  }

  // Zu kurz
  img = image(oldSample(), 135);
  c = s;
  CHECK(!decodeLegacy(img.data(), img.size(), c));
  CHECK(same(c, s));
  img = image(oldSample(), 132);  // 0xCD braucht 136
  CHECK(!decodeLegacy(img.data(), img.size(), c));
  OldConfigCC cc;
  memset(&cc, 0, sizeof cc);
  cc.magic = 0xCC;
  img = image(cc, 131);
  CHECK(!decodeLegacy(img.data(), img.size(), c));
  img = image(cc, 132);
  CHECK(decodeLegacy(img.data(), img.size(), c));  // Inhalt wird bereinigt
  c = s;
  img = image(oldSample(), 1);
  CHECK(!decodeLegacy(img.data(), 1, c));
  CHECK(!decodeLegacy(img.data(), 0, c));
  CHECK(!decodeLegacy(nullptr, 136, c));
  CHECK(same(c, s));
}

static void testLegacyStrings() {
  // Kein NUL in SSID (64 Bytes) und Passwort (32 Bytes)
  OldConfig o = oldSample();
  memset(o.apSSID, 'S', sizeof o.apSSID);
  memset(o.adminPassword, 'p', sizeof o.adminPassword);
  std::vector<uint8_t> img = image(o);
  Config c = sentinel();
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == std::string(32, 'S'));
  CHECK(std::string(c.adminPassword) == std::string(31, 'p'));
  CHECK(c.goal == 250.0f);  // Nachbarfelder unbeeinflusst

  // 63-Byte-SSID: ae auf Byte 31/32 → 31 Bytes
  o = oldSample();
  std::string ssid = std::string(31, 'a') + "\xC3\xA4" + std::string(30, 'b');
  CHECK(ssid.size() == 63);
  putOldStr(o.apSSID, ssid.c_str());
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == std::string(31, 'a'));
  // Euro auf Byte 30..32 → 30 Bytes
  ssid = std::string(30, 'a') + "\xE2\x82\xAC" + std::string(30, 'b');
  putOldStr(o.apSSID, ssid.c_str());
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == std::string(30, 'a'));
  // U+1F37A auf Byte 29..32 → 29 Bytes
  ssid = std::string(29, 'a') + "\xF0\x9F\x8D\xBA" + std::string(30, 'b');
  putOldStr(o.apSSID, ssid.c_str());
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == std::string(29, 'a'));
  // ae genau auf Byte 30/31 → passt komplett
  ssid = std::string(30, 'a') + "\xC3\xA4" + std::string(31, 'b');
  putOldStr(o.apSSID, ssid.c_str());
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == std::string(30, 'a') + "\xC3\xA4");

  // Leere SSID / leeres Passwort / Steuerzeichen → Defaults
  o = oldSample();
  putOldStr(o.apSSID, "");
  putOldStr(o.adminPassword, "");
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(strcmp(c.apSSID, DEFAULT_AP_SSID) == 0);
  CHECK(strcmp(c.adminPassword, "admin") == 0);
  o = oldSample();
  putOldStr(o.apSSID, "Waage\x01");
  putOldStr(o.adminPassword, "abc");  // kurzes altes Passwort bleibt
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(strcmp(c.apSSID, DEFAULT_AP_SSID) == 0);
  CHECK(strcmp(c.adminPassword, "abc") == 0);
}

static void testLegacySanitized() {
  OldConfig o = oldSample();
  o.scaleFactor = 0.0f;
  o.goal = 99999.0f;
  o.tolerance = 0.0f;
  o.displayRotation = 1;
  o.battDividerRatio = 0.0f;
  o.scaleMode = 5;
  o.autoResetRange = 200;
  o.autoZeroThreshold = 50.0f;
  o.autoZeroDelay = 0;
  o.randomMin = 5.0f;
  std::vector<uint8_t> img = image(o);
  img[122] = 7;  // autoZeroEnabled mit Muell-Byte
  img[129] = 9;  // randomModeEnabled mit Muell-Byte
  Config c = sentinel();
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.scaleFactor == 708.0f);
  CHECK(c.tolerance == 10.0f);
  CHECK(c.goal == 5000.0f);
  CHECK(c.displayRotation == 0);
  CHECK(c.battDividerRatio == 2.0f);
  CHECK(c.scaleMode == ScaleMode::Game);
  CHECK(c.autoResetRange == 100);
  CHECK(c.autoZeroThreshold == 10.0f);
  CHECK(c.autoZeroDelay == 5);
  CHECK(c.randomMin == 11.0f);
  CHECK(c.autoZeroEnabled == true && c.randomModeEnabled == true);
  CHECK(inRanges(c));

  // NaN-Muster in allen Floats
  o = oldSample();
  o.scaleFactor = NAN_F;
  o.goal = NAN_F;
  o.tolerance = NAN_F;
  o.battDividerRatio = INF_F;
  o.autoZeroThreshold = NAN_F;
  o.randomMin = -INF_F;
  img = image(o);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.scaleFactor == 708.0f && c.goal == 100.0f && c.tolerance == 10.0f);
  CHECK(c.battDividerRatio == 2.0f && c.autoZeroThreshold == 2.0f && c.randomMin == 20.0f);

  // Zufaellige Abbilder mit gueltigem Magic: nie ein Fehler, immer bereinigt
  int bad = 0;
  for (int iter = 0; iter < 5000; iter++) {
    size_t len = (iter % 2) ? 136 : 132;
    std::vector<uint8_t> v(len);
    for (auto &b : v) b = (uint8_t)rnd();
    v[0] = (len == 136) ? 0xCD : 0xCC;
    Config d = sentinel();
    if (!decodeLegacy(v.data(), v.size(), d) || !inRanges(d)) bad++;
    Config s = d;
    if (sanitize(s)) bad++;
  }
  CHECK(bad == 0);
}

// ── Zufallsziel ───────────────────────────────────────────────────────────────

static void testRollGoal() {
  Config c = defaults();  // goal 100, tolerance 10, randomMin 20 → [20..100]
  CHECK(rollGoal(c, 0) == 20.0f);
  CHECK(rollGoal(c, 0xFFFFFFFFu) == 100.0f);
  CHECK(rollGoal(c, 0x80000000u) == 60.0f);
  CHECK(rollGoal(c, 0xFFFFF000u) == 100.0f);  // nahe am Ueberlauf
  CHECK(rollGoal(c, 0x00000FFFu) == 20.0f);

  // Alle Werte erreichbar, ganzzahlig, monoton in r
  std::set<int> seen;
  bool ok = true;
  float prev = 0.0f;
  const int N = 81 * 50;
  for (int i = 0; i < N; i++) {
    uint32_t r = (uint32_t)((uint64_t)i * 0xFFFFFFFFull / (N - 1));
    float g = rollGoal(c, r);
    if (g < 20.0f || g > 100.0f || g != std::floor(g) || g < prev) ok = false;
    prev = g;
    seen.insert((int)g);
  }
  CHECK(ok);
  CHECK(seen.size() == 81);

  // Untergrenze tolerance + 1, wenn randomMin darunter
  c = defaults();
  c.randomMin = 5.0f;
  CHECK(rollGoal(c, 0) == 11.0f);
  // Gebrochene Grenzen: ceil unten, floor oben
  c = defaults();
  c.tolerance = 0.55f;
  c.randomMin = 1.6f;
  c.goal = 100.5f;
  CHECK(rollGoal(c, 0) == 2.0f);
  CHECK(rollGoal(c, 0xFFFFFFFFu) == 100.0f);
  c = defaults();
  c.randomMin = 20.1f;
  CHECK(rollGoal(c, 0) == 21.0f);
  // Ein einziger Wert
  c = defaults();
  c.goal = 21.0f;
  c.randomMin = 21.0f;
  CHECK(rollGoal(c, 0) == 21.0f && rollGoal(c, 0xFFFFFFFFu) == 21.0f);
  c.goal = 11.0f;
  c.randomMin = 11.0f;
  CHECK(rollGoal(c, 0x12345678u) == 11.0f);
  // Leerer Bereich → goal
  c = defaults();
  c.goal = 20.5f;
  c.randomMin = 20.5f;
  CHECK(rollGoal(c, 0) == 20.5f);
  CHECK(rollGoal(c, 0xFFFFFFFFu) == 20.5f);
  c = defaults();
  c.tolerance = 10.5f;
  c.goal = 11.5f;
  c.randomMin = 11.5f;  // [12..11]
  CHECK(rollGoal(c, 0x80000000u) == 11.5f);
  // Grosser Bereich
  c = defaults();
  c.tolerance = 0.5f;
  c.randomMin = 1.5f;
  c.goal = 5000.0f;
  CHECK(rollGoal(c, 0) == 2.0f);
  CHECK(rollGoal(c, 0xFFFFFFFFu) == 5000.0f);
  CHECK(rollGoal(c, 0xFFFFF000u) <= 5000.0f && rollGoal(c, 0xFFFFF000u) >= 4999.0f);
  // Unbereinigte Werte: kein UB, sinnvolles Ergebnis
  c = defaults();
  c.goal = NAN_F;
  CHECK(std::isnan(rollGoal(c, 123)));
  c = defaults();
  c.randomMin = NAN_F;
  CHECK(rollGoal(c, 0) == 11.0f);
  c = defaults();
  c.goal = 3e38f;
  CHECK(rollGoal(c, 0) == 3e38f);
  c = defaults();
  c.tolerance = -100.0f;
  c.randomMin = -50.0f;
  CHECK(rollGoal(c, 0) == 100.0f);
}

// ── Review: Passwortregeln ────────────────────────────────────────────────────

static void testReviewPasswords() {
  // validNewPassword: "keine Steuerzeichen" heisst dasselbe wie bei der SSID
  CHECK(!validNewPassword("abcd\xC2\x85"));   // U+0085 (C1, NEL)
  CHECK(!validNewPassword("\xC2\x80" "abcd")); // U+0080
  CHECK(!validNewPassword("abcd\xC2\x9F"));   // U+009F
  CHECK(validNewPassword("abcd\xC2\xA0"));    // U+00A0 ist druckbar
  CHECK(!validNewPassword("abcd\x85"));       // Latin-1-C1 / kaputtes UTF-8
  CHECK(!validNewPassword("abc\xE4"));        // Latin-1-ae
  CHECK(!validNewPassword("abcd\xC3"));       // abgeschnittene Sequenz
  CHECK(!validNewPassword("abcd\xED\xA0\x80"));  // Surrogate
  CHECK(validNewPassword("Bier\xF0\x9F\x8D\xBA"));  // 8 Bytes mit Emoji
  CHECK(validNewPassword("\xC3\xA4\xC3\xB6"));      // "aeoe": 2 Zeichen, 4 Bytes
  std::string p31 = std::string(29, 'x') + "\xC3\xA4";  // genau 31 Bytes
  CHECK(validNewPassword(p31.c_str()));
  std::string p32 = std::string(30, 'x') + "\xC3\xA4";  // 32 Bytes
  CHECK(!validNewPassword(p32.c_str()));

  // validate: gleiche Bereiche wie sanitize, also leer/unterminiert ablehnen
  Config base = defaults();
  Config c = base;
  setStr(c.adminPassword, sizeof c.adminPassword, "");
  {
    Config s = c;
    CHECK(sanitize(s));  // sanitize wuerde korrigieren ...
  }
  CHECK(rejects(c, "newPassword"));  // ... also lehnt validate ab
  c = base;
  memset(c.adminPassword, 'p', sizeof c.adminPassword);  // 32 Bytes ohne NUL
  CHECK(rejects(c, "newPassword"));
  const char *m = messageFor(c);
  CHECK(m && strstr(m, "Passwort") != nullptr);
  // Kurze alte und unbequeme Passwoerter bleiben gueltig (kein validNewPassword)
  c = base;
  setStr(c.adminPassword, sizeof c.adminPassword, "abc");
  CHECK(accepts(c));
  CHECK(strcmp(c.adminPassword, "abc") == 0);
  c = base;
  setStr(c.adminPassword, sizeof c.adminPassword, "p\x01");
  CHECK(accepts(c));
  c = base;
  memset(c.adminPassword, 'q', PASSWORD_MAX);
  c.adminPassword[PASSWORD_MAX] = '\0';
  CHECK(accepts(c));
  Config s = c;
  CHECK(!sanitize(s));
  // SSID-Fehler kommt vor dem Passwortfehler
  c = base;
  setStr(c.apSSID, sizeof c.apSSID, "");
  setStr(c.adminPassword, sizeof c.adminPassword, "");
  CHECK(rejects(c, "apSSID"));
}

// ── Review: UTF-8 gegen Referenzdecoder ───────────────────────────────────────

// Referenz: gueltiges UTF-8 (kuerzeste Form, keine Surrogates, <= U+10FFFF)
// ohne C0, DEL und C1.
static bool refPrintable(const std::string &x) {
  size_t i = 0;
  while (i < x.size()) {
    uint8_t b = (uint8_t)x[i];
    uint32_t cp;
    size_t len;
    if (b < 0x80) cp = b, len = 1;
    else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, len = 2;
    else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, len = 3;
    else if ((b & 0xF8) == 0xF0) cp = b & 0x07, len = 4;
    else return false;
    if (i + len > x.size()) return false;
    for (size_t k = 1; k < len; k++) {
      uint8_t cb = (uint8_t)x[i + k];
      if ((cb & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cb & 0x3F);
    }
    static const uint32_t minCp[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < minCp[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) return false;
    i += len;
  }
  return true;
}

static const uint8_t kEdgeBytes[] = {0x01, 0x1F, 0x20, 0x41, 0x7E, 0x7F, 0x80, 0x8F, 0x90,
                                     0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xDF, 0xE0,
                                     0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4,
                                     0xF5, 0xF7, 0xF8, 0xFE, 0xFF};

static std::string edgeBytes(size_t n) {
  std::string x;
  for (size_t k = 0; k < n; k++) {
    uint8_t b = (rnd() % 3 == 0) ? (uint8_t)rnd()
                                 : kEdgeBytes[rnd() % sizeof kEdgeBytes];
    x.push_back((char)(b ? b : 'A'));
  }
  return x;
}

static void testReviewUtf8Reference() {
  int bad = 0, accepted = 0;
  for (int iter = 0; iter < 60000; iter++) {
    std::string x = edgeBytes(1 + rnd() % 8);
    bool ref = refPrintable(x);
    // validate (SSID)
    Config c = defaults();
    setStr(c.apSSID, sizeof c.apSSID, x.c_str());
    Config v = c;
    bool acc = validate(v).field == nullptr;
    if (acc != ref) bad++;
    // sanitize ersetzt genau die abgelehnten SSIDs
    Config s = c;
    bool ch = sanitize(s);
    if (ch != !ref) bad++;
    if (ref && strcmp(s.apSSID, x.c_str()) != 0) bad++;
    if (!ref && strcmp(s.apSSID, DEFAULT_AP_SSID) != 0) bad++;
    // validNewPassword mit gleicher Definition (auf 4 Bytes auffuellen)
    std::string pw = x + "abcd";
    if (validNewPassword(pw.c_str()) != refPrintable(pw)) bad++;
    if (ref) accepted++;
  }
  CHECK(bad == 0);
  CHECK(accepted > 1000);
}

// ── Review: copyUtf8 mit kaputten Eingaben ────────────────────────────────────

static void testReviewCopyUtf8() {
  copyUtf8(nullptr, "abc", 3);  // kein Absturz
  char one[1] = {'Z'};
  copyUtf8(one, "abc", 0);
  CHECK(one[0] == '\0');

  int bad = 0;
  for (int iter = 0; iter < 40000; iter++) {
    std::string x = edgeBytes(rnd() % 10);
    // Quelle und Ziel exakt gross auf dem Heap (ASan sieht jedes Ueberlesen)
    std::vector<char> src(x.begin(), x.end());
    src.push_back('\0');
    size_t maxBytes = rnd() % 12;
    std::vector<char> dst(maxBytes + 1, 'Z');
    copyUtf8(dst.data(), src.data(), maxBytes);
    size_t n = strnlen(dst.data(), dst.size());
    if (n > maxBytes || n > x.size() || memcmp(dst.data(), x.data(), n) != 0) {
      bad++;
      continue;
    }
    if (n < maxBytes && n < x.size()) {
      // Gekuerzt unter maxBytes: nur erlaubt, um eine Sequenz nicht zu teilen,
      // die erst hinter maxBytes endet
      uint8_t lead = (uint8_t)x[n];
      size_t len = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      if (lead < 0xC0 || lead > 0xF7 || n + len <= maxBytes) bad++;
    }
    // Gueltiges UTF-8: Ergebnis ist der laengste Praefix an einer Zeichengrenze
    if (refPrintable(x)) {
      std::string r(dst.data(), n);
      if (!refPrintable(r)) bad++;
      size_t next = n;
      if (next < x.size()) {
        uint8_t b = (uint8_t)x[next];
        next += b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        if (next <= maxBytes) bad++;  // haette noch gepasst
      }
    }
  }
  CHECK(bad == 0);
}

// ── Review: Parser gegen Referenz ─────────────────────────────────────────────

// Referenz fuer parseFloat: Syntax per Hand, Wert per strtof.
static bool refParseFloat(const std::string &in, float *out) {
  size_t a = 0, b = in.size();
  auto sp = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
  while (a < b && sp(in[a])) a++;
  while (b > a && sp(in[b - 1])) b--;
  std::string t = in.substr(a, b - a);
  if (t.empty() || t.size() >= 48) return false;
  size_t i = 0;
  if (t[i] == '+' || t[i] == '-') i++;
  size_t intDigits = 0, fracDigits = 0;
  while (i < t.size() && isdigit((unsigned char)t[i])) i++, intDigits++;
  if (i < t.size() && (t[i] == '.' || t[i] == ',')) {
    t[i] = '.';
    i++;
    while (i < t.size() && isdigit((unsigned char)t[i])) i++, fracDigits++;
  }
  if (intDigits + fracDigits == 0) return false;
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
    i++;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++;
    size_t e = 0;
    while (i < t.size() && isdigit((unsigned char)t[i])) i++, e++;
    if (e == 0) return false;
  }
  if (i != t.size()) return false;
  float v = strtof(t.c_str(), nullptr);
  if (!std::isfinite(v)) return false;
  *out = v;
  return true;
}

static void testReviewParsers() {
  const char alpha[] = "0123456789.,+-eE \t9x5";
  int bad = 0, okF = 0, okU = 0;
  for (int iter = 0; iter < 60000; iter++) {
    size_t n = rnd() % 9;
    std::string x;
    for (size_t k = 0; k < n; k++) x.push_back(alpha[rnd() % (sizeof alpha - 1)]);
    if (rnd() % 8 == 0) x = std::to_string(rnd()) + (rnd() % 2 ? "e3" : "e39");

    float ref = 0.0f, got = -777.0f;
    bool r = refParseFloat(x, &ref);
    bool g = parseFloat(x.c_str(), &got);
    if (r != g || (g && bits(got) != bits(ref)) || (!g && got != -777.0f)) bad++;
    if (g) okF++;

    uint32_t maxV = (rnd() % 3 == 0) ? 0xFFFFFFFFu : (rnd() % 2 ? 255u : rnd() % 100000);
    // Referenz: nur Ziffern (plus Leerraum aussen), Wert per strtoull
    size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n");
    bool refU = false;
    unsigned long long uv = 0;
    if (a != std::string::npos) {
      std::string t = x.substr(a, b - a + 1);
      if (t.find_first_not_of("0123456789") == std::string::npos && t.size() < 19) {
        uv = strtoull(t.c_str(), nullptr, 10);
        refU = uv <= maxV;
      }
    }
    uint32_t u = 777;
    bool gu = parseUint(x.c_str(), maxV, &u);
    if (gu != refU || (gu && u != uv) || (!gu && u != 777)) bad++;
    if (gu) okU++;
  }
  CHECK(bad == 0);
  CHECK(okF > 1000 && okU > 1000);

  // Gezielte Randfaelle
  CHECK(pf("1e-45", 1e-45f));  // kleinste Subnormale ist endlich
  CHECK(pf("-1e-50", -0.0f));
  CHECK(pf("+,5", 0.5f));
  CHECK(pf("-5,", -5.0f));
  CHECK(pf("3,40282346e38", 3.40282346e38f));
  CHECK(pfFails("\v1"));  // nur Leerzeichen, Tab, CR, LF werden getrimmt
  CHECK(pfFails("1\f"));
  CHECK(pfFails("1 e5"));
  CHECK(pfFails("1e 5"));
  CHECK(pu("4294967294", 0xFFFFFFFEu, 0xFFFFFFFEu));
  CHECK(puFails("4294967295", 0xFFFFFFFEu));
  CHECK(puFails("42949672950", 0xFFFFFFFFu));  // waere nach Verengung 4294967286
  CHECK(puFails("256", 255));
  CHECK(puFails("\v1", 255));
}

// ── Review: Float-Sonderwerte ─────────────────────────────────────────────────

static void testReviewFloatEdges() {
  // -0,0 in jedem Float-Feld: wie 0 behandelt
  float o;
  CHECK(sanF(&Config::tolerance, -0.0f, o) && o == 10.0f);
  CHECK(sanF(&Config::goal, -0.0f, o) && o == 11.0f);
  CHECK(sanF(&Config::randomMin, -0.0f, o) && o == 11.0f && !std::signbit(o));
  CHECK(sanF(&Config::autoZeroThreshold, -0.0f, o) && bits(o) == bits(0.1f));
  CHECK(sanF(&Config::battDividerRatio, -0.0f, o) && o == 2.0f);
  CHECK(sanF(&Config::scaleFactor, -1e-40f, o) && o == 708.0f);  // subnormal
  CHECK(sanF(&Config::goal, 1e-40f, o) && o == 11.0f);
  // Groesster/kleinster endlicher Wert
  const float big = std::numeric_limits<float>::max();
  CHECK(!sanF(&Config::scaleFactor, big, o) && o == big);
  CHECK(!sanF(&Config::scaleFactor, -big, o) && o == -big);
  CHECK(sanF(&Config::goal, big, o) && o == 5000.0f);
  CHECK(sanF(&Config::goal, -big, o) && o == 11.0f);
  CHECK(sanF(&Config::randomMin, big, o) && o == 100.0f);
  CHECK(sanF(&Config::autoZeroThreshold, big, o) && o == 10.0f);

  // validate: -0,0 und Subnormale
  Config base = defaults(), c = base;
  c.scaleFactor = -0.0f;
  CHECK(rejects(c, "scaleFactor"));
  c = base;
  c.goal = -0.0f;
  CHECK(rejects(c, "goal"));
  c = base;
  c.randomMin = -0.0f;
  CHECK(accepts(c) && c.randomMin == 11.0f && !std::signbit(c.randomMin));
  c = base;
  c.goal = big;  // round10 laeuft ueber → trotzdem sauber abgelehnt
  CHECK(rejects(c, "goal"));
  c = base;
  c.goal = -big;
  CHECK(rejects(c, "goal"));

  // diff vergleicht bitgenau: -0 != +0, gleiche NaN-Bits gleich
  Config a = defaults(), b = a;
  a.randomMin = 0.0f;
  b.randomMin = -0.0f;
  CHECK(diff(a, b) == CH_RANDOM);
  a = defaults();
  b = a;
  a.scaleFactor = NAN_F;
  b.scaleFactor = NAN_F;
  CHECK(diff(a, b) == 0);
  b.scaleFactor = -NAN_F;  // anderes Vorzeichenbit
  CHECK(diff(a, b) == CH_SCALE);
}

// ── Review: Toleranz-Sweep (kleinstes Rasterziel) ─────────────────────────────

static void testReviewToleranceSweep() {
  int bad = 0;
  for (int iter = 0; iter < 200000; iter++) {
    // Beliebiger Float in [0,5 .. 100] (nicht nur Rasterwerte)
    float t = 0.5f + 99.5f * (float)(rnd() >> 8) / 16777216.0f;
    if (t > 100.0f) t = 100.0f;
    Config c = defaults();
    c.tolerance = t;
    c.goal = 1.0f;
    c.randomMin = -5.0f;
    c.autoZeroThreshold = 0.1f;
    sanitize(c);
    float m = t + 1.0f;
    // goal: auf dem Raster, >= tolerance + 1 und der kleinste solche Wert
    float below = roundf(c.goal * 10.0f - 1.0f) / 10.0f;
    if (!(c.goal >= m) || !onGrid(c.goal) || below >= m) bad++;
    if (bits(c.randomMin) != bits(c.goal)) bad++;
    Config d = c;
    if (sanitize(d)) bad++;
    Config v = c;
    if (!accepts(v) || !same(v, c)) bad++;
    // validate auf genau diesem Ziel bzw. einen Rasterschritt darunter
    v = c;
    v.goal = below;
    if (accepts(v)) bad++;
  }
  CHECK(bad == 0);
}

// ── Review: Zufallsziel exakt gleichverteilt ──────────────────────────────────

static void testReviewRollGoalBuckets() {
  // Kleinstes r fuer Stufe k ist ceil(k * 2^32 / span)
  struct Range {
    float tol, rmin, goal;
    uint32_t lo, hi;
  };
  const Range ranges[] = {{10.0f, 20.0f, 100.0f, 20, 100},
                          {0.5f, 1.5f, 5000.0f, 2, 5000},
                          {10.0f, 11.0f, 12.0f, 11, 12},
                          {99.5f, 100.5f, 4999.9f, 101, 4999}};
  int bad = 0;
  for (const Range &rg : ranges) {
    Config c = defaults();
    c.tolerance = rg.tol;
    c.randomMin = rg.rmin;
    c.goal = rg.goal;
    c.autoZeroThreshold = 0.5f;
    uint64_t span = rg.hi - rg.lo + 1;
    for (uint64_t k = 1; k < span; k++) {
      uint64_t t = (k * 0x100000000ull + span - 1) / span;
      if (rollGoal(c, (uint32_t)(t - 1)) != (float)(rg.lo + k - 1)) bad++;
      if (rollGoal(c, (uint32_t)t) != (float)(rg.lo + k)) bad++;
    }
    if (rollGoal(c, 0) != (float)rg.lo) bad++;
    if (rollGoal(c, 0xFFFFFFFFu) != (float)rg.hi) bad++;
    if (rollGoal(c, 0xFFFFF000u) != (float)rg.hi) bad++;  // nahe am Ueberlauf
  }
  CHECK(bad == 0);
  // Bereinigte Zufallsconfigs: Ergebnis immer ganzzahlig im Bereich oder goal
  bad = 0;
  for (int iter = 0; iter < 20000; iter++) {
    Config c = defaults();
    c.tolerance = (float)(rnd() % 1000) / 10.0f + 0.5f;
    c.goal = (float)(rnd() % 50000) / 10.0f;
    c.randomMin = (float)(rnd() % 50000) / 10.0f;
    sanitize(c);
    uint32_t r = (iter % 3 == 0) ? 0xFFFFF000u + (rnd() % 0x1000) : rnd();
    float g = rollGoal(c, r);
    float lo = std::ceil(std::fmax(c.randomMin, c.tolerance + 1.0f));
    float hi = std::floor(c.goal);
    if (lo > hi) {
      if (bits(g) != bits(c.goal)) bad++;
    } else if (g < lo || g > hi || g != std::floor(g)) {
      bad++;
    }
  }
  CHECK(bad == 0);
}

// ── Review: Legacy-Abbild im ganzen EEPROM-Bereich ────────────────────────────

static void testReviewLegacyEeprom() {
  // 0xCC in 512 Byte: Bytes 129..135 sind Muell/Folgedaten, Zufallsfelder Default
  OldConfigCC o;
  memset(&o, 0, sizeof o);
  o.magic = 0xCC;
  putOldStr(o.apSSID, "Alt");
  o.scaleFactor = 708.0f;
  o.goal = 100.0f;
  o.tolerance = 10.0f;
  putOldStr(o.adminPassword, "admin");
  o.battDividerRatio = 2.0f;
  o.autoResetRange = 10;
  o.autoZeroEnabled = true;
  o.autoZeroThreshold = 2.0f;
  o.autoZeroDelay = 5;
  std::vector<uint8_t> img(512, 0xA5);
  memcpy(img.data(), &o, sizeof o);
  img[129] = 1;  // waere randomModeEnabled = true
  const float rmin = 55.0f;
  memcpy(img.data() + 132, &rmin, 4);
  Config c = sentinel();
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.randomModeEnabled == false && c.randomMin == 20.0f);
  CHECK(strcmp(c.apSSID, "Alt") == 0 && strcmp(c.adminPassword, "admin") == 0);

  // Gleicher Inhalt als 0xCD: Zufallsfelder werden gelesen
  img[0] = 0xCD;
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(c.randomModeEnabled == true && c.randomMin == 55.0f);

  // SSID exakt 32 Bytes + NUL, Passwort exakt 31 Bytes + NUL: unveraendert
  OldConfig n = oldSample();
  std::string s32 = std::string(31, 's') + "!";
  std::string p31(31, 'p');
  putOldStr(n.apSSID, s32.c_str());
  putOldStr(n.adminPassword, p31.c_str());
  img = image(n);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == s32 && std::string(c.adminPassword) == p31);
  // 33 Bytes → 32
  putOldStr(n.apSSID, (s32 + "x").c_str());
  img = image(n);
  CHECK(decodeLegacy(img.data(), img.size(), c));
  CHECK(std::string(c.apSSID) == s32);

  // Ergebnis ist bereinigt und besteht validate unveraendert
  img = image(oldSample());
  CHECK(decodeLegacy(img.data(), img.size(), c));
  Config v = c;
  CHECK(accepts(v) && same(v, c));
}

// ── Review: AP-Name ───────────────────────────────────────────────────────────

static void testReviewApName() {
  const uint8_t mac[6] = {0, 0, 0, 0, 0xFE, 0x01};
  Config c = defaults();
  // Default-SSID mit Resten hinter dem NUL zaehlt als Default
  memset(c.apSSID + sizeof DEFAULT_AP_SSID, 'x', sizeof c.apSSID - sizeof DEFAULT_AP_SSID);
  CHECK(apName(c, mac) == "100-Waage-FE01");
  // Ausgabe exakt SSID_MAX + 1 Bytes auf dem Heap
  std::vector<char> out(SSID_MAX + 1, 'Z');
  effectiveApName(c, mac, out.data());
  CHECK(std::string(out.data()) == "100-Waage-FE01");
  memset(c.apSSID, 'w', sizeof c.apSSID);  // unterminiert
  effectiveApName(c, mac, out.data());
  CHECK(strnlen(out.data(), out.size()) == SSID_MAX);
  // Praefix der Default-SSID mit Zusatz ist nicht die Default-SSID
  setStr(c.apSSID, sizeof c.apSSID, "100-Waage-Config ");
  CHECK(apName(c, mac) == "100-Waage-Config ");
}

int main() {
  testDefaults();
  testSanitizeScaleFactor();
  testSanitizeTolerance();
  testSanitizeGoal();
  testSanitizeRandomMin();
  testSanitizeAutoZero();
  testSanitizeSmallFields();
  testSanitizeStrings();
  testSanitizeFuzz();
  testValidateFields();
  testValidateMatchesSanitize();
  testValidNewPassword();
  testParseFloat();
  testParseUint();
  testCopyUtf8();
  testEffectiveApName();
  testDiff();
  testLegacyRoundtrip();
  testLegacyCC();
  testLegacyRejects();
  testLegacyStrings();
  testLegacySanitized();
  testRollGoal();
  testReviewPasswords();
  testReviewUtf8Reference();
  testReviewCopyUtf8();
  testReviewParsers();
  testReviewFloatEdges();
  testReviewToleranceSweep();
  testReviewRollGoalBuckets();
  testReviewLegacyEeprom();
  testReviewApName();
  return finish("config_core_test");
}
