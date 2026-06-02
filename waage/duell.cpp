#include "duell.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

enum class DuellMsgType : uint8_t { Heartbeat,
                                    Ready,
                                    Start,
                                    Result,
                                    Ranking };

struct __attribute__((packed)) DuellMsg {
  DuellMsgType type;
  uint8_t subjectMac[6];  // Wer sendet, oder um wen es geht (beim Ranking)
  float value;
};

constexpr int MAX_PEERS = 10;
struct Peer {
  uint8_t mac[6];
  unsigned long lastSeen;
  bool isReady;
  float goal;
  float result;
  bool hasResult;
};

static Peer peers[MAX_PEERS];
static int peerCount = 0;
static uint8_t myMac[6];

static bool hasStart = false;
static float targetWeight = 0.0f;

static bool hasRank = false;
static int myRank = 0;

static unsigned long lastHeartbeat = 0;
static unsigned long readySince = 0;
static bool amReady = false;

static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int data_len) {
  const uint8_t *mac_addr = info->src_addr;
  if (data_len != sizeof(DuellMsg)) return;
  DuellMsg msg;
  memcpy(&msg, data, sizeof(DuellMsg));

  // Finde oder erstelle Peer
  int pIdx = -1;
  for (int i = 0; i < peerCount; i++) {
    if (memcmp(peers[i].mac, mac_addr, 6) == 0) {
      pIdx = i;
      break;
    }
  }
  if (pIdx == -1 && peerCount < MAX_PEERS) {
    pIdx = peerCount++;
    memcpy(peers[pIdx].mac, mac_addr, 6);
    peers[pIdx].isReady = false;
    peers[pIdx].hasResult = false;
  }

  if (pIdx != -1) {
    peers[pIdx].lastSeen = millis();
  }

  if (msg.type == DuellMsgType::Heartbeat && pIdx != -1) {
    peers[pIdx].goal = msg.value;
  } else if (msg.type == DuellMsgType::Ready && pIdx != -1) {
    peers[pIdx].isReady = true;
  } else if (msg.type == DuellMsgType::Start) {
    hasStart = true;
    targetWeight = msg.value;
  } else if (msg.type == DuellMsgType::Result && pIdx != -1) {
    peers[pIdx].result = msg.value;
    peers[pIdx].hasResult = true;
  } else if (msg.type == DuellMsgType::Ranking) {
    if (memcmp(msg.subjectMac, myMac, 6) == 0) {
      myRank = (int)msg.value;
      hasRank = true;
    }
  }
}

void duell_init() {
  WiFi.macAddress(myMac);
  if (esp_now_init() != ESP_OK) {
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  for (int i = 0; i < 6; i++) peerInfo.peer_addr[i] = 0xFF;
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);  // Broadcast Peer
}

int duell_get_peers_count() {
  int active = 0;
  unsigned long now = millis();
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen < 5000UL) active++;
  }
  return active;
}

bool duell_is_active() {
  return duell_get_peers_count() > 0;
}

static void send_broadcast(DuellMsgType type, float value, const uint8_t *subject = myMac) {
  DuellMsg msg;
  msg.type = type;
  msg.value = value;
  memcpy(msg.subjectMac, subject, 6);
  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_send(bcast, (uint8_t *)&msg, sizeof(msg));
}

void duell_send_ready() {
  amReady = true;
  readySince = millis();
  send_broadcast(DuellMsgType::Ready, 0.0f);
}

static float myResultWeight = -1.0f;

void duell_send_result(float drank_weight) {
  myResultWeight = drank_weight;
  send_broadcast(DuellMsgType::Result, drank_weight);
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
  hasStart = false;
  hasRank = false;
  amReady = false;
  targetWeight = 0.0f;
  myRank = 0;
  myResultWeight = -1.0f;
  for (int i = 0; i < peerCount; i++) {
    peers[i].isReady = false;
    peers[i].hasResult = false;
  }
}

static bool isMaster() {
  unsigned long now = millis();
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen < 5000UL) {
      if (memcmp(peers[i].mac, myMac, 6) > 0) return false;
    }
  }
  return true;
}

static void checkMasterLogic(float local_goal) {
  if (!isMaster()) return;
  unsigned long now = millis();

  if (amReady && !hasStart) {
    bool allReady = true;
    for (int i = 0; i < peerCount; i++) {
      if (now - peers[i].lastSeen < 5000UL && !peers[i].isReady) {
        allReady = false;
        break;
      }
    }
    if (allReady && (now - readySince > 3000UL)) {
      int active = 1;
      float goals[MAX_PEERS + 1];
      goals[0] = local_goal;  // master's own goal
      for (int i = 0; i < peerCount; i++) {
        if (now - peers[i].lastSeen < 5000UL) {
          goals[active++] = peers[i].goal;
        }
      }
      targetWeight = goals[random(0, active)];
      hasStart = true;
      send_broadcast(DuellMsgType::Start, targetWeight);
    }
  }


  if (hasStart && amReady && !hasRank) {
    bool allDone = true;
    for (int i = 0; i < peerCount; i++) {
      if (now - peers[i].lastSeen < 5000UL && peers[i].isReady && !peers[i].hasResult) {
        allDone = false;
        break;
      }
    }
    if (allDone || (now - readySince > 60000UL)) {
      struct PlayerRes {
        uint8_t mac[6];
        float diff;
      };
      PlayerRes results[MAX_PEERS + 1];
      int rCount = 0;

      memcpy(results[rCount].mac, myMac, 6);
      results[rCount].diff = (myResultWeight >= 0.0f) ? abs(myResultWeight - targetWeight) : 9999.0f;
      rCount++;

      for (int i = 0; i < peerCount; i++) {
        if (now - peers[i].lastSeen < 5000UL && peers[i].isReady) {
          memcpy(results[rCount].mac, peers[i].mac, 6);
          results[rCount].diff = (peers[i].hasResult) ? abs(peers[i].result - targetWeight) : 9999.0f;
          rCount++;
        }
      }

      // Sort by diff (ascending)
      for (int i = 0; i < rCount - 1; i++) {
        for (int j = i + 1; j < rCount; j++) {
          if (results[j].diff < results[i].diff) {
            PlayerRes temp = results[i];
            results[i] = results[j];
            results[j] = temp;
          }
        }
      }

      // Assign ranks (broadcast)
      for (int i = 0; i < rCount; i++) {
        int rank = i + 1;
        send_broadcast(DuellMsgType::Ranking, (float)rank, results[i].mac);
        if (memcmp(results[i].mac, myMac, 6) == 0) {
          myRank = rank;
          hasRank = true;
        }
      }
    }
  }
}


void duell_update(float local_goal) {
  unsigned long now = millis();
  if (now - lastHeartbeat > 2000UL) {
    send_broadcast(DuellMsgType::Heartbeat, local_goal);
    lastHeartbeat = now;
  }

  // Cleanup old peers
  for (int i = 0; i < peerCount; i++) {
    if (now - peers[i].lastSeen > 10000UL) {
      peers[i] = peers[peerCount - 1];
      peerCount--;
      i--;
    }
  }

  checkMasterLogic(local_goal);
}
