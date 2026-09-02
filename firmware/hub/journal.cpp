// =============================================================================
//  journal.cpp — recent history: a fixed ring in NVS, an unbounded archive on the card
//
//  This module owns the last JOURNAL_CAPACITY events and nothing else. It keeps them in
//  a RAM ring, persists the whole ring to NVS as one throttled blob write so the LOG page
//  survives a power cut with no card in the slot, and hands every append straight to
//  Store::appendEvent for the plain-text archive. It classifies nothing and decides
//  nothing: the kind, the code and the silent flag are supplied by state.cpp, and the
//  human-readable names are resolved at write time by SecEvents.h. The only opinions in
//  here are about flash wear and about which end of the ring is the newest.
// =============================================================================

#include "journal.h"
#include "config.h"
#include "store.h"
#include <Preferences.h>
#include <SecEvents.h>

namespace Journal {

// ========== [Storage layout] ==========
// Same pattern as the roster: one key, one fixed-size blob, a magic word in front of it so
// a truncated write or an older layout is rejected instead of being read as valid history.
#define JOURNAL_KEY   "log"
#define JOURNAL_MAGIC 0x4A31

// head and count travel with the ring, because the ring alone does not say where the newest
// entry is. Storing them means a reboot restores the reading order, not just the bytes.
struct Blob {
  uint16_t magic;
  uint8_t  count;
  uint8_t  head;
  LogEntry ring[JOURNAL_CAPACITY];
};

// ========== [Module state] ==========
// A ring buffer with a head and no tail. g_head is the write cursor — the slot the *next*
// entry will occupy, not the last one written — and it wraps at capacity. g_count saturates
// at capacity and never decreases, so it means "how many slots hold real data", which is
// what distinguishes a half-full ring on a fresh boot from a full one that has wrapped.
// There is no tail cursor because nothing consumes from this buffer: readers index into it
// newest-first (see at()) and the oldest entry is simply the one that gets overwritten.
static LogEntry g_ring[JOURNAL_CAPACITY];
static uint8_t  g_head  = 0;
static uint8_t  g_count = 0;
// Dirty flag plus a timestamp is the whole throttle: appends mark, tick() writes.
static bool     g_dirty = false;
static uint32_t g_flushAt = 0;
// Returned by reference for an out-of-range index, so at() can hand back something valid
// without every caller having to null-check. It is never written.
static LogEntry g_empty = {0, 0, 0, 0, 0, 0};

// ========== [Load] ==========
static void load() {
  Blob b;
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  const size_t got = p.getBytes(JOURNAL_KEY, &b, sizeof(b));
  p.end();

  if (got != sizeof(b) || b.magic != JOURNAL_MAGIC) return;
  // Bounds-check the cursors before trusting them. A head at or past capacity, or a count
  // above it, would make at() index outside the array — so a corrupt blob is discarded
  // whole rather than partially believed.
  if (b.head >= JOURNAL_CAPACITY || b.count > JOURNAL_CAPACITY) return;

  memcpy(g_ring, b.ring, sizeof(g_ring));
  g_head  = b.head;
  g_count = b.count;
  // Every restored entry's uptime field is zeroed. `at` is a millis() reading, and a
  // millis() value from a previous boot has no relationship to this boot's clock — left
  // alone it would render as "43s ago" for an event from last week, which is worse than no
  // relative time at all. Zero is the agreed "no uptime for this one" marker, and the UI
  // falls back to the absolute epoch stamp when it sees it.
  for (uint8_t i = 0; i < JOURNAL_CAPACITY; i++) g_ring[i].at = 0;
}

// ========== [Flush] ==========
// The entire ring, head and count go out as a single putBytes. One key write per flush
// rather than one per entry: NVS is a key-value store, so forty keys would mean forty
// records to fragment the partition, and rewriting one blob is both simpler and cheaper.
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

// ========== [Lifecycle] ==========
void begin() {
  load();
  g_flushAt = millis();
}

// ========== [Append] ==========
// Write at the head, then advance. Both timestamps are recorded because they answer
// different questions: `epoch` is wall-clock time, which is 0 until the first server sync,
// and `at` is uptime, which is always available and is what lets the LOG page show "12s ago"
// for something that happened before the hub knew the date.
void append(uint8_t kind, uint8_t code, uint16_t value, bool silent) {
  LogEntry &e = g_ring[g_head];
  e.epoch  = Store::epoch();
  e.at     = millis();
  e.kind   = kind;
  e.code   = code;
  e.value  = value;
  e.silent = silent ? 1 : 0;

  // Advance and wrap. When the ring is full this overwrites the oldest entry, which is the
  // intended behaviour: recent history is bounded on purpose and the card holds the rest.
  g_head = (g_head + 1) % JOURNAL_CAPACITY;
  if (g_count < JOURNAL_CAPACITY) g_count++;
  // Marked, not written. tick() decides when it actually reaches flash.
  g_dirty = true;

  // The card append is immediate and unbuffered, deliberately out of step with the throttled
  // NVS write: the CSV is the archive, and the few seconds the ring may lose to a power cut
  // at the wrong instant are exactly the seconds most worth having. A no-op when no card is
  // mounted. Names are resolved here rather than stored, so the CSV is readable text while
  // the ring keeps fixed-size binary records.
  Store::appendEvent(e.epoch, secKindName(kind), secEventName(code), value, silent);
}

int count() { return g_count; }

// ========== [Newest-first access] ==========
// at(0) is the newest entry, at(count()-1) the oldest. That ordering is not a convenience —
// it is what the LOG page needs, since it draws the most recent event at the top and stops
// when it runs out of rows. Exposing the raw ring order instead would make every caller
// reconstruct the head arithmetic, and the page would have to know how many entries exist
// before it could draw the first line.
//
// The index maths, term by term:
//   g_head - 1   the newest entry, because g_head points at the *next* slot to be written
//   - i          step backwards through history
//   + CAP * 2    keep the intermediate non-negative before the modulo
//   % CAP        wrap into the array
//
// That third term is the one that matters. g_head is a uint8_t and i is an int, so the
// expression is evaluated as int and `g_head - 1 - i` is genuinely negative in the ordinary
// case — head 0 asking for the newest entry gives -1. C's % on a negative left operand
// yields a negative result (it truncates toward zero rather than flooring), so -1 % 40 is
// -1, not 39, and the ring would be indexed out of bounds on the very first read after a
// wrap. Adding a whole capacity first makes the value non-negative; adding two leaves margin
// so the expression stays correct even if the `i >= g_count` guard above is ever loosened —
// with the current bounds one capacity lands on exactly 0 in the worst case, which is
// correct but has no room in it.
const LogEntry &at(int i) {
  if (i < 0 || i >= g_count) return g_empty;
  const int idx = (g_head - 1 - i + JOURNAL_CAPACITY * 2) % JOURNAL_CAPACITY;
  return g_ring[idx];
}

// ========== [Clear] ==========
// Flushes immediately rather than marking dirty: a wipe the user asked for must survive the
// next power cut even if it arrives before the throttle window elapses.
void clear() {
  g_head = 0;
  g_count = 0;
  memset(g_ring, 0, sizeof(g_ring));
  flush();
}

// ========== [Throttled flush] ==========
// The wear guard. Every append dirties the ring, and events are bursty — an alarm produces a
// handful in a second or two. Flushing on each one would put a flash write behind every
// event, forty times the wear, to buy at most JOURNAL_FLUSH_MS of freshness; instead a whole
// burst inside one window costs a single blob write. The tradeoff is stated plainly: up to
// JOURNAL_FLUSH_MS of the newest entries are lost if power fails at the wrong instant, which
// is exactly why the CSV append in append() is not batched. The comparison is written as an
// unsigned difference so it is correct across the millis() rollover.
void tick() {
  if (!g_dirty) return;
  const uint32_t now = millis();
  if (now - g_flushAt < JOURNAL_FLUSH_MS) return;
  g_flushAt = now;
  flush();
}

}
