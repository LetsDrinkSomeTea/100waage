#include "config_core.h"
#include <cmath>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cfg {

namespace {

constexpr float GOAL_DEFAULT = 100.0f;
constexpr float TOLERANCE_DEFAULT = 10.0f;
constexpr float RANDOM_MIN_DEFAULT = 20.0f;
constexpr float AZ_THRESHOLD_DEFAULT = 2.0f;
constexpr uint8_t AZ_DELAY_DEFAULT = 5;
constexpr size_t NUM_BUF = 48; // laengere Zahl-Strings werden abgelehnt

static_assert(PASSWORD_MAX <= SSID_MAX,
              "Hilfspuffer sind auf SSID_MAX ausgelegt");
static_assert(sizeof DEFAULT_AP_SSID <= SSID_MAX + 1, "Default-SSID zu lang");
static_assert(sizeof DEFAULT_PASSWORD <= PASSWORD_MAX + 1,
              "Default-Passwort zu lang");

// ── Kleine Helfer ─────────────────────────────────────────────────────────────

// Laenge bis NUL, liest hoechstens limit Bytes.
size_t boundedLen(const char *s, size_t limit) {
  size_t n = 0;
  while (n < limit && s[n] != '\0')
    n++;
  return n;
}

bool spaceChar(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool digitChar(char ch) { return ch >= '0' && ch <= '9'; }

bool asciiControl(uint8_t b) { return b < 0x20 || b == 0x7F; }

bool isCont(uint8_t b) { return (b & 0xC0) == 0x80; }

// Laenge einer UTF-8-Sequenz anhand des Startbytes (ungueltig → 1).
size_t seqLen(uint8_t b) {
  if (b >= 0xC0 && b <= 0xDF)
    return 2;
  if (b >= 0xE0 && b <= 0xEF)
    return 3;
  if (b >= 0xF0 && b <= 0xF7)
    return 4;
  return 1;
}

// Druckbar: gueltiges UTF-8 (keine Overlongs/Surrogates), keine Steuerzeichen
// (C0, DEL und C1 U+0080..U+009F).
bool printableUtf8(const char *str, size_t n) {
  const uint8_t *s = (const uint8_t *)str;
  size_t i = 0;
  while (i < n) {
    uint8_t b = s[i];
    if (b < 0x80) {
      if (asciiControl(b))
        return false;
      i++;
      continue;
    }
    size_t len;
    uint8_t lo = 0x80, hi = 0xBF; // erlaubter Bereich des zweiten Bytes
    if (b >= 0xC2 && b <= 0xDF) {
      len = 2;
      if (b == 0xC2)
        lo = 0xA0; // C1-Steuerzeichen ausschliessen
    } else if (b == 0xE0) {
      len = 3, lo = 0xA0;
    } else if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF) {
      len = 3;
    } else if (b == 0xED) {
      len = 3, hi = 0x9F; // keine Surrogates
    } else if (b == 0xF0) {
      len = 4, lo = 0x90;
    } else if (b >= 0xF1 && b <= 0xF3) {
      len = 4;
    } else if (b == 0xF4) {
      len = 4, hi = 0x8F; // <= U+10FFFF
    } else {
      return false;
    }
    if (n - i < len)
      return false;
    if (s[i + 1] < lo || s[i + 1] > hi)
      return false;
    for (size_t k = 2; k < len; k++)
      if (!isCont(s[i + k]))
        return false;
    i += len;
  }
  return true;
}

// SSID gueltig: 1..SSID_MAX Bytes, terminiert im Array, druckbar.
bool ssidOk(const char (&ssid)[SSID_MAX + 1]) {
  size_t n = boundedLen(ssid, SSID_MAX + 1);
  return n >= 1 && n <= SSID_MAX && printableUtf8(ssid, n);
}

// Feld fester Groesse (maxBytes + 1) terminieren; fehlt das NUL, wird an
// einer Zeichengrenze gekuerzt. Liefert true bei Korrektur.
bool terminate(char *s, size_t maxBytes) {
  if (boundedLen(s, maxBytes + 1) <= maxBytes)
    return false;
  char tmp[SSID_MAX + 2];
  memcpy(tmp, s, maxBytes + 1);
  tmp[maxBytes + 1] = '\0';
  memset(s, 0, maxBytes + 1);
  copyUtf8(s, tmp, maxBytes);
  return true;
}

void setString(char *s, size_t size, const char *v) {
  memset(s, 0, size);
  copyUtf8(s, v, size - 1);
}

bool sameBits(float a, float b) {
  uint32_t x, y;
  memcpy(&x, &a, sizeof x);
  memcpy(&y, &b, sizeof y);
  return x == y;
}

void setF(float &f, float v, bool &changed) {
  if (!sameBits(f, v)) {
    f = v;
    changed = true;
  }
}

void setU8(uint8_t &f, uint8_t v, bool &changed) {
  if (f != v) {
    f = v;
    changed = true;
  }
}

// bool aus Rohspeicher (NVS/EEPROM) kann andere Bytes als 0/1 enthalten.
void fixBool(bool &f, bool &changed) {
  uint8_t raw;
  memcpy(&raw, &f, 1);
  if (raw > 1) {
    f = true;
    changed = true;
  }
}

float clampF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Auf 0,1 g runden (nur fuer Werte im Grammbereich aufgerufen).
float round10(float v) { return roundf(v * 10.0f) / 10.0f; }

// Kleinster Wert im 0,1-g-Raster, der >= v ist.
float ceil10(float v) {
  float k = roundf(v * 10.0f);
  float g = k / 10.0f;
  if (g < v)
    g = (k + 1.0f) / 10.0f;
  return g;
}

bool scaleFactorOk(float f) {
  return std::isfinite(f) && fabsf(f) >= SCALE_FACTOR_MIN_ABS;
}

// Untergrenze fuer randomMin (im Raster, nie ueber goal).
float randomMinLow(const Config &c) {
  float minG = c.tolerance + 1.0f;
  float lo = ceil10(minG < c.goal ? minG : c.goal);
  return lo > c.goal ? c.goal : lo;
}

// randomMin klemmen und runden; tolerance und goal muessen gueltig sein.
float normRandomMin(const Config &c) {
  float r = std::isfinite(c.randomMin) ? c.randomMin : RANDOM_MIN_DEFAULT;
  return round10(clampF(r, randomMinLow(c), c.goal));
}

// randomMinPct klemmen; goalPct muss gueltig sein.
uint8_t normRandomMinPct(const Config &c) {
  uint8_t r = c.randomMinPct < GOAL_PCT_MIN ? GOAL_PCT_MIN : c.randomMinPct;
  return r > c.goalPct ? c.goalPct : r;
}

uint32_t readU32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

float readF32le(const uint8_t *p) {
  uint32_t v = readU32le(p);
  float f;
  memcpy(&f, &v, sizeof f);
  return f;
}

// String-Feld fester Laenge begrenzt lesen (NUL optional) und kuerzen.
void readString(const uint8_t *p, size_t fieldLen, char *dst, size_t dstSize) {
  char tmp[64 + 1];
  size_t n = 0;
  while (n < fieldLen && n < sizeof tmp - 1 && p[n] != 0)
    n++;
  memcpy(tmp, p, n);
  tmp[n] = '\0';
  memset(dst, 0, dstSize);
  copyUtf8(dst, tmp, dstSize - 1);
}

// Altes Layout (WaageConfig, RV32/x86, Little Endian)
namespace legacy {
constexpr uint8_t MAGIC_RANDOM = 0xCD; // mit Zufallsfeldern
constexpr uint8_t MAGIC_PLAIN = 0xCC;  // ohne Zufallsfelder
constexpr size_t SIZE_RANDOM = 136, SIZE_PLAIN = 132;
constexpr size_t SSID = 1, SSID_LEN = 64;
constexpr size_t SCALE = 68, GOAL = 72, TOL = 76, ROT = 80;
constexpr size_t PW = 81, PW_LEN = 32;
constexpr size_t WIFI_TO = 113, SLEEP_TO = 114, BATT = 116;
constexpr size_t MODE = 120, AR_RANGE = 121, AZ_ON = 122, AZ_THR = 124,
                 AZ_DELAY = 128;
constexpr size_t RND_ON = 129, RND_MIN = 132;
} // namespace legacy

} // namespace

// ── Modi ──────────────────────────────────────────────────────────────────────

ScaleMode nextMode(ScaleMode m) {
  switch (m) {
  case ScaleMode::Game:
    return ScaleMode::Duel;
  case ScaleMode::Duel:
    return ScaleMode::Standard;
  case ScaleMode::Standard:
    break;
  }
  return ScaleMode::Game;
}

uint8_t modePosition(ScaleMode m) {
  switch (m) {
  case ScaleMode::Game:
    return 0;
  case ScaleMode::Duel:
    return 1;
  case ScaleMode::Standard:
    return 2;
  }
  return 0;
}

const char *modeKey(ScaleMode m) {
  switch (m) {
  case ScaleMode::Game:
    return "Game";
  case ScaleMode::Duel:
    return "Duel";
  case ScaleMode::Standard:
    return "Standard";
  }
  return "Game";
}

bool parseMode(const char *key, ScaleMode *out) {
  for (uint8_t i = 0; i < MODE_COUNT; i++) {
    ScaleMode m = (ScaleMode)i;
    if (strcmp(key, modeKey(m)) == 0) {
      *out = m;
      return true;
    }
  }
  return false;
}

// ── Defaults ──────────────────────────────────────────────────────────────────

Config defaults() {
  Config c;
  memset(&c, 0, sizeof c); // auch Padding und String-Reste nullen
  memcpy(c.apSSID, DEFAULT_AP_SSID, sizeof DEFAULT_AP_SSID);
  memcpy(c.adminPassword, DEFAULT_PASSWORD, sizeof DEFAULT_PASSWORD);
  c.scaleFactor = SCALE_FACTOR_DEFAULT;
  c.goal = GOAL_DEFAULT;
  c.tolerance = TOLERANCE_DEFAULT;
  c.displayRotation = 0;
  c.wifiTimeout = 10;
  c.sleepTimeout = 5;
  c.battDividerRatio = BATT_RATIO_DEFAULT;
  c.batteryPresent = true;
  c.scaleMode = ScaleMode::Game;
  c.autoResetRange = 10;
  c.autoZeroEnabled = true;
  c.autoZeroThreshold = AZ_THRESHOLD_DEFAULT;
  c.autoZeroDelay = AZ_DELAY_DEFAULT;
  c.randomModeEnabled = false;
  c.randomMin = RANDOM_MIN_DEFAULT;
  c.goalPercent = false;
  c.goalPct = GOAL_PCT_DEFAULT;
  c.randomMinPct = RANDOM_MIN_PCT_DEFAULT;
  c.glassSwapMin = GLASS_SWAP_DEFAULT;
  c.statsRotation = true;
  c.statsAfterS = STATS_AFTER_DEFAULT;
  c.statsGoalS = STATS_GOAL_DEFAULT;
  c.statsStepS = STATS_STEP_DEFAULT;
  return c;
}

// ── Plausibilisierung beim Laden ──────────────────────────────────────────────

bool sanitize(Config &c) {
  bool ch = false;

  // Strings
  ch |= terminate(c.apSSID, SSID_MAX);
  if (!ssidOk(c.apSSID)) {
    setString(c.apSSID, sizeof c.apSSID, DEFAULT_AP_SSID);
    ch = true;
  }
  ch |= terminate(c.adminPassword, PASSWORD_MAX);
  if (c.adminPassword[0] == '\0') {
    setString(c.adminPassword, sizeof c.adminPassword, DEFAULT_PASSWORD);
    ch = true;
  }

  // Bools zuerst normalisieren, danach sind Lesezugriffe definiert
  fixBool(c.autoZeroEnabled, ch);
  fixBool(c.randomModeEnabled, ch);
  fixBool(c.statsRotation, ch);
  fixBool(c.batteryPresent, ch);
  fixBool(c.goalPercent, ch);

  if (!scaleFactorOk(c.scaleFactor))
    setF(c.scaleFactor, SCALE_FACTOR_DEFAULT, ch);

  if (!(c.tolerance >= TOLERANCE_MIN && c.tolerance <= TOLERANCE_MAX))
    setF(c.tolerance, TOLERANCE_DEFAULT, ch);

  // goal haengt von tolerance ab
  float g = std::isfinite(c.goal) ? c.goal : GOAL_DEFAULT;
  g = round10(clampF(g, GOAL_MIN, GOAL_MAX));
  float minGoal = c.tolerance + 1.0f;
  if (g < minGoal)
    g = ceil10(minGoal);
  setF(c.goal, g, ch);

  // randomMin haengt von tolerance und goal ab
  setF(c.randomMin, normRandomMin(c), ch);

  if (c.goalPct < GOAL_PCT_MIN || c.goalPct > GOAL_PCT_MAX)
    setU8(c.goalPct, GOAL_PCT_DEFAULT, ch);
  setU8(c.randomMinPct, normRandomMinPct(c), ch);
  if (c.glassSwapMin > GLASS_SWAP_MAX)
    setU8(c.glassSwapMin, GLASS_SWAP_DEFAULT, ch);

  float az = std::isfinite(c.autoZeroThreshold) ? c.autoZeroThreshold
                                                : AZ_THRESHOLD_DEFAULT;
  float azMax = c.tolerance < AZ_THRESHOLD_MAX ? c.tolerance : AZ_THRESHOLD_MAX;
  setF(c.autoZeroThreshold, clampF(az, AZ_THRESHOLD_MIN, azMax), ch);

  if (c.autoZeroDelay < AZ_DELAY_MIN || c.autoZeroDelay > AZ_DELAY_MAX)
    setU8(c.autoZeroDelay, AZ_DELAY_DEFAULT, ch);
  if (c.autoResetRange > AUTO_RESET_MAX)
    setU8(c.autoResetRange, AUTO_RESET_MAX, ch);
  if (c.statsAfterS < STATS_AFTER_MIN)
    setU8(c.statsAfterS, STATS_AFTER_DEFAULT, ch);
  if (c.statsGoalS < STATS_SHOW_MIN || c.statsGoalS > STATS_SHOW_MAX)
    setU8(c.statsGoalS, STATS_GOAL_DEFAULT, ch);
  if (c.statsStepS < STATS_SHOW_MIN || c.statsStepS > STATS_SHOW_MAX)
    setU8(c.statsStepS, STATS_STEP_DEFAULT, ch);
  if (c.displayRotation != 0 && c.displayRotation != 2)
    setU8(c.displayRotation, 0, ch);

  if (!(c.battDividerRatio >= BATT_RATIO_MIN &&
        c.battDividerRatio <= BATT_RATIO_MAX))
    setF(c.battDividerRatio, BATT_RATIO_DEFAULT, ch);

  if ((uint8_t)c.scaleMode >= MODE_COUNT) {
    c.scaleMode = ScaleMode::Game;
    ch = true;
  }
  return ch;
}

// ── Web-Validierung ───────────────────────────────────────────────────────────

Error validate(Config &c) {
  size_t n = boundedLen(c.apSSID, SSID_MAX + 1);
  if (n == 0)
    return {"apSSID", "SSID darf nicht leer sein"};
  if (n > SSID_MAX)
    return {"apSSID", "SSID ist zu lang (max. 32 Bytes)"};
  if (!printableUtf8(c.apSSID, n))
    return {"apSSID", "SSID enthält ungültige Zeichen"};

  // Passwort wie sanitize: leer oder nicht terminiert wuerde dort korrigiert.
  // Kurze alte Passwoerter bleiben gueltig (daher kein validNewPassword).
  size_t pn = boundedLen(c.adminPassword, PASSWORD_MAX + 1);
  if (pn == 0)
    return {"newPassword", "Passwort darf nicht leer sein"};
  if (pn > PASSWORD_MAX)
    return {"newPassword", "Passwort ist zu lang (max. 31 Bytes)"};

  const float tol = c.tolerance;
  if (!(tol >= TOLERANCE_MIN && tol <= TOLERANCE_MAX))
    return {"tolerance", "Toleranz muss zwischen 0,5 und 100 g liegen"};

  // goal wird wie in sanitize auf 0,1 g gerundet (Raster, kein Klemmen)
  float g = c.goal;
  if (std::isfinite(g))
    g = round10(g);
  if (!(g >= GOAL_MIN && g <= GOAL_MAX))
    return {"goal", "Zielgewicht muss zwischen 1 und 5000 g liegen"};
  if (g < tol + 1.0f)
    return {"goal", "Zielgewicht muss mindestens Toleranz + 1 g betragen"};

  if (!std::isfinite(c.randomMin))
    return {"randomMin", "Zufalls-Minimum ist keine gültige Zahl"};
  if (c.goalPct < GOAL_PCT_MIN || c.goalPct > GOAL_PCT_MAX)
    return {"goalPct", "Ziel muss zwischen 1 und 100 % liegen"};
  if (c.glassSwapMin > GLASS_SWAP_MAX)
    return {"glassSwapMin", "Tauschzeit muss zwischen 0 und 60 min liegen"};

  if (c.autoResetRange > AUTO_RESET_MAX)
    return {"autoResetRange",
            "Auto-Reset-Bereich darf höchstens 100 % betragen"};

  // wifiTimeout, sleepTimeout: jeder uint8_t-Wert ist gueltig (0 = nie)

  const float az = c.autoZeroThreshold;
  if (!(az >= AZ_THRESHOLD_MIN && az <= AZ_THRESHOLD_MAX))
    return {"autoZeroThreshold",
            "Auto-Zero-Schwellwert muss zwischen 0,1 und 20 g liegen"};
  if (az > tol)
    return {"autoZeroThreshold",
            "Auto-Zero-Schwellwert darf nicht größer als die Toleranz sein"};
  if (c.autoZeroDelay < AZ_DELAY_MIN || c.autoZeroDelay > AZ_DELAY_MAX)
    return {"autoZeroDelay",
            "Auto-Zero-Verzögerung muss zwischen 1 und 60 s liegen"};
  if (c.statsAfterS < STATS_AFTER_MIN)
    return {"statsAfterS", "Wartezeit muss zwischen 1 und 255 s liegen"};
  if (c.statsGoalS < STATS_SHOW_MIN || c.statsGoalS > STATS_SHOW_MAX)
    return {"statsGoalS", "Anzeigedauer muss zwischen 1 und 60 s liegen"};
  if (c.statsStepS < STATS_SHOW_MIN || c.statsStepS > STATS_SHOW_MAX)
    return {"statsStepS", "Anzeigedauer muss zwischen 1 und 60 s liegen"};

  if (c.displayRotation != 0 && c.displayRotation != 2)
    return {"displayRotation", "Display-Rotation muss 0° oder 180° sein"};
  if (!(c.battDividerRatio >= BATT_RATIO_MIN &&
        c.battDividerRatio <= BATT_RATIO_MAX))
    return {"battDividerRatio", "Spannungsteiler muss zwischen 1 und 6 liegen"};
  if ((uint8_t)c.scaleMode >= MODE_COUNT)
    return {"scaleMode", "Unbekannter Waagen-Modus"};
  if (!scaleFactorOk(c.scaleFactor))
    return {"scaleFactor", "Kalibrierfaktor ist ungültig"};

  // Alles gueltig: erst jetzt aendern
  c.goal = g;
  c.randomMin = normRandomMin(c);
  c.randomMinPct = normRandomMinPct(c);
  return {nullptr, nullptr};
}

bool validNewPassword(const char *pw) {
  if (!pw)
    return false;
  size_t n = boundedLen(pw, PASSWORD_MAX + 1);
  // Steuerzeichen wie bei der SSID: C0, DEL, C1 und kaputtes UTF-8
  return n >= PASSWORD_MIN && n <= PASSWORD_MAX && printableUtf8(pw, n);
}

// ── Parser ────────────────────────────────────────────────────────────────────

bool parseFloat(const char *s, float *out) {
  if (!s || !out)
    return false;
  while (spaceChar(*s))
    s++;
  size_t len = strlen(s);
  while (len > 0 && spaceChar(s[len - 1]))
    len--;
  if (len == 0 || len >= NUM_BUF)
    return false;

  // Syntax: [+-] Ziffern [(.|,) Ziffern] [(e|E) [+-] Ziffern]
  size_t i = 0;
  bool digits = false;
  if (s[i] == '+' || s[i] == '-')
    i++;
  while (i < len && digitChar(s[i]))
    i++, digits = true;
  if (i < len && (s[i] == '.' || s[i] == ',')) {
    i++;
    while (i < len && digitChar(s[i]))
      i++, digits = true;
  }
  if (!digits)
    return false;
  if (i < len && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < len && (s[i] == '+' || s[i] == '-'))
      i++;
    bool expDigits = false;
    while (i < len && digitChar(s[i]))
      i++, expDigits = true;
    if (!expDigits)
      return false;
  }
  if (i != len)
    return false;

  char buf[NUM_BUF];
  for (size_t k = 0; k < len; k++)
    buf[k] = s[k] == ',' ? '.' : s[k];
  buf[len] = '\0';
  char *end = nullptr;
  float v = strtof(buf, &end);
  if (end != buf + len || !std::isfinite(v))
    return false;
  *out = v;
  return true;
}

bool parseUint(const char *s, uint32_t maxValue, uint32_t *out) {
  if (!s || !out)
    return false;
  while (spaceChar(*s))
    s++;
  uint64_t v = 0;
  bool any = false;
  while (digitChar(*s)) {
    v = v * 10 + (uint64_t)(*s - '0');
    if (v > maxValue)
      return false; // vor jeder Verengung, ohne Ueberlauf
    any = true;
    s++;
  }
  while (spaceChar(*s))
    s++;
  if (!any || *s != '\0')
    return false;
  *out = (uint32_t)v;
  return true;
}

// ── Strings ───────────────────────────────────────────────────────────────────

void copyUtf8(char *dst, const char *src, size_t maxBytes) {
  if (!dst)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = boundedLen(src, maxBytes);
  const uint8_t *s = (const uint8_t *)src;
  if (n == maxBytes && s[n] != 0 && isCont(s[n])) {
    // Abgeschnitten mitten in einer Sequenz? Bis zum Startbyte zurueck.
    size_t k = n;
    int steps = 0;
    while (k > 0 && steps < 3 && isCont(s[k]))
      k--, steps++;
    if (s[k] >= 0xC0 && k + seqLen(s[k]) > n)
      n = k;
  }
  memmove(dst, src, n);
  dst[n] = '\0';
}

void effectiveApName(const Config &c, const uint8_t mac[6],
                     char out[SSID_MAX + 1]) {
  size_t n = boundedLen(c.apSSID, SSID_MAX + 1);
  bool isDefault = n == sizeof DEFAULT_AP_SSID - 1 &&
                   memcmp(c.apSSID, DEFAULT_AP_SSID, n) == 0;
  if (isDefault || n == 0) { // leer: AP braucht trotzdem einen Namen
    snprintf(out, SSID_MAX + 1, "100-Waage-%02X%02X", mac[4], mac[5]);
    return;
  }
  char tmp[SSID_MAX + 2];
  memcpy(tmp, c.apSSID, n);
  tmp[n] = '\0';
  copyUtf8(out, tmp, SSID_MAX);
}

// ── Aenderungen ───────────────────────────────────────────────────────────────

uint32_t diff(const Config &a, const Config &b) {
  uint32_t m = 0;
  if (strncmp(a.apSSID, b.apSSID, sizeof a.apSSID) != 0)
    m |= CH_SSID;
  if (strncmp(a.adminPassword, b.adminPassword, sizeof a.adminPassword) != 0)
    m |= CH_PASSWORD;
  if (!sameBits(a.scaleFactor, b.scaleFactor))
    m |= CH_SCALE;
  if (!sameBits(a.goal, b.goal))
    m |= CH_GOAL;
  if (a.goalPercent != b.goalPercent || a.goalPct != b.goalPct)
    m |= CH_GOAL;
  if (a.randomModeEnabled != b.randomModeEnabled ||
      !sameBits(a.randomMin, b.randomMin) || a.randomMinPct != b.randomMinPct)
    m |= CH_RANDOM;
  if (a.displayRotation != b.displayRotation)
    m |= CH_ROTATION;
  if (a.scaleMode != b.scaleMode)
    m |= CH_MODE;
  if (a.wifiTimeout != b.wifiTimeout || a.sleepTimeout != b.sleepTimeout)
    m |= CH_TIMEOUTS;
  if (!sameBits(a.tolerance, b.tolerance) ||
      a.autoResetRange != b.autoResetRange || a.glassSwapMin != b.glassSwapMin)
    m |= CH_GAME;
  if (a.autoZeroEnabled != b.autoZeroEnabled ||
      !sameBits(a.autoZeroThreshold, b.autoZeroThreshold) ||
      a.autoZeroDelay != b.autoZeroDelay)
    m |= CH_AUTOZERO;
  if (!sameBits(a.battDividerRatio, b.battDividerRatio) ||
      a.batteryPresent != b.batteryPresent)
    m |= CH_BATT;
  if (a.statsRotation != b.statsRotation || a.statsAfterS != b.statsAfterS ||
      a.statsGoalS != b.statsGoalS || a.statsStepS != b.statsStepS)
    m |= CH_STATS;
  return m;
}

// ── Altes EEPROM-Abbild ───────────────────────────────────────────────────────

bool decodeLegacy(const uint8_t *blob, size_t len, Config &out) {
  using namespace legacy;
  if (!blob || len < 1)
    return false;
  bool withRandom;
  if (blob[0] == MAGIC_RANDOM) {
    if (len < SIZE_RANDOM)
      return false;
    withRandom = true;
  } else if (blob[0] == MAGIC_PLAIN) {
    if (len < SIZE_PLAIN)
      return false;
    withRandom = false;
  } else {
    return false;
  }

  Config c = defaults();
  readString(blob + SSID, SSID_LEN, c.apSSID, sizeof c.apSSID);
  readString(blob + PW, PW_LEN, c.adminPassword, sizeof c.adminPassword);
  c.scaleFactor = readF32le(blob + SCALE);
  c.goal = readF32le(blob + GOAL);
  c.tolerance = readF32le(blob + TOL);
  c.displayRotation = blob[ROT];
  c.wifiTimeout = blob[WIFI_TO];
  c.sleepTimeout = blob[SLEEP_TO];
  c.battDividerRatio = readF32le(blob + BATT);
  c.scaleMode = (ScaleMode)blob[MODE]; // ungueltige Werte faengt sanitize ab
  c.autoResetRange = blob[AR_RANGE];
  c.autoZeroEnabled = blob[AZ_ON] != 0;
  c.autoZeroThreshold = readF32le(blob + AZ_THR);
  c.autoZeroDelay = blob[AZ_DELAY];
  if (withRandom) {
    c.randomModeEnabled = blob[RND_ON] != 0;
    c.randomMin = readF32le(blob + RND_MIN);
  }
  sanitize(c);
  out = c;
  return true;
}

// ── Zufallsziel ───────────────────────────────────────────────────────────────

float rollGoal(const Config &c, uint32_t r) {
  float lo = ceilf(fmaxf(c.randomMin, c.tolerance + 1.0f));
  float hi = floorf(c.goal);
  // NaN/negativ/riesig (unplausibel) oder leerer Bereich → goal
  if (!(lo >= 0.0f && hi <= 1.0e6f && lo <= hi))
    return c.goal;
  uint32_t a = (uint32_t)lo, b = (uint32_t)hi;
  uint32_t span = b - a + 1;
  uint32_t k = (uint32_t)(((uint64_t)r * span) >> 32); // 0 → a, 0xFFFFFFFF → b
  return (float)(a + k);
}

uint8_t rollGoalPct(const Config &c, uint32_t r) {
  uint8_t hi = c.goalPct;
  uint8_t lo = c.randomMinPct < GOAL_PCT_MIN ? GOAL_PCT_MIN : c.randomMinPct;
  if (hi < GOAL_PCT_MIN || hi > GOAL_PCT_MAX || lo > hi)
    return hi;
  uint32_t span = (uint32_t)(hi - lo) + 1;
  return (uint8_t)(lo + (uint32_t)(((uint64_t)r * span) >> 32));
}

} // namespace cfg
