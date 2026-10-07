#include "glass_core.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace glass {

namespace {

constexpr uint8_t BLOB_VERSION = 1;
constexpr float SAME_EPS = 0.05f; // Export schreibt eine Nachkommastelle

// Laenge eines erlaubten Zeichens an s (0 = nicht erlaubt)
int allowedChar(const unsigned char *s) {
  if (s[0] >= 0x20 && s[0] <= 0x7E)
    return 1;
  if (s[0] == 0xC3) {
    switch (s[1]) {
    case 0xA4: // ä
    case 0xB6: // ö
    case 0xBC: // ü
    case 0x84: // Ä
    case 0x96: // Ö
    case 0x9C: // Ü
    case 0x9F: // ß
      return 2;
    }
  }
  return 0;
}

bool weightOk(float v, float lo, float hi) {
  return isfinite(v) && v >= lo && v <= hi;
}

float round10(float v) { return roundf(v * 10.0f) / 10.0f; }

bool nearly(float a, float b) { return fabsf(a - b) < SAME_EPS; }

bool sameGlass(const Glass &a, const Glass &b) {
  return strcmp(a.name, b.name) == 0 && nearly(a.emptyG, b.emptyG) &&
         nearly(a.nominalG, b.nominalG);
}

// Leerzeichen am Rand entfernen; false, wenn zu lang fuer dst
bool trimInto(const char *src, char dst[NAME_BYTES + 1]) {
  if (!src)
    return false;
  while (*src == ' ')
    src++;
  size_t n = strlen(src);
  while (n > 0 && src[n - 1] == ' ')
    n--;
  if (n > (size_t)NAME_BYTES)
    return false;
  memcpy(dst, src, n);
  dst[n] = 0;
  return true;
}

Error check(const char *name, float emptyG, float nominalG, Glass *out) {
  if (!trimInto(name, out->name) || !validName(out->name))
    return {"name", "Name: 1 bis 12 Zeichen (Buchstaben, Ziffern, Umlaute)"};
  if (!weightOk(emptyG, EMPTY_MIN, EMPTY_MAX))
    return {"empty", "Leergewicht muss zwischen 1 und 3000 g liegen"};
  if (!weightOk(nominalG, NOMINAL_MIN, NOMINAL_MAX))
    return {"nominal", "Füllmenge muss zwischen 10 und 3000 g liegen"};
  out->emptyG = round10(emptyG);
  out->nominalG = round10(nominalG);
  return {nullptr, nullptr};
}

void putU16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
uint16_t getU16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
void putF32(uint8_t *p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  for (int i = 0; i < 4; i++)
    p[i] = (uint8_t)(v >> (8 * i));
}
float getF32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++)
    v |= (uint32_t)p[i] << (8 * i);
  float f;
  memcpy(&f, &v, 4);
  return f;
}

// Fortlaufendes Schreiben in einen festen Puffer
struct Out {
  char *buf;
  size_t cap, len;
  bool ok;
  __attribute__((format(printf, 2, 3))) void add(const char *fmt, ...) {
    if (!ok)
      return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - len) {
      ok = false;
      return;
    }
    len += (size_t)n;
  }
};

} // namespace

bool validName(const char *utf8) {
  if (!utf8 || !utf8[0])
    return false;
  const unsigned char *s = (const unsigned char *)utf8;
  size_t n = strlen(utf8);
  if (n > (size_t)NAME_BYTES || s[0] == ' ' || s[n - 1] == ' ')
    return false;
  int glyphs = 0;
  for (size_t i = 0; i < n;) {
    int k = allowedChar(s + i);
    if (k == 0)
      return false;
    i += (size_t)k;
    glyphs++;
  }
  return glyphs <= NAME_GLYPHS;
}

const char *sourceKey(Source s) {
  switch (s) {
  case Source::None:
    return "none";
  case Source::Manual:
    return "manual";
  case Source::Same:
    return "same";
  case Source::Empty:
    return "empty";
  case Source::Auto:
    return "auto";
  }
  return "none";
}

// ── Liste ─────────────────────────────────────────────────────────────────────

void List::begin(const Glass *defaults, int n, uint16_t nextId) {
  defs_ = defaults;
  defCount_ = n < 0 ? 0 : (n > MAX_GLASSES ? MAX_GLASSES : n);
  nextId_ = nextId;
  for (int i = 0; i < defCount_; i++)
    if (defs_[i].id >= nextId_)
      nextId_ = (uint16_t)(defs_[i].id + 1);
  deltaCount_ = 0;
  pruned_ = false;
  rebuild();
}

const Glass *List::defaultById(uint16_t id) const {
  for (int i = 0; i < defCount_; i++)
    if (defs_[i].id == id)
      return &defs_[i];
  return nullptr;
}

int List::deltaIndex(uint16_t id) const {
  for (int i = 0; i < deltaCount_; i++)
    if (deltas_[i].g.id == id)
      return i;
  return -1;
}

bool List::pushDelta(const Delta &d) {
  if (deltaCount_ >= MAX_DELTAS)
    return false;
  deltas_[deltaCount_++] = d;
  return true;
}

void List::eraseDelta(int i) {
  for (int k = i; k + 1 < deltaCount_; k++)
    deltas_[k] = deltas_[k + 1];
  deltaCount_--;
}

void List::rebuild() {
  n_ = 0;
  for (int i = 0; i < defCount_ && n_ < MAX_GLASSES; i++) {
    int d = deltaIndex(defs_[i].id);
    if (d >= 0 && deltas_[d].kind == K_DELETED)
      continue;
    if (d >= 0 && deltas_[d].kind == K_MODIFIED) {
      list_[n_] = deltas_[d].g;
      origin_[n_++] = Origin::Modified;
    } else {
      list_[n_] = defs_[i];
      origin_[n_++] = Origin::Default;
    }
  }
  for (int i = 0; i < deltaCount_ && n_ < MAX_GLASSES; i++) {
    if (deltas_[i].kind != K_CUSTOM)
      continue;
    list_[n_] = deltas_[i].g;
    origin_[n_++] = Origin::Custom;
  }
}

const Glass *List::find(uint16_t id) const {
  if (id == 0)
    return nullptr;
  for (int i = 0; i < n_; i++)
    if (list_[i].id == id)
      return &list_[i];
  return nullptr;
}

int List::deletedCount() const {
  int n = 0;
  for (int i = 0; i < deltaCount_; i++)
    if (deltas_[i].kind == K_DELETED)
      n++;
  return n;
}

const Glass *List::deletedAt(int i) const {
  for (int k = 0; k < deltaCount_; k++)
    if (deltas_[k].kind == K_DELETED && i-- == 0)
      return defaultById(deltas_[k].g.id);
  return nullptr;
}

Error List::add(const char *name, float emptyG, float nominalG,
                uint16_t *newId) {
  Delta d = {};
  d.kind = K_CUSTOM;
  Error e = check(name, emptyG, nominalG, &d.g);
  if (e.field)
    return e;
  if (n_ >= MAX_GLASSES || deltaCount_ >= MAX_DELTAS)
    return {"name", "Liste ist voll (max. 24 Gläser)"};
  uint16_t id = USER_ID_MIN;
  for (int i = 0; i < deltaCount_; i++)
    if (deltas_[i].kind == K_CUSTOM && deltas_[i].g.id >= id)
      id = (uint16_t)(deltas_[i].g.id + 1);
  d.g.id = id;
  pushDelta(d);
  rebuild();
  if (newId)
    *newId = id;
  return {nullptr, nullptr};
}

Error List::update(uint16_t id, const char *name, float emptyG,
                   float nominalG) {
  if (!find(id))
    return {"id", "Glas nicht gefunden"};
  Glass g = {};
  Error e = check(name, emptyG, nominalG, &g);
  if (e.field)
    return e;
  g.id = id;
  int d = deltaIndex(id);
  const Glass *def = defaultById(id);
  if (def && sameGlass(*def, g)) {
    if (d >= 0)
      eraseDelta(d); // wieder wie in der Firmware
  } else if (d >= 0) {
    deltas_[d].g = g;
  } else if (!pushDelta({K_MODIFIED, g})) {
    return {"name", "Zu viele Änderungen gespeichert"};
  }
  rebuild();
  return {nullptr, nullptr};
}

bool List::remove(uint16_t id) {
  if (!find(id))
    return false;
  int d = deltaIndex(id);
  if (defaultById(id)) {
    if (d >= 0) {
      deltas_[d].kind = K_DELETED;
      deltas_[d].g = *defaultById(id);
    } else {
      Delta del = {K_DELETED, *defaultById(id)};
      if (!pushDelta(del))
        return false;
    }
  } else if (d >= 0) {
    eraseDelta(d);
  }
  rebuild();
  return true;
}

bool List::restore(uint16_t id) {
  int d = deltaIndex(id);
  if (d < 0 || !defaultById(id))
    return false;
  eraseDelta(d);
  rebuild();
  return true;
}

void List::restoreAll() {
  deltaCount_ = 0;
  rebuild();
}

void List::prune() {
  for (int i = 0; i < deltaCount_;) {
    Delta &d = deltas_[i];
    const Glass *def = d.kind == K_CUSTOM ? nullptr : defaultById(d.g.id);
    bool drop = false;
    if (d.kind == K_CUSTOM) {
      // Eigenes Glas steckt inzwischen in der Firmware (Export + Flashen)
      for (int k = 0; k < defCount_ && !drop; k++)
        drop = sameGlass(defs_[k], d.g);
    } else if (!def) {
      if (d.kind == K_DELETED) {
        drop = true; // Standardglas gibt es nicht mehr
      } else {
        // geaendertes Standardglas, das die Firmware nicht mehr kennt: eigenes
        uint16_t id = USER_ID_MIN;
        for (int k = 0; k < deltaCount_; k++)
          if (deltas_[k].kind == K_CUSTOM && deltas_[k].g.id >= id)
            id = (uint16_t)(deltas_[k].g.id + 1);
        d.kind = K_CUSTOM;
        d.g.id = id;
        pruned_ = true;
      }
    } else if (d.kind == K_MODIFIED) {
      drop = sameGlass(*def, d.g);
    }
    if (drop) {
      eraseDelta(i);
      pruned_ = true;
    } else {
      i++;
    }
  }
}

size_t List::save(uint8_t *buf, size_t cap) const {
  size_t len = 2;
  if (cap < len)
    return 0;
  buf[0] = BLOB_VERSION;
  buf[1] = (uint8_t)deltaCount_;
  for (int i = 0; i < deltaCount_; i++) {
    const Delta &d = deltas_[i];
    size_t nl = strlen(d.g.name);
    size_t need = 2 + 1 + 1 + nl + 8;
    if (len + need > cap)
      return 0;
    uint8_t *p = buf + len;
    putU16(p, d.g.id);
    p[2] = d.kind;
    p[3] = (uint8_t)nl;
    memcpy(p + 4, d.g.name, nl);
    putF32(p + 4 + nl, d.g.emptyG);
    putF32(p + 8 + nl, d.g.nominalG);
    len += need;
  }
  return len;
}

bool List::load(const uint8_t *buf, size_t len) {
  deltaCount_ = 0;
  pruned_ = false;
  bool ok = buf && len >= 2 && buf[0] == BLOB_VERSION && buf[1] <= MAX_DELTAS;
  size_t pos = 2;
  for (int i = 0; ok && i < buf[1]; i++) {
    if (pos + 4 > len) {
      ok = false;
      break;
    }
    const uint8_t *p = buf + pos;
    Delta d = {};
    d.g.id = getU16(p);
    d.kind = p[2];
    size_t nl = p[3];
    if (nl > (size_t)NAME_BYTES || pos + 4 + nl + 8 > len) {
      ok = false;
      break;
    }
    memcpy(d.g.name, p + 4, nl);
    d.g.name[nl] = 0;
    d.g.emptyG = getF32(p + 4 + nl);
    d.g.nominalG = getF32(p + 8 + nl);
    pos += 4 + nl + 8;
    const bool kindOk =
        d.kind == K_CUSTOM || d.kind == K_MODIFIED || d.kind == K_DELETED;
    const bool idOk = d.kind == K_CUSTOM ? d.g.id >= USER_ID_MIN
                                         : d.g.id > 0 && d.g.id < USER_ID_MIN;
    const bool dataOk =
        d.kind == K_DELETED ||
        (validName(d.g.name) && weightOk(d.g.emptyG, EMPTY_MIN, EMPTY_MAX) &&
         weightOk(d.g.nominalG, NOMINAL_MIN, NOMINAL_MAX));
    if (!kindOk || !idOk || !dataOk || deltaIndex(d.g.id) >= 0) {
      ok = false;
      break;
    }
    deltas_[deltaCount_++] = d;
  }
  if (!ok) {
    deltaCount_ = 0;
    rebuild();
    return false;
  }
  prune();
  rebuild();
  return true;
}

size_t List::exportHeader(char *buf, size_t cap) const {
  if (!buf || cap == 0)
    return 0;
  Out o = {buf, cap, 0, true};
  uint16_t next = nextId_;
  o.add("#pragma once\n#include \"glass_core.h\"\n\n"
        "// Standardglaeser der Firmware (erzeugt von der Weboberflaeche,\n"
        "// \"Als Firmware-Liste exportieren\"). IDs nie wiederverwenden.\n"
        "// Felder: ID, Name, Leergewicht [g], Nennfuellung [g].\n\n"
        "namespace glass {\n\n");
  o.add("constexpr Glass DEFAULTS[] = {\n");
  for (int i = 0; i < n_; i++) {
    const Glass &g = list_[i];
    uint16_t id = origin_[i] == Origin::Custom ? next++ : g.id;
    o.add("    {%u, \"", (unsigned)id);
    for (const char *s = g.name; *s; s++)
      o.add(*s == '"' || *s == '\\' ? "\\%c" : "%c", *s);
    o.add("\", %.1ff, %.1ff},\n", (double)g.emptyG, (double)g.nominalG);
  }
  o.add("};\n\nconstexpr int DEFAULT_COUNT = "
        "(int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]));\n");
  o.add("constexpr uint16_t DEFAULT_NEXT_ID = %u; // erste freie ID\n\n",
        (unsigned)next);
  o.add("} // namespace glass\n");
  if (!o.ok) {
    buf[0] = 0;
    return 0;
  }
  return o.len;
}

// ── Bestimmung ────────────────────────────────────────────────────────────────

Detection Detector::place(const List &l, float absW, float tol) {
  // 0. Im Web festgelegt
  if (mem_.manualId) {
    const Glass *g = l.find(mem_.manualId);
    if (g) {
      mem_.lastId = g->id;
      mem_.refG = absW;
      return {g->id, Source::Manual, absW - g->emptyG};
    }
    mem_.manualId = 0; // Glas gibt es nicht mehr
  }

  // 1. Dasselbe Glas: nicht schwerer als zuletzt, nicht leichter als leer
  const Glass *last = l.find(mem_.lastId);
  if (last && absW <= mem_.refG + tol && absW >= last->emptyG - tol) {
    mem_.refG = absW;
    return {last->id, Source::Same, absW - last->emptyG};
  }

  // 2. Leeres Glas
  const Glass *best = nullptr;
  float bestD = 0.0f;
  for (int i = 0; i < l.count(); i++) {
    const Glass &g = l.at(i);
    float d = fabsf(absW - g.emptyG);
    if (d <= tol && (!best || d < bestD)) {
      best = &g;
      bestD = d;
    }
  }
  if (best) {
    mem_.lastId = best->id;
    mem_.refG = absW;
    return {best->id, Source::Empty, absW - best->emptyG};
  }

  // 3. Volles Glas: Inhalt passt zur Nennfuellung. Abweichung von der
  // Nennfuellung als Anteil; das letzte Glas bekommt LAST_BONUS_PCT Vorsprung.
  bool lastFits = false;
  float lastD = 0.0f;
  for (int i = 0; i < l.count(); i++) {
    const Glass &g = l.at(i);
    const float content = absW - g.emptyG;
    const float lo = g.nominalG * (float)FILL_MIN_PCT / 100.0f;
    const float hi = g.nominalG * (float)FILL_MAX_PCT / 100.0f;
    if (content < lo || content > hi)
      continue;
    float d = fabsf(content / g.nominalG - 1.0f);
    if (last && g.id == last->id) {
      lastFits = true;
      lastD = d;
    }
    if (!best || d < bestD) {
      best = &g;
      bestD = d;
    }
  }
  if (lastFits && lastD <= bestD + (float)LAST_BONUS_PCT / 100.0f)
    best = last;
  // Kein Kandidat: das letzte Glas gilt auch mit wenig Inhalt (z. B. leer
  // erkannt und nur halb eingeschenkt), solange es nicht ueberlaeuft
  if (!best && last) {
    const float content = absW - last->emptyG;
    if (content > tol &&
        content <= last->nominalG * (float)FILL_MAX_PCT / 100.0f)
      best = last;
  }
  if (best) {
    mem_.lastId = best->id;
    mem_.refG = absW;
    return {best->id, Source::Auto, absW - best->emptyG};
  }

  // 4. Unbekannt
  return {0, Source::None, 0.0f};
}

void Detector::settle(float absW) {
  if (mem_.lastId)
    mem_.refG = absW;
}

} // namespace glass
