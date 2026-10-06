// Unit-Tests fuer die Web-Helfer (JSON, Cookies, Token, Login-Bremse).
#include "check.h"
#include "web_core.h"
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace web;

// ── Hilfen ────────────────────────────────────────────────────────────────────

static const float NaN = std::numeric_limits<float>::quiet_NaN();
static const float INF = std::numeric_limits<float>::infinity();

// Schreibt mit fn in einen grossen Puffer; liefert Inhalt, ok in *okOut.
template <typename Fn>
static std::string json(Fn fn, bool *okOut = nullptr) {
  static char buf[4096];
  JsonWriter j(buf, sizeof(buf));
  fn(j);
  if (okOut) *okOut = j.ok();
  CHECK(j.length() == strlen(j.c_str()));
  CHECK(j.c_str() == buf);
  return std::string(j.c_str());
}

static std::string numStr(float v, int decimals) {
  return json([&](JsonWriter &j) { j.num(v, decimals); });
}

static std::string strStr(const char *s) {
  return json([&](JsonWriter &j) { j.str(s); });
}

// ── JsonWriter: Struktur ─────────────────────────────────────────────────────

static void testJsonEmpty() {
  bool ok = false;
  CHECK(json([](JsonWriter &j) { j.beginObject().endObject(); }, &ok) == "{}");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.beginArray().endArray(); }, &ok) == "[]");
  CHECK(ok);

  // Frischer Writer: leer, terminiert, ok
  char buf[8];
  memset(buf, 'x', sizeof(buf));
  JsonWriter j(buf, sizeof(buf));
  CHECK(j.ok());
  CHECK(j.length() == 0);
  CHECK(buf[0] == '\0');
  CHECK(strcmp(j.c_str(), "") == 0);
}

static void testJsonNested() {
  bool ok = false;
  std::string s = json(
      [](JsonWriter &j) {
        j.beginObject();
        j.key("a").integer(1);
        j.key("b").beginArray();
        j.integer(1).integer(-2).beginObject().endObject().beginArray().endArray();
        j.beginArray().str("x").null().endArray();
        j.endArray();
        j.key("c").beginObject().key("d").null().key("e").beginObject().key("f").flag(false).endObject().endObject();
        j.key("t").flag(true);
        j.key("f").flag(false);
        j.key("n").null();
        j.key("s").str("hi");
        j.key("u").uinteger(7);
        j.key("x").num(1.5f, 2);
        j.key("arr").beginArray().beginObject().key("k").integer(1).endObject().beginObject().key("k").integer(2).endObject().endArray();
        j.endObject();
      },
      &ok);
  CHECK(ok);
  CHECK(s ==
        "{\"a\":1,\"b\":[1,-2,{},[],[\"x\",null]],\"c\":{\"d\":null,\"e\":{\"f\":false}},"
        "\"t\":true,\"f\":false,\"n\":null,\"s\":\"hi\",\"u\":7,\"x\":1.50,"
        "\"arr\":[{\"k\":1},{\"k\":2}]}");

  // Arrays aus allen Werttypen: Kommas zwischen allen Elementen
  s = json(
      [](JsonWriter &j) {
        j.beginArray().integer(0).uinteger(1).num(2.25f, 2).flag(true).flag(false).null().str("").str(nullptr).num(NaN, 1).endArray();
      },
      &ok);
  CHECK(ok);
  CHECK(s == "[0,1,2.25,true,false,null,\"\",null,null]");

  // Objekt mit nur einem Schluessel: kein Komma
  CHECK(json([](JsonWriter &j) { j.beginObject().key("k").str("v").endObject(); }) == "{\"k\":\"v\"}");

  // Wie in web.cpp: Kette ueber mehrere Anweisungen
  char buf[128];
  JsonWriter j(buf, sizeof(buf));
  j.beginObject().key("ok").flag(false).key("error").str("Ungültige Zahl");
  j.key("field").str("goal");
  j.endObject();
  CHECK(j.ok());
  CHECK(strcmp(j.c_str(), "{\"ok\":false,\"error\":\"Ungültige Zahl\",\"field\":\"goal\"}") == 0);
}

static void testJsonRootScalars() {
  bool ok = false;
  CHECK(json([](JsonWriter &j) { j.str("x"); }, &ok) == "\"x\"");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.integer(-5); }, &ok) == "-5");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.uinteger(5); }, &ok) == "5");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.flag(true); }, &ok) == "true");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.null(); }, &ok) == "null");
  CHECK(ok);
  CHECK(json([](JsonWriter &j) { j.num(INF, 2); }, &ok) == "null");
  CHECK(ok);
}

static void testJsonDepth() {
  // Genau 16 Ebenen sind erlaubt
  bool ok = false;
  std::string s = json(
      [](JsonWriter &j) {
        for (int i = 0; i < 16; i++) j.beginArray();
        j.integer(1);
        for (int i = 0; i < 16; i++) j.endArray();
      },
      &ok);
  CHECK(ok);
  CHECK(s == std::string(16, '[') + "1" + std::string(16, ']'));

  // Gemischt Objekt/Array ueber 16 Ebenen, mit Geschwistern auf jeder Ebene
  s = json(
      [](JsonWriter &j) {
        for (int i = 0; i < 16; i++) {
          if (i % 2 == 0) j.beginObject().key("a").integer(i).key("n");
          else j.beginArray().integer(i);
        }
        j.null();
        for (int i = 15; i >= 0; i--) {
          if (i % 2 == 0) j.endObject();
          else j.integer(-i).endArray();
        }
      },
      &ok);
  CHECK(ok);
  std::string exp;
  for (int i = 0; i < 16; i++) {
    if (i % 2 == 0) exp += "{\"a\":" + std::to_string(i) + ",\"n\":";
    else exp += "[" + std::to_string(i) + ",";
  }
  exp += "null";
  for (int i = 15; i >= 0; i--) {
    if (i % 2 == 0) exp += "}";
    else exp += ",-" + std::to_string(i) + "]";
  }
  CHECK(s == exp);

  // 17. Ebene: Fehler, es wird nichts mehr geschrieben
  char buf[256];
  JsonWriter j(buf, sizeof(buf));
  for (int i = 0; i < 16; i++) j.beginArray();
  CHECK(j.ok());
  CHECK(j.length() == 16);
  j.beginArray();
  CHECK(!j.ok());
  CHECK(j.length() == 16);
  j.beginObject().integer(1).endArray();
  CHECK(!j.ok());
  CHECK(j.length() == 16);
  CHECK(strcmp(j.c_str(), std::string(16, '[').c_str()) == 0);

  // Auch als Objekt
  JsonWriter k(buf, sizeof(buf));
  for (int i = 0; i < 16; i++) k.beginObject().key("k");
  CHECK(k.ok());
  k.beginObject();
  CHECK(!k.ok());

  // Nach dem Schliessen wieder Platz fuer neue Ebenen
  s = json(
      [](JsonWriter &j) {
        j.beginArray();
        for (int r = 0; r < 3; r++) {
          for (int i = 0; i < 15; i++) j.beginArray();
          for (int i = 0; i < 15; i++) j.endArray();
        }
        j.endArray();
      },
      &ok);
  CHECK(ok);
  std::string one = std::string(15, '[') + std::string(15, ']');
  CHECK(s == "[" + one + "," + one + "," + one + "]");
}

// Falsche Nutzung: ok() false, danach wird nichts mehr geschrieben.
template <typename Fn>
static void expectMisuse(Fn fn, const char *expectedPrefix) {
  char buf[128];
  JsonWriter j(buf, sizeof(buf));
  fn(j);
  CHECK(!j.ok());
  CHECK(strcmp(j.c_str(), expectedPrefix) == 0);
  CHECK(j.length() == strlen(expectedPrefix));
  size_t len = j.length();
  j.beginObject().key("a").integer(1).endObject().beginArray().str("x").num(1.0f, 1).uinteger(1).flag(true).null().endArray();
  CHECK(!j.ok());
  CHECK(j.length() == len);
  CHECK(strcmp(j.c_str(), expectedPrefix) == 0);
}

static void testJsonMisuse() {
  // Wert im Objekt ohne key()
  expectMisuse([](JsonWriter &j) { j.beginObject().integer(1); }, "{");
  expectMisuse([](JsonWriter &j) { j.beginObject().key("a").integer(1).str("b"); }, "{\"a\":1");
  expectMisuse([](JsonWriter &j) { j.beginObject().beginArray(); }, "{");
  expectMisuse([](JsonWriter &j) { j.beginObject().beginObject(); }, "{");
  expectMisuse([](JsonWriter &j) { j.beginObject().null(); }, "{");
  expectMisuse([](JsonWriter &j) { j.beginObject().num(NaN, 1); }, "{");
  expectMisuse([](JsonWriter &j) { j.beginObject().str(nullptr); }, "{");
  // key() im Array oder auf oberster Ebene
  expectMisuse([](JsonWriter &j) { j.beginArray().key("a"); }, "[");
  expectMisuse([](JsonWriter &j) { j.key("a"); }, "");
  // Zwei Schluessel hintereinander
  expectMisuse([](JsonWriter &j) { j.beginObject().key("a").key("b"); }, "{\"a\":");
  // Schluessel ohne Wert vor dem Schliessen
  expectMisuse([](JsonWriter &j) { j.beginObject().key("a").endObject(); }, "{\"a\":");
  // key(nullptr)
  expectMisuse([](JsonWriter &j) { j.beginObject().key(nullptr); }, "{");
  // Falscher Abschluss
  expectMisuse([](JsonWriter &j) { j.beginArray().endObject(); }, "[");
  expectMisuse([](JsonWriter &j) { j.beginObject().endArray(); }, "{");
  expectMisuse([](JsonWriter &j) { j.endObject(); }, "");
  expectMisuse([](JsonWriter &j) { j.endArray(); }, "");
  expectMisuse([](JsonWriter &j) { j.beginArray().endArray().endArray(); }, "[]");
  // Zweiter Wurzelwert
  expectMisuse([](JsonWriter &j) { j.beginObject().endObject().beginObject(); }, "{}");
  expectMisuse([](JsonWriter &j) { j.integer(1).integer(2); }, "1");
  expectMisuse([](JsonWriter &j) { j.str("a").null(); }, "\"a\"");
  expectMisuse([](JsonWriter &j) { j.num(INF, 1).flag(true); }, "null");
}

// ── JsonWriter: Werte ─────────────────────────────────────────────────────────

static void testJsonIntegers() {
  CHECK(json([](JsonWriter &j) { j.integer(0); }) == "0");
  CHECK(json([](JsonWriter &j) { j.integer(7); }) == "7");
  CHECK(json([](JsonWriter &j) { j.integer(-1); }) == "-1");
  CHECK(json([](JsonWriter &j) { j.integer(10); }) == "10");
  CHECK(json([](JsonWriter &j) { j.integer(-100); }) == "-100");
  CHECK(json([](JsonWriter &j) { j.integer(INT32_MAX); }) == "2147483647");
  CHECK(json([](JsonWriter &j) { j.integer(INT32_MIN); }) == "-2147483648");
  CHECK(json([](JsonWriter &j) { j.uinteger(0); }) == "0");
  CHECK(json([](JsonWriter &j) { j.uinteger(1000000000u); }) == "1000000000");
  CHECK(json([](JsonWriter &j) { j.uinteger(UINT32_MAX); }) == "4294967295");
  CHECK(json([](JsonWriter &j) { j.uinteger(0x80000000u); }) == "2147483648");

  // Stichproben gegen snprintf
  uint32_t x = 0x9E3779B9u;
  for (int i = 0; i < 2000; i++) {
    x = x * 1664525u + 1013904223u;
    char exp[16];
    snprintf(exp, sizeof(exp), "%u", (unsigned)x);
    CHECK(json([&](JsonWriter &j) { j.uinteger(x); }) == exp);
    int32_t s = (int32_t)x;
    snprintf(exp, sizeof(exp), "%ld", (long)s);
    CHECK(json([&](JsonWriter &j) { j.integer(s); }) == exp);
  }
}

static void testJsonNumbers() {
  CHECK(numStr(0.0f, 0) == "0");
  CHECK(numStr(0.0f, 1) == "0.0");
  CHECK(numStr(12.34f, 1) == "12.3");
  CHECK(numStr(12.36f, 1) == "12.4");
  CHECK(numStr(12.34f, 2) == "12.34");
  CHECK(numStr(12.34f, 0) == "12");
  CHECK(numStr(99.96f, 1) == "100.0");
  CHECK(numStr(-3.76f, 1) == "-3.8");
  CHECK(numStr(-12.5f, 3) == "-12.500");
  CHECK(numStr(0.1f, 3) == "0.100");
  CHECK(numStr(1.0f, 6) == "1.000000");
  CHECK(numStr(0.000001f, 6) == "0.000001");
  CHECK(numStr(123456.0f, 0) == "123456");
  CHECK(numStr(1.2345f, 3) == "1.234" || numStr(1.2345f, 3) == "1.235");  // Float-Repraesentation
  CHECK(numStr(0.5f, 4) == "0.5000");

  // Alle Stellenzahlen 0..6
  const char *exp[] = { "3", "3.1", "3.14", "3.142", "3.1416", "3.14159", "3.141593" };
  for (int d = 0; d <= 6; d++) CHECK(numStr(3.14159265f, d) == exp[d]);

  // Ausserhalb 0..6 wird begrenzt
  CHECK(numStr(3.14159265f, -1) == "3");
  CHECK(numStr(3.14159265f, INT_MIN) == "3");
  CHECK(numStr(3.14159265f, 7) == "3.141593");
  CHECK(numStr(3.14159265f, 100) == "3.141593");
  CHECK(numStr(3.14159265f, INT_MAX) == "3.141593");

  // Keine "-0"
  CHECK(numStr(-0.0f, 1) == "0.0");
  CHECK(numStr(-0.0f, 0) == "0");
  CHECK(numStr(-0.04f, 1) == "0.0");
  CHECK(numStr(-0.4f, 0) == "0");
  CHECK(numStr(-0.0000001f, 6) == "0.000000");
  CHECK(numStr(-0.05f, 2) == "-0.05");
  CHECK(numStr(-0.6f, 0) == "-1");

  // Nicht endlich -> null
  CHECK(numStr(NaN, 1) == "null");
  CHECK(numStr(-NaN, 1) == "null");
  CHECK(numStr(INF, 1) == "null");
  CHECK(numStr(-INF, 0) == "null");
  CHECK(numStr(NaN, 100) == "null");

  // Extremwerte bleiben endlich und vollstaendig
  char exp6[80];
  snprintf(exp6, sizeof(exp6), "%.6f", (double)FLT_MAX);
  CHECK(numStr(FLT_MAX, 6) == exp6);
  snprintf(exp6, sizeof(exp6), "%.6f", (double)-FLT_MAX);
  CHECK(numStr(-FLT_MAX, 6) == exp6);
  CHECK(numStr(-FLT_MAX, 6).size() == 47);
  CHECK(numStr(FLT_MIN, 6) == "0.000000");
  CHECK(numStr(-FLT_MIN, 6) == "0.000000");
  CHECK(numStr(std::numeric_limits<float>::denorm_min(), 3) == "0.000");

  // In Objekten mit Kommas
  CHECK(json([](JsonWriter &j) {
          j.beginObject().key("a").num(NaN, 1).key("b").num(2.0f, 1).key("c").num(-INF, 1).endObject();
        }) == "{\"a\":null,\"b\":2.0,\"c\":null}");
}

static void testJsonStrings() {
  CHECK(strStr("") == "\"\"");
  CHECK(strStr("abc") == "\"abc\"");
  CHECK(strStr(nullptr) == "null");
  CHECK(strStr("a\"b") == "\"a\\\"b\"");
  CHECK(strStr("a\\b") == "\"a\\\\b\"");
  CHECK(strStr("\"\\\"") == "\"\\\"\\\\\\\"\"");
  CHECK(strStr("/") == "\"/\"");          // '/' bleibt
  CHECK(strStr("\x7f") == "\"\x7f\"");    // DEL ist kein Steuerzeichen < 0x20
  CHECK(strStr("a\nb") == "\"a\\u000ab\"");
  CHECK(strStr("\t\r") == "\"\\u0009\\u000d\"");
  CHECK(strStr("\x1f") == "\"\\u001f\"");
  CHECK(strStr("\x01x") == "\"\\u0001x\"");

  // Alle Steuerzeichen 0x01..0x1F
  for (int c = 1; c < 0x20; c++) {
    char in[2] = { (char)c, 0 };
    char exp[16];
    snprintf(exp, sizeof(exp), "\"\\u00%02x\"", c);
    CHECK(strStr(in) == exp);
  }
  // Alle anderen Bytes unveraendert (ausser " und \)
  for (int c = 0x20; c < 0x100; c++) {
    if (c == '"' || c == '\\') continue;
    char in[2] = { (char)c, 0 };
    std::string exp = "\"" + std::string(1, (char)c) + "\"";
    CHECK(strStr(in) == exp);
  }

  // UTF-8 bleibt Byte fuer Byte erhalten
  CHECK(strStr("Größe äöüß ÄÖÜ €") == "\"Größe äöüß ÄÖÜ €\"");
  CHECK(strStr("\xff\xfe\x80") == "\"\xff\xfe\x80\"");  // auch ungueltiges UTF-8

  // Schluessel werden ebenso escaped
  CHECK(json([](JsonWriter &j) { j.beginObject().key("a\"b\\c\n").str("ü").endObject(); }) ==
        "{\"a\\\"b\\\\c\\u000a\":\"ü\"}");
  CHECK(json([](JsonWriter &j) { j.beginObject().key("").integer(1).endObject(); }) == "{\"\":1}");
}

// ── JsonWriter: Ueberlauf ─────────────────────────────────────────────────────

// Festes Dokument mit allen Werttypen, Escapes und Verschachtelung.
static void buildDoc(JsonWriter &j) {
  j.beginObject();
  j.key("ok").flag(true);
  j.key("name").str("Wa\"ag\\e\n Größe");
  j.key("goal").num(123.45f, 2);
  j.key("nan").num(NaN, 1);
  j.key("neg").integer(INT32_MIN);
  j.key("big").uinteger(UINT32_MAX);
  j.key("list").beginArray().integer(1).null().flag(false).beginObject().key("k").str("").endObject().beginArray().endArray().endArray();
  j.key("deep").beginArray().beginArray().beginObject().key("x").num(-0.5f, 1).endObject().endArray().endArray();
  j.key("null").str(nullptr);
  j.endObject();
}

static const char DOC[] =
    "{\"ok\":true,\"name\":\"Wa\\\"ag\\\\e\\u000a Größe\",\"goal\":123.45,\"nan\":null,"
    "\"neg\":-2147483648,\"big\":4294967295,\"list\":[1,null,false,{\"k\":\"\"},[]],"
    "\"deep\":[[{\"x\":-0.5}]],\"null\":null}";

static void testJsonOverflow() {
  const size_t N = strlen(DOC);
  {
    std::vector<char> big(N + 64);
    JsonWriter j(big.data(), big.size());
    buildDoc(j);
    CHECK(j.ok());
    CHECK(strcmp(j.c_str(), DOC) == 0);
    CHECK(j.length() == N);
  }

  const char SENT = (char)0xA5;
  for (size_t cap = 1; cap <= N + 1; cap++) {
    // Exakt grosser Heap-Puffer: ASan meldet jeden Zugriff hinter cap
    std::unique_ptr<char[]> exact(new char[cap]);
    memset(exact.get(), SENT, cap);
    JsonWriter j(exact.get(), cap);
    buildDoc(j);
    if (cap <= N) {
      CHECK(!j.ok());
      // Alles, was passte: genau cap-1 Zeichen, Praefix des Dokuments
      CHECK(j.length() == cap - 1);
      CHECK(memcmp(exact.get(), DOC, cap - 1) == 0);
    } else {
      CHECK(j.ok());
      CHECK(j.length() == N);
      CHECK(strcmp(exact.get(), DOC) == 0);
    }
    CHECK(exact[j.length()] == '\0');
    CHECK(strlen(j.c_str()) == j.length());
    CHECK(j.c_str() == exact.get());

    // Waechterbytes hinter cap bleiben unberuehrt
    std::vector<char> guarded(cap + 16, SENT);
    JsonWriter g(guarded.data(), cap);
    buildDoc(g);
    CHECK(g.ok() == (cap > N));
    bool untouched = true;
    for (size_t i = cap; i < guarded.size(); i++) untouched = untouched && guarded[i] == SENT;
    CHECK(untouched);
    CHECK(memchr(guarded.data(), '\0', cap) == guarded.data() + g.length());
  }

  // Nach dem Ueberlauf passt ein kleiner Wert wieder, wird aber nicht geschrieben
  char buf[8];
  JsonWriter j(buf, sizeof(buf));
  j.beginArray().str("abcdefgh");
  CHECK(!j.ok());
  size_t len = j.length();
  CHECK(len == 7);
  CHECK(strcmp(buf, "[\"abcde") == 0);
  j.integer(1).endArray();
  CHECK(j.length() == len);
  CHECK(strcmp(buf, "[\"abcde") == 0);

  // Exakt passend: cap = Laenge + 1
  char b5[5];
  JsonWriter t(b5, sizeof(b5));
  t.flag(true);
  CHECK(t.ok());
  CHECK(strcmp(b5, "true") == 0);
  char b4[4];
  JsonWriter f(b4, sizeof(b4));
  f.flag(true);
  CHECK(!f.ok());
  CHECK(strcmp(b4, "tru") == 0);

  // cap = 1: nur NUL
  char b1[1] = { 'x' };
  JsonWriter one(b1, 1);
  CHECK(one.ok());
  CHECK(b1[0] == '\0');
  one.beginObject();
  CHECK(!one.ok());
  CHECK(b1[0] == '\0');
  CHECK(one.length() == 0);
}

static void testJsonNoBuffer() {
  // cap = 0: kein Byte wird beschrieben, c_str() ist trotzdem ""
  char b[4] = { 'x', 'y', 'z', 'w' };
  JsonWriter z(b, 0);
  CHECK(!z.ok());
  CHECK(z.c_str() != nullptr);
  CHECK(strcmp(z.c_str(), "") == 0);
  CHECK(z.length() == 0);
  z.beginObject().key("a").integer(1).endObject();
  CHECK(!z.ok());
  CHECK(z.length() == 0);
  CHECK(strcmp(z.c_str(), "") == 0);
  CHECK(b[0] == 'x' && b[1] == 'y' && b[2] == 'z' && b[3] == 'w');

  JsonWriter n(nullptr, 100);
  CHECK(!n.ok());
  CHECK(n.c_str() != nullptr);
  CHECK(strcmp(n.c_str(), "") == 0);
  n.str("abc").num(1.0f, 1);
  CHECK(!n.ok());
  CHECK(n.length() == 0);
  CHECK(strcmp(n.c_str(), "") == 0);
}

// ── cookieValue ───────────────────────────────────────────────────────────────

static const char SESS[] = "waage_session";

// Liefert Wert oder "<none>"
static std::string cookie(const char *header, const char *name = SESS, size_t outSize = 64) {
  std::vector<char> out(outSize + 8, (char)0xA5);
  bool found = cookieValue(header, name, out.data(), outSize);
  bool untouched = true;
  for (size_t i = outSize; i < out.size(); i++) untouched = untouched && out[i] == (char)0xA5;
  CHECK(untouched);
  if (outSize > 0) CHECK(memchr(out.data(), '\0', outSize) != nullptr);
  if (!found) {
    if (outSize > 0) CHECK(out[0] == '\0');
    return "<none>";
  }
  return std::string(out.data());
}

static void testCookieBasic() {
  CHECK(cookie("a=1; waage_session=abc; b=2") == "abc");
  CHECK(cookie("waage_session=abc; b=2") == "abc");
  CHECK(cookie("a=1; waage_session=abc") == "abc");
  CHECK(cookie("waage_session=abc") == "abc");
  CHECK(cookie("a=1;waage_session=abc;b=2") == "abc");
  CHECK(cookie("a=1; b=2") == "<none>");
  CHECK(cookie("") == "<none>");
  CHECK(cookie("a=1; waage_session=abc; b=2", "a") == "1");
  CHECK(cookie("a=1; waage_session=abc; b=2", "b") == "2");
  CHECK(cookie("a=1; waage_session=abc; b=2", "c") == "<none>");

  // Typischer Token
  CHECK(cookie("theme=dark; waage_session=0123456789abcdef0123456789abcdef") == "0123456789abcdef0123456789abcdef");
}

static void testCookieExactName() {
  CHECK(cookie("xwaage_session=bad") == "<none>");
  CHECK(cookie("waage_session_x=bad") == "<none>");
  CHECK(cookie("waage_sessio=bad") == "<none>");
  CHECK(cookie("WAAGE_SESSION=bad") == "<none>");
  CHECK(cookie("xwaage_session=bad; waage_session=good") == "good");
  CHECK(cookie("waage_session_x=bad; waage_session=good") == "good");
  CHECK(cookie("waage_session2=bad;waage_session=good;waage_session3=bad") == "good");
  // Name taucht nur im Wert eines anderen Cookies auf
  CHECK(cookie("a=waage_session=bad") == "<none>");
  CHECK(cookie("a=waage_session=bad; waage_session=good") == "good");
  CHECK(cookie("a=x waage_session=bad") == "<none>");
  // Eintraege ohne '=' sind keine Cookies
  CHECK(cookie("waage_session") == "<none>");
  CHECK(cookie("waage_session; a=1") == "<none>");
  CHECK(cookie("flag; waage_session=v") == "v");
  CHECK(cookie(";;; waage_session=v ;;") == "v");
  // Mehrfach: erster Treffer zaehlt
  CHECK(cookie("waage_session=first; waage_session=second") == "first");
}

static void testCookieWhitespace() {
  CHECK(cookie("  a=1 ;\twaage_session = abc  ; b=2") == "abc");
  CHECK(cookie("waage_session=abc   ") == "abc");
  CHECK(cookie("   waage_session=abc") == "abc");
  CHECK(cookie("waage_session\t=\tabc\t;") == "abc");
  CHECK(cookie("a=1 ;  waage_session=  abc") == "abc");
  CHECK(cookie("waage_session=a b") == "a b");  // innen bleibt
  // Leerzeichen im Namen gehoeren nicht dazu
  CHECK(cookie("waage _session=bad") == "<none>");
}

static void testCookieValues() {
  // Leerer Wert ist erlaubt
  CHECK(cookie("waage_session=") == "");
  CHECK(cookie("waage_session=; b=2") == "");
  CHECK(cookie("a=1; waage_session=") == "");
  CHECK(cookie("a=1; waage_session=   ; b=2") == "");
  CHECK(cookie("waage_session=", SESS, 1) == "");
  // Wert mit '='
  CHECK(cookie("waage_session=a=b") == "a=b");
  CHECK(cookie("waage_session==") == "=");
  // Wert endet an ';'
  CHECK(cookie("waage_session=abc;def") == "abc");
  CHECK(cookie("waage_session=abc;") == "abc");
  // UTF-8 / Anfuehrungszeichen unveraendert
  CHECK(cookie("waage_session=\"q\"") == "\"q\"");
  CHECK(cookie("waage_session=Größe") == "Größe");
}

static void testCookieOutSize() {
  // "abc" braucht 4 Bytes
  CHECK(cookie("waage_session=abc", SESS, 4) == "abc");
  CHECK(cookie("waage_session=abc", SESS, 3) == "<none>");
  CHECK(cookie("waage_session=abc", SESS, 2) == "<none>");
  CHECK(cookie("waage_session=abc", SESS, 1) == "<none>");
  CHECK(cookie("a=1; waage_session=abc; b=2", SESS, 4) == "abc");
  CHECK(cookie("a=1; waage_session=abc; b=2", SESS, 3) == "<none>");
  // 32-Zeichen-Token in 33 Bytes
  std::string tok(32, 'f');
  std::string h = "waage_session=" + tok;
  CHECK(cookie(h.c_str(), SESS, 33) == tok);
  CHECK(cookie(h.c_str(), SESS, 32) == "<none>");
  // Erster Treffer zu lang: false, auch wenn ein spaeterer Treffer passen wuerde
  CHECK(cookie("waage_session=zulang; waage_session=ok", SESS, 4) == "<none>");
  CHECK(cookie("waage_session=ok; waage_session=zulang", SESS, 4) == "ok");

  // outSize 0: nichts geschrieben
  char out[4] = { 'x', 'x', 'x', 'x' };
  CHECK(!cookieValue("waage_session=", SESS, out, 0));
  CHECK(out[0] == 'x');
  // Ungueltige Argumente
  CHECK(!cookieValue("waage_session=a", SESS, nullptr, 10));
  CHECK(!cookieValue(nullptr, SESS, out, sizeof(out)));
  CHECK(out[0] == '\0');
  CHECK(!cookieValue("waage_session=a", nullptr, out, sizeof(out)));
  CHECK(!cookieValue("=a", "", out, sizeof(out)));
  CHECK(!cookieValue("a=1", "", out, sizeof(out)));
  CHECK(out[0] == '\0');
}

// ── ctEquals / tokenHex ───────────────────────────────────────────────────────

static void testCtEquals() {
  CHECK(ctEquals("abc", "abc"));
  CHECK(ctEquals("", ""));
  CHECK(!ctEquals("abc", "abd"));
  CHECK(!ctEquals("xbc", "abc"));
  CHECK(!ctEquals("abc", "ab"));
  CHECK(!ctEquals("ab", "abc"));
  CHECK(!ctEquals("", "a"));
  CHECK(!ctEquals("a", ""));
  CHECK(!ctEquals("ABC", "abc"));
  CHECK(!ctEquals(nullptr, "abc"));
  CHECK(!ctEquals("abc", nullptr));
  CHECK(!ctEquals(nullptr, nullptr));
  CHECK(ctEquals("Größe", "Größe"));
  CHECK(!ctEquals("Größe", "Grösse"));

  const char *tok = "0123456789abcdef0123456789abcdef";
  CHECK(ctEquals(tok, tok));
  std::string copy(tok);
  CHECK(ctEquals(copy.c_str(), tok));
  // Jede einzelne abweichende Stelle wird erkannt
  for (size_t i = 0; i < copy.size(); i++) {
    std::string m = copy;
    m[i] = m[i] == 'x' ? 'y' : 'x';
    CHECK(!ctEquals(m.c_str(), tok));
    CHECK(!ctEquals(tok, m.c_str()));
    // Abweichung nur im hoechsten Bit
    m = copy;
    m[i] = (char)(m[i] ^ 0x80);
    CHECK(!ctEquals(m.c_str(), tok));
  }
  // Jedes Praefix und jede Verlaengerung ist ungleich
  for (size_t n = 0; n < copy.size(); n++) {
    std::string p = copy.substr(0, n);
    CHECK(!ctEquals(p.c_str(), tok));
    CHECK(!ctEquals(tok, p.c_str()));
  }
  std::string longer = copy + "0";
  CHECK(!ctEquals(longer.c_str(), tok));
  CHECK(!ctEquals(tok, longer.c_str()));

  // a wird nicht ueber sein Ende hinaus gelesen (ASan): exakt grosser Puffer
  std::unique_ptr<char[]> shortA(new char[2]);
  shortA[0] = '0';
  shortA[1] = '\0';
  CHECK(!ctEquals(shortA.get(), tok));
  std::unique_ptr<char[]> emptyA(new char[1]);
  emptyA[0] = '\0';
  CHECK(!ctEquals(emptyA.get(), tok));
}

static void testTokenHex() {
  char out[33];
  const uint32_t w1[4] = { 0x01234567u, 0x89abcdefu, 0x00000000u, 0xffffffffu };
  memset(out, 'x', sizeof(out));
  tokenHex(w1, out);
  CHECK(strcmp(out, "0123456789abcdef00000000ffffffff") == 0);
  CHECK(out[32] == '\0');

  const uint32_t w0[4] = { 0, 0, 0, 0 };
  tokenHex(w0, out);
  CHECK(strcmp(out, "00000000000000000000000000000000") == 0);

  const uint32_t w2[4] = { 0xDEADBEEFu, 0x0000000Au, 0xA0000000u, 0x00010000u };
  tokenHex(w2, out);
  CHECK(strcmp(out, "deadbeef0000000aa000000000010000") == 0);

  // Exakt 33 Bytes (ASan), nur Kleinbuchstaben/Ziffern, verschiedene Woerter -> verschiedene Token
  std::set<std::string> seen;
  uint32_t x = 0x12345678u;
  for (int i = 0; i < 500; i++) {
    uint32_t w[4];
    for (int k = 0; k < 4; k++) {
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      w[k] = x;
    }
    std::unique_ptr<char[]> o(new char[33]);
    tokenHex(w, o.get());
    CHECK(strlen(o.get()) == 32);
    bool hex = true;
    for (int k = 0; k < 32; k++) {
      char c = o[k];
      hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    CHECK(hex);
    char exp[40];
    snprintf(exp, sizeof(exp), "%08x%08x%08x%08x", (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3]);
    CHECK(strcmp(o.get(), exp) == 0);
    seen.insert(o.get());
  }
  CHECK(seen.size() == 500);
}

// ── LoginThrottle ─────────────────────────────────────────────────────────────

static void failN(LoginThrottle &t, int n, uint32_t now, uint32_t step = 1000) {
  for (int i = 0; i < n; i++) t.failure(now + (uint32_t)i * step);
}

static void testThrottleConstants() {
  CHECK(LoginThrottle::MAX_FAILS == 5);
  CHECK(LoginThrottle::LOCK_MS == 30000);
}

static void testThrottleBasic() {
  LoginThrottle t;
  CHECK(!t.locked(0));
  CHECK(!t.locked(123456));
  CHECK(!t.locked(UINT32_MAX));

  // 4 Fehlversuche sperren nicht
  failN(t, 4, 1000);
  CHECK(!t.locked(5000));
  CHECK(!t.locked(1000000));
  // 5. sperrt ab sofort fuer LOCK_MS
  t.failure(10000);
  CHECK(t.locked(10000));
  CHECK(t.locked(10001));
  CHECK(t.locked(10000 + LoginThrottle::LOCK_MS - 1));
  CHECK(!t.locked(10000 + LoginThrottle::LOCK_MS));
  CHECK(!t.locked(10000 + 3600000));

  // Nach Ablauf zaehlt es neu: 4 Fehler sperren nicht, der 5. schon
  uint32_t t0 = 10000 + LoginThrottle::LOCK_MS;
  failN(t, 4, t0);
  CHECK(!t.locked(t0 + 4000));
  t.failure(t0 + 5000);
  CHECK(t.locked(t0 + 5000));
  CHECK(!t.locked(t0 + 5000 + LoginThrottle::LOCK_MS));
}

static void testThrottleSuccess() {
  LoginThrottle t;
  // Erfolg setzt den Zaehler zurueck
  failN(t, 4, 0);
  t.success();
  failN(t, 4, 10000);
  CHECK(!t.locked(20000));
  t.failure(20000);
  CHECK(t.locked(20000));

  // Erfolg hebt auch eine Sperre auf
  t.success();
  CHECK(!t.locked(20001));
  failN(t, 4, 20002);
  CHECK(!t.locked(30000));

  // Erfolg ohne vorherige Fehler: harmlos
  LoginThrottle u;
  u.success();
  CHECK(!u.locked(0));
  failN(u, 5, 0);
  CHECK(u.locked(4000));
}

static void testThrottleNoExtend() {
  // Fehler waehrend der Sperre verlaengern sie nicht
  LoginThrottle t;
  failN(t, 5, 0, 0);
  CHECK(t.locked(0));
  for (uint32_t now = 1000; now < LoginThrottle::LOCK_MS; now += 1000) t.failure(now);
  t.failure(LoginThrottle::LOCK_MS - 1);
  CHECK(t.locked(LoginThrottle::LOCK_MS - 1));
  CHECK(!t.locked(LoginThrottle::LOCK_MS));
  // ...und zaehlen auch nicht fuer danach
  t.failure(LoginThrottle::LOCK_MS);
  CHECK(!t.locked(LoginThrottle::LOCK_MS));
  failN(t, 3, LoginThrottle::LOCK_MS + 1);
  CHECK(!t.locked(LoginThrottle::LOCK_MS + 5000));
  t.failure(LoginThrottle::LOCK_MS + 6000);
  CHECK(t.locked(LoginThrottle::LOCK_MS + 6000));

  // Fehler lange nach den ersten zaehlen trotzdem (kein Zeitfenster)
  LoginThrottle u;
  failN(u, 4, 0, 3600000);
  CHECK(!u.locked(4 * 3600000u));
  u.failure(5 * 3600000u);
  CHECK(u.locked(5 * 3600000u));
}

static void testThrottleWrap() {
  // Sperre ueber den Ueberlauf von millis()
  const uint32_t start = 0xFFFFF000u;
  LoginThrottle t;
  failN(t, 4, start, 100);
  CHECK(!t.locked(start + 400));
  t.failure(start + 500);  // 0xFFFFF1F4
  const uint32_t at = start + 500;
  CHECK(t.locked(at));
  CHECK(t.locked(0xFFFFFFFFu));
  CHECK(t.locked(0));
  CHECK(t.locked(1000));
  CHECK(t.locked(at + LoginThrottle::LOCK_MS - 1));  // nach dem Ueberlauf
  CHECK(at + LoginThrottle::LOCK_MS < at);           // Test liegt wirklich ueber der Grenze
  CHECK(!t.locked(at + LoginThrottle::LOCK_MS));
  CHECK(!t.locked(at + LoginThrottle::LOCK_MS + 100000));
  // Vor der Sperrzeit (aus Sicht der Differenz: weit in der Zukunft) gesperrt? Nein.
  CHECK(!t.locked(at - 1));

  // Fehlversuche verteilt ueber die Grenze
  LoginThrottle u;
  failN(u, 5, 0xFFFFFFF0u, 4);  // ..F0, F4, F8, FC, 0x00
  CHECK(u.locked(0));
  CHECK(u.locked(LoginThrottle::LOCK_MS - 1));
  CHECK(!u.locked(LoginThrottle::LOCK_MS));

  // Sperre beginnt genau bei 0xFFFFFFFF
  LoginThrottle v;
  failN(v, 5, 0xFFFFFFFFu, 0);
  CHECK(v.locked(0xFFFFFFFFu));
  CHECK(v.locked(LoginThrottle::LOCK_MS - 2));
  CHECK(!v.locked(LoginThrottle::LOCK_MS - 1));
}

// ── Zufallstests gegen Referenzmodelle (deterministisch) ─────────────────────

static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

// Unabhaengige Referenz fuer das Escaping
static std::string quoteRef(const char *s) {
  std::string r = "\"";
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"') r += "\\\"";
    else if (c == '\\') r += "\\\\";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof(b), "\\u%04x", c);
      r += b;
    } else r += (char)c;
  }
  return r + "\"";
}

// Referenz fuer num(): Stellen begrenzt, "-0..." ohne Vorzeichen
static std::string numRef(float v, int d) {
  if (!std::isfinite(v)) return "null";
  d = d < 0 ? 0 : (d > 6 ? 6 : d);
  char b[80];
  snprintf(b, sizeof(b), "%.*f", d, (double)v);
  if (b[0] == '-' && strtod(b, nullptr) == 0.0) return std::string(b + 1);
  return b;
}

// Referenzmodell des Writers: erwartete Ausgabe und ob die Nutzung gueltig war.
struct JsonModel {
  std::string out;
  bool ok = true;
  std::vector<char> st;  // 'o' oder 'a'
  std::vector<bool> first;
  bool afterKey = false, rootDone = false;

  bool prefix() {
    if (st.empty()) return !rootDone;
    if (st.back() == 'o') {
      if (!afterKey) return false;
      afterKey = false;
      return true;
    }
    if (!first.back()) out += ',';
    first.back() = false;
    return true;
  }
  void value(const std::string &t) {
    if (!ok) return;
    bool root = st.empty();
    if (!prefix()) {
      ok = false;
      return;
    }
    out += t;
    if (root) rootDone = true;
  }
  void open(char t) {
    if (!ok) return;
    if (st.size() >= 16) {
      ok = false;
      return;
    }
    bool root = st.empty();
    if (!prefix()) {
      ok = false;
      return;
    }
    out += t == 'o' ? '{' : '[';
    st.push_back(t);
    first.push_back(true);
    if (root) rootDone = true;
  }
  void close(char t) {
    if (!ok) return;
    if (st.empty() || st.back() != t || afterKey) {
      ok = false;
      return;
    }
    out += t == 'o' ? '}' : ']';
    st.pop_back();
    first.pop_back();
  }
  void key(const char *k) {
    if (!ok) return;
    if (!k || st.empty() || st.back() != 'o' || afterKey) {
      ok = false;
      return;
    }
    if (!first.back()) out += ',';
    first.back() = false;
    out += quoteRef(k) + ":";
    afterKey = true;
  }
};

enum OpKind { OBeginObj, OEndObj, OBeginArr, OEndArr, OKey, OStr, ONum, OInt, OUint, OFlag, ONull, OKinds };

struct JsonOp {
  OpKind kind;
  std::string s;
  bool isNull;
  float f;
  int d;
  int32_t i;
  uint32_t u;
};

static std::string randText() {
  static const unsigned char alpha[] = { 'a', 'Z', '0', ' ', '"', '\\', '/', '\n', '\t', 0x01, 0x1f, 0x7f, 0x80, 0xc3, 0xbc, 0xff, ':', ',', '{', ']' };
  int n = (int)(rnd() % 6);
  std::string s;
  for (int i = 0; i < n; i++) s += (char)alpha[rnd() % sizeof(alpha)];
  return s;
}

static float randFloat() {
  static const float special[] = { 0.0f, -0.0f, NaN, -NaN, INF, -INF, 1e-30f, -1e-30f, FLT_MAX, -FLT_MAX, 0.5f, -0.5f, 0.05f, -0.05f, 99.95f, -0.004f, -21.3456f, 2.5f, -2.5f, FLT_MIN };
  uint32_t r = rnd() % 3;
  if (r == 0) return special[rnd() % (sizeof(special) / sizeof(special[0]))];
  if (r == 1) {
    uint32_t bits = rnd();  // beliebige Bitmuster: NaN, Inf, Denormale, -0
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
  }
  return (float)((int)(rnd() % 2000001) - 1000000) / 1000.0f;
}

static JsonOp randJsonOp(OpKind kind) {
  JsonOp o{};
  o.kind = kind;
  switch (kind) {
    case OKey: o.isNull = rnd() % 25 == 0; o.s = randText(); break;
    case OStr: o.isNull = rnd() % 10 == 0; o.s = randText(); break;
    case ONum: o.f = randFloat(); o.d = (int)(rnd() % 12) - 3; break;
    case OInt: o.i = (int32_t)rnd(); break;
    case OUint: o.u = rnd(); break;
    case OFlag: o.i = (int32_t)(rnd() % 2); break;
    default: break;
  }
  return o;
}

static const OpKind VALUE_KINDS[] = { OBeginObj, OBeginArr, OStr, ONum, OInt, OUint, OFlag, ONull };

// Meist ein im Modellzustand gueltiger Schritt, manchmal ein beliebiger.
static JsonOp nextJsonOp(const JsonModel &m, bool allowMisuse) {
  if (allowMisuse && rnd() % 12 == 0) return randJsonOp((OpKind)(rnd() % OKinds));
  bool inObj = !m.st.empty() && m.st.back() == 'o';
  if (inObj && !m.afterKey) return randJsonOp(rnd() % 4 == 0 ? OEndObj : OKey);
  if (!m.st.empty() && !inObj && rnd() % 5 == 0) return randJsonOp(OEndArr);
  OpKind k = VALUE_KINDS[rnd() % (sizeof(VALUE_KINDS) / sizeof(VALUE_KINDS[0]))];
  if ((k == OBeginObj || k == OBeginArr) && m.st.size() >= 16 && rnd() % 4 != 0) k = ONull;
  return randJsonOp(k);
}

static void applyJson(JsonWriter &j, const JsonOp &o) {
  switch (o.kind) {
    case OBeginObj: j.beginObject(); break;
    case OEndObj: j.endObject(); break;
    case OBeginArr: j.beginArray(); break;
    case OEndArr: j.endArray(); break;
    case OKey: j.key(o.isNull ? nullptr : o.s.c_str()); break;
    case OStr: j.str(o.isNull ? nullptr : o.s.c_str()); break;
    case ONum: j.num(o.f, o.d); break;
    case OInt: j.integer(o.i); break;
    case OUint: j.uinteger(o.u); break;
    case OFlag: j.flag(o.i != 0); break;
    case ONull: j.null(); break;
    case OKinds: break;
  }
}

static void applyModel(JsonModel &m, const JsonOp &o) {
  switch (o.kind) {
    case OBeginObj: m.open('o'); break;
    case OEndObj: m.close('o'); break;
    case OBeginArr: m.open('a'); break;
    case OEndArr: m.close('a'); break;
    case OKey: m.key(o.isNull ? nullptr : o.s.c_str()); break;
    case OStr: m.value(o.isNull ? "null" : quoteRef(o.s.c_str())); break;
    case ONum: m.value(numRef(o.f, o.d)); break;
    case OInt: m.value(std::to_string(o.i)); break;
    case OUint: m.value(std::to_string(o.u)); break;
    case OFlag: m.value(o.i ? "true" : "false"); break;
    case ONull: m.value("null"); break;
    case OKinds: break;
  }
}

// Strenger Mini-Parser (RFC 8259 ohne Leerraum): ist s genau ein JSON-Wert?
struct JsonCheck {
  const char *p;
  bool value(int depth) {
    if (depth > 32) return false;
    if (*p == '{' || *p == '[') {
      char close = *p == '{' ? '}' : ']';
      bool obj = *p++ == '{';
      if (*p == close) {
        p++;
        return true;
      }
      for (;;) {
        if (obj && (!string() || *p++ != ':')) return false;
        if (!value(depth + 1)) return false;
        if (*p == ',') {
          p++;
          continue;
        }
        return *p++ == close;
      }
    }
    if (*p == '"') return string();
    if (!strncmp(p, "true", 4) || !strncmp(p, "null", 4)) return p += 4, true;
    if (!strncmp(p, "false", 5)) return p += 5, true;
    return number();
  }
  bool string() {
    if (*p++ != '"') return false;
    for (;;) {
      unsigned char c = (unsigned char)*p++;
      if (c < 0x20) return false;  // auch NUL: unterminiert
      if (c == '"') return true;
      if (c != '\\') continue;
      char e = *p++;
      if (e == 'u') {
        for (int i = 0; i < 4; i++)
          if (!isxdigit((unsigned char)*p++)) return false;
      } else if (!e || !strchr("\"\\/bfnrt", e)) {
        return false;
      }
    }
  }
  bool number() {
    if (*p == '-') p++;
    if (*p == '0') p++;
    else if (*p >= '1' && *p <= '9') {
      while (isdigit((unsigned char)*p)) p++;
    } else return false;
    if (*p == '.') {
      p++;
      if (!isdigit((unsigned char)*p)) return false;
      while (isdigit((unsigned char)*p)) p++;
    }
    return true;
  }
};

static bool validJson(const std::string &s) {
  JsonCheck c{ s.c_str() };
  return c.value(0) && *c.p == '\0';
}

static void testJsonValidator() {
  // Der Pruefer selbst muss Fehler erkennen, sonst beweist er nichts
  CHECK(validJson("{\"a\":[1,-2.50,null,true,false,\"x\\u001f\"]}"));
  CHECK(validJson("0"));
  CHECK(!validJson("{\"a\":1,}"));
  CHECK(!validJson("[1 2]"));
  CHECK(!validJson("[1,]"));
  CHECK(!validJson("{\"a\"}"));
  CHECK(!validJson("{1:2}"));
  CHECK(!validJson("\"a\nb\""));
  CHECK(!validJson("-0.0e"));
  CHECK(!validJson("01"));
  CHECK(!validJson("[1]]"));
  CHECK(!validJson("1."));
  CHECK(!validJson("nan"));
  CHECK(!validJson("\"\\x\""));
}

static void testJsonModelFuzz() {
  int complete = 0, misuse = 0;
  for (int iter = 0; iter < 6000; iter++) {
    std::vector<JsonOp> ops;
    JsonModel m;
    bool allowMisuse = iter % 3 == 0;
    int n = 1 + (int)(rnd() % 48);
    for (int k = 0; k < n && (allowMisuse || !(m.rootDone && m.st.empty())); k++) {
      ops.push_back(nextJsonOp(m, allowMisuse));
      applyModel(m, ops.back());
    }
    // Meist sauber abschliessen, damit viele vollstaendige Dokumente entstehen
    if (iter % 4 != 0) {
      while (m.ok && !m.st.empty()) {
        if (m.st.back() == 'o' && m.afterKey) ops.push_back(randJsonOp(ONull));
        else ops.push_back(randJsonOp(m.st.back() == 'o' ? OEndObj : OEndArr));
        applyModel(m, ops.back());
      }
    }
    if (!m.ok) misuse++;

    // Grosser Puffer: exakt wie das Modell
    std::vector<char> big(8192);
    JsonWriter j(big.data(), big.size());
    for (const JsonOp &o : ops) applyJson(j, o);
    CHECK(j.ok() == m.ok);
    CHECK(j.length() == m.out.size());
    CHECK(std::string(j.c_str()) == m.out);
    if (m.ok && m.st.empty() && m.rootDone) {
      complete++;
      CHECK(validJson(j.c_str()));
    }

    // Kleine Puffer: Praefix der Ausgabe, terminiert, nie hinter cap
    const size_t L = m.out.size();
    for (int t = 0; t < 3; t++) {
      size_t cap = t == 0 ? L + 1 : (t == 1 ? (L > 0 ? L : 1) : 1 + rnd() % (L + 1));
      std::unique_ptr<char[]> exact(new char[cap]);  // ASan: exakt cap Bytes
      std::vector<char> guarded(cap + 8, (char)0x5A);
      JsonWriter a(exact.get(), cap), g(guarded.data(), cap);
      for (const JsonOp &o : ops) {
        applyJson(a, o);
        applyJson(g, o);
      }
      size_t want = L < cap - 1 ? L : cap - 1;
      CHECK(a.length() == want);
      CHECK(a.ok() == (m.ok && L <= cap - 1));
      CHECK(memcmp(exact.get(), m.out.data(), want) == 0);
      CHECK(exact[want] == '\0');
      bool untouched = true;
      for (size_t i = cap; i < guarded.size(); i++) untouched = untouched && guarded[i] == (char)0x5A;
      CHECK(untouched);
      CHECK(g.length() == want);
    }
  }
  // Der Generator muss beide Faelle reichlich liefern
  CHECK(complete > 2000);
  CHECK(misuse > 200);
}

// Muster wie in web.cpp/app.cpp: Schluessel und Wert in getrennten Aufrufen,
// verschachteltes Objekt aus einer Hilfsfunktion.
static void writeInner(JsonWriter &j, bool asNull) {
  if (asNull) {
    j.null();
    return;
  }
  j.beginObject();
  j.key("goal").num(100.0f, 1);
  j.key("scaleMode").str("Game");
  j.endObject();
}

static void testJsonWebPatterns() {
  for (int variant = 0; variant < 2; variant++) {
    char buf[256];
    JsonWriter j(buf, sizeof(buf));
    j.beginObject();
    j.key("ok").flag(true);
    j.key("weight");
    if (variant == 0) j.num(12.345f, 2);
    else j.null();
    j.key("config");
    writeInner(j, variant == 1);
    j.key("battery");
    writeInner(j, variant == 0);
    j.endObject();
    CHECK(j.ok());
    if (variant == 0) {
      CHECK(strcmp(buf, "{\"ok\":true,\"weight\":12.35,\"config\":{\"goal\":100.0,\"scaleMode\":\"Game\"},\"battery\":null}") == 0);
    } else {
      CHECK(strcmp(buf, "{\"ok\":true,\"weight\":null,\"config\":null,\"battery\":{\"goal\":100.0,\"scaleMode\":\"Game\"}}") == 0);
    }
    CHECK(validJson(buf));
  }
}

static void testJsonNumbersMore() {
  // Negativer Kalibrierfaktor (Waegezelle verkehrt herum) bleibt negativ
  CHECK(numStr(-21.3456f, 4) == "-21.3456");
  CHECK(numStr(-0.0001f, 4) == "-0.0001");
  CHECK(numStr(-0.00004f, 4) == "0.0000");
  CHECK(numStr(-1.0f, 0) == "-1");
  CHECK(numStr(-1e-30f, 6) == "0.000000");
  CHECK(numStr(16777217.0f, 0) == "16777216");  // Float-Genauigkeit, keine Exponentenschreibweise
  CHECK(numStr(1e10f, 1) == "10000000000.0");

  // Eigenschaften fuer viele Werte: wie printf, nie "-0...", nie Exponent, gueltiges JSON
  for (int i = 0; i < 20000; i++) {
    float v = randFloat();
    int d = (int)(rnd() % 10) - 2;
    std::string s = numStr(v, d);
    CHECK(s == numRef(v, d));
    if (!std::isfinite(v)) continue;
    CHECK(validJson(s));
    CHECK(s.find_first_of("eE") == std::string::npos);
    if (s[0] == '-') CHECK(s.find_first_of("123456789") != std::string::npos);
    int dc = d < 0 ? 0 : (d > 6 ? 6 : d);
    size_t dot = s.find('.');
    if (dc == 0) CHECK(dot == std::string::npos);
    else CHECK(dot != std::string::npos && s.size() - dot - 1 == (size_t)dc);
  }
}

// Unabhaengige Referenz fuer cookieValue (std::string)
static bool cookieRef(const std::string &h, const std::string &name, std::string *val) {
  auto trim = [](std::string s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
  };
  size_t pos = 0;
  for (;;) {
    size_t e = h.find(';', pos);
    std::string seg = h.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    size_t eq = seg.find('=');
    if (eq != std::string::npos && trim(seg.substr(0, eq)) == name) {
      *val = trim(seg.substr(eq + 1));
      return true;
    }
    if (e == std::string::npos) return false;
    pos = e + 1;
  }
}

static std::string randWs() {
  static const char *ws[] = { "", "", " ", "  ", "\t", " \t" };
  return ws[rnd() % 6];
}

static void testCookieFuzz() {
  static const char *names[] = { "n", "nm", "mn", "n m", "nmn" };
  static const char valChars[] = { 'v', 'x', '=', ' ', '\t', 'n', '"' };
  static const char noise[] = { 'n', 'm', '=', ';', ' ', '\t', 'v', 'x' };
  int hits = 0, misses = 0, tooSmall = 0;
  for (int it = 0; it < 60000; it++) {
    const char *name = names[rnd() % 5];
    std::string h;
    if (rnd() % 4 == 0) {
      // Reines Rauschen
      int len = (int)(rnd() % 16);
      for (int i = 0; i < len; i++) h += noise[rnd() % sizeof(noise)];
    } else {
      // Aus Eintraegen zusammengesetzt: Ziel, Nachbarnamen, Praefix/Suffix
      int segs = (int)(rnd() % 5);
      for (int sgi = 0; sgi < segs; sgi++) {
        if (sgi > 0) h += rnd() % 2 ? ";" : "; ";
        std::string nm;
        switch (rnd() % 5) {
          case 0:
          case 1: nm = name; break;
          case 2: nm = std::string("x") + name; break;
          case 3: nm = std::string(name) + "x"; break;
          default: nm = names[rnd() % 5]; break;
        }
        h += randWs() + nm + randWs();
        if (rnd() % 8 != 0) {
          h += "=" + randWs();
          int vl = (int)(rnd() % 6);
          for (int i = 0; i < vl; i++) h += valChars[rnd() % sizeof(valChars)];
        }
      }
    }
    std::string exp;
    bool present = cookieRef(h, name, &exp);
    size_t outSize = 1 + rnd() % 8;
    // Exakt grosse Puffer (ASan) fuer Header und Ausgabe
    std::unique_ptr<char[]> hb(new char[h.size() + 1]);
    memcpy(hb.get(), h.c_str(), h.size() + 1);
    std::unique_ptr<char[]> out(new char[outSize]);
    memset(out.get(), 'Q', outSize);
    bool got = cookieValue(hb.get(), name, out.get(), outSize);
    bool want = present && exp.size() + 1 <= outSize;
    CHECK(got == want);
    if (got) {
      hits++;
      CHECK(std::string(out.get()) == exp);
    } else {
      if (present) tooSmall++;
      else misses++;
      CHECK(out[0] == '\0');
    }
  }
  CHECK(hits > 10000);
  CHECK(misses > 10000);
  CHECK(tooSmall > 1000);
}

static void testCtEqualsFuzz() {
  static const char alpha[] = { 'a', 'b', (char)0x80, (char)0xff };
  for (int it = 0; it < 60000; it++) {
    std::string s[2];
    for (std::string &x : s) {
      int n = (int)(rnd() % 5);
      for (int i = 0; i < n; i++) x += alpha[rnd() % sizeof(alpha)];
    }
    if (rnd() % 4 == 0) s[1] = s[0];
    std::unique_ptr<char[]> a(new char[s[0].size() + 1]), b(new char[s[1].size() + 1]);
    memcpy(a.get(), s[0].c_str(), s[0].size() + 1);
    memcpy(b.get(), s[1].c_str(), s[1].size() + 1);
    CHECK(ctEquals(a.get(), b.get()) == (s[0] == s[1]));
    CHECK(ctEquals(b.get(), a.get()) == (s[0] == s[1]));
  }
}

// Referenz mit 64-Bit-Zeit (kein Ueberlauf) gegen die 32-Bit-Implementierung.
struct ThrottleRef {
  int fails = 0;
  bool lockActive = false;
  uint64_t since = 0;
  bool locked(uint64_t now) const { return lockActive && now - since < LoginThrottle::LOCK_MS; }
  void failure(uint64_t now) {
    if (locked(now)) return;
    if (lockActive) {
      lockActive = false;
      fails = 0;
    }
    if (++fails >= LoginThrottle::MAX_FAILS) {
      lockActive = true;
      since = now;
    }
  }
  void success() {
    fails = 0;
    lockActive = false;
  }
};

static void testThrottleModel() {
  int lockedSeen = 0, wrapsSeen = 0;
  for (int run = 0; run < 200; run++) {
    // Start kurz vor dem Ueberlauf (0xFFFFF000 und frueher)
    uint64_t now = 0xFFFFF000ull - (uint64_t)(rnd() % 200000);
    LoginThrottle t;
    ThrottleRef r;
    for (int step = 0; step < 300; step++) {
      uint32_t k = rnd() % 10;
      if (k < 6) {
        t.failure((uint32_t)now);
        r.failure(now);
      } else if (k == 6) {
        t.success();
        r.success();
      }
      CHECK(t.locked((uint32_t)now) == r.locked(now));
      if (r.locked(now)) lockedSeen++;
      // Zeit laeuft, gerne ueber die Sperrgrenze hinweg
      uint32_t dt = rnd() % 4 == 0 ? LoginThrottle::LOCK_MS - 1 + rnd() % 3 : rnd() % 3000;
      uint64_t before = now;
      now += dt;
      if ((uint32_t)now < (uint32_t)before) wrapsSeen++;
      CHECK(t.locked((uint32_t)now) == r.locked(now));
    }
  }
  CHECK(lockedSeen > 1000);
  CHECK(wrapsSeen >= 150);

  // Eine abgelaufene Sperre wird durch failure()/success() geloescht und
  // taucht auch nach einem vollen 32-Bit-Umlauf (49,7 Tage) nicht wieder auf.
  const uint32_t at = 0xFFFFF000u;
  LoginThrottle a;
  failN(a, 5, at, 0);
  CHECK(a.locked(at));
  a.failure(at + LoginThrottle::LOCK_MS);  // Sperre vorbei: zaehlt als 1. neuer Fehler
  CHECK(!a.locked(at + LoginThrottle::LOCK_MS));
  CHECK(!a.locked(at));       // gleiche 32-Bit-Zeit wie der Sperrbeginn, aber eine Runde spaeter
  CHECK(!a.locked(at + 10));
  LoginThrottle b;
  failN(b, 5, at, 0);
  b.success();
  CHECK(!b.locked(at));
  CHECK(!b.locked(at + 10));
}

int main() {
  testJsonEmpty();
  testJsonNested();
  testJsonRootScalars();
  testJsonDepth();
  testJsonMisuse();
  testJsonIntegers();
  testJsonNumbers();
  testJsonStrings();
  testJsonOverflow();
  testJsonNoBuffer();
  testCookieBasic();
  testCookieExactName();
  testCookieWhitespace();
  testCookieValues();
  testCookieOutSize();
  testCtEquals();
  testTokenHex();
  testThrottleConstants();
  testThrottleBasic();
  testThrottleSuccess();
  testThrottleNoExtend();
  testThrottleWrap();
  testJsonValidator();
  testJsonModelFuzz();
  testJsonWebPatterns();
  testJsonNumbersMore();
  testCookieFuzz();
  testCtEqualsFuzz();
  testThrottleModel();
  return finish("web_core_test");
}
