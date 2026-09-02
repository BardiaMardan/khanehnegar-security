// =============================================================================
//  fingers.cpp — the fingerprint roster: which template ids are people
//
//  NVS is authoritative, the SD card is a mirror. A missing card must never
//  decide whether a finger is known.
// =============================================================================

#include "fingers.h"
#include "config.h"
#include "store.h"
#include <Preferences.h>

namespace Fingers {

// ========== [Storage layout] ==========
#define ROSTER_KEY   "fp"
#define ROSTER_MAGIC 0x4631

struct Slot {
  uint16_t id;
  char     label[FP_LABEL_LEN];
};

struct Blob {
  uint16_t magic;
  uint16_t count;
  Slot     slot[FP_SLOTS];
};

// ========== [Module state] ==========
static Slot g_slot[FP_SLOTS];
static int  g_count = 0;

// ========== [NVS blob] ==========
static void save() {
  Blob b;
  b.magic = ROSTER_MAGIC;
  b.count = (uint16_t)g_count;
  memcpy(b.slot, g_slot, sizeof(b.slot));

  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes(ROSTER_KEY, &b, sizeof(b));
  p.end();

  Store::writeAccess(asIni());
}

static bool load() {
  Blob b;
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  const size_t got = p.getBytes(ROSTER_KEY, &b, sizeof(b));
  p.end();

  if (got != sizeof(b) || b.magic != ROSTER_MAGIC) return false;
  if (b.count > FP_SLOTS) return false;

  memcpy(g_slot, b.slot, sizeof(g_slot));
  g_count = (int)b.count;
  return true;
}

// ========== [Labels] ==========
static void setLabel(Slot &s, const String &label) {
  String v = label;
  v.trim();
  if (!v.length()) v = String("finger ") + String(s.id);
  strncpy(s.label, v.c_str(), FP_LABEL_LEN - 1);
  s.label[FP_LABEL_LEN - 1] = 0;
}

// ========== [Lookup] ==========
static int indexOf(uint16_t id) {
  for (int i = 0; i < g_count; i++) if (g_slot[i].id == id) return i;
  return -1;
}

// ========== [SD import — one-time migration only] ==========
static void importIni(const String &body) {
  int from = 0;
  while (from < (int)body.length() && g_count < FP_SLOTS) {
    int nl = body.indexOf('\n', from);
    if (nl < 0) nl = body.length();
    String line = body.substring(from, nl);
    from = nl + 1;

    line.replace("\r", "");
    line.trim();
    if (!line.length() || line[0] == '#' || line[0] == ';') continue;

    const int eq = line.indexOf('=');
    if (eq <= 0) continue;
    const uint16_t id = (uint16_t)line.substring(0, eq).toInt();
    if (id == 0 || indexOf(id) >= 0) continue;

    String label = line.substring(eq + 1);
    const int bar = label.indexOf('|');
    if (bar >= 0) label = label.substring(0, bar);

    g_slot[g_count].id = id;
    setLabel(g_slot[g_count], label);
    g_count++;
  }
}

// ========== [Lifecycle] ==========
void begin() {
  g_count = 0;
  memset(g_slot, 0, sizeof(g_slot));
  if (load()) return;

  const String body = Store::readAccess();
  if (body.length()) {
    importIni(body);
    if (g_count) Serial.printf("fingers: imported %d from sd\n", g_count);
  }
  save();
}

// ========== [Queries] ==========
int count() { return g_count; }

bool known(uint16_t id) { return indexOf(id) >= 0; }

String label(uint16_t id) {
  const int i = indexOf(id);
  return i < 0 ? String("") : String(g_slot[i].label);
}

uint16_t idAt(int i) { return (i < 0 || i >= g_count) ? 0 : g_slot[i].id; }

String labelAt(int i) {
  return (i < 0 || i >= g_count) ? String("") : String(g_slot[i].label);
}

// ========== [Mutation] ==========
bool add(uint16_t id, const String &name) {
  if (g_count >= FP_SLOTS || id == 0 || indexOf(id) >= 0) return false;
  g_slot[g_count].id = id;
  setLabel(g_slot[g_count], name);
  g_count++;
  save();
  return true;
}

bool rename(uint16_t id, const String &name) {
  const int i = indexOf(id);
  if (i < 0) return false;
  setLabel(g_slot[i], name);
  save();
  return true;
}

bool remove(uint16_t id) {
  const int i = indexOf(id);
  if (i < 0) return false;
  for (int j = i; j < g_count - 1; j++) g_slot[j] = g_slot[j + 1];
  g_count--;
  memset(&g_slot[g_count], 0, sizeof(Slot));
  save();
  return true;
}

void clear() {
  g_count = 0;
  memset(g_slot, 0, sizeof(g_slot));
  save();
}

// ========== [Id allocation] ==========
uint16_t nextFreeId() {
  for (uint16_t id = 1; id <= FP_MAX_TEMPLATES; id++) {
    if (indexOf(id) < 0) return id;
  }
  return 0;
}

// ========== [Export] ==========
String asIni() {
  String out;
  for (int i = 0; i < g_count; i++) {
    out += String(g_slot[i].id);
    out += "=";
    out += g_slot[i].label;
    out += "\n";
  }
  return out;
}

}
