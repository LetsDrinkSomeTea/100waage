#include "game_core.h"
#include <math.h>

namespace game {

Rating rate(int32_t drankCg, int32_t goalCg) {
  int32_t d = drankCg - goalCg;
  int32_t ad = d < 0 ? -d : d;
  if (d == 0)
    return Rating::Perfect;
  if (ad <= 10)
    return Rating::NotBad;
  if (ad <= 100)
    return Rating::Ok;
  return d < 0 ? Rating::Shy : Rating::Greedy;
}

bool isGood(int32_t drankCg, int32_t goalCg, uint8_t autoResetRange) {
  if (goalCg <= 0)
    return false;
  int64_t d = (int64_t)drankCg - goalCg;
  if (d < 0)
    d = -d;
  return d * 100 <= (int64_t)autoResetRange * goalCg;
}

// ── Allgemein ─────────────────────────────────────────────────────────────────

void Game::begin(DuelPort *duel, uint32_t (*rnd)(void *), void *rndCtx) {
  port_ = duel;
  rnd_ = rnd;
  rndCtx_ = rndCtx;
  view_ = View();
}

void Game::setScreen(Screen s, uint32_t now) {
  if (view_.screen != s) {
    view_.screen = s;
    view_.screenSince = now;
  }
}

void Game::stopTimers() {
  place_.stop();
  lift_.stop();
  ret_.stop();
  removed_.stop();
  autoZeroStable_.stop();
  negZero_.stop();
}

void Game::reset(const cfg::Config &c, uint32_t now, ScaleReq tare) {
  if (port_)
    port_->leave();
  phase_ = Phase::Idle;
  duel_ = Duel::Offline;
  stopTimers();
  final_.stop();
  waitReady_.stop();
  fullWeight_ = emptyWeight_ = finalWeight_ = 0.0f;
  duelTargetSet_ = false;
  haveCached_ = false;
  cachedView_ = duell::View();
  localGoal_ = c.randomModeEnabled ? cfg::rollGoal(c, nextRandom()) : c.goal;
  // Nach einer Tara erst nach der Auto-Zero-Pause erneut nullen
  autoZeroLast_ = now;
  autoZeroDone_ = true;
  view_.rank = view_.settled = view_.total = 0;
  view_.isFinal = view_.forfeit = false;
  view_.drankCg = 0;
  view_.durationMs = 0;
  view_.screen = c.scaleMode == cfg::ScaleMode::Game ? Screen::IdleGame
                                                     : Screen::IdleStandard;
  view_.screenSince = now;
  req_ = tare;
}

void Game::applyGoalSettings(const cfg::Config &c, uint32_t now) {
  (void)now;
  if (c.scaleMode != cfg::ScaleMode::Game || phase_ != Phase::Idle)
    return;
  localGoal_ = c.randomModeEnabled ? cfg::rollGoal(c, nextRandom()) : c.goal;
  place_.stop();
}

ScaleReq Game::takeScaleReq() {
  ScaleReq r = req_;
  req_ = ScaleReq::None;
  return r;
}

bool Game::ownRoundOpen() const {
  if (!port_)
    return false;
  duell::View v = port_->view();
  return v.inRound && !v.isFinal;
}

void Game::update(const cfg::Config &c, const Input &in) {
  view_.randomMode = c.randomModeEnabled;
  view_.soloFallbackSeq = soloSeq_;

  if (!in.weightValid) {
    // Tara laeuft oder Sensorfehler: nichts aendert sich
    stopTimers();
    if (phase_ == Phase::Idle)
      setScreen(Screen::Taring, in.now);
    return;
  }
  if (c.scaleMode == cfg::ScaleMode::Standard) {
    updateStandard(c, in);
    return;
  }
  switch (phase_) {
  case Phase::Idle:
    updateIdle(c, in);
    break;
  case Phase::Ready:
    updateReady(c, in);
    break;
  case Phase::Drinking:
    updateDrinking(c, in);
    break;
  case Phase::Result:
    updateResult(c, in);
    break;
  }
  view_.soloFallbackSeq = soloSeq_;
}

// ── Idle ──────────────────────────────────────────────────────────────────────

void Game::updateAutoZero(const cfg::Config &c, const Input &in) {
  if (!c.autoZeroEnabled) {
    autoZeroStable_.stop();
    return;
  }
  uint32_t delayMs = (uint32_t)c.autoZeroDelay * 1000UL;
  if (autoZeroDone_ && (uint32_t)(in.now - autoZeroLast_) < 3 * delayMs) {
    autoZeroStable_.stop(); // Pause nach dem letzten Nullen
    return;
  }
  if (fabsf(in.weight) < c.autoZeroThreshold && in.stable)
    autoZeroStable_.start(in.now);
  else
    autoZeroStable_.stop();
  if (autoZeroStable_.held(in.now, delayMs)) {
    request(ScaleReq::AutoZero);
    autoZeroStable_.stop();
    autoZeroLast_ = in.now;
    autoZeroDone_ = true;
  }
}

void Game::updateStandard(const cfg::Config &c, const Input &in) {
  phase_ = Phase::Idle;
  duel_ = Duel::Offline;
  setScreen(Screen::IdleStandard, in.now);
  updateAutoZero(c, in);
  bool zeroShown = c.autoZeroEnabled && fabsf(in.weight) < c.autoZeroThreshold;
  view_.weight = zeroShown ? 0.0f : in.weight;
}

void Game::updateIdle(const cfg::Config &c, const Input &in) {
  const uint32_t now = in.now;
  const float w = in.weight;
  setScreen(Screen::IdleGame, now);
  view_.goal = localGoal_;
  view_.glassOn = w > c.tolerance;

  updateAutoZero(c, in);

  // NegZero: mit Glas tariert und Glas abgehoben → leere Waage nullen
  if (w < -c.tolerance && in.stable)
    negZero_.start(now);
  else
    negZero_.stop();
  if (negZero_.held(now, NEGZERO_MS)) {
    request(ScaleReq::NegZero);
    negZero_.stop();
  }

  // Volles Glas steht stabil → Bereit
  if (w >= localGoal_ && in.stable)
    place_.start(now);
  else
    place_.stop();
  if (!place_.held(now, PLACE_STABLE_MS))
    return;

  place_.stop();
  negZero_.stop();
  fullWeight_ = w;
  phase_ = Phase::Ready;
  view_.toastIdx = (uint16_t)(nextRandom() & 0xFFFF);
  if (in.radioOn && port_ && port_->active()) {
    duel_ = Duel::WaitReady;
    waitReady_.on = true;
    waitReady_.since = now;
    port_->setReady();
    port_->readyCount(&view_.ready, &view_.readyTotal);
    setScreen(Screen::WaitDuel, now);
  } else {
    duel_ = Duel::Offline;
    setScreen(Screen::Ready, now);
  }
}

// ── Ready ─────────────────────────────────────────────────────────────────────

void Game::updateReady(const cfg::Config &c, const Input &in) {
  const uint32_t now = in.now;
  const bool below = in.weight <= fullWeight_ - c.tolerance;

  if (duel_ == Duel::WaitReady) {
    float target;
    if (port_->startSignal(&target)) {
      duelTarget_ = target;
      duelTargetSet_ = true;
      duel_ = Duel::WaitStart;
      waitReady_.stop();
      view_.goal = target;
      setScreen(Screen::DuelStart, now);
      // Laufendes Anheben zaehlt weiter (lift_ bleibt erhalten)
    } else if (!port_->active() ||
               (uint32_t)(now - waitReady_.since) >= WAITREADY_TIMEOUT_MS) {
      // Gegner weg oder Timeout → solo weiterspielen
      duel_ = Duel::Offline;
      waitReady_.stop();
      port_->leave();
      soloSeq_++;
      setScreen(Screen::Ready, now);
    } else {
      port_->readyCount(&view_.ready, &view_.readyTotal);
      // Glas lange genug abgehoben → solo, Trinken laeuft schon
      if (below)
        lift_.start(now);
      else
        lift_.stop();
      if (lift_.held(now, LIFT_MS)) {
        duel_ = Duel::Offline;
        waitReady_.stop();
        port_->leave();
        soloSeq_++;
        startDrinking(in, lift_.since);
      }
      return;
    }
  } else if (duel_ == Duel::WaitStart && !port_->view().inRound) {
    // Runde verschwunden (Funk aus): solo gegen das Duell-Ziel
    duel_ = Duel::Offline;
    soloSeq_++;
  }

  if (below)
    lift_.start(now);
  else
    lift_.stop();
  if (lift_.held(now, LIFT_MS))
    startDrinking(in, lift_.since);
}

// ── Drinking ──────────────────────────────────────────────────────────────────

void Game::startDrinking(const Input &in, uint32_t since) {
  phase_ = Phase::Drinking;
  timeStarted_ = since;
  emptyWeight_ = in.weight;
  lift_.stop();
  ret_.stop();
  setScreen(Screen::Drinking, in.now);
}

void Game::updateDrinking(const cfg::Config &c, const Input &in) {
  const uint32_t now = in.now;
  if (in.weight >= emptyWeight_ + c.tolerance) {
    if (!ret_.on) {
      ret_.start(now);
      timeEnd_ = now;
    }
    if ((ret_.held(now, RETURN_STABLE_MS) && in.stable) ||
        ret_.held(now, RETURN_MAX_MS))
      finishDrinking(in);
    return;
  }
  ret_.stop();
  // Leere Waage nachfuehren, solange das Glas weg ist
  if (in.stable)
    emptyWeight_ = in.weight;
}

void Game::finishDrinking(const Input &in) {
  finalWeight_ = in.weight;
  view_.drankCg = toCg(fullWeight_ - finalWeight_);
  view_.durationMs = timeEnd_ - timeStarted_;
  phase_ = Phase::Result;
  ret_.stop();
  removed_.stop();
  final_.stop();
  view_.rank = view_.settled = view_.total = 0;
  view_.isFinal = view_.forfeit = false;

  if (duel_ != Duel::Offline) {
    if (port_->view().inRound) {
      port_->submit(view_.drankCg / 100.0f, view_.durationMs);
      duel_ = Duel::Live;
      setScreen(Screen::ResultDuel, in.now);
      return;
    }
    duel_ = Duel::Offline;
    soloSeq_++;
  }
  view_.rating = rate(view_.drankCg, toCg(refGoal()));
  setScreen(Screen::ResultSolo, in.now);
}

// ── Result ────────────────────────────────────────────────────────────────────

void Game::updateResult(const cfg::Config &c, const Input &in) {
  const uint32_t now = in.now;
  // Glas weg, relativ zur gemessenen leeren Waage (klappt auch nach Tara mit
  // Glas)
  if (in.weight < emptyWeight_ + c.tolerance)
    removed_.start(now);
  else
    removed_.stop();
  const bool removed = removed_.held(now, REMOVED_MS);

  if (duel_ == Duel::Live) {
    duell::View live = port_->view();
    duell::View v;
    if (live.inRound) {
      cachedView_ = live;
      haveCached_ = true;
      v = live;
    } else if (haveCached_ && cachedView_.isFinal) {
      v = cachedView_; // Funk nach dem Final aus: Rang bleibt
    } else {
      // Runde vor dem Final verloren → solo gegen das Duell-Ziel
      duel_ = Duel::Offline;
      soloSeq_++;
      view_.rating = rate(view_.drankCg, toCg(refGoal()));
      setScreen(Screen::ResultSolo, now);
      return;
    }
    const bool forfeit = v.myStatus == duell::Status::Forfeit;
    view_.rank = v.rank;
    view_.settled = v.settled;
    view_.total = v.total;
    view_.isFinal = v.isFinal;
    view_.forfeit = forfeit;
    view_.resultSig = (uint32_t)v.rank | ((uint32_t)v.settled << 8) |
                      ((uint32_t)v.total << 16) | ((uint32_t)v.isFinal << 24) |
                      ((uint32_t)forfeit << 25);
    if (v.isFinal)
      final_.start(now);
    else
      final_.stop();
    const bool bad =
        forfeit || !isGood(view_.drankCg, toCg(v.target), c.autoResetRange);
    if (bad && removed && final_.held(now, FINAL_MIN_SHOW_MS))
      reset(c, now, ScaleReq::TareEmpty);
    return;
  }

  // Solo: gutes Ergebnis bleibt zum Prahlen, schlechtes geht beim Abheben
  if (!isGood(view_.drankCg, toCg(refGoal()), c.autoResetRange) && removed) {
    reset(c, now, ScaleReq::TareEmpty);
  }
}

} // namespace game
