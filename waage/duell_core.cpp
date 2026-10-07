#include "duell_core.h"
#include <cmath>
#include <string.h>

namespace duell {

// ── Wire-Format ───────────────────────────────────────────────────────────────

namespace {

struct __attribute__((packed)) WireHeader {
  uint8_t magic;
  uint8_t phase;
  float goal;
  uint16_t roundId;
  uint16_t elapsedDs;
  float target;
  uint8_t n;
};
static_assert(sizeof(WireHeader) == HEADER_SIZE, "WireHeader wire size");

struct __attribute__((packed)) WireEntry {
  uint8_t mac[6];
  uint8_t status;
  float result;
  uint16_t durationCs;
};
static_assert(sizeof(WireEntry) == ENTRY_SIZE, "WireEntry wire size");

bool macEq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

} // namespace

size_t encode(const Message &m, uint8_t *buf, size_t cap) {
  uint8_t n = m.round.id ? m.round.n : 0;
  if (n > MAX_PLAYERS)
    return 0;
  size_t len = HEADER_SIZE + n * ENTRY_SIZE;
  if (cap < len)
    return 0;

  WireHeader h;
  h.magic = MAGIC;
  h.phase = (uint8_t)m.phase;
  h.goal = m.goal;
  h.roundId = m.round.id;
  uint32_t ds = m.elapsedMs / 100;
  h.elapsedDs = ds > 0xFFFF ? 0xFFFF : (uint16_t)ds;
  h.target = m.round.id ? m.round.target : 0.0f;
  h.n = n;
  memcpy(buf, &h, HEADER_SIZE);

  for (int i = 0; i < n; i++) {
    const Entry &e = m.round.e[i];
    WireEntry w;
    memcpy(w.mac, e.mac, 6);
    w.status = (uint8_t)e.status;
    w.result = e.result;
    w.durationCs = e.durationCs;
    memcpy(buf + HEADER_SIZE + i * ENTRY_SIZE, &w, ENTRY_SIZE);
  }
  return len;
}

bool decode(const uint8_t *data, size_t len, Message &out) {
  if (len < HEADER_SIZE)
    return false;
  WireHeader h;
  memcpy(&h, data, HEADER_SIZE);
  if (h.magic != MAGIC)
    return false;
  if (h.phase > (uint8_t)Phase::InRound)
    return false;
  if (h.n > MAX_PLAYERS)
    return false;
  if (len != HEADER_SIZE + h.n * ENTRY_SIZE)
    return false;
  if ((h.roundId == 0) != (h.n == 0))
    return false;
  float goal = h.goal;
  float target = h.target;
  if (!std::isfinite(goal) || !std::isfinite(target))
    return false;

  out = {};
  out.phase = (Phase)h.phase;
  out.goal = goal;
  out.elapsedMs = (uint32_t)h.elapsedDs * 100UL;
  out.round.id = h.roundId;
  out.round.target = target;
  out.round.n = h.n;

  for (int i = 0; i < h.n; i++) {
    WireEntry w;
    memcpy(&w, data + HEADER_SIZE + i * ENTRY_SIZE, ENTRY_SIZE);
    if (w.status > (uint8_t)Status::Done)
      return false;
    float result = w.result;
    if (w.status == (uint8_t)Status::Done && !std::isfinite(result))
      return false;
    Entry &e = out.round.e[i];
    memcpy(e.mac, w.mac, 6);
    e.status = (Status)w.status;
    e.result = result;
    e.durationCs = w.durationCs;
    for (int j = 0; j < i; j++) {
      if (macEq(out.round.e[j].mac, e.mac))
        return false;
    }
  }
  return true;
}

// ── Tabellen-Logik ────────────────────────────────────────────────────────────

int findEntry(const Round &r, const uint8_t mac[6]) {
  for (int i = 0; i < r.n; i++) {
    if (macEq(r.e[i].mac, mac))
      return i;
  }
  return -1;
}

bool mergeRound(Round &local, const Round &incoming, int skipIdx) {
  if (local.id == 0 || local.id != incoming.id)
    return false;
  bool changed = false;
  for (int k = 0; k < incoming.n; k++) {
    const Entry &in = incoming.e[k];
    int i = findEntry(local, in.mac);
    if (i < 0 || i == skipIdx)
      continue;
    Entry &l = local.e[i];
    if (l.status == Status::Done)
      continue;
    if (in.status == Status::Done) {
      l.status = Status::Done;
      l.result = in.result;
      l.durationCs = in.durationCs;
      changed = true;
    } else if (in.status == Status::Forfeit && l.status == Status::Pending) {
      l.status = Status::Forfeit;
      changed = true;
    }
  }
  return changed;
}

static int32_t diffCg(const Round &r, int i) {
  float d = std::fabs(r.e[i].result - r.target) * 100.0f;
  if (d > 1e9f)
    d = 1e9f;
  return (int32_t)std::lround(d);
}

void computeRanks(const Round &r, uint8_t ranks[MAX_PLAYERS]) {
  int32_t diff[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++)
    ranks[i] = 0;
  for (int i = 0; i < r.n; i++) {
    if (r.e[i].status == Status::Done)
      diff[i] = diffCg(r, i);
  }
  for (int i = 0; i < r.n; i++) {
    if (r.e[i].status != Status::Done)
      continue;
    int rank = 1;
    for (int j = 0; j < r.n; j++) {
      if (j == i || r.e[j].status != Status::Done)
        continue;
      if (diff[j] < diff[i] ||
          (diff[j] == diff[i] && r.e[j].durationCs < r.e[i].durationCs))
        rank++;
    }
    ranks[i] = (uint8_t)rank;
  }
}

// ── Core ──────────────────────────────────────────────────────────────────────

void Core::begin(const uint8_t mac[6], uint32_t now, SendFn send, RandFn rnd,
                 void *ctx) {
  *this = Core();
  memcpy(myMac_, mac, 6);
  beganAt_ = now;
  send_ = send;
  rand_ = rnd;
  ctx_ = ctx;
  dirty_ = true; // sofort melden
}

int Core::findPeer(const uint8_t mac[6]) const {
  for (int i = 0; i < nPeers_; i++) {
    if (macEq(peers_[i].mac, mac))
      return i;
  }
  return -1;
}

int Core::findOrAddPeer(const uint8_t mac[6]) {
  int i = findPeer(mac);
  if (i >= 0)
    return i;
  if (nPeers_ >= MAX_PEERS)
    return -1;
  i = nPeers_++;
  peers_[i] = {};
  memcpy(peers_[i].mac, mac, 6);
  return i;
}

bool Core::recentlyLeft(uint16_t id) const {
  for (int i = 0; i < RECENT_ROUNDS; i++) {
    if (recent_[i] == id)
      return true;
  }
  return false;
}

void Core::onReceive(const uint8_t mac[6], const uint8_t *data, size_t len,
                     uint32_t now) {
  if (macEq(mac, myMac_))
    return;
  Message m;
  if (!decode(data, len, m))
    return;

  int p = findOrAddPeer(mac);
  if (p >= 0) {
    peers_[p].lastSeen = now;
    peers_[p].phase = m.phase;
    peers_[p].roundId = m.round.id;
    peers_[p].goal = m.goal;
  }

  if (phase_ == Phase::InRound) {
    int idx = findEntry(cur_, mac);
    if (idx >= 0)
      lastHeard_[idx] = now;
    if (m.round.id == cur_.id) {
      if (mergeRound(cur_, m.round, myIdx_))
        dirty_ = true;
    } else if (idx >= 0 && cur_.e[idx].status == Status::Pending) {
      // Teilnehmer ist nachweislich nicht in meiner Runde: spielt eine andere,
      // ist nie beigetreten bzw. ausgestiegen, oder hat das Beitrittsfenster
      // verpasst und wartet noch auf eine neue Runde.
      bool otherRound = m.phase == Phase::InRound;
      bool gaveUp = m.phase == Phase::Idle;
      bool missedJoin = m.phase == Phase::Ready &&
                        now - curStartedAt_ > JOIN_WINDOW_MS + PEER_ACTIVE_MS;
      if (otherRound || gaveUp || missedJoin) {
        cur_.e[idx].status = Status::Forfeit;
        dirty_ = true;
      }
    }
  }

  if (linger_.id != 0 && m.round.id == linger_.id) {
    mergeRound(linger_, m.round, findEntry(linger_, myMac_));
  }

  // Beitritt: jede Nachricht eines Teilnehmers reicht, nicht nur die des
  // Leaders
  if (phase_ == Phase::Ready && m.phase == Phase::InRound && m.round.id != 0 &&
      m.elapsedMs < JOIN_WINDOW_MS && !recentlyLeft(m.round.id)) {
    int me = findEntry(m.round, myMac_);
    if (me >= 0 && m.round.e[me].status == Status::Pending)
      join(m, me, now);
  }
}

void Core::join(const Message &m, int me, uint32_t now) {
  cur_ = m.round;
  myIdx_ = me;
  curStartedAt_ = now - m.elapsedMs;
  for (int i = 0; i < MAX_PLAYERS; i++)
    lastHeard_[i] = now;
  phase_ = Phase::InRound;
  graceRunning_ = false;
  dirty_ = true; // sofort quittieren
}

void Core::checkForfeits(uint32_t now) {
  bool timeout = now - curStartedAt_ > ROUND_MAX_MS;
  for (int i = 0; i < cur_.n; i++) {
    if (cur_.e[i].status != Status::Pending)
      continue;
    if (timeout ||
        (i != myIdx_ && now - lastHeard_[i] > INVISIBLE_FORFEIT_MS)) {
      cur_.e[i].status = Status::Forfeit;
      dirty_ = true;
    }
  }
}

void Core::maybeStartRound(uint32_t now) {
  if (phase_ != Phase::Ready || now - beganAt_ < STARTUP_GUARD_MS) {
    graceRunning_ = false;
    return;
  }

  // Leader = hoechste MAC unter allen aktiven Waagen; alle muessen bereit sein
  int active = 0;
  bool allReady = true;
  bool highest = true;
  for (int i = 0; i < nPeers_; i++) {
    if (now - peers_[i].lastSeen >= PEER_ACTIVE_MS)
      continue;
    active++;
    if (peers_[i].phase != Phase::Ready)
      allReady = false;
    if (memcmp(peers_[i].mac, myMac_, 6) > 0)
      highest = false;
  }
  if (!highest || !allReady || active == 0) {
    graceRunning_ = false;
    return;
  }
  // Karenz neu starten, wenn eine Waage dazukommt oder wegfaellt
  if (!graceRunning_ || graceActive_ != active) {
    graceRunning_ = true;
    graceSince_ = now;
    graceActive_ = active;
    return;
  }
  if (now - graceSince_ < READY_GRACE_MS)
    return;
  startRound(now);
}

void Core::startRound(uint32_t now) {
  Round r = {};
  float goals[MAX_PLAYERS];

  memcpy(r.e[0].mac, myMac_, 6);
  goals[0] = goal_;
  r.n = 1;
  for (int i = 0; i < nPeers_ && r.n < MAX_PLAYERS; i++) {
    if (now - peers_[i].lastSeen >= PEER_ACTIVE_MS ||
        peers_[i].phase != Phase::Ready)
      continue;
    memcpy(r.e[r.n].mac, peers_[i].mac, 6);
    goals[r.n] = peers_[i].goal;
    r.n++;
  }

  // Nach MAC sortieren: gleiche Reihenfolge auf allen Waagen
  for (int i = 1; i < r.n; i++) {
    for (int j = i; j > 0 && memcmp(r.e[j - 1].mac, r.e[j].mac, 6) > 0; j--) {
      Entry te = r.e[j];
      r.e[j] = r.e[j - 1];
      r.e[j - 1] = te;
      float tg = goals[j];
      goals[j] = goals[j - 1];
      goals[j - 1] = tg;
    }
  }
  for (int i = 0; i < r.n; i++)
    r.e[i].status = Status::Pending;

  r.target = goals[rand_(ctx_, 0, r.n)];
  uint16_t id;
  do {
    id = (uint16_t)rand_(ctx_, 1, 65536);
  } while (id == 0 || id == linger_.id || recentlyLeft(id));
  r.id = id;

  cur_ = r;
  myIdx_ = findEntry(cur_, myMac_);
  curStartedAt_ = now;
  for (int i = 0; i < MAX_PLAYERS; i++)
    lastHeard_[i] = now;
  phase_ = Phase::InRound;
  graceRunning_ = false;
  dirty_ = true;
}

void Core::send(uint32_t now) {
  Message m = {};
  m.phase = phase_;
  m.goal = goal_;
  if (phase_ == Phase::InRound) {
    m.round = cur_;
    m.elapsedMs = now - curStartedAt_;
  } else if (linger_.id != 0) {
    m.round = linger_;
    m.elapsedMs = now - lingerStartedAt_;
  }
  uint8_t buf[MAX_MSG_SIZE];
  size_t len = encode(m, buf, sizeof(buf));
  if (len > 0 && send_)
    send_(ctx_, buf, len);
  lastTx_ = now;
  dirty_ = false;
}

void Core::tick(uint32_t now, float localGoal) {
  goal_ = localGoal;

  // Inaktive Peers vergessen
  for (int i = 0; i < nPeers_; i++) {
    if (now - peers_[i].lastSeen > PEER_FORGET_MS) {
      peers_[i] = peers_[nPeers_ - 1];
      nPeers_--;
      i--;
    }
  }

  if (phase_ == Phase::InRound)
    checkForfeits(now);
  if (linger_.id != 0 && now - lingerSince_ >= LINGER_MS)
    linger_ = {};

  maybeStartRound(now);

  uint32_t interval = (phase_ != Phase::Idle || linger_.id != 0)
                          ? HEARTBEAT_ACTIVE_MS
                          : HEARTBEAT_IDLE_MS;
  if (dirty_ || now - lastTx_ >= interval)
    send(now);
}

void Core::setReady() {
  if (phase_ != Phase::Idle)
    return;
  phase_ = Phase::Ready;
  dirty_ = true;
}

void Core::submitResult(float grams, uint32_t durationMs) {
  if (phase_ != Phase::InRound || myIdx_ < 0)
    return;
  Entry &e = cur_.e[myIdx_];
  if (e.status != Status::Pending)
    return;
  uint32_t cs = durationMs / 10;
  e.status = Status::Done;
  e.result = grams;
  e.durationCs = cs > 0xFFFF ? 0xFFFF : (uint16_t)cs;
  dirty_ = true;
}

void Core::leave(uint32_t now) {
  if (phase_ == Phase::InRound) {
    if (myIdx_ >= 0 && cur_.e[myIdx_].status == Status::Pending)
      cur_.e[myIdx_].status = Status::Forfeit;
    linger_ = cur_;
    lingerStartedAt_ = curStartedAt_;
    lingerSince_ = now;
    recent_[recentIdx_] = cur_.id;
    recentIdx_ = (recentIdx_ + 1) % RECENT_ROUNDS;
    cur_ = {};
    myIdx_ = -1;
  }
  phase_ = Phase::Idle;
  graceRunning_ = false;
  dirty_ = true;
}

int Core::activePeers(uint32_t now) const {
  int n = 0;
  for (int i = 0; i < nPeers_; i++) {
    if (now - peers_[i].lastSeen < PEER_ACTIVE_MS)
      n++;
  }
  return n;
}

int Core::readyCount(uint32_t now) const {
  int n = phase_ == Phase::Ready ? 1 : 0;
  for (int i = 0; i < nPeers_; i++) {
    if (now - peers_[i].lastSeen < PEER_ACTIVE_MS &&
        peers_[i].phase == Phase::Ready)
      n++;
  }
  return n;
}

bool Core::startSignal(float *target) const {
  if (phase_ != Phase::InRound)
    return false;
  *target = cur_.target;
  return true;
}

View Core::view() const {
  View v = {};
  if (phase_ != Phase::InRound || myIdx_ < 0)
    return v;
  uint8_t ranks[MAX_PLAYERS];
  computeRanks(cur_, ranks);
  v.inRound = true;
  v.total = cur_.n;
  for (int i = 0; i < cur_.n; i++) {
    if (cur_.e[i].status != Status::Pending)
      v.settled++;
  }
  v.isFinal = v.settled == v.total;
  v.myStatus = cur_.e[myIdx_].status;
  v.rank = ranks[myIdx_];
  v.target = cur_.target;
  return v;
}

bool Core::busy(uint32_t now) const {
  // Nachlauf auch ohne tick() pruefen (Standard-Mode ruft duell_update nicht
  // auf)
  return phase_ == Phase::InRound ||
         (linger_.id != 0 && now - lingerSince_ < LINGER_MS);
}

} // namespace duell
