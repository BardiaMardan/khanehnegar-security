// =============================================================================
//  fingers.cpp — the fingerprint roster: which template ids are people
//
//  The sensor stores fingerprints; this module stores the fact that a given template
//  id is an enrolled person, and what to call them. It owns FP_SLOTS id-and-label
//  pairs, kept in RAM for lookup speed, written to NVS as a single blob on every
//  change, and mirrored to the SD card as text for the archive. It owns no policy:
//  it answers "is this id enrolled" and never decides what to do about the answer.
//  fp.cpp talks to the hardware, state.cpp decides, and this file is the register in
//  between — which is why the whole module is a flat array and a magic word.
// =============================================================================

#include "fingers.h"
#include "config.h"
#include "store.h"
#include <Preferences.h>

namespace Fingers {

// ========== [Storage layout] ==========
// A magic word in front of the blob is the cheapest possible schema check: it tells a
// half-written record, a leftover from an older layout, and a genuinely empty namespace
// apart from a valid roster. Bump it if the Slot or Blob layout ever changes, because the
// blob is read back with memcpy and a stale layout would be read as if it were current.
#define ROSTER_KEY   "fp"
#define ROSTER_MAGIC 0x4631

struct Slot {
  uint16_t id;
  char     label[FP_LABEL_LEN];
};

// Fixed size, always written whole. Empty slots are written too — 16 slots is a few
// hundred bytes, and a fixed-size record means one NVS key and no fragmentation.
struct Blob {
  uint16_t magic;
  uint16_t count;
  Slot     slot[FP_SLOTS];
};

// ========== [Module state] ==========
// The array is dense: entries 0..g_count-1 are live and the rest are zeroed, so a lookup
// is a short linear scan and removal is a shift. Ids are not array indices — the sensor
// picks the id, this module picks where to keep it.
static Slot g_slot[FP_SLOTS];
static int  g_count = 0;

// ========== [NVS blob] ==========
// One write, both destinations. NVS is authoritative and goes first; the card gets the
// same content as flat text afterwards, and Store::writeAccess() failing because no card
// is mounted is not an error worth reporting here — see load() for why.
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

// Three separate rejections, each of which leaves the in-RAM roster untouched: wrong size
// (the layout changed or the write was truncated), wrong magic (not our blob), and a count
// larger than the array (corrupt, and the one that would otherwise scan off the end).
// Returning false means "no valid roster in NVS", which is the only condition that lets
// begin() look at the card at all.
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
// Never leaves a slot with an empty label: an unnamed finger would show as a blank row on
// the panel and a blank name in a "welcome" toast. Truncation is by hand because strncpy
// does not terminate when the source fills the buffer.
static void setLabel(Slot &s, const String &label) {
  String v = label;
  v.trim();
  if (!v.length()) v = String("finger ") + String(s.id);
  strncpy(s.label, v.c_str(), FP_LABEL_LEN - 1);
  s.label[FP_LABEL_LEN - 1] = 0;
}

// ========== [Lookup] ==========
// The single place an id becomes a position. Returns -1 for "not enrolled", and every
// public query in this file is a thin wrapper over it. Sixteen entries make a linear scan
// the right answer; a map would be more code, more flash and slower at this size.
static int indexOf(uint16_t id) {
  for (int i = 0; i < g_count; i++) if (g_slot[i].id == id) return i;
  return -1;
}

// ========== [SD import — one-time migration only] ==========
// Parses `id=label` lines out of /khaneh/access.ini. This is a migration path, not a read
// path: it runs only when NVS holds no valid roster, and the very next thing begin() does
// is write what it found back to NVS so it never runs again.
//
// Tolerant by design, because the file is hand-editable: blank lines, `#` and `;` comments,
// CRLF endings, and a legacy `label|flags` suffix from the build that carried duress flags
// are all accepted and ignored. Id 0 is rejected because the sensor never uses it, and a
// duplicate id is skipped rather than overwriting the entry already loaded.
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
// NVS is authoritative. The SD card is a mirror and nothing more.
//
// This is worth stating in full because an earlier build had it the other way round and
// the failure was ugly. That build read the roster from /khaneh/access.ini on every boot.
// A card that was missing, unseated, or that simply failed to mount — Store::begin walks
// 400 kHz, 1 MHz and 4 MHz before giving up — left the roster empty, so Fingers::known()
// returned false for every id and a correctly matched finger came back as "finger not
// recognised". Worse, an enrolled finger is also the local way out of lockdown, so a loose
// card locked the owner out of their own recovery path. And it presented as a rejected
// finger, not as a storage fault, so the obvious thing to suspect was the sensor.
//
// The rule that came out of it, stated so it cannot drift: a missing card must never grant
// or deny access. Storage faults and access faults are separate failures and must stay
// separate. Hence the order below — NVS first, and if NVS answers, the card is never even
// opened. The import is reached only by a hub with nothing in NVS, and save() immediately
// promotes what it read into NVS, so the card's copy is an export from that moment on.
// Costs, accepted: two copies that can disagree — pull the card, remove a finger, put the
// card back, and the mirror is stale until the next change rewrites it. That is fine
// precisely because the mirror is never an input again. See ADR-010.
void begin() {
  g_count = 0;
  memset(g_slot, 0, sizeof(g_slot));
  if (load()) return;

  const String body = Store::readAccess();
  if (body.length()) {
    importIni(body);
    if (g_count) Serial.printf("fingers: imported %d from sd\n", g_count);
  }
  // Reached on a first boot with no card too, which is deliberate: it writes a valid empty
  // blob with the right magic, so the next boot takes the NVS path instead of looking at
  // the card again.
  save();
}

// ========== [Queries] ==========
// known() is the security-relevant one: state.cpp treats false as "not a credential", so
// this function must depend on nothing that can be unplugged.
int count() { return g_count; }

bool known(uint16_t id) { return indexOf(id) >= 0; }

String label(uint16_t id) {
  const int i = indexOf(id);
  return i < 0 ? String("") : String(g_slot[i].label);
}

// Positional accessors for the UI and the roster upload, which iterate 0..count()-1 and do
// not know about ids. Out-of-range returns an empty value rather than reading past the end.
uint16_t idAt(int i) { return (i < 0 || i >= g_count) ? 0 : g_slot[i].id; }

String labelAt(int i) {
  return (i < 0 || i >= g_count) ? String("") : String(g_slot[i].label);
}

// ========== [Mutation] ==========
// Every mutator saves before returning, so RAM and NVS can never disagree, and returns
// false rather than silently doing nothing so the caller can report the failure. Note that
// none of them touch the sensor: erasing the template is fp.cpp's job and sequencing the
// two is state.cpp's.
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

// Closes the gap by shifting the tail down and zeroes the vacated slot, keeping the array
// dense so indexOf() and the positional accessors stay correct. Ids of other entries are
// untouched — a removal must never renumber someone else's finger.
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
// Lowest unused id, so slots freed by a removal are reused and ids stay small and legible
// on the panel. Searching to FP_MAX_TEMPLATES (the sensor's capacity) rather than FP_SLOTS
// is harmless because the roster is capped at FP_SLOTS entries anyway; 0 means full, and 0
// is also the id the sensor never issues, so it doubles as "none".
uint16_t nextFreeId() {
  for (uint16_t id = 1; id <= FP_MAX_TEMPLATES; id++) {
    if (indexOf(id) < 0) return id;
  }
  return 0;
}

// ========== [Export] ==========
// The card mirror's format, and the same text importIni() parses — so the file this writes
// is exactly the file a future first boot could read back. Deliberately not JSON: the point
// of the mirror is that it stays readable and hand-editable on any computer.
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
