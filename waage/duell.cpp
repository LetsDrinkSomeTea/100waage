#include "duell.h"
#include "version.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>

// ── RX-Queue ──────────────────────────────────────────────────────────────────
// Der ESP-NOW-Callback laeuft im WiFi-Task. Er legt Pakete nur in die Queue,
// verarbeitet wird im Main Loop (duell_update) — kein geteilter Zustand.
// Die Queue bleibt nach duell_deinit() bestehen, damit ein spaeter Callback
// nie auf freigegebenen Speicher trifft.

struct RxPacket {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[duell::MAX_MSG_SIZE];
};

constexpr int RX_QUEUE_LEN = 32;

static QueueHandle_t rxQueue = nullptr;
static bool initialized = false;
static duell::Core core;

static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int data_len) {
  if (data_len < (int)duell::HEADER_SIZE || data_len > (int)duell::MAX_MSG_SIZE) return;
  if (data[0] != duell::MAGIC) return;
  if (!rxQueue) return;
  RxPacket pkt;
  memcpy(pkt.mac, info->src_addr, 6);
  pkt.len = (uint8_t)data_len;
  memcpy(pkt.data, data, data_len);
  xQueueSend(rxQueue, &pkt, 0);  // Queue voll: Paket verwerfen, Gossip wiederholt es
}

static void sendBroadcast(void *, const uint8_t *data, size_t len) {
  static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_send(bcast, data, len);
}

static uint32_t randomRange(void *, uint32_t lo, uint32_t hi) {
  return (uint32_t)random((long)lo, (long)hi);
}

// ── API ───────────────────────────────────────────────────────────────────────

void duell_init() {
  if (initialized) return;
  if (!rxQueue) rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxPacket));
  if (!rxQueue) return;
  xQueueReset(rxQueue);
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = DUELL_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);  // Broadcast-Peer

  uint8_t mac[6];
  WiFi.macAddress(mac);
  core.begin(mac, millis(), sendBroadcast, randomRange, nullptr);
  initialized = true;
}

// Ausstieg sofort mehrfach senden (Broadcast ohne ACK), damit die anderen nicht
// 30 s bis zum Forfeit warten. Blockiert ca. FLUSH_COUNT * FLUSH_GAP_MS.
constexpr int FLUSH_COUNT = 3;
constexpr uint32_t FLUSH_GAP_MS = 25;

void duell_flush_burst() {
  if (!initialized) return;
  for (int i = 0; i < FLUSH_COUNT; i++) {
    core.flush(millis());
    delay(FLUSH_GAP_MS);
  }
}

void duell_deinit() {
  if (!initialized) return;
  if (core.phase() != duell::Phase::Idle || core.busy(millis())) {
    core.leave(millis());
    duell_flush_burst();  // esp_now_send ist asynchron: Luecke danach laesst es raus
  }
  initialized = false;
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  core = duell::Core();
  if (rxQueue) xQueueReset(rxQueue);
}

void duell_update(float local_goal) {
  if (!initialized) return;
  RxPacket pkt;
  while (xQueueReceive(rxQueue, &pkt, 0) == pdTRUE) {
    core.onReceive(pkt.mac, pkt.data, pkt.len, millis());
  }
  core.tick(millis(), local_goal);
}

int duell_get_peers_count() {
  return initialized ? core.activePeers(millis()) : 0;
}

bool duell_is_active() {
  return duell_get_peers_count() > 0;
}

void duell_ready_count(int *ready, int *total) {
  *ready = initialized ? core.readyCount(millis()) : 0;
  *total = duell_get_peers_count() + 1;
}

void duell_set_ready() {
  if (initialized) core.setReady();
}

void duell_submit_result(float drank_weight, unsigned long duration_ms) {
  if (initialized) core.submitResult(drank_weight, (uint32_t)duration_ms);
}

void duell_leave() {
  if (initialized) core.leave(millis());
}

bool duell_has_start_signal(float *out_target_weight) {
  return initialized && core.startSignal(out_target_weight);
}

duell::View duell_get_view() {
  if (!initialized) return duell::View();
  return core.view();
}

bool duell_busy() {
  return initialized && core.busy(millis());
}

// ── Anbindung an die Spiellogik ───────────────────────────────────────────────

namespace {
class EspDuelPort final : public game::DuelPort {
public:
  bool active() override { return duell_is_active(); }
  void readyCount(int *ready, int *total) override { duell_ready_count(ready, total); }
  void setReady() override { duell_set_ready(); }
  bool startSignal(float *target) override { return duell_has_start_signal(target); }
  duell::View view() override { return duell_get_view(); }
  void submit(float grams, uint32_t durationMs) override { duell_submit_result(grams, durationMs); }
  void leave() override { duell_leave(); }
};
EspDuelPort port;
}  // namespace

game::DuelPort &duell_port() {
  return port;
}

// ── Debug-JSON fuer das Web-UI ────────────────────────────────────────────────

static const char *phaseName(duell::Phase p) {
  switch (p) {
    case duell::Phase::Idle: return "Idle";
    case duell::Phase::Ready: return "Ready";
    case duell::Phase::InRound: return "InRound";
  }
  return "?";
}

static const char *statusName(duell::Status s) {
  switch (s) {
    case duell::Status::Pending: return "Pending";
    case duell::Status::Forfeit: return "Forfeit";
    case duell::Status::Done: return "Done";
  }
  return "?";
}

static String macStr(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

static String roundJson(const duell::Round &r, uint32_t elapsedMs) {
  if (r.id == 0) return "null";
  uint8_t ranks[duell::MAX_PLAYERS];
  duell::computeRanks(r, ranks);
  bool isFinal = true;
  for (int i = 0; i < r.n; i++) {
    if (r.e[i].status == duell::Status::Pending) isFinal = false;
  }
  String j = "{\"id\":" + String(r.id);
  j += ",\"target\":" + String(r.target, 1);
  j += ",\"elapsed\":" + String(elapsedMs / 1000.0f, 1);
  j += ",\"final\":" + String(isFinal ? "true" : "false");
  j += ",\"players\":[";
  for (int i = 0; i < r.n; i++) {
    const duell::Entry &e = r.e[i];
    if (i) j += ",";
    j += "{\"mac\":\"" + macStr(e.mac) + "\"";
    j += ",\"me\":" + String(memcmp(e.mac, core.mac(), 6) == 0 ? "true" : "false");
    j += ",\"status\":\"" + String(statusName(e.status)) + "\"";
    if (e.status == duell::Status::Done) {
      j += ",\"result\":" + String(e.result, 2);
      j += ",\"time\":" + String(e.durationCs / 100.0f, 2);
    }
    j += ",\"rank\":" + String(ranks[i]) + "}";
  }
  j += "]}";
  return j;
}

String duell_status_json() {
  uint32_t now = millis();
  String j = "{\"proto\":" + String(duell::MAGIC);
  j += ",\"fw\":\"" + String(FW_VERSION) + "\"";
  j += ",\"radio\":" + String(initialized ? "true" : "false");
  if (!initialized) return j + "}";
  j += ",\"mac\":\"" + macStr(core.mac()) + "\"";
  j += ",\"phase\":\"" + String(phaseName(core.phase())) + "\"";
  j += ",\"peers\":[";
  for (int i = 0; i < core.peerCount(); i++) {
    const duell::Peer &p = core.peer(i);
    if (i) j += ",";
    j += "{\"mac\":\"" + macStr(p.mac) + "\"";
    j += ",\"phase\":\"" + String(phaseName(p.phase)) + "\"";
    j += ",\"round\":" + String(p.roundId);
    j += ",\"ago\":" + String((now - p.lastSeen) / 1000.0f, 1) + "}";
  }
  j += "],\"round\":" + roundJson(core.currentRound(), core.roundElapsed(now));
  j += ",\"last\":" + roundJson(core.lingerRound(), 0);
  j += "}";
  return j;
}
