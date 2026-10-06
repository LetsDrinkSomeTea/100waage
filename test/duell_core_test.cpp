// Unit-Tests fuer die reine Duell-Logik (Wire-Format, Merge, Ranking).
#include "check.h"
#include "duell_core.h"
#include <cmath>
#include <cstring>

using namespace duell;

static void setMac(uint8_t *mac, uint8_t last) {
  const uint8_t base[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
  memcpy(mac, base, 6);
  mac[5] = last;
}

static Round makeRound(int n, float target) {
  Round r = {};
  r.id = 4711;
  r.target = target;
  r.n = (uint8_t)n;
  for (int i = 0; i < n; i++) setMac(r.e[i].mac, (uint8_t)(i + 1));
  return r;
}

static void setDone(Round &r, int i, float result, uint16_t durationCs) {
  r.e[i].status = Status::Done;
  r.e[i].result = result;
  r.e[i].durationCs = durationCs;
}

static void testRoundtrip() {
  Message m = {};
  m.phase = Phase::InRound;
  m.goal = 123.5f;
  m.elapsedMs = 4567;
  m.round = makeRound(3, 99.5f);
  setDone(m.round, 1, 98.25f, 1234);
  m.round.e[2].status = Status::Forfeit;

  uint8_t buf[MAX_MSG_SIZE];
  size_t len = encode(m, buf, sizeof(buf));
  CHECK(len == HEADER_SIZE + 3 * ENTRY_SIZE);

  Message d;
  CHECK(decode(buf, len, d));
  CHECK(d.phase == Phase::InRound);
  CHECK(d.goal == 123.5f);
  CHECK(d.elapsedMs == 4500);  // 100-ms-Aufloesung
  CHECK(d.round.id == 4711);
  CHECK(d.round.target == 99.5f);
  CHECK(d.round.n == 3);
  CHECK(d.round.e[0].status == Status::Pending);
  CHECK(d.round.e[1].status == Status::Done);
  CHECK(d.round.e[1].result == 98.25f);
  CHECK(d.round.e[1].durationCs == 1234);
  CHECK(d.round.e[2].status == Status::Forfeit);
  CHECK(memcmp(d.round.e[2].mac, m.round.e[2].mac, 6) == 0);

  // Ohne Runde: nur Header
  Message idle = {};
  idle.phase = Phase::Ready;
  idle.goal = 50.0f;
  len = encode(idle, buf, sizeof(buf));
  CHECK(len == HEADER_SIZE);
  CHECK(decode(buf, len, d));
  CHECK(d.phase == Phase::Ready && d.round.id == 0 && d.round.n == 0);

  // Volle Tabelle passt in ein ESP-NOW-Paket
  Message full = {};
  full.phase = Phase::InRound;
  full.round = makeRound(MAX_PLAYERS, 100.0f);
  len = encode(full, buf, sizeof(buf));
  CHECK(len == MAX_MSG_SIZE);
  CHECK(MAX_MSG_SIZE <= 250);
  CHECK(decode(buf, len, d));

  // Zu kleiner Puffer
  CHECK(encode(full, buf, MAX_MSG_SIZE - 1) == 0);
}

static void testDecodeRejects() {
  Message m = {};
  m.phase = Phase::InRound;
  m.goal = 100.0f;
  m.round = makeRound(2, 100.0f);
  uint8_t good[MAX_MSG_SIZE];
  size_t len = encode(m, good, sizeof(good));
  Message d;
  CHECK(decode(good, len, d));

  uint8_t buf[MAX_MSG_SIZE + 1];

  memcpy(buf, good, len);
  buf[0] = 0xD2;  // alte Protokollversion
  CHECK(!decode(buf, len, d));

  CHECK(!decode(good, HEADER_SIZE - 1, d));  // zu kurz
  CHECK(!decode(good, len - 1, d));          // abgeschnittener Eintrag
  memcpy(buf, good, len);
  buf[len] = 0;
  CHECK(!decode(buf, len + 1, d));  // Muell am Ende

  memcpy(buf, good, len);
  buf[1] = 7;  // ungueltige Phase
  CHECK(!decode(buf, len, d));

  memcpy(buf, good, len);
  buf[HEADER_SIZE - 1] = MAX_PLAYERS + 1;  // n zu gross
  CHECK(!decode(buf, len, d));

  memcpy(buf, good, len);
  buf[HEADER_SIZE + 6] = 9;  // ungueltiger Status
  CHECK(!decode(buf, len, d));

  // roundId 0 mit Eintraegen
  memcpy(buf, good, len);
  buf[6] = 0;
  buf[7] = 0;
  CHECK(!decode(buf, len, d));

  // roundId != 0 ohne Eintraege
  Message noEntries = {};
  noEntries.round.id = 5;
  uint8_t hb[MAX_MSG_SIZE];
  size_t hl = encode(noEntries, hb, sizeof(hb));
  CHECK(hl == HEADER_SIZE);
  hb[6] = 5;  // roundId wieder setzen, encode hat n=0 geschrieben
  CHECK(!decode(hb, hl, d));

  // Doppelte MAC
  Message dup = m;
  memcpy(dup.round.e[1].mac, dup.round.e[0].mac, 6);
  size_t dl = encode(dup, buf, sizeof(buf));
  CHECK(!decode(buf, dl, d));

  // NaN als Ergebnis eines fertigen Teilnehmers
  Message nan = m;
  setDone(nan.round, 0, NAN, 100);
  size_t nl = encode(nan, buf, sizeof(buf));
  CHECK(!decode(buf, nl, d));

  // NaN als Ziel
  Message nanGoal = m;
  nanGoal.goal = NAN;
  nl = encode(nanGoal, buf, sizeof(buf));
  CHECK(!decode(buf, nl, d));
}

static void testRanking() {
  uint8_t ranks[MAX_PLAYERS];

  // Abstand zum Ziel entscheidet
  Round r = makeRound(4, 100.0f);
  setDone(r, 0, 100.5f, 500);
  setDone(r, 1, 99.0f, 400);
  setDone(r, 2, 100.2f, 900);
  r.e[3].status = Status::Forfeit;
  computeRanks(r, ranks);
  CHECK(ranks[2] == 1);
  CHECK(ranks[0] == 2);
  CHECK(ranks[1] == 3);
  CHECK(ranks[3] == 0);

  // Gleicher Abstand: kuerzere Zeit gewinnt
  r = makeRound(2, 100.0f);
  setDone(r, 0, 100.5f, 800);
  setDone(r, 1, 99.5f, 700);
  computeRanks(r, ranks);
  CHECK(ranks[1] == 1);
  CHECK(ranks[0] == 2);

  // Komplett gleich: geteilter Platz (1, 1, 3)
  r = makeRound(3, 100.0f);
  setDone(r, 0, 100.5f, 700);
  setDone(r, 1, 99.5f, 700);
  setDone(r, 2, 103.0f, 100);
  computeRanks(r, ranks);
  CHECK(ranks[0] == 1);
  CHECK(ranks[1] == 1);
  CHECK(ranks[2] == 3);

  // Vergleich auf 0,01 g: Float-Rauschen erzeugt keinen kuenstlichen Sieger
  r = makeRound(2, 100.0f);
  setDone(r, 0, 100.004f, 500);
  setDone(r, 1, 99.996f, 500);
  computeRanks(r, ranks);
  CHECK(ranks[0] == 1 && ranks[1] == 1);

  // Pending hat (noch) keinen Platz, beeinflusst die anderen nicht
  r = makeRound(3, 100.0f);
  setDone(r, 1, 105.0f, 500);
  computeRanks(r, ranks);
  CHECK(ranks[0] == 0);
  CHECK(ranks[1] == 1);
  CHECK(ranks[2] == 0);

  // Riesige Werte laufen nicht ueber
  r = makeRound(2, 100.0f);
  setDone(r, 0, 1e30f, 500);
  setDone(r, 1, 50.0f, 500);
  computeRanks(r, ranks);
  CHECK(ranks[1] == 1 && ranks[0] == 2);
}

static void testMerge() {
  Round local = makeRound(4, 100.0f);
  setDone(local, 0, 100.0f, 100);  // eigener Eintrag
  local.e[3].status = Status::Forfeit;

  Round in = makeRound(4, 100.0f);
  in.e[0].status = Status::Forfeit;  // darf eigenen Eintrag nicht ueberschreiben
  in.e[1].status = Status::Forfeit;  // Pending -> Forfeit
  setDone(in, 2, 101.0f, 200);       // Pending -> Done
  setDone(in, 3, 102.0f, 300);       // Forfeit -> Done

  CHECK(mergeRound(local, in, 0));
  CHECK(local.e[0].status == Status::Done && local.e[0].result == 100.0f);
  CHECK(local.e[1].status == Status::Forfeit);
  CHECK(local.e[2].status == Status::Done && local.e[2].result == 101.0f);
  CHECK(local.e[3].status == Status::Done && local.e[3].durationCs == 300);

  // Done wird nie zurueckgestuft oder ersetzt
  Round in2 = makeRound(4, 100.0f);
  in2.e[2].status = Status::Forfeit;
  setDone(in2, 3, 999.0f, 1);
  CHECK(!mergeRound(local, in2, 0));
  CHECK(local.e[2].status == Status::Done && local.e[2].result == 101.0f);
  CHECK(local.e[3].result == 102.0f);

  // Fremde Runde wird ignoriert
  Round other = makeRound(4, 100.0f);
  other.id = 1;
  setDone(other, 1, 50.0f, 1);
  CHECK(!mergeRound(local, other, 0));
  CHECK(local.e[1].status == Status::Forfeit);

  // Merge ist idempotent
  CHECK(!mergeRound(local, in, 0));
}

int main() {
  testRoundtrip();
  testDecodeRejects();
  testRanking();
  testMerge();
  return finish("duell_core_test");
}
