#pragma once
#include "button_core.h"
#include "config_core.h"
#include "game_core.h"
#include "text_core.h"
#include <stdint.h>

// ── Anzeige-Modell (rein, ohne Arduino) ───────────────────────────────────────
// Baut pro Loop ein Frame aus Spielzustand, Taster, Toasts und Status. Der
// Renderer (ui.cpp) zeichnet nur, wenn sich das Frame aendert. Ebenen, oberste
// zuerst: Haltebalken > Systembildschirm > Toast > Spiel.

namespace ui {

constexpr uint32_t READY_PROMPT_MS = 400; // "Bereit?" vor dem Trinkspruch
constexpr uint32_t RESULT_ALT_MS = 3000;  // Ergebnis-Wechsel (Wert/Zeit)
constexpr uint32_t ANIM_MS = 300;         // Ladeanimation
constexpr uint32_t TOAST_MS = 1500;
constexpr int MODE_DOTS = 3; // Game, Duell, Standard
constexpr int BAR_W = 128;   // Haltebalken ueber CANCEL_MS
constexpr int TICK_MODE_PX =
    (int)((uint64_t)button::MODE_MS * BAR_W / button::CANCEL_MS);
constexpr int TICK_RADIO_PX =
    (int)((uint64_t)button::RADIO_MS * BAR_W / button::CANCEL_MS);

enum class Kind : uint8_t {
  Text, // Textzeilen (+ ggf. Symbole)
  Hold, // Aktionstext + Haltebalken
  Anim
}; // Ladeanimation (Trinken)

struct Frame {
  Kind kind;
  text::Layout text; // CP437, fertig umbrochen
  bool icons;        // Statussymbole zeichnen (Idle-Bildschirme)
  bool border;       // Rahmen (Glas steht, Game-Idle)
  bool shuffle;      // Zufallsmodus-Symbol oben links
  // Oben rechts, von rechts nach links: Akku, "Vs n", WLAN-Bogen (AP)
  bool battIcon;
  uint8_t battPercent;
  bool duelIcon; // Duell-Modus, auch mit 0 Gegnern
  uint8_t peers;
  bool apIcon;
  bool lowBatt; // Akku-Warnung: Akkusymbol mit "!" statt Fuellung
  // Modus-Punkte (Game, Duell, Standard) unter einer Zeile in Groesse 1:
  // 0 = keine, sonst 1 + Position des gefuellten Punkts
  uint8_t modeDots;
  uint8_t barPx;     // Hold: gefuellte Breite 0..BAR_W
  uint8_t animFrame; // Anim: 0..4
};

// Byteweiser Vergleich; build() initialisiert jedes Frame vollstaendig mit 0.
bool sameFrame(const Frame &a, const Frame &b);

struct Status {
  bool apOn;
  int peers;      // sichtbare Duell-Waagen (nur im Duell-Modus relevant)
  bool battShown; // Akku angeschlossen
  int battPercent;
  bool battLow;
  cfg::ScaleMode mode;
};

struct Hold {
  bool active; // Taster gedrueckt und >= OVERLAY_MS
  button::Zone zone;
  uint32_t heldMs;
};

// Anzeigename eines Modus (UTF-8), z. B. "Duell-Modus".
const char *modeName(cfg::ScaleMode m);

// Aktionstext beim Loslassen in der jeweiligen Zone (UTF-8):
// Short "Tara", Mode Name des naechsten Modus (Game → Duell → Standard),
// Radio "AP aus" / "AP an", Cancel "Abbrechen".
const char *holdLabel(button::Zone z, cfg::ScaleMode mode, bool apOn);

// Texte der Bewertung (UTF-8), z. B. "Schüchtern".
const char *ratingText(game::Rating r);

class Model {
public:
  // Kurzmeldung ueber dem Spielbildschirm (UTF-8, wird kopiert, max. 63 Bytes).
  void toast(const char *utf8, uint32_t now, uint32_t ms = TOAST_MS);
  // Kurzmeldung nach einem Moduswechsel: Name + Modus-Punkte.
  void modeToast(cfg::ScaleMode m, uint32_t now);

  // system: nullptr oder bis zu 3 UTF-8-Zeilen (nullptr-Eintraege = leer),
  // z. B. Kalibrierschritte, "Sensorfehler", OTA-Fortschritt.
  Frame build(const game::View &v, const Status &s, const Hold &h,
              const char *const system[3], uint32_t now);

private:
  char toast_[64] = {};
  uint32_t toastUntil_ = 0;
  bool toastOn_ = false;
  uint8_t toastDots_ = 0;
  // Ergebnis-Wechsel
  bool altTime_ = false;
  uint32_t altSince_ = 0;
  uint32_t lastSig_ = 0xFFFFFFFFu;
  game::Screen lastScreen_ = game::Screen::IdleGame;
  uint32_t lastScreenSince_ = 0;
  // stiller Wechsel auf Solo → Toast "Solo!"
  bool seqInit_ = false;
  uint32_t lastSoloSeq_ = 0;
};

} // namespace ui
