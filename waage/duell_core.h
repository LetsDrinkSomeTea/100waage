#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Duell-Protokoll v3 ────────────────────────────────────────────────────────
// Reine Logik ohne Arduino-/ESP-Abhaengigkeiten, damit sie auf dem Host
// getestet und mit mehreren Knoten simuliert werden kann (siehe test/).
//
// Jede Waage broadcastet periodisch eine Nachricht mit ihrer Phase und — wenn
// sie in einer Runde ist oder eine gerade verlassen hat — der kompletten
// Rundentabelle (Teilnehmer, Ziel, Status/Ergebnis jedes Teilnehmers).
// Tabellen werden monoton gemerged (Pending < Forfeit < Done), jede Waage
// berechnet das Ranking selbst und deterministisch. Ein Leader (hoechste MAC)
// wird nur noch fuer den Rundenstart gebraucht.
//
// Bei inkompatiblen Aenderungen MAGIC erhoehen — alte und neue Firmware
// ignorieren sich dann gegenseitig.

namespace duell {

constexpr uint8_t MAGIC = 0xD3;
constexpr int MAX_PEERS = 10;
constexpr int MAX_PLAYERS = MAX_PEERS + 1;
constexpr int RECENT_ROUNDS = 4;

// ── Timing ────────────────────────────────────────────────────────────────────
constexpr uint32_t HEARTBEAT_IDLE_MS = 1000;
constexpr uint32_t HEARTBEAT_ACTIVE_MS = 400;
constexpr uint32_t PEER_ACTIVE_MS = 5000;
constexpr uint32_t PEER_FORGET_MS = 10000;
constexpr uint32_t STARTUP_GUARD_MS = 5000;       // so lange nach Funkstart nicht Leader sein
constexpr uint32_t READY_GRACE_MS = 3000;         // alle bereit so lange, dann Start
constexpr uint32_t JOIN_WINDOW_MS = 10000;        // Beitritt nur so lange nach Rundenstart
constexpr uint32_t INVISIBLE_FORFEIT_MS = 30000;  // Teilnehmer so lange nicht gehoert = aufgegeben
constexpr uint32_t ROUND_MAX_MS = 180000;         // danach gilt jeder Pending als aufgegeben
constexpr uint32_t LINGER_MS = 20000;             // verlassene Runde so lange weitersenden

enum class Phase : uint8_t { Idle = 0,
                             Ready = 1,
                             InRound = 2 };

enum class Status : uint8_t { Pending = 0,
                              Forfeit = 1,
                              Done = 2 };

struct Entry {
  uint8_t mac[6];
  Status status;
  float result;         // getrunkene Gramm, gueltig bei Done
  uint16_t durationCs;  // Trinkzeit in 1/100 s, gueltig bei Done
};

struct Round {
  uint16_t id;  // 0 = keine Runde
  float target;
  uint8_t n;
  Entry e[MAX_PLAYERS];  // nach MAC aufsteigend sortiert
};

struct Message {
  Phase phase;
  float goal;          // eigenes Ziel, Kandidat fuer das Rundenziel
  uint32_t elapsedMs;  // Zeit seit Rundenstart (Aufloesung 100 ms)
  Round round;         // round.id == 0: keine Tabelle
};

// ── Wire-Format (little endian, wie ESP32 und x86) ───────────────────────────
// magic u8 | phase u8 | goal f32 | roundId u16 | elapsedDs u16 | target f32 | n u8
// n × { mac[6] | status u8 | result f32 | durationCs u16 }
constexpr size_t HEADER_SIZE = 15;
constexpr size_t ENTRY_SIZE = 13;
constexpr size_t MAX_MSG_SIZE = HEADER_SIZE + MAX_PLAYERS * ENTRY_SIZE;

size_t encode(const Message &m, uint8_t *buf, size_t cap);  // 0 = Fehler
bool decode(const uint8_t *data, size_t len, Message &out);

int findEntry(const Round &r, const uint8_t mac[6]);

// Monotoner Merge: Done schlaegt Forfeit schlaegt Pending. Der Eintrag
// skipIdx (die eigene Waage) wird nie von aussen ueberschrieben.
// Liefert true, wenn sich etwas geaendert hat.
bool mergeRound(Round &local, const Round &incoming, int skipIdx);

// ranks[i] = Platz (1..n) fuer Done-Eintraege, 0 sonst. Kriterium:
// |Ergebnis - Ziel| in Centigramm, dann kuerzere Zeit, sonst geteilter Platz.
void computeRanks(const Round &r, uint8_t ranks[MAX_PLAYERS]);

// ── Zustand einer Waage ───────────────────────────────────────────────────────

struct View {
  bool inRound;
  bool isFinal;     // kein Teilnehmer mehr Pending
  Status myStatus;
  uint8_t rank;     // 0 = (noch) kein Platz
  uint8_t settled;  // Teilnehmer mit Done oder Forfeit
  uint8_t total;
  float target;
};

struct Peer {
  uint8_t mac[6];
  uint32_t lastSeen;
  Phase phase;
  uint16_t roundId;
  float goal;
};

class Core {
public:
  typedef void (*SendFn)(void *ctx, const uint8_t *data, size_t len);
  typedef uint32_t (*RandFn)(void *ctx, uint32_t lo, uint32_t hi);  // [lo, hi)

  void begin(const uint8_t mac[6], uint32_t now, SendFn send, RandFn rnd, void *ctx);

  void onReceive(const uint8_t mac[6], const uint8_t *data, size_t len, uint32_t now);
  void tick(uint32_t now, float localGoal);

  // Ereignisse aus der State-Machine
  void setReady();
  void submitResult(float grams, uint32_t durationMs);
  void leave(uint32_t now);

  int activePeers(uint32_t now) const;
  int readyCount(uint32_t now) const;  // bereite Waagen inkl. mir
  bool startSignal(float *target) const;
  View view() const;
  bool busy(uint32_t now) const;  // Runde oder Nachlauf aktiv

  // Fuer Debug-Ausgaben (Web)
  const uint8_t *mac() const { return myMac_; }
  Phase phase() const { return phase_; }
  const Round &currentRound() const { return cur_; }
  const Round &lingerRound() const { return linger_; }
  uint32_t roundElapsed(uint32_t now) const { return now - curStartedAt_; }
  int peerCount() const { return nPeers_; }
  const Peer &peer(int i) const { return peers_[i]; }

private:
  int findPeer(const uint8_t mac[6]) const;
  int findOrAddPeer(const uint8_t mac[6]);
  bool recentlyLeft(uint16_t id) const;
  void join(const Message &m, int me, uint32_t now);
  void checkForfeits(uint32_t now);
  void maybeStartRound(uint32_t now);
  void startRound(uint32_t now);
  void send(uint32_t now);

  uint8_t myMac_[6] = {};
  uint32_t beganAt_ = 0;
  SendFn send_ = nullptr;
  RandFn rand_ = nullptr;
  void *ctx_ = nullptr;

  Phase phase_ = Phase::Idle;
  float goal_ = 0.0f;

  Round cur_ = {};  // aktuelle Runde, cur_.id != 0 genau bei Phase::InRound
  int myIdx_ = -1;
  uint32_t curStartedAt_ = 0;
  uint32_t lastHeard_[MAX_PLAYERS] = {};

  Round linger_ = {};  // zuletzt verlassene Runde, wird noch mitgesendet
  uint32_t lingerStartedAt_ = 0;
  uint32_t lingerSince_ = 0;

  uint16_t recent_[RECENT_ROUNDS] = {};
  int recentIdx_ = 0;

  Peer peers_[MAX_PEERS] = {};
  int nPeers_ = 0;

  bool graceRunning_ = false;
  uint32_t graceSince_ = 0;
  int graceActive_ = 0;

  bool dirty_ = false;
  uint32_t lastTx_ = 0;
};

}  // namespace duell
