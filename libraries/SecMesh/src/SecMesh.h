// =============================================================================
//  SecMesh.h — public surface: the wire frame, the flag bits, the class
// =============================================================================
#pragma once
#include <Arduino.h>
#include "SecEvents.h"
#include "SecMeshConfig.h"

// ========== [Wire frame] ==========
// 15 bytes, packed. Widening or reordering any field is a protocol break: bump
// SECMESH_VERSION and reflash every board on the site.
struct __attribute__((packed)) SecFrame {
  uint16_t magic;
  uint8_t  version;
  uint8_t  kind;
  uint32_t counter;
  uint8_t  code;
  uint8_t  arg;
  uint16_t value;
  uint16_t supplyMv;
  uint8_t  flags;
};

// ========== [Flag bits] ==========
#define SF_TAMPER   0x01
#define SF_LOWBATT  0x02
#define SF_PAIRED   0x04

// ========== [Receive callback] ==========
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
  void acceptPairing(const uint8_t mac[6], uint8_t mode);
  bool onPairAccept(const uint8_t mac[6], const SecFrame &f);
  static uint16_t macTail(const uint8_t mac[6]);

  // ========== [Send paths] ==========
  bool send(uint8_t code, uint8_t arg = 0, uint16_t value = 0);
  bool sendUrgent(uint8_t code, uint8_t arg = 0, uint16_t value = 0);

  // ========== [Peer table and addressed sends] ==========
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
  uint8_t     _channel   = SECMESH_CHANNEL;

  int      _tamperPin        = -1;
  bool     _tamperActiveHigh = true;
  bool     _tamperLast       = false;
  uint32_t _lastHeartbeat    = 0;
  uint32_t _lastPairTry      = 0;
  uint8_t  _hopChannel       = 1;
  uint32_t _lastDown         = 0;
  uint32_t _bootAt           = 0;
};

extern SecMeshClass SecMesh;
