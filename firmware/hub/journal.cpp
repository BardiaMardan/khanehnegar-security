#include "journal.h"
#include "config.h"
#include "store.h"
#include <Preferences.h>
#include <SecEvents.h>

namespace Journal {

#define JOURNAL_KEY   "log"
#define JOURNAL_MAGIC 0x4A31

struct Blob {
  uint16_t magic;
  uint8_t  count;
  uint8_t  head;
  LogEntry ring[JOURNAL_CAPACITY];
};

static LogEntry g_ring[JOURNAL_CAPACITY];
static uint8_t  g_head  = 0;
static uint8_t  g_count = 0;
static bool     g_dirty = false;
static uint32_t g_flushAt = 0;
static LogEntry g_empty = {0, 0, 0, 0, 0, 0};

static void load() {
  Blob b;
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  const size_t got = p.getBytes(JOURNAL_KEY, &b, sizeof(b));
  p.end();

  if (got != sizeof(b) || b.magic != JOURNAL_MAGIC) return;
  if (b.head >= JOURNAL_CAPACITY || b.count > JOURNAL_CAPACITY) return;

  memcpy(g_ring, b.ring, sizeof(g_ring));
  g_head  = b.head;
  g_count = b.count;
  for (uint8_t i = 0; i < JOURNAL_CAPACITY; i++) g_ring[i].at = 0;
}

static void flush() {
  Blob b;
  b.magic = JOURNAL_MAGIC;
  b.count = g_count;
  b.head  = g_head;
  memcpy(b.ring, g_ring, sizeof(b.ring));

  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes(JOURNAL_KEY, &b, sizeof(b));
  p.end();
  g_dirty = false;
}

void begin() {
  load();
  g_flushAt = millis();
}

void append(uint8_t kind, uint8_t code, uint16_t value, bool silent) {
  LogEntry &e = g_ring[g_head];
  e.epoch  = Store::epoch();
  e.at     = millis();
  e.kind   = kind;
  e.code   = code;
  e.value  = value;
  e.silent = silent ? 1 : 0;

  g_head = (g_head + 1) % JOURNAL_CAPACITY;
  if (g_count < JOURNAL_CAPACITY) g_count++;
  g_dirty = true;

  Store::appendEvent(e.epoch, secKindName(kind), secEventName(code), value, silent);
}

int count() { return g_count; }

const LogEntry &at(int i) {
  if (i < 0 || i >= g_count) return g_empty;
  const int idx = (g_head - 1 - i + JOURNAL_CAPACITY * 2) % JOURNAL_CAPACITY;
  return g_ring[idx];
}

void clear() {
  g_head = 0;
  g_count = 0;
  memset(g_ring, 0, sizeof(g_ring));
  flush();
}

void tick() {
  if (!g_dirty) return;
  const uint32_t now = millis();
  if (now - g_flushAt < JOURNAL_FLUSH_MS) return;
  g_flushAt = now;
  flush();
}

}
