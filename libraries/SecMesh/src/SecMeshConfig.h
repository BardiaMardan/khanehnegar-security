// =============================================================================
//  SecMeshConfig.h — site-wide constants: protocol identity, keys, timing
// =============================================================================
#pragma once

// ========== [Protocol identity] ==========
#define SECMESH_MAGIC       0x4B4E
#define SECMESH_VERSION     1
#define SECMESH_CHANNEL     1

// ========== [Keys] ==========
// Per-site keys live in SecMeshKeys.h next to this file, which git ignores. Without it the
// build uses the published example keys, which anyone with this repository can read, so the
// warning is there to make that impossible to miss. Every board on a site must share one pair.
#if __has_include("SecMeshKeys.h")
  #include "SecMeshKeys.h"
#else
  #warning "SecMesh: using the example keys. Copy SecMeshKeys.example.h to SecMeshKeys.h and change them."
  #include "SecMeshKeys.example.h"
#endif

static_assert(sizeof(SECMESH_PMK) == 17, "SECMESH_PMK must be exactly 16 characters");
static_assert(sizeof(SECMESH_LMK) == 17, "SECMESH_LMK must be exactly 16 characters");

// ESP8266 ESP-NOW does not interoperate reliably with the ESP32's encrypted unicast: the
// broadcast pairing frames get through and every encrypted unicast is dropped, which left an
// ESP8266 node claimed but structurally mute. Those nodes use plaintext unicast. The link is
// still counter-protected and MAC-filtered, but it is readable off the air — see
// docs/security.md, "ESP8266 nodes".
#if defined(ESP8266)
  #define SECMESH_WANT_ENCRYPTION 0
#else
  #define SECMESH_WANT_ENCRYPTION 1
#endif

// ========== [Liveness timing] ==========
// Dead at two and a half missed heartbeats. This is a security system: a node going quiet
// may mean it was destroyed, so the detection window is short at the cost of more traffic.
#define SECMESH_HEARTBEAT_MS    5000UL
#define SECMESH_DEAD_MS        13000UL
// The hub answers every heartbeat, so a claimed node that hears nothing for this long has
// genuinely lost the link and may hop channel to look for it. It must be several heartbeats
// or a single dropped reply sends a healthy node hunting and breaks the link it had.
#define SECMESH_SILENT_MS      30000UL

// ========== [Roster size] ==========
#define SECMESH_MAX_NODES      6

// ========== [Alarm burst] ==========
#define SECMESH_ALARM_REPEAT   3
#define SECMESH_ALARM_GAP_MS   40

// ========== [Tamper switch] ==========
// 0 = report the edge on the next tick. Raise it only if the enclosure switch genuinely
// chatters; at 250 ms a real contact of ten milliseconds never registered at all.
#define SECMESH_TAMPER_DEBOUNCE_MS 0UL
