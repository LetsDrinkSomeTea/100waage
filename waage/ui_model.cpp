#include "ui_model.h"
#include <stdio.h>
#include <string.h>

namespace ui {

bool sameFrame(const Frame &a, const Frame &b) {
  return memcmp(&a, &b, sizeof(Frame)) == 0;
}

const char *modeName(cfg::ScaleMode m) {
  switch (m) {
  case cfg::ScaleMode::Game:
    return "Game-Modus";
  case cfg::ScaleMode::Duel:
    return "Duell-Modus";
  case cfg::ScaleMode::Standard:
    return "Standard-Modus";
  }
  return "";
}

const char *holdLabel(button::Zone z, cfg::ScaleMode mode, bool apOn) {
  switch (z) {
  case button::Zone::Short:
    return "Tara";
  case button::Zone::Mode:
    return modeName(cfg::nextMode(mode));
  case button::Zone::Radio:
    return apOn ? "AP aus" : "AP an";
  case button::Zone::Cancel:
    return "Abbrechen";
  case button::Zone::None:
    break;
  }
  return "";
}

const char *ratingText(game::Rating r) {
  switch (r) {
  case game::Rating::Perfect:
    return "Perfekt!";
  case game::Rating::NotBad:
    return "Not Bad!";
  case game::Rating::Ok:
    return "Ganz ok!";
  case game::Rating::Shy:
    return "Schüchtern";
  case game::Rating::Greedy:
    return "Zu gierig!";
  }
  return "";
}

// Kopiert hoechstens n-1 Bytes, ohne ein UTF-8-Zeichen zu zerschneiden
static void copyText(char *dst, const char *src, size_t n) {
  size_t len = strlen(src);
  if (len > n - 1) {
    len = n - 1;
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80)
      len--;
  }
  memcpy(dst, src, len);
  dst[len] = 0;
}

void Model::toast(const char *utf8, uint32_t now, uint32_t ms) {
  copyText(toast_, utf8 ? utf8 : "", sizeof(toast_));
  toastUntil_ = now + ms;
  toastOn_ = true;
  toastDots_ = 0;
}

void Model::modeToast(cfg::ScaleMode m, uint32_t now) {
  toast(modeName(m), now);
  toastDots_ = (uint8_t)(1 + cfg::modePosition(m));
}

static void setText(Frame &f, const char *a, const char *b = nullptr,
                    const char *c = nullptr) {
  text::layout(a, b, c, f.text);
}

// Eine Zeile in Groesse 1 mit Modus-Punkten darunter (Platz fuer die Punkte)
static void setModeText(Frame &f, const char *utf8, uint8_t dots) {
  setText(f, utf8);
  f.text.size = 1;
  f.modeDots = dots;
}

static void setIcons(Frame &f, const Status &s) {
  f.icons = true;
  f.apIcon = s.apOn;
  if (s.mode == cfg::ScaleMode::Duel) {
    f.duelIcon = true;
    f.peers = (uint8_t)(s.peers < 0 ? 0 : (s.peers > 99 ? 99 : s.peers));
  }
  if (s.battShown) {
    f.battIcon = true;
    f.lowBatt = s.battLow;
    f.battPercent =
        (uint8_t)(s.battPercent < 0
                      ? 0
                      : (s.battPercent > 100 ? 100 : s.battPercent));
  }
}

// "123.45" + Suffix (2 Nachkommastellen aus Hundertsteln)
static void fmtHundredths(int32_t v, const char *suffix, char *out, size_t n) {
  char num[16];
  text::fmtCentigrams(v, num, sizeof(num));
  snprintf(out, n, "%s%s", num, suffix);
}

// "+0.41g" / "-0.08g" / "0.00g"
static void fmtDev(int32_t devCg, char *out, size_t n) {
  char num[16];
  text::fmtCentigrams(devCg, num, sizeof(num));
  snprintf(out, n, "%s%sg", devCg > 0 ? "+" : "", num);
}

static void fmtSeconds(uint32_t ms, char *out, size_t n) {
  fmtHundredths((int32_t)((ms + 5) / 10), "s", out, n);
}

static void fmtGoal(int32_t goalCg, char *out, size_t n) {
  text::fmtGrams1((float)goalCg / 100.0f, out, n);
  strncat(out, "g", n - strlen(out) - 1);
}

static const char *plural(uint32_t n, const char *one, const char *many) {
  return n == 1 ? one : many;
}

// Erfolg der aktuellen Runde, der im Ergebnis-Wechsel mitlaeuft
static bool showsAchievement(const game::View &v, const Status &s) {
  if (!s.stats || s.ach == stats::Achievement::None || s.achSeq != v.roundSeq)
    return false;
  if (v.screen == game::Screen::ResultSolo)
    return true;
  return v.screen == game::Screen::ResultDuel && v.isFinal && !v.forfeit &&
         v.rank != 0;
}

static void buildAchievement(Frame &f, const Status &s) {
  const stats::Totals &t = s.stats->totals();
  char b[32];
  if (s.ach == stats::Achievement::Record) {
    fmtHundredths(t.bestDevCg, "g daneben", b, sizeof(b));
    setText(f, "Neuer Rekord!", b);
  } else {
    fmtSeconds(t.fastestMs, b, sizeof(b));
    setText(f, "Schnellste Zeit!", b);
  }
}

static void buildStats(Frame &f, StatsScreen sc, const stats::Tracker &st) {
  const stats::Totals &t = st.totals();
  char a[64], b[24], c[24], line[64];
  switch (sc) {
  case StatsScreen::Best:
    if (!t.hasBest) {
      setText(f, "Bester Treffer", "noch keiner");
      break;
    }
    fmtHundredths(t.bestDevCg, "g daneben", a, sizeof(a));
    fmtGoal(t.bestGoalCg, b, sizeof(b));
    fmtSeconds(t.bestMs, c, sizeof(c));
    snprintf(line, sizeof(line), "Ziel %s, %s", b, c);
    setText(f, "Bester Treffer", a, line);
    break;
  case StatsScreen::Fastest:
    if (!t.hasFastest) {
      setText(f, "Schnellste Zeit", "noch keine");
      break;
    }
    fmtSeconds(t.fastestMs, a, sizeof(a));
    fmtHundredths(t.fastestDevCg < 0 ? -t.fastestDevCg : t.fastestDevCg,
                  "g daneben", b, sizeof(b));
    setText(f, "Schnellste Zeit", a, b);
    break;
  case StatsScreen::Rounds:
    snprintf(a, sizeof(a), "%lu %s", (unsigned long)t.rounds,
             plural(t.rounds, "Runde", "Runden"));
    if (t.duels > 0) {
      snprintf(line, sizeof(line), "%lu %s, %lu %s", (unsigned long)t.wins,
               plural(t.wins, "Sieg", "Siege"), (unsigned long)t.duels,
               plural(t.duels, "Duell", "Duelle"));
      setText(f, a, line);
    } else {
      setText(f, a);
    }
    break;
  case StatsScreen::Levels:
    snprintf(a, sizeof(a), "Perfekt %lu", (unsigned long)t.perfect);
    snprintf(b, sizeof(b), "Not Bad %lu", (unsigned long)t.notBad);
    snprintf(c, sizeof(c), "Ganz ok %lu", (unsigned long)t.ok);
    setText(f, a, b, c);
    break;
  case StatsScreen::Recent: {
    // bis zu 4 Abweichungen, neueste zuerst, zwei pro Zeile
    char d[4][24] = {};
    const int n = st.recentCount() < 4 ? st.recentCount() : 4;
    for (int i = 0; i < n; i++)
      fmtDev(st.recent(i).devCg, d[i], sizeof(d[i]));
    snprintf(a, sizeof(a), "%s%s%s", d[0], n > 1 ? "  " : "", d[1]);
    snprintf(line, sizeof(line), "%s%s%s", d[2], n > 3 ? "  " : "", d[3]);
    setText(f, "Letzte Runden", a, n > 2 ? line : nullptr);
    break;
  }
  case StatsScreen::None:
    break;
  }
}

static void buildGame(Frame &f, const game::View &v, const Status &s,
                      uint8_t alt, StatsScreen sc, uint32_t now) {
  char a[32], b[32];
  // Ergebnis-Wechsel: Wert, Zeit und ggf. Erfolg
  const bool ach = showsAchievement(v, s);
  const uint8_t k = (uint8_t)(alt % (ach ? 3 : 2));
  const bool altTime = k == 1;
  if (ach && k == 2) {
    buildAchievement(f, s);
    return;
  }
  switch (v.screen) {
  case game::Screen::IdleGame:
    if (sc != StatsScreen::None && s.stats) {
      buildStats(f, sc, *s.stats); // ohne Statusleiste
      break;
    }
    text::fmtGrams1(v.goal, a, sizeof(a));
    strncat(a, "g?", sizeof(a) - strlen(a) - 1);
    setText(f, a);
    setIcons(f, s);
    f.border = v.glassOn;
    f.shuffle = v.randomMode;
    break;

  case game::Screen::IdleStandard:
    text::fmtGrams1(v.weight, a, sizeof(a));
    strncat(a, "g", sizeof(a) - strlen(a) - 1);
    setText(f, a);
    setIcons(f, s);
    break;

  case game::Screen::Taring:
    // Kein eigener Hinweis: wie Idle, nur ohne veraltetes Gewicht/Glas
    if (cfg::playsGame(s.mode)) {
      text::fmtGrams1(v.goal, a, sizeof(a));
      strncat(a, "g?", sizeof(a) - strlen(a) - 1);
      f.shuffle = v.randomMode;
    } else {
      text::fmtGrams1(0.0f, a, sizeof(a));
      strncat(a, "g", sizeof(a) - strlen(a) - 1);
    }
    setText(f, a);
    setIcons(f, s);
    break;

  case game::Screen::WaitDuel:
    snprintf(b, sizeof(b), "%d/%d bereit", v.ready, v.readyTotal);
    setText(f, "Warte...", b);
    break;

  case game::Screen::Ready:
    if ((uint32_t)(now - v.screenSince) < READY_PROMPT_MS)
      setText(f, "Bereit?");
    else
      setText(f, text::trinkspruch(v.toastIdx));
    break;

  case game::Screen::DuelStart:
    text::fmtGrams1(v.goal, a, sizeof(a));
    strncat(a, "g", sizeof(a) - strlen(a) - 1);
    setText(f, "Ziel", a);
    break;

  case game::Screen::Drinking:
    f.kind = Kind::Anim;
    f.animFrame = (uint8_t)(((uint32_t)(now - v.screenSince) / ANIM_MS) % 5);
    break;

  case game::Screen::ResultSolo: {
    char grams[24], dur[24];
    fmtHundredths(v.drankCg, "g", grams, sizeof(grams));
    fmtHundredths((int32_t)((v.durationMs + 5) / 10), "s", dur, sizeof(dur));
    setText(f, altTime ? dur : grams, ratingText(v.rating));
    break;
  }

  case game::Screen::ResultDuel: {
    char grams[24], dur[24];
    fmtHundredths(v.drankCg, "g", grams, sizeof(grams));
    fmtHundredths((int32_t)((v.durationMs + 5) / 10), "s", dur, sizeof(dur));
    if (v.forfeit) {
      setText(f, grams, "Zu spät!");
    } else if (v.rank == 0) {
      setText(f, grams, "Auswertung");
    } else if (!v.isFinal) {
      if (!altTime) {
        snprintf(b, sizeof(b), "~%u. Platz", (unsigned)v.rank);
        setText(f, grams, b);
      } else {
        snprintf(a, sizeof(a), "%u/%u fertig", (unsigned)v.settled,
                 (unsigned)v.total);
        setText(f, a, dur);
      }
    } else {
      snprintf(b, sizeof(b), "%u. Platz!", (unsigned)v.rank);
      setText(f, grams, altTime ? dur : b);
    }
    break;
  }
  }
}

// Info-Rotation: nach STATS_AFTER_MS ohne Glas im STATS_STEP_MS-Takt durch Ziel
// und Statistik. Neustart bei Tara/Reset (neuer Bildschirm), Glas oder Taster.
StatsScreen Model::statsScreen(const game::View &v, const Status &s, bool hold,
                               uint32_t now) {
  const bool idle = s.statsRotation && s.stats &&
                    v.screen == game::Screen::IdleGame && !v.glassOn && !hold;
  if (!idle) {
    rotOn_ = false;
    return StatsScreen::None;
  }
  if (!rotOn_ || v.screenSince != rotScreenSince_) {
    rotOn_ = true;
    rotSince_ = now;
    rotScreenSince_ = v.screenSince;
  }
  const uint32_t idleMs = now - rotSince_;
  if (idleMs < STATS_AFTER_MS)
    return StatsScreen::None;
  StatsScreen list[] = {StatsScreen::None,    StatsScreen::Best,
                        StatsScreen::Fastest, StatsScreen::Rounds,
                        StatsScreen::Levels,  StatsScreen::Recent};
  const uint32_t n = s.stats->recentCount() > 0 ? 6 : 5;
  return list[((idleMs - STATS_AFTER_MS) / STATS_STEP_MS) % n];
}

Frame Model::build(const game::View &v, const Status &s, const Hold &h,
                   const char *const system[3], uint32_t now) {
  Frame f;
  memset(&f, 0, sizeof(f));
  f.kind = Kind::Text;

  // Stiller Wechsel von Duell auf Solo → kurzer Hinweis
  if (!seqInit_) {
    seqInit_ = true;
    lastSoloSeq_ = v.soloFallbackSeq;
  } else if (v.soloFallbackSeq != lastSoloSeq_) {
    lastSoloSeq_ = v.soloFallbackSeq;
    toast("Solo!", now);
  }

  // Ergebnis-Wechsel (Wert/Zeit) neu starten bei neuem Bildschirm oder neuem
  // Duell-Stand
  bool isResult = v.screen == game::Screen::ResultSolo ||
                  v.screen == game::Screen::ResultDuel;
  if (v.screen != lastScreen_ || v.screenSince != lastScreenSince_ ||
      (v.screen == game::Screen::ResultDuel && v.resultSig != lastSig_)) {
    lastScreen_ = v.screen;
    lastScreenSince_ = v.screenSince;
    lastSig_ = v.resultSig;
    alt_ = 0;
    altSince_ = now;
  } else if (isResult && (uint32_t)(now - altSince_) >= RESULT_ALT_MS) {
    alt_ = (uint8_t)((alt_ + 1) % 6); // 6 = Vielfaches von 2 und 3
    altSince_ = now;
  }
  const StatsScreen sc = statsScreen(v, s, h.active, now);

  if (h.active) {
    f.kind = Kind::Hold;
    if (h.zone == button::Zone::Mode)
      setModeText(f, holdLabel(h.zone, s.mode, s.apOn),
                  (uint8_t)(1 + cfg::modePosition(cfg::nextMode(s.mode))));
    else
      setText(f, holdLabel(h.zone, s.mode, s.apOn));
    uint32_t held = h.heldMs > button::CANCEL_MS ? button::CANCEL_MS : h.heldMs;
    f.barPx = (uint8_t)((uint64_t)held * BAR_W / button::CANCEL_MS);
    return f;
  }
  if (system) {
    setText(f, system[0], system[1], system[2]);
    return f;
  }
  if (toastOn_) {
    if ((int32_t)(now - toastUntil_) < 0) {
      if (toastDots_)
        setModeText(f, toast_, toastDots_);
      else
        setText(f, toast_);
      return f;
    }
    toastOn_ = false;
  }
  buildGame(f, v, s, alt_, sc, now);
  return f;
}

} // namespace ui
