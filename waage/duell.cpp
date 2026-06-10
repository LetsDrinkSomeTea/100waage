#include "duell.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>
#include <stddef.h>

// ── Protokoll ─────────────────────────────────────────────────────────────────
// Level-getriggertes Gossip statt Einmal-Nachrichten: Jede Waage meldet ihren
// Zustand periodisch per StateMsg, der Master wiederholt die Runden-Nachricht,
// solange die Runde laeuft. Verlorene Pakete heilen sich so von selbst.
// Bei inkompatiblen Aenderungen DUELL_MAGIC erhoehen — alte und neue Firmware
// ignorieren sich dann gegenseitig.

constexpr uint8_t DUELL_MAGIC = 0xD2;

enum class DuellMsgType : uint8_t { State = 1,
                                    Round = 2 };

struct __attribute__((packed)) DuellStateMsg {
  uint8_t magic;
  uint8_t type;
  uint8_t phase;     // DuellPhase
  uint16_t roundId;  // Runde, in der ich Teilnehmer bin (0 = keine)
  float goal;
  float result;  // getrunkene Gramm, gueltig ab Phase HasResult
};
static_assert(sizeof(DuellStateMsg) == 13, "DuellStateMsg wire size");

constexpr int MAX_PEERS = 10;

struct __attribute__((packed)) DuellRoundEntry {
  uint8_t mac[6];
  uint8_t rank;  // 0 = noch kein Rang
};
static_assert(sizeof(DuellRoundEntry) == 7, "DuellRoundEntry wire size");

struct __attribute__((packed)) DuellRoundMsg {
  uint8_t magic;
  uint8_t type;
  uint16_t roundId;  // vom Master pro Runde zufaellig gewaehlt, nie 0
  uint8_t finished;  // 1 = Raenge in players[] gueltig
  float target;
  uint8_t playerCount;
  DuellRoundEntry players[MAX_PEERS + 1];
};
static_assert(offsetof(DuellRoundMsg, players) == 10, "DuellRoundMsg header size");

// ── Timing ────────────────────────────────────────────────────────────────────
constexpr unsigned long HEARTBEAT_IDLE_MS = 2000UL;
constexpr unsigned long HEARTBEAT_ACTIVE_MS = 400UL;
constexpr unsigned long PEER_ACTIVE_MS = 5000UL;
constexpr unsigned long PEER_FORGET_MS = 10000UL;
constexpr unsigned long ROUND_TX_MS = 500UL;
constexpr unsigned long READY_GRACE_MS = 3000UL;
constexpr unsigned long RESULT_TIMEOUT_MS = 120000UL;   // Runde spaetestens dann auswerten
constexpr unsigned long FORFEIT_MS = 10000UL;          // Teilnehmer so lange unsichtbar = aufgegeben
constexpr unsigned long FINISHED_TX_MAX_MS = 15000UL;  // fertige Runde hoechstens so lange wiederholen

// ── RX-Queue ──────────────────────────────────────────────────────────────────
// Der ESP-NOW-Callback laeuft im WiFi-Task. Er legt Pakete nur in die Queue,
// verarbeitet wird im Main Loop (duell_update) — kein geteilter Zustand.

struct RxPacket {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[sizeof(DuellRoundMsg)];
};

static QueueHandle_t rxQueue = nullptr;
static bool initialized = false;

// ── Peers ─────────────────────────────────────────────────────────────────────
// Reiner Spiegel der zuletzt empfangenen StateMsg jedes Peers.

struct Peer {
  uint8_t mac[6];
  unsigned long lastSeen;
  DuellPhase phase;
  uint16_t roundId;
  float goal;
  float result;
};

static Peer peers[MAX_PEERS];
static int peerCount = 0;
static uint8_t myMac[6];

// ── Lokaler Spielzustand ──────────────────────────────────────────────────────

static DuellPhase myPhase = DuellPhase::Idle;
static uint16_t currentRoundId = 0;       // Runde, an der ich teilnehme
static uint16_t lastFinishedRoundId = 0;  // zuletzt von mir abgeschlossene Runde
static float myGoal = 0.0f;
static float myResultWeight = -1.0f;

static bool hasStart = false;
static float targetWeight = 0.0f;
static bool hasRank = false;
static int myRank = 0;

static unsigned long lastStateTx = 0;

// ── Master-Rundenzustand ──────────────────────────────────────────────────────
// Lebt unabhaengig vom lokalen Spielzustand: Auch wenn der Master selbst schon
// resettet hat, wird die fertige Runde weiter gesendet, bis alle Teilnehmer sie
// gesehen haben (oder FINISHED_TX_MAX_MS abgelaufen ist).

struct RoundPlayer {
  uint8_t mac[6];
  float result;
  bool hasResult;
  bool forfeit;
  bool acked;  // hat das fertige Ranking gesehen bzw. ist weitergezogen
  uint8_t rank;
};

struct Round {
  bool active;
  bool finished;
  uint16_t id;
  float target;
  uint8_t n;
  RoundPlayer p[MAX_PEERS + 1];
  unsigned long startedAt;
  unsigned long finishedAt;
  unsigned long lastTx;
};

static Round masterRound = {};
static unsigned long allReadySince = 0;

// ── Senden ────────────────────────────────────────────────────────────────────

static void sendRaw(const void *data, size_t len) {
  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_send(bcast, (const uint8_t *)data, len);
}

static void sendStateMsg() {
  if (!initialized) return;
  DuellStateMsg msg;
  msg.magic = DUELL_MAGIC;
  msg.type = (uint8_t)DuellMsgType::State;
  msg.phase = (uint8_t)myPhase;
  msg.roundId = currentRoundId;
  msg.goal = myGoal;
  msg.result = myResultWeight;
  sendRaw(&msg, sizeof(msg));
  lastStateTx = millis();
}

static void sendRoundMsg() {
  if (!initialized) return;
  DuellRoundMsg msg;
  msg.magic = DUELL_MAGIC;
  msg.type = (uint8_t)DuellMsgType::Round;
  msg.roundId = masterRound.id;
  msg.finished = masterRound.finished ? 1 : 0;
  msg.target = masterRound.target;
  msg.playerCount = masterRound.n;
  for (int i = 0; i < masterRound.n; i++) {
    memcpy(msg.players[i].mac, masterRound.p[i].mac, 6);
    msg.players[i].rank = masterRound.p[i].rank;
  }
  sendRaw(&msg, offsetof(DuellRoundMsg, players) + masterRound.n * sizeof(DuellRoundEntry));
  masterRound.lastTx = millis();
}

// ── Empfang ───────────────────────────────────────────────────────────────────

static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int data_len) {
  if (data_len < 2 || data_len > (int)sizeof(DuellRoundMsg)) return;
  if (data[0] != DUELL_MAGIC) return;
  if (!rxQueue) return;
  RxPacket pkt;
  memcpy(pkt.mac, info->src_addr, 6);
  pkt.len = (uint8_t)data_len;
  memcpy(pkt.data, data, data_len);
  xQueueSend(rxQueue, &pkt, 0);  // Queue voll: Paket verwerfen, Gossip wiederholt es
}

static int findPeer(const uint8_t *mac) {
  for (int i = 0; i < peerCount; i++) {
    if (memcmp(peers[i].mac, mac, 6) == 0) return i;
  }
  return -1;
}

static int findOrAddPeer(const uint8_t *mac) {
  int i = findPeer(mac);
  if (i >= 0) return i;
  if (peerCount >= MAX_PEERS) return -1;
  i = peerCount++;
  memset(&peers[i], 0, sizeof(Peer));
  memcpy(peers[i].mac, mac, 6);
  return i;
}

static bool peerHasResult(const Peer &p) {
  return p.phase == DuellPhase::HasResult || p.phase == DuellPhase::ShowingResult;
}

static void processStateMsg(const uint8_t *mac, const DuellStateMsg &msg) {
  int i = findOrAddPeer(mac);
  if (i < 0) return;
  peers[i].lastSeen = millis();
  peers[i].phase = (DuellPhase)msg.phase;
  peers[i].roundId = msg.roundId;
  peers[i].goal = msg.goal;
  peers[i].result = msg.result;
}

static void processRoundMsg(const uint8_t *mac, const DuellRoundMsg &msg, int data_len) {
  if (msg.playerCount > MAX_PEERS + 1) return;
  if (data_len != (int)(offsetof(DuellRoundMsg, players) + msg.playerCount * sizeof(DuellRoundEntry))) return;
  if (msg.roundId == 0) return;

  // Der Master ist auch ein Peer — Lebenszeichen mitnehmen
  int pi = findOrAddPeer(mac);
  if (pi >= 0) peers[pi].lastSeen = millis();

  // Nur Runden beachten, die mich als Teilnehmer nennen
  int meIdx = -1;
  for (int i = 0; i < msg.playerCount; i++) {
    if (memcmp(msg.players[i].mac, myMac, 6) == 0) {
      meIdx = i;
      break;
    }
  }
  if (meIdx < 0) return;

  if (msg.roundId == lastFinishedRoundId) return;  // alte, schon abgeschlossene Runde

  if (currentRoundId == 0) {
    // Neue Runde nur annehmen, wenn ich bereit bin und sie noch laeuft
    if (myPhase != DuellPhase::Ready || msg.finished) return;
    currentRoundId = msg.roundId;
    targetWeight = msg.target;
    hasStart = true;
    sendStateMsg();  // sofort quittieren, damit der Master den Beitritt sieht
  } else if (msg.roundId != currentRoundId) {
    return;  // ich stecke in einer anderen Runde
  } else {
    targetWeight = msg.target;
  }

  if (msg.finished && !hasRank && msg.players[meIdx].rank > 0) {
    myRank = msg.players[meIdx].rank;
    hasRank = true;
  }
}

// ── Master-Logik ──────────────────────────────────────────────────────────────

static bool isMaster() {
  unsigned long now = millis();
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen < PEER_ACTIVE_MS) {
      if (memcmp(peers[i].mac, myMac, 6) > 0) return false;
    }
  }
  return true;
}

static void maybeStartRound(unsigned long now) {
  if (myPhase != DuellPhase::Ready || currentRoundId != 0 || !isMaster()) {
    allReadySince = 0;
    return;
  }

  int readyPeers = 0;
  bool allReady = true;
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen < PEER_ACTIVE_MS) {
      if (peers[i].phase == DuellPhase::Ready && peers[i].roundId == 0) readyPeers++;
      else allReady = false;
    }
  }
  if (!allReady || readyPeers == 0) {
    allReadySince = 0;
    return;
  }
  if (allReadySince == 0) allReadySince = now;
  if (now - allReadySince < READY_GRACE_MS) return;

  // Teilnehmer einfrieren und Ziel aus deren Zielen wuerfeln
  masterRound = {};
  masterRound.active = true;
  masterRound.id = (uint16_t)random(1, 65536);
  masterRound.startedAt = now;

  float goals[MAX_PEERS + 1];
  memcpy(masterRound.p[masterRound.n].mac, myMac, 6);
  goals[masterRound.n] = myGoal;
  masterRound.n++;
  for (int i = 0; i < peerCount && masterRound.n < MAX_PEERS + 1; i++) {
    if (now - peers[i].lastSeen < PEER_ACTIVE_MS && peers[i].phase == DuellPhase::Ready && peers[i].roundId == 0) {
      memcpy(masterRound.p[masterRound.n].mac, peers[i].mac, 6);
      goals[masterRound.n] = peers[i].goal;
      masterRound.n++;
    }
  }
  masterRound.target = goals[random(0, masterRound.n)];

  // Eigener Beitritt (der Master empfaengt seine Broadcasts nicht selbst)
  currentRoundId = masterRound.id;
  targetWeight = masterRound.target;
  hasStart = true;
  allReadySince = 0;

  sendRoundMsg();
  sendStateMsg();
}

static void runRound(unsigned long now) {
  Round &r = masterRound;

  if (!r.finished) {
    // Ergebnisse level-getriggert aus den Peer-StateMsgs ernten
    bool allDone = true;
    for (int i = 0; i < r.n; i++) {
      RoundPlayer &rp = r.p[i];
      if (memcmp(rp.mac, myMac, 6) == 0) {
        if (!rp.hasResult && !rp.forfeit && myResultWeight >= 0.0f && currentRoundId == r.id) {
          rp.result = myResultWeight;
          rp.hasResult = true;
        }
      } else if (!rp.hasResult && !rp.forfeit) {
        int pi = findPeer(rp.mac);
        if (pi < 0 || now - peers[pi].lastSeen > FORFEIT_MS) {
          rp.forfeit = true;
        } else if (peers[pi].roundId == r.id && peerHasResult(peers[pi])) {
          rp.result = peers[pi].result;
          rp.hasResult = true;
        } else if (peers[pi].roundId != r.id && peers[pi].phase == DuellPhase::Idle) {
          rp.forfeit = true;  // Teilnehmer hat resettet/aufgegeben
        }
      }
      if (!rp.hasResult && !rp.forfeit) allDone = false;
    }

    if (allDone || now - r.startedAt > RESULT_TIMEOUT_MS) {
      // Raenge nach Abstand zum Ziel vergeben (kein Ergebnis = letzter Platz)
      float diffs[MAX_PEERS + 1];
      for (int i = 0; i < r.n; i++) {
        diffs[i] = r.p[i].hasResult ? fabsf(r.p[i].result - r.target) : 9999.0f;
      }
      for (int i = 0; i < r.n; i++) {
        int rank = 1;
        for (int j = 0; j < r.n; j++) {
          if (diffs[j] < diffs[i] || (diffs[j] == diffs[i] && j < i)) rank++;
        }
        r.p[i].rank = (uint8_t)rank;
        if (memcmp(r.p[i].mac, myMac, 6) == 0 && currentRoundId == r.id) {
          myRank = rank;
          hasRank = true;
        }
      }
      r.finished = true;
      r.finishedAt = now;
      sendRoundMsg();
    } else if (now - r.lastTx >= ROUND_TX_MS) {
      sendRoundMsg();
    }
    return;
  }

  // Fertige Runde wiederholen, bis alle Teilnehmer sie quittiert haben
  bool allAcked = true;
  for (int i = 0; i < r.n; i++) {
    RoundPlayer &rp = r.p[i];
    if (rp.acked || memcmp(rp.mac, myMac, 6) == 0) continue;
    int pi = findPeer(rp.mac);
    if (pi < 0) {
      rp.acked = true;  // Peer vergessen — nicht mehr erreichbar
    } else if (peers[pi].roundId == r.id && peers[pi].phase == DuellPhase::ShowingResult) {
      rp.acked = true;  // zeigt das Ergebnis an
    } else if (peers[pi].roundId != r.id && peers[pi].phase == DuellPhase::Idle) {
      rp.acked = true;  // hat schon resettet
    } else {
      allAcked = false;
    }
  }
  if (allAcked || now - r.finishedAt > FINISHED_TX_MAX_MS) {
    r.active = false;
  } else if (now - r.lastTx >= ROUND_TX_MS) {
    sendRoundMsg();
  }
}

// ── API ───────────────────────────────────────────────────────────────────────

void duell_init() {
  WiFi.macAddress(myMac);
  if (esp_now_init() != ESP_OK) {
    return;
  }
  if (!rxQueue) rxQueue = xQueueCreate(8, sizeof(RxPacket));
  if (!rxQueue) return;
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  for (int i = 0; i < 6; i++) peerInfo.peer_addr[i] = 0xFF;
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);  // Broadcast Peer
  initialized = true;
}

int duell_get_peers_count() {
  int active = 0;
  unsigned long now = millis();
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen < PEER_ACTIVE_MS) active++;
  }
  return active;
}

bool duell_is_active() {
  return duell_get_peers_count() > 0;
}

void duell_set_phase(DuellPhase phase) {
  myPhase = phase;
  sendStateMsg();
}

void duell_send_ready() {
  duell_set_phase(DuellPhase::Ready);
}

void duell_send_result(float drank_weight) {
  myResultWeight = drank_weight;
  duell_set_phase(DuellPhase::HasResult);
}

bool duell_has_start_signal(float *out_target_weight) {
  if (hasStart) {
    *out_target_weight = targetWeight;
    return true;
  }
  return false;
}

bool duell_has_ranking(int *out_rank) {
  if (hasRank) {
    *out_rank = myRank;
    return true;
  }
  return false;
}

void duell_reset_state() {
  if (currentRoundId != 0) lastFinishedRoundId = currentRoundId;
  currentRoundId = 0;
  myPhase = DuellPhase::Idle;
  hasStart = false;
  hasRank = false;
  myRank = 0;
  myResultWeight = -1.0f;
  targetWeight = 0.0f;
  allReadySince = 0;

  // Steige ich als Master mitten aus meiner eigenen laufenden Runde aus,
  // gebe ich auf — die Runde laeuft fuer die anderen normal zu Ende.
  if (masterRound.active && !masterRound.finished) {
    for (int i = 0; i < masterRound.n; i++) {
      if (memcmp(masterRound.p[i].mac, myMac, 6) == 0 && !masterRound.p[i].hasResult) {
        masterRound.p[i].forfeit = true;
      }
    }
  }

  sendStateMsg();
}

void duell_update(float local_goal) {
  if (!initialized) return;
  myGoal = local_goal;
  unsigned long now = millis();

  // Empfangene Pakete verarbeiten (im Main Loop, nicht im WiFi-Task)
  RxPacket pkt;
  while (xQueueReceive(rxQueue, &pkt, 0) == pdTRUE) {
    if (pkt.len == sizeof(DuellStateMsg) && pkt.data[1] == (uint8_t)DuellMsgType::State) {
      DuellStateMsg msg;
      memcpy(&msg, pkt.data, sizeof(msg));
      processStateMsg(pkt.mac, msg);
    } else if (pkt.len >= (int)offsetof(DuellRoundMsg, players) && pkt.data[1] == (uint8_t)DuellMsgType::Round) {
      DuellRoundMsg msg = {};
      memcpy(&msg, pkt.data, pkt.len);
      processRoundMsg(pkt.mac, msg, pkt.len);
    }
  }

  // Inaktive Peers vergessen
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen > PEER_FORGET_MS) {
      peers[i] = peers[peerCount - 1];
      peerCount--;
      i--;
    }
  }

  // Heartbeat: schneller, sobald irgendetwas laeuft
  unsigned long hbInterval = (myPhase != DuellPhase::Idle || masterRound.active)
                               ? HEARTBEAT_ACTIVE_MS
                               : HEARTBEAT_IDLE_MS;
  if (now - lastStateTx >= hbInterval) sendStateMsg();

  if (masterRound.active) runRound(now);
  else maybeStartRound(now);
}
