#include "web_core.h"
#include <cmath>
#include <stdio.h>
#include <string.h>

namespace web {

namespace {

constexpr uint8_t MAX_DEPTH = 16;

// Ziel fuer Writer ohne Puffer: c_str() liefert dann "" (wird nie beschrieben).
char g_empty[1] = {0};

const char HEX_DIGITS[] = "0123456789abcdef";

// Dezimalziffern von v nach out (mind. 11 Bytes), liefert die Laenge.
size_t fmtU32(uint32_t v, char *out) {
  char tmp[10];
  size_t n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  for (size_t i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  out[n] = '\0';
  return n;
}

bool isSpace(char c) { return c == ' ' || c == '\t'; }

} // namespace

// ── JsonWriter ────────────────────────────────────────────────────────────────

JsonWriter::JsonWriter(char *buf, size_t cap) : buf_(buf), cap_(cap) {
  static_assert(sizeof(first_) / sizeof(first_[0]) == MAX_DEPTH,
                "first_ passt nicht zu MAX_DEPTH");
  static_assert(sizeof(objMask_) * 8 >= MAX_DEPTH, "objMask_ zu klein");
  if (!buf || cap == 0) {
    // Nicht einmal Platz fuer NUL: nichts schreiben, aber terminiert bleiben
    buf_ = g_empty;
    cap_ = 0;
    ok_ = false;
    return;
  }
  buf_[0] = '\0';
}

void JsonWriter::rawChar(char c) {
  if (!ok_)
    return;
  if (cap_ - len_ < 2) { // Zeichen + NUL muessen passen
    ok_ = false;
    return;
  }
  buf_[len_++] = c;
  buf_[len_] = '\0';
}

void JsonWriter::raw(const char *s) {
  while (ok_ && *s)
    rawChar(*s++);
}

// Komma bzw. Pruefung vor jedem Wert; setzt ok_ = false bei falscher Nutzung.
void JsonWriter::valuePrefix() {
  if (!ok_)
    return;
  if (depth_ == 0) {
    if (len_ > 0)
      ok_ = false; // nur ein Wurzelwert
    return;
  }
  uint8_t lvl = depth_ - 1;
  if (objMask_ & (1u << lvl)) {
    if (!afterKey_)
      ok_ = false; // im Objekt nur nach key()
    afterKey_ = false;
    return;
  }
  if (first_[lvl])
    first_[lvl] = false;
  else
    rawChar(',');
}

namespace {

// String in Anfuehrungszeichen mit Escapes; UTF-8 (>= 0x80) bleibt
// unveraendert.
template <typename W> void quoted(W &&put, const char *s) {
  put('"');
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      put('\\');
      put((char)c);
    } else if (c < 0x20) {
      put('\\');
      put('u');
      put('0');
      put('0');
      put(HEX_DIGITS[c >> 4]);
      put(HEX_DIGITS[c & 0xF]);
    } else {
      put((char)c);
    }
  }
  put('"');
}

} // namespace

JsonWriter &JsonWriter::beginObject() {
  if (!ok_)
    return *this;
  if (depth_ >= MAX_DEPTH) {
    ok_ = false;
    return *this;
  }
  valuePrefix();
  rawChar('{');
  if (!ok_)
    return *this;
  first_[depth_] = true;
  objMask_ |= (uint16_t)(1u << depth_);
  depth_++;
  return *this;
}

JsonWriter &JsonWriter::endObject() {
  if (!ok_)
    return *this;
  if (depth_ == 0 || !(objMask_ & (1u << (depth_ - 1))) || afterKey_) {
    ok_ = false; // keine offene Ebene, falscher Typ oder key() ohne Wert
    return *this;
  }
  rawChar('}');
  if (ok_)
    depth_--;
  return *this;
}

JsonWriter &JsonWriter::beginArray() {
  if (!ok_)
    return *this;
  if (depth_ >= MAX_DEPTH) {
    ok_ = false;
    return *this;
  }
  valuePrefix();
  rawChar('[');
  if (!ok_)
    return *this;
  first_[depth_] = true;
  objMask_ &= (uint16_t) ~(1u << depth_);
  depth_++;
  return *this;
}

JsonWriter &JsonWriter::endArray() {
  if (!ok_)
    return *this;
  if (depth_ == 0 || (objMask_ & (1u << (depth_ - 1)))) {
    ok_ = false;
    return *this;
  }
  rawChar(']');
  if (ok_)
    depth_--;
  return *this;
}

JsonWriter &JsonWriter::key(const char *k) {
  if (!ok_)
    return *this;
  if (!k || depth_ == 0 || !(objMask_ & (1u << (depth_ - 1))) || afterKey_) {
    ok_ = false; // nur im Objekt, und nicht zwei Schluessel hintereinander
    return *this;
  }
  uint8_t lvl = depth_ - 1;
  if (first_[lvl])
    first_[lvl] = false;
  else
    rawChar(',');
  quoted([this](char c) { rawChar(c); }, k);
  rawChar(':');
  afterKey_ = true;
  return *this;
}

JsonWriter &JsonWriter::str(const char *s) {
  if (!s)
    return null();
  valuePrefix();
  quoted([this](char c) { rawChar(c); }, s);
  return *this;
}

JsonWriter &JsonWriter::num(float v, int decimals) {
  if (!std::isfinite(v))
    return null();
  valuePrefix();
  if (!ok_)
    return *this;
  if (decimals < 0)
    decimals = 0;
  if (decimals > 6)
    decimals = 6;
  char tmp[64]; // FLT_MAX mit 6 Stellen braucht 47 Zeichen
  int n = snprintf(tmp, sizeof(tmp), "%.*f", decimals, (double)v);
  if (n < 0 || (size_t)n >= sizeof(tmp)) {
    ok_ = false;
    return *this;
  }
  // "-0.0" (kleine negative Werte) als "0.0" ausgeben
  const char *p = tmp;
  if (tmp[0] == '-') {
    bool zero = true;
    for (const char *q = tmp + 1; *q; q++) {
      if (*q != '0' && *q != '.')
        zero = false;
    }
    if (zero)
      p++;
  }
  raw(p);
  return *this;
}

JsonWriter &JsonWriter::integer(int32_t v) {
  valuePrefix();
  char tmp[12];
  if (v < 0) {
    tmp[0] = '-';
    fmtU32((uint32_t)0 - (uint32_t)v, tmp + 1);
  } else {
    fmtU32((uint32_t)v, tmp);
  }
  raw(tmp);
  return *this;
}

JsonWriter &JsonWriter::uinteger(uint32_t v) {
  valuePrefix();
  char tmp[11];
  fmtU32(v, tmp);
  raw(tmp);
  return *this;
}

JsonWriter &JsonWriter::flag(bool v) {
  valuePrefix();
  raw(v ? "true" : "false");
  return *this;
}

JsonWriter &JsonWriter::null() {
  valuePrefix();
  raw("null");
  return *this;
}

// ── Cookies ───────────────────────────────────────────────────────────────────

bool cookieValue(const char *header, const char *name, char *out,
                 size_t outSize) {
  if (!out || outSize == 0)
    return false;
  out[0] = '\0';
  if (!header || !name || !name[0])
    return false;
  size_t nameLen = strlen(name);

  const char *p = header;
  while (*p) {
    // Ein Eintrag reicht bis ';' oder Ende
    const char *seg = p;
    while (*p && *p != ';')
      p++;
    const char *segEnd = p;
    if (*p == ';')
      p++;

    while (seg < segEnd && isSpace(*seg))
      seg++;
    const char *eq = seg;
    while (eq < segEnd && *eq != '=')
      eq++;
    if (eq == segEnd)
      continue; // kein '=': kein Cookie

    const char *nEnd = eq;
    while (nEnd > seg && isSpace(nEnd[-1]))
      nEnd--;
    if ((size_t)(nEnd - seg) != nameLen || memcmp(seg, name, nameLen) != 0)
      continue;

    const char *v = eq + 1;
    const char *vEnd = segEnd;
    while (v < vEnd && isSpace(*v))
      v++;
    while (vEnd > v && isSpace(vEnd[-1]))
      vEnd--;
    size_t len = (size_t)(vEnd - v);
    if (len + 1 > outSize)
      return false; // erster Treffer zaehlt
    memcpy(out, v, len);
    out[len] = '\0';
    return true;
  }
  return false;
}

// ── Token ─────────────────────────────────────────────────────────────────────

bool ctEquals(const char *a, const char *b) {
  if (!a || !b)
    return false;
  // Laufzeit haengt nur von der Laenge von b ab; a wird nie ueber sein NUL
  // hinaus gelesen (j bleibt dort stehen).
  unsigned diff = 0;
  size_t j = 0;
  for (size_t i = 0; b[i]; i++) {
    char ca = a[j];
    diff |= (unsigned char)(ca ^ b[i]);
    j += (ca != '\0');
  }
  diff |= (unsigned char)a[j]; // a laenger als b
  return diff == 0;
}

void tokenHex(const uint32_t words[4], char out[33]) {
  for (int w = 0; w < 4; w++) {
    for (int i = 0; i < 8; i++) {
      out[w * 8 + i] = HEX_DIGITS[(words[w] >> (28 - 4 * i)) & 0xF];
    }
  }
  out[32] = '\0';
}

// ── Login-Bremse ──────────────────────────────────────────────────────────────

// Hinweis: Wird locked() nach einer Sperre ueber 49 Tage nicht aufgerufen,
// kann sie nach dem Ueberlauf von now fuer hoechstens LOCK_MS wieder greifen
// (harmlos, Zaehler startet danach neu).
bool LoginThrottle::locked(uint32_t now) const {
  return locked_ && (uint32_t)(now - lockedSince_) < LOCK_MS;
}

void LoginThrottle::failure(uint32_t now) {
  if (locked(now))
    return;      // waehrend der Sperre zaehlt nichts (keine Verlaengerung)
  if (locked_) { // Sperre abgelaufen: neu zaehlen
    locked_ = false;
    fails_ = 0;
  }
  if (++fails_ >= MAX_FAILS) {
    locked_ = true;
    lockedSince_ = now;
  }
}

void LoginThrottle::success() {
  fails_ = 0;
  locked_ = false;
}

} // namespace web
