#include "stats.h"
#include <Preferences.h>

static constexpr char NS[] = "stats";
static constexpr char K_TOTALS[] = "t";
static constexpr char K_VERSION[] = "v";
// Bei Aenderungen an stats::Totals erhoehen; aeltere Versionen werden in
// stats_begin() uebernommen (neue Felder am Ende, mit 0 vorbelegt).
static constexpr uint8_t VERSION = 2;

// Version 1: stats::Totals ohne bestPct/bestGlass
struct TotalsV1 {
  uint32_t rounds;
  uint32_t perfect, notBad, ok;
  bool hasBest;
  int32_t bestDevCg;
  int32_t bestGoalCg;
  uint32_t bestMs;
  bool hasFastest;
  uint32_t fastestMs;
  int32_t fastestGoalCg;
  int32_t fastestDevCg;
  uint32_t duels, wins;
};

static stats::Tracker tracker;

static void save() {
  Preferences p;
  if (!p.begin(NS, false))
    return;
  const stats::Totals &t = tracker.totals();
  p.putBytes(K_TOTALS, &t, sizeof(t));
  p.putUChar(K_VERSION, VERSION);
  p.end();
}

void stats_begin() {
  Preferences p;
  if (!p.begin(NS, true)) // Namespace fehlt beim ersten Start
    return;
  stats::Totals t = {};
  const uint8_t v = p.getUChar(K_VERSION, 0);
  const size_t len = p.getBytesLength(K_TOTALS);
  TotalsV1 o = {};
  if (v == VERSION && len == sizeof(t) &&
      p.getBytes(K_TOTALS, &t, sizeof(t)) == sizeof(t)) {
    tracker.load(t);
  } else if (v == 1 && len == sizeof(o) &&
             p.getBytes(K_TOTALS, &o, sizeof(o)) == sizeof(o)) {
    t.rounds = o.rounds;
    t.perfect = o.perfect;
    t.notBad = o.notBad;
    t.ok = o.ok;
    t.hasBest = o.hasBest;
    t.bestDevCg = o.bestDevCg;
    t.bestGoalCg = o.bestGoalCg;
    t.bestMs = o.bestMs;
    t.hasFastest = o.hasFastest;
    t.fastestMs = o.fastestMs;
    t.fastestGoalCg = o.fastestGoalCg;
    t.fastestDevCg = o.fastestDevCg;
    t.duels = o.duels;
    t.wins = o.wins;
    tracker.load(t);
  }
  p.end();
}

const stats::Tracker &stats_tracker() { return tracker; }

stats::Achievement stats_record(const game::RoundDone &r) {
  stats::Achievement a = tracker.record(
      {r.drankCg, r.goalCg, r.durationMs, r.duel, r.goalPct, r.glass});
  save();
  return a;
}

void stats_duelFinal(const game::DuelFinal &f) {
  tracker.duelFinal(f.rank, f.total, f.forfeit);
  save();
}

void stats_reset() {
  tracker.reset();
  save();
}

static float cgToG(int32_t cg) { return (float)cg / 100.0f; }
static float msToS(uint32_t ms) { return (float)ms / 1000.0f; }

void stats_writeJson(web::JsonWriter &j) {
  const stats::Totals &t = tracker.totals();
  j.beginObject();
  j.key("rounds").uinteger(t.rounds);
  j.key("perfect").uinteger(t.perfect);
  j.key("notBad").uinteger(t.notBad);
  j.key("ok").uinteger(t.ok);
  j.key("duels").uinteger(t.duels);
  j.key("wins").uinteger(t.wins);
  j.key("best");
  if (t.hasBest) {
    j.beginObject();
    j.key("dev").num(cgToG(t.bestDevCg), 2);
    j.key("goal").num(cgToG(t.bestGoalCg), 1);
    j.key("time").num(msToS(t.bestMs), 2);
    j.key("pct").uinteger(t.bestPct);
    j.key("glass").str(t.bestPct ? t.bestGlass : nullptr);
    j.endObject();
  } else {
    j.null();
  }
  j.key("fastest");
  if (t.hasFastest) {
    j.beginObject();
    j.key("time").num(msToS(t.fastestMs), 2);
    j.key("goal").num(cgToG(t.fastestGoalCg), 1);
    j.key("dev").num(cgToG(t.fastestDevCg), 2);
    j.endObject();
  } else {
    j.null();
  }
  j.key("recent").beginArray();
  for (int i = 0; i < tracker.recentCount(); i++) {
    const stats::Entry &e = tracker.recent(i);
    j.beginObject();
    j.key("dev").num(cgToG(e.devCg), 2);
    j.key("goal").num(cgToG(e.goalCg), 1);
    j.key("time").num(msToS(e.durationMs), 2);
    j.key("duel").flag(e.duel);
    j.key("rank").uinteger(e.rank);
    j.key("pct").uinteger(e.pct);
    j.endObject();
  }
  j.endArray();
  j.endObject();
}
