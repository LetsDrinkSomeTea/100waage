#pragma once
#include <stddef.h>
#include <stdint.h>

// ── Web-Helfer (rein, ohne Arduino) ───────────────────────────────────────────

namespace web {

// JSON in einen festen Puffer. Kommas werden automatisch gesetzt, Strings
// escaped (", \, Steuerzeichen als \u00XX), NaN/Inf als null. Laeuft der
// Puffer ueber, wird nichts mehr geschrieben und ok() liefert false; der
// Inhalt bleibt dann terminiert, ist aber unvollstaendig.
class JsonWriter {
public:
  JsonWriter(char *buf, size_t cap);
  JsonWriter &beginObject();
  JsonWriter &endObject();
  JsonWriter &beginArray();
  JsonWriter &endArray();
  JsonWriter &key(const char *k);              // danach genau ein Wert
  JsonWriter &str(const char *s);              // nullptr → null
  JsonWriter &num(float v, int decimals);      // nicht endlich → null
  JsonWriter &integer(int32_t v);
  JsonWriter &uinteger(uint32_t v);
  JsonWriter &flag(bool v);
  JsonWriter &null();
  bool ok() const { return ok_; }
  const char *c_str() const { return buf_; }
  size_t length() const { return len_; }

private:
  void raw(const char *s);
  void rawChar(char c);
  void valuePrefix();
  char *buf_;
  size_t cap_, len_ = 0;
  bool ok_ = true;
  uint8_t depth_ = 0;
  bool first_[16] = {};    // erstes Element auf dieser Ebene?
  uint16_t objMask_ = 0;   // Bit i gesetzt: Ebene i ist ein Objekt
  bool afterKey_ = false;
};

// Wert eines Cookies aus dem Cookie-Header (exakter Name, mehrere Cookies,
// Leerzeichen). false, wenn nicht vorhanden oder out zu klein.
bool cookieValue(const char *header, const char *name, char *out, size_t outSize);

// Vergleich in konstanter Zeit (bezogen auf die Laenge von b).
bool ctEquals(const char *a, const char *b);

// 128-Bit-Token als 32 Hex-Zeichen (klein) + NUL.
void tokenHex(const uint32_t words[4], char out[33]);

// Login-Bremse: nach MAX_FAILS Fehlversuchen LOCK_MS gesperrt, Erfolg setzt zurueck.
class LoginThrottle {
public:
  static constexpr int MAX_FAILS = 5;
  static constexpr uint32_t LOCK_MS = 30000;
  bool locked(uint32_t now) const;
  void failure(uint32_t now);
  void success();

private:
  int fails_ = 0;
  bool locked_ = false;
  uint32_t lockedSince_ = 0;
};

}  // namespace web
