#include "stats_core.h"

namespace stats {

void Tracker::load(const Totals &t) {
  reset();
  t_ = t;
}

void Tracker::reset() { *this = Tracker(); }

Achievement Tracker::record(const Round &r) {
  const int32_t dev = r.drankCg - r.goalCg;
  const int32_t ad = dev < 0 ? -dev : dev;

  t_.rounds++;
  if (ad == 0)
    t_.perfect++;
  else if (ad <= NOT_BAD_CG)
    t_.notBad++;
  else if (ad <= OK_CG)
    t_.ok++;

  Achievement a = Achievement::None;
  if (!t_.hasBest || ad < t_.bestDevCg) {
    t_.hasBest = true;
    t_.bestDevCg = ad;
    t_.bestGoalCg = r.goalCg;
    t_.bestMs = r.durationMs;
    a = Achievement::Record;
  }
  const bool fastOk =
      r.goalCg > 0 && (int64_t)ad * 100 <= (int64_t)FAST_PCT * r.goalCg;
  if (fastOk && (!t_.hasFastest || r.durationMs < t_.fastestMs)) {
    t_.hasFastest = true;
    t_.fastestMs = r.durationMs;
    t_.fastestGoalCg = r.goalCg;
    t_.fastestDevCg = dev;
    if (a == Achievement::None)
      a = Achievement::Fastest;
  }

  recent_[head_] = {dev, r.goalCg, r.durationMs, r.duel, 0};
  head_ = (head_ + 1) % RECENT;
  if (count_ < RECENT)
    count_++;
  return a;
}

void Tracker::duelFinal(uint8_t rank, uint8_t total, bool forfeit) {
  if (total < 2)
    return;
  t_.duels++;
  if (rank == 1 && !forfeit)
    t_.wins++;
  if (count_ > 0) {
    Entry &e = recent_[(head_ + RECENT - 1) % RECENT];
    if (e.duel && e.rank == 0 && !forfeit)
      e.rank = rank;
  }
}

const Entry &Tracker::recent(int i) const {
  return recent_[(head_ + RECENT - 1 - i) % RECENT];
}

} // namespace stats
