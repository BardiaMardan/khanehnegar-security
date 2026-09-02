// =============================================================================
//  SecMesh.h — public surface: the wire frame, the flag bits, the class
//
//  Declares the one packed frame every board on the site exchanges and the
//  singleton that carries it. This header owns the shape of the protocol —
//  field widths, flag bits and the method surface a node sketch is allowed to
//  touch — and owns no behaviour: radio bring-up and the send paths live in
//  SecMesh.cpp, and the peer table, claim persistence, pairing handshake,
//  channel control and tick state machine live in SecMeshPeers.cpp. On-air
//  numbering is in SecEvents.h, tunables and keys in SecMeshConfig.h.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "SecEvents.h"
#include "SecMeshConfig.h"

// ========== [Wire frame] ==========
// 15 bytes, packed, byte-identical in both directions and on both chip
// families. Offsets, in order:
//   0  magic 2 | 2 version 1 | 3 kind 1 | 4  counter  4 | 8  code  1
//   9  arg   1 | 10 value   2 | 12 supplyMv 2 | 14 flags 1
// __attribute__((packed)) is what holds that at 15. Unpacked, the compiler is
// free to align `counter` and pad the tail, and sizeof() becomes 16 — the
// receiver's only structural check is `len != sizeof(SecFrame)`, so a board
// that disagreed by one byte would have every frame silently dropped by every
// other board on the site. Every width is pinned for the same reason. Adding,
// widening or reordering a field is a protocol break: bump SECMESH_VERSION and
// reflash the whole site.
struct __attribute__((packed)) SecFrame {
  uint16_t magic;
  uint8_t  version;
  uint8_t  kind;
  // Bumped on every frame this board builds. The hub keeps the last accepted
  // value per MAC and demands strictly greater, or a backwards jump of more
  // than 1000 which it reads as a reboot (Nodes::acceptCounter, ADR-005).
  uint32_t counter;
  // SecEvent when a node sends, SecCommand when the hub sends. Same byte.
  uint8_t  code;
  uint8_t  arg;
  uint16_t value;
  uint16_t supplyMv;
  uint8_t  flags;
};

// ========== [Flag bits] ==========
// The sender's own health, carried on every frame so the hub never has to ask.
// SF_LOWBATT has no producer yet: nothing calls setSupplyMv(), so supplyMv is
// 0 and this bit is never raised.
#define SF_TAMPER   0x01
#define SF_LOWBATT  0x02
#define SF_PAIRED   0x04

// ========== [Receive callback] ==========
// Called from the WiFi task, not from loop(). Do not write NVS, draw, or block
// inside it — copy the frame out and return. hub.ino pushes into a ring buffer
// and drains it from loop() for exactly this reason.
typedef void (*SecMeshRxCb)(const uint8_t mac[6], const SecFrame &frame);

class SecMeshClass {
 public:
  // ========== [Bring-up] ==========
  bool begin(NodeKind kind, SecMeshRxCb cb);

  // ========== [Hub binding and claim persistence] ==========
  void requestPairing();
  bool hasHub() const { return _hasHub; }
  const uint8_t *hubMac() const { return _hubMac; }
  void setHub(const uint8_t mac[6]);
  void forgetHub();
  void loadClaim();
  void clearClaim();

  // ========== [Pairing handshake] ==========
  // acceptPairing() is the hub side, onPairAccept() the node side. macTail()
  // is how the broadcast accept names one node without spending a MAC field.
  void acceptPairing(const uint8_t mac[6], uint8_t mode);
  bool onPairAccept(const uint8_t mac[6], const SecFrame &f);
  static uint16_t macTail(const uint8_t mac[6]);

  // ========== [Send paths] ==========
  // sendUrgent() blocks the caller for SECMESH_ALARM_REPEAT *
  // SECMESH_ALARM_GAP_MS (~120 ms today) and is not idempotent at the hub —
  // see the note in SecMesh.cpp and docs/SecMesh.md.
  bool send(uint8_t code, uint8_t arg = 0, uint16_t value = 0);
  bool sendUrgent(uint8_t code, uint8_t arg = 0, uint16_t value = 0);

  // ========== [Peer table and addressed sends] ==========
  // Nothing can be sent to, or received encrypted from, a MAC that is not in
  // the peer table. ESP32 allows 20 peers of which at most 6 may be
  // encrypted, which is where SECMESH_MAX_NODES comes from.
  bool sendTo(const uint8_t mac[6], uint8_t code, uint8_t arg = 0, uint16_t value = 0);
  bool addPeer(const uint8_t mac[6], bool encrypted = true);
  bool removePeer(const uint8_t mac[6]);
  bool broadcast(uint8_t code, uint8_t arg = 0, uint16_t value = 0);

  // ========== [Housekeeping] ==========
  void tick();
  void setChannel(uint8_t ch);
  uint8_t channel() const { return _channel; }
  void setTamperPin(int pin, bool openIsHigh = true);
  void setSupplyMv(uint16_t mv) { _supplyMv = mv; }
  uint8_t flags() const { return _flags; }

  // ========== [Identity] ==========
  const uint8_t *selfMac();
  static String macToStr(const uint8_t mac[6]);
  static bool macEq(const uint8_t a[6], const uint8_t b[6]);

  // ========== [Callback entry point] ==========
  // Public only because the three C receive shims in SecMesh.cpp are free
  // functions and have to reach it. Sketches must not call this.
  void _onRaw(const uint8_t *mac, const uint8_t *data, int len);

 private:
  bool rawSend(const uint8_t mac[6], const SecFrame &f);
  void buildFrame(SecFrame &f, uint8_t code, uint8_t arg, uint16_t value);

  // ========== [State] ==========
  NodeKind    _kind      = KIND_HUB;
  SecMeshRxCb _cb        = nullptr;
  uint32_t    _counter   = 0;
  uint8_t     _hubMac[6] = {0};
  bool        _hasHub    = false;
  uint8_t     _flags     = 0;
  uint16_t    _supplyMv  = 0;
  uint8_t     _mac[6]    = {0};
  // What we last asked the radio for, not necessarily where the radio is: on
  // the hub the station association drags the channel around underneath us.
  uint8_t     _channel   = SECMESH_CHANNEL;

  int      _tamperPin        = -1;
  bool     _tamperActiveHigh = true;
  bool     _tamperLast       = false;
  uint32_t _lastHeartbeat    = 0;
  uint32_t _lastPairTry      = 0;
  // Channel currently being probed while orphaned; walks 1..13 and wraps.
  uint8_t  _hopChannel       = 1;
  // millis() of the last frame accepted by _onRaw. Zero means nothing has ever
  // been heard, which is why the silence check in tick() guards on it.
  uint32_t _lastDown         = 0;
};

extern SecMeshClass SecMesh;
