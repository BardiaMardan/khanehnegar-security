// =============================================================================
//  nodes.cpp — the roster: which modules the hub has adopted, and which are still alive
// =============================================================================

#include "nodes.h"
#include <Preferences.h>

namespace Nodes {

// ========== [The roster, and what it is not] ==========
static NodeRec     g[SECMESH_MAX_NODES];
static bool        g_pairing = false;
static uint32_t    g_pairingOpenedAt = 0;
static Preferences prefs;

NodeRec *justDied = nullptr;

void begin() {
  memset(g, 0, sizeof(g));
  load();
}

NodeRec *find(const uint8_t mac[6]) {
  for (int i = 0; i < SECMESH_MAX_NODES; i++)
    if (g[i].used && SecMeshClass::macEq(g[i].mac, mac)) return &g[i];
  return nullptr;
}

// ========== [Adoption] ==========
// A pair request is not evidence of life. Reviving lastSeen and clearing `dead` here meant a
// node that could broadcast but never deliver a real frame was resurrected every time it
// asked, died again 13 s later, and raised a lost-module alarm on every cycle. Liveness is
// touchSeen's job and only an accepted data frame gets there.
NodeRec *adopt(const uint8_t mac[6], uint8_t kind, bool encrypted, bool &fresh) {
  NodeRec *n = find(mac);
  fresh = false;
  if (!n) {
    if (!g_pairing) return nullptr;
    for (int i = 0; i < SECMESH_MAX_NODES; i++) {
      if (!g[i].used) { n = &g[i]; break; }
    }
    if (!n) return nullptr;
    memset(n, 0, sizeof(NodeRec));
    memcpy(n->mac, mac, 6);
    n->used     = true;
    n->lastSeen = millis();
    fresh = true;
  }
  const bool modeChanged = (!fresh && n->encrypted != encrypted);
  n->kind        = kind;
  n->encrypted   = encrypted;
  n->lastCounter = 0;
  SecMesh.addPeer(mac, encrypted);
  // Persist a change of link mode too, or a node that switched to plaintext would be
  // re-added encrypted on the next boot and go mute again.
  if (fresh || modeChanged) save();
  return n;
}

// ========== [Replay protection] ==========
// Strictly greater rejects replays. A node's counter restarts at 1 after a power cut, so a
// backwards jump into the first few frames is a reboot, not an attack — refusing it left a
// rebooted node permanently mute, which is the worst failure this system can have.
// An attacker would have to replay one of a node's first COUNTER_REBOOT_MAX frames, and the
// next genuine frame resynchronises past it.
#define COUNTER_REBOOT_MAX 8

bool acceptCounter(NodeRec *n, uint32_t counter) {
  if (counter > n->lastCounter) {
    n->lastCounter = counter;
    return true;
  }
  if (counter <= COUNTER_REBOOT_MAX || n->lastCounter - counter > 1000) {
    n->lastCounter = counter;
    return true;
  }
  return false;
}

void resetCounter(NodeRec *n) { n->lastCounter = 0; }

// ========== [Liveness] ==========
void touchSeen(NodeRec *n, const SecFrame &f) {
  n->lastSeen  = millis();
  n->supplyMv  = f.supplyMv;
  n->flags     = f.flags;
  n->lastEvent = f.code;
  n->dead      = false;
}

void tick() {
  const uint32_t now = millis();
  for (int i = 0; i < SECMESH_MAX_NODES; i++) {
    if (!g[i].used || g[i].dead) continue;
    if (now - g[i].lastSeen > SECMESH_DEAD_MS) {
      g[i].dead = true;
      justDied = &g[i];
    }
  }
  if (g_pairing && now - g_pairingOpenedAt > 120000UL) g_pairing = false;
}

// ========== [Queries for the UI] ==========
int capacity() { return SECMESH_MAX_NODES; }
NodeRec *at(int i) { return (i >= 0 && i < SECMESH_MAX_NODES) ? &g[i] : nullptr; }

int liveCount() {
  int c = 0;
  for (int i = 0; i < SECMESH_MAX_NODES; i++) if (g[i].used && !g[i].dead) c++;
  return c;
}

int deadCount() {
  int c = 0;
  for (int i = 0; i < SECMESH_MAX_NODES; i++) if (g[i].used && g[i].dead) c++;
  return c;
}

// ========== [Pairing window] ==========
void setPairingMode(bool on) {
  g_pairing = on;
  g_pairingOpenedAt = millis();
}

bool pairingMode() { return g_pairing; }

// ========== [Persistence] ==========
void save() {
  prefs.begin(NVS_NAMESPACE, false);
  for (int i = 0; i < SECMESH_MAX_NODES; i++) {
    char key[8];
    snprintf(key, sizeof(key), "n%d", i);
    if (g[i].used) {
      uint8_t blob[8];
      memcpy(blob, g[i].mac, 6);
      blob[6] = g[i].kind;
      blob[7] = g[i].encrypted ? 1 : 0;
      prefs.putBytes(key, blob, sizeof(blob));
    } else {
      prefs.remove(key);
    }
  }
  prefs.end();
}

void load() {
  prefs.begin(NVS_NAMESPACE, true);
  for (int i = 0; i < SECMESH_MAX_NODES; i++) {
    char key[8];
    snprintf(key, sizeof(key), "n%d", i);
    uint8_t blob[8];
    if (prefs.getBytes(key, blob, sizeof(blob)) == sizeof(blob)) {
      memcpy(g[i].mac, blob, 6);
      g[i].kind      = blob[6];
      g[i].encrypted = blob[7] != 0;
      g[i].used      = true;
      g[i].dead      = false;
      SecMesh.addPeer(g[i].mac, g[i].encrypted);
    }
  }
  prefs.end();
}

void forgetAll() {
  for (int i = 0; i < SECMESH_MAX_NODES; i++) {
    if (g[i].used) SecMesh.removePeer(g[i].mac);
  }
  memset(g, 0, sizeof(g));
  save();
}

}
