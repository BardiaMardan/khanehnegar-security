// =============================================================================
//  SecMeshConfig.h — site-wide constants: protocol identity, keys, timing
//
//  Every number in this file is part of the site's contract with itself. It is
//  compiled into the hub and into every node, so nothing here can be changed on
//  one board alone: changing a value means reflashing everything that talks to
//  everything else. The file owns no code and includes nothing, so it can be
//  pulled in from anywhere.
// =============================================================================
#pragma once

// ========== [Protocol identity] ==========
// 0x4B4E is "KN" for KhanehNegar; it goes out little-endian, so the first two
// bytes on the air are 4E 4B. Its only job is to make foreign ESP-NOW traffic
// that happens to be 15 bytes long cheap to reject.
#define SECMESH_MAGIC       0x4B4E
// Bump this whenever SecFrame's layout changes in any way — a field added,
// widened, reordered, or a `code` renumbered. _onRaw drops any frame whose
// version does not match exactly, so a bump is a hard cut: the site is mute
// until every board is reflashed. That is the intended failure, and it is the
// reason the frame is over-provisioned rather than grown field by field.
#define SECMESH_VERSION     1
// The channel every board comes up on. ESP-NOW neither scans nor hops, so two
// boards on different channels simply cannot hear each other. This is the
// starting point, not a guarantee: the hub's own station association can move it,
// which is why an orphaned node sweeps 1..13 in SecMeshPeers.cpp rather than
// trusting this. The decoy AP, whenever it is built, has to come up here too.
#define SECMESH_CHANNEL     1

// ========== [Keys] ==========
// Exactly 16 characters each; the ESP-NOW API reads 16 bytes and ignores the
// string terminator. PMK is device-wide and encrypts the per-peer LMKs; the LMK
// encrypts the payload of a unicast frame. Both are site-wide compile-time
// constants, which has two consequences worth stating plainly: rotation means
// reflashing every board, and one node recovered from a wall yields the keys to
// every link on the site. Broadcast frames are never encrypted by either
// (ESP-NOW cannot), so nothing secret may ever travel by broadcast.
#define SECMESH_PMK         "KhanehNegarPMK01"
#define SECMESH_LMK         "KhanehNegarLMK01"

// ========== [Liveness timing] ==========
// A node beats every 10 s; the hub calls it dead after 30 s, i.e. three missed
// beats, and treats that as an alarm rather than a warning. The 3:1 ratio is the
// whole design — one lost frame must not kill a node, and a node that is really
// gone must be noticed while whoever removed it is still standing there.
// SECMESH_DEAD_MS is consumed by the hub (Nodes::tick), not by this library.
#define SECMESH_HEARTBEAT_MS   10000UL
#define SECMESH_DEAD_MS        30000UL

// ========== [Roster size] ==========
// Not an arbitrary limit and not a RAM budget: the ESP32 radio accepts at most 6
// *encrypted* ESP-NOW peers (20 total, but only 6 keyed), and every node is a
// keyed peer. The hub's NVS roster is sized from this. Raising it does nothing
// except let the hub adopt a seventh node whose addPeer() then fails.
#define SECMESH_MAX_NODES      6

// ========== [Alarm burst] ==========
// sendUrgent() sends the frame this many times, this far apart, because there is
// no ack to retry on and a single collision must not lose an alarm. 3 x 40 ms
// also means sendUrgent() blocks its caller for about 120 ms — see the note on
// sendUrgent() in SecMesh.cpp before raising either number.
#define SECMESH_ALARM_REPEAT   3
#define SECMESH_ALARM_GAP_MS   40
