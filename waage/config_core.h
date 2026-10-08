#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Konfiguration (rein, ohne Arduino) ────────────────────────────────────────
// Modell, Defaults, Plausibilisierung, Web-Validierung, Parser und der Import
// des alten EEPROM-Abbilds. Gespeichert wird in config.cpp (NVS).

namespace cfg {

constexpr size_t SSID_MAX = 32;     // Bytes, WLAN-Grenze
constexpr size_t PASSWORD_MAX = 31; // Bytes
constexpr size_t PASSWORD_MIN = 4;  // nur fuer neu gesetzte Passwoerter
constexpr char DEFAULT_AP_SSID[] = "100-Waage-Config";
constexpr char DEFAULT_PASSWORD[] = "admin";

// Gespeicherte Werte bleiben stabil (NVS): Duel kam als 2 dazu.
enum class ScaleMode : uint8_t { Game = 0, Standard = 1, Duel = 2 };
constexpr uint8_t MODE_COUNT = 3;

// Game und Duel spielen, Standard ist eine einfache Waage.
inline bool playsGame(ScaleMode m) { return m != ScaleMode::Standard; }
// Reihenfolge am Taster: Game → Duel → Standard → Game.
ScaleMode nextMode(ScaleMode m);
uint8_t modePosition(ScaleMode m); // 0..2 in dieser Reihenfolge
// Name in der Web-API ("Game", "Duel", "Standard") und zurueck.
const char *modeKey(ScaleMode m);
bool parseMode(const char *key, ScaleMode *out);

struct Config {
  char apSSID[SSID_MAX + 1];
  char adminPassword[PASSWORD_MAX + 1];
  float scaleFactor;       // HX711-Zaehlschritte pro Gramm (Vorzeichen erlaubt)
  float goal;              // Zielgewicht [g], 0,1 g Raster
  float tolerance;         // Messtoleranz [g]
  uint8_t displayRotation; // 0 = normal, 2 = 180°
  uint8_t wifiTimeout;     // AP-Auto-Aus [min], 0 = nie
  uint8_t sleepTimeout;    // Deep-Sleep nach Inaktivitaet [min], 0 = nie
  float battDividerRatio;  // Spannungsteiler am Akku-Pin
  bool batteryPresent;     // Akku messen und anzeigen (aus: Netzbetrieb)
  ScaleMode scaleMode;
  uint8_t autoResetRange; // [%] Ergebnis gilt als gut innerhalb dieses Bereichs
  bool autoZeroEnabled;
  float autoZeroThreshold; // [g]
  uint8_t autoZeroDelay;   // [s]
  bool randomModeEnabled;
  float randomMin;      // [g] Untergrenze des Zufallsziels
  bool goalPercent;     // Ziel in % vom Glasinhalt (nur Game-Modus)
  uint8_t goalPct;      // [%] Ziel im Prozent-Modus
  uint8_t randomMinPct; // [%] Untergrenze des Zufallsziels im Prozent-Modus
  uint8_t glassSwapMin; // [min] Tauschzeit der Glasbestimmung, 0 = aus
  bool statsRotation;   // Statistik im Ruhezustand im Wechsel mit dem Ziel
  uint8_t statsAfterS;  // [s] ohne Glas bis zur ersten Statistik
  uint8_t statsGoalS;   // [s] Anzeigedauer des Ziels in der Rotation
  uint8_t statsStepS;   // [s] Anzeigedauer je Statistik-Bildschirm
};

// Bereiche (gelten fuer sanitize und validate)
constexpr float SCALE_FACTOR_DEFAULT = 708.0f;
constexpr float SCALE_FACTOR_MIN_ABS = 1.0f;
constexpr float TOLERANCE_MIN = 0.5f, TOLERANCE_MAX = 100.0f;
constexpr float GOAL_MIN = 1.0f, GOAL_MAX = 5000.0f;
constexpr float AZ_THRESHOLD_MIN = 0.1f, AZ_THRESHOLD_MAX = 20.0f;
constexpr uint8_t AZ_DELAY_MIN = 1, AZ_DELAY_MAX = 60;
constexpr uint8_t AUTO_RESET_MAX = 100;
constexpr float BATT_RATIO_MIN = 1.0f, BATT_RATIO_MAX = 6.0f,
                BATT_RATIO_DEFAULT = 2.0f;
constexpr uint8_t STATS_AFTER_MIN = 1, STATS_AFTER_DEFAULT = 15;
constexpr uint8_t STATS_SHOW_MIN = 1, STATS_SHOW_MAX = 60;
constexpr uint8_t STATS_GOAL_DEFAULT = 6, STATS_STEP_DEFAULT = 4;
constexpr uint8_t GOAL_PCT_MIN = 1, GOAL_PCT_MAX = 100;
constexpr uint8_t GOAL_PCT_DEFAULT = 50, RANDOM_MIN_PCT_DEFAULT = 20;
constexpr uint8_t GLASS_SWAP_MAX = 60, GLASS_SWAP_DEFAULT = 5;

Config defaults();

// Beim Laden: klemmt jeden Wert in seinen gueltigen Bereich und scheitert nie.
// Regeln: scaleFactor endlich und |f| >= 1, sonst Default (gueltige Werte
// bleiben bit-genau). tolerance 0,5..100 (sonst 10). goal 1..5000, auf 0,1 g
// gerundet, mindestens tolerance + 1. randomMin auf [min(tolerance + 1, goal),
// goal] geklemmt, 0,1 g Raster. autoZeroThreshold 0,1..20 und <= tolerance.
// autoZeroDelay 1..60 (sonst 5). autoResetRange <= 100. displayRotation 0/2
// (sonst 0). battDividerRatio 1..6 (sonst 2). scaleMode 0..2 (sonst Game).
// statsAfterS 1..255 (sonst 20), statsGoalS/statsStepS 1..60 (sonst 6/3).
// goalPct 1..100 (sonst 50), randomMinPct auf [1, goalPct] geklemmt.
// glassSwapMin 0..60 (sonst 5).
// Strings werden terminiert; leere oder nicht druckbare SSID → Default-SSID,
// leeres Passwort → "admin" (kurze alte Passwoerter bleiben erhalten).
// Liefert true, wenn etwas korrigiert wurde.
bool sanitize(Config &c);

// Web-Eingabe: strikte Pruefung der zusammengefuehrten Config, gleiche
// Bereiche wie sanitize, aber Ablehnen statt Klemmen. Ausnahme: randomMin und
// randomMinPct werden geklemmt (das UI zeigt den wirksamen Wert). Zusatz:
// autoZeroThreshold <= tolerance, goal >= tolerance + 1, SSID 1..32 Bytes ohne
// Steuerzeichen.
struct Error {
  const char *field;   // nullptr = ok, sonst Feldname wie in der Web-API
  const char *message; // deutsch, fuer die Anzeige im Web
};
Error validate(Config &c);
bool validNewPassword(const char *pw); // 4..31 Bytes, keine Steuerzeichen

// Zahlen aus Web-Formularen: der ganze String muss eine endliche Zahl sein,
// fuehrende/abschliessende Leerzeichen erlaubt, ',' gilt als Dezimaltrenner.
bool parseFloat(const char *s, float *out);
bool parseUint(const char *s, uint32_t maxValue, uint32_t *out);

// Kopiert hoechstens maxBytes, ohne ein UTF-8-Zeichen zu zerschneiden.
// dst muss maxBytes + 1 Bytes fassen und ist danach terminiert.
void copyUtf8(char *dst, const char *src, size_t maxBytes);

// Wirksamer AP-Name: Default-SSID → "100-Waage-XXXX" (letzte zwei MAC-Bytes,
// Hex gross), sonst die SSID selbst.
void effectiveApName(const Config &c, const uint8_t mac[6],
                     char out[SSID_MAX + 1]);

// Welche Bereiche sich zwischen a und b unterscheiden (fuer Live-Uebernahme).
enum Change : uint32_t {
  CH_SSID = 1u << 0,
  CH_PASSWORD = 1u << 1,
  CH_SCALE = 1u << 2,  // scaleFactor
  CH_GOAL = 1u << 3,   // goal, goalPercent, goalPct
  CH_RANDOM = 1u << 4, // randomModeEnabled, randomMin, randomMinPct
  CH_ROTATION = 1u << 5,
  CH_MODE = 1u << 6,
  CH_TIMEOUTS = 1u << 7, // wifiTimeout, sleepTimeout
  CH_GAME = 1u << 8,     // tolerance, autoResetRange, glassSwapMin
  CH_AUTOZERO = 1u << 9, // autoZero*
  CH_BATT = 1u << 10,    // battDividerRatio, batteryPresent
  CH_STATS = 1u << 11,   // statsRotation, statsAfterS/GoalS/StepS
};
uint32_t diff(const Config &a, const Config &b);

// Altes EEPROM-Abbild (Struct WaageConfig, Magic 0xCD = 136 Byte, 0xCC = 132
// Byte ohne Zufallsfelder) mit expliziten Little-Endian-Offsets dekodieren:
// ssid@1[64], scaleFactor@68, goal@72, tolerance@76, rot@80, pw@81[32],
// wifiTo@113, sleepTo@114, battDiv@116, scaleMode@120, arRange@121,
// azOn@122, azThr@124, azDelay@128, rndOn@129 (nur 0xCD), rndMin@132 (nur
// 0xCD). Strings werden begrenzt gelesen und auf SSID_MAX/PASSWORD_MAX
// gekuerzt. Liefert false bei unbekanntem Magic oder zu kurzem Blob. Danach
// sanitize().
bool decodeLegacy(const uint8_t *blob, size_t len, Config &out);

// Zufallsziel aus einer Zufallszahl r: ganze Gramm in
// [ceil(max(randomMin, tolerance + 1)) .. floor(goal)]; leerer Bereich → goal.
float rollGoal(const Config &c, uint32_t r);

// Prozent-Ziel nur im Game-Modus (Duell und Standard: Gramm).
inline bool percentGoal(const Config &c) {
  return c.goalPercent && c.scaleMode == ScaleMode::Game;
}
// Zufallsziel im Prozent-Modus: ganze Prozent in [randomMinPct .. goalPct].
uint8_t rollGoalPct(const Config &c, uint32_t r);

} // namespace cfg
