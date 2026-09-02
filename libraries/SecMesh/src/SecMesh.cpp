// =============================================================================
//  SecMesh.cpp — ESP-NOW transport: radio bring-up, frame build, send paths
//
//  Owns everything between a caller's event code and the air: putting the
//  station interface on the home channel, starting ESP-NOW, installing the
//  primary key, registering the receive shim, stamping frames, and the four
//  send paths. It deliberately owns no policy — it does not decide who a peer
//  is, when to pair, what a claim means or when to heartbeat (SecMeshPeers.cpp),
//  and it never interprets a `code` (SecEvents.h, and the hub's state.cpp).
//  The only frame it inspects is on receive, and only far enough to reject
//  something that is not ours.
// =============================================================================
#include "SecMesh.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
#else
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
#endif

SecMeshClass SecMesh;

// ========== [Broadcast address] ==========
// FF:FF:FF:FF:FF:FF has to be an actual peer table entry like any other MAC,
// because ESP-NOW refuses to send to an address it does not hold. It is added
// once in begin() and never removed.
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ========== [Receive callback shims] ==========
// Three variants because the ESP-NOW receive signature is not portable and has
// changed under us:
//   ESP8266 NONOS SDK  — (uint8_t *mac, uint8_t *data, uint8_t len), non-const,
//                        length is a single byte.
//   arduino-esp32 3.x  — (const esp_now_recv_info_t *info, ...), where the
//                        sender MAC moved inside the struct as info->src_addr
//                        so the callback could also see the destination
//                        address and the RX control block.
//   arduino-esp32 2.x  — (const uint8_t *mac, const uint8_t *data, int len).
// ESP_ARDUINO_VERSION_MAJOR is the only thing that separates the last two, and
// it does not exist at all on older cores, hence the defined() guard. All three
// do nothing but funnel into _onRaw so the rest of the library sees one shape.
// This runs in the WiFi task on both families.
#if defined(ESP8266)
static void secmeshRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  SecMesh._onRaw(mac, data, len);
}
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void secmeshRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  SecMesh._onRaw(info->src_addr, data, len);
}
#else
static void secmeshRecv(const uint8_t *mac, const uint8_t *data, int len) {
  SecMesh._onRaw(mac, data, len);
}
#endif

// ========== [Radio bring-up] ==========
// Order matters: interface up and on channel, then esp_now_init(), then the
// key, then the callback. esp_now_init() fails outright without an initialised
// station interface, and a key installed before init is discarded.
bool SecMeshClass::begin(NodeKind kind, SecMeshRxCb cb) {
  _kind = kind;
  _cb = cb;

  // STA and nothing else. ESP-NOW rides the station interface (peers are added
  // with ifidx WIFI_IF_STA), and disconnect() drops any stored association so
  // it cannot drag the channel away underneath the mesh. On the hub, Net::begin()
  // does associate afterwards, which is precisely why orphaned nodes hunt.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

#if defined(ESP8266)
  wifi_set_channel(SECMESH_CHANNEL);
#else
  // esp_wifi_set_channel() is only honoured when the interface is in a state
  // that owns its channel: an unassociated station is not, and the call is
  // rejected or immediately undone. Promiscuous mode is that state, so it is
  // switched on purely to bracket the change and switched off again — we do
  // not want the sniffer callbacks or the extra RX load.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(SECMESH_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
#endif

  if (esp_now_init() != 0) return false;

#if defined(ESP8266)
  // The 8266 API needs an explicit role before it will do anything; COMBO is
  // "talk and listen". Its PMK setter is called set_kok (key-of-keys).
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_set_kok((uint8_t *)SECMESH_PMK, 16);
#else
  esp_now_set_pmk((const uint8_t *)SECMESH_PMK);
#endif

  esp_now_register_recv_cb(secmeshRecv);
  WiFi.macAddress(_mac);

  // Unconditional, and unencrypted, on every board including the hub. ESP-NOW
  // cannot encrypt broadcast — there is no per-peer key for "everyone" — so a
  // broadcast peer must be added with encrypt = false or the add fails and
  // nothing can be broadcast at all. Both ends of the handshake need it: the
  // node broadcasts EV_PAIR_REQUEST, and the hub answers with a broadcast
  // CMD_PAIR_ACCEPT (see acceptPairing).
  addPeer(kBroadcast, false);
  return true;
}

// ========== [Frame build] ==========
// Pre-increment, so the first frame a board ever sends carries counter 1 and 0
// is reserved as "nothing accepted yet" on the hub side. The counter is per
// board, not per peer or per event, and it is RAM only: a reboot restarts it at
// 1, which is the case ADR-005's backwards-jump rule exists to absorb.
void SecMeshClass::buildFrame(SecFrame &f, uint8_t code, uint8_t arg, uint16_t value) {
  f.magic    = SECMESH_MAGIC;
  f.version  = SECMESH_VERSION;
  f.kind     = _kind;
  f.counter  = ++_counter;
  f.code     = code;
  f.arg      = arg;
  f.value    = value;
  f.supplyMv = _supplyMv;
  f.flags    = _flags;
}

// Whether the frame goes out in clear or as AES-128-CCM is not decided here: it
// is a property of the peer table entry for `mac`. Same call, encrypted to a
// claimed hub, plaintext to the broadcast address.
// The return value means "the radio accepted it for transmission", not
// "delivered" — no send callback is registered, so there is no delivery
// confirmation anywhere in this library.
bool SecMeshClass::rawSend(const uint8_t mac[6], const SecFrame &f) {
  return esp_now_send((uint8_t *)mac, (uint8_t *)&f, sizeof(SecFrame)) == 0;
}

// ========== [Send paths] ==========
bool SecMeshClass::send(uint8_t code, uint8_t arg, uint16_t value) {
  if (!_hasHub) return false;
  return sendTo(_hubMac, code, arg, value);
}

// Three copies, SECMESH_ALARM_GAP_MS apart, so one collision or one lost frame
// cannot lose an alarm. There is no ack to retry on, so the retry is blind.
//
// buildFrame() is deliberately outside the loop: all three copies carry the
// same counter, so the hub's strictly-greater check accepts the first and drops
// the other two. The burst costs nothing at the receiver. Building inside the
// loop gave each copy its own counter, and a single handle touch was scored
// three times — 25 * 3 = 75 against a threshold of 70, which alarmed on the
// first touch and skipped the entry delay on a door open.
//
// It blocks the caller for REPEAT * GAP (~120 ms), including a trailing gap
// after the last send, and tick()'s tamper edge calls it.
bool SecMeshClass::sendUrgent(uint8_t code, uint8_t arg, uint16_t value) {
  if (!_hasHub) return false;
  SecFrame f;
  buildFrame(f, code, arg, value);
  bool ok = false;
  for (uint8_t i = 0; i < SECMESH_ALARM_REPEAT; i++) {
    ok |= rawSend(_hubMac, f);
    delay(SECMESH_ALARM_GAP_MS);
  }
  return ok;
}

bool SecMeshClass::sendTo(const uint8_t mac[6], uint8_t code, uint8_t arg, uint16_t value) {
  SecFrame f;
  buildFrame(f, code, arg, value);
  return rawSend(mac, f);
}

bool SecMeshClass::broadcast(uint8_t code, uint8_t arg, uint16_t value) {
  return sendTo(kBroadcast, code, arg, value);
}

// ========== [Receive path] ==========
// Runs in the WiFi task. Three cheap rejections before the user callback:
// wrong length (anything that is not our struct, including another sketch's
// ESP-NOW traffic), wrong magic, wrong protocol version.
void SecMeshClass::_onRaw(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(SecFrame) || !_cb) return;
  SecFrame f;
  memcpy(&f, data, sizeof(SecFrame));
  if (f.magic != SECMESH_MAGIC || f.version != SECMESH_VERSION) return;
  // A claimed node is deaf to every source except its own hub. This is what
  // closes the hole opened by the unauthenticated broadcast accept: once the
  // claim exists, a forged accept — or a second hub, or a neighbour's site
  // running the same firmware and the same compile-time keys — has nothing to
  // talk to. The hub itself does not filter here; it has to hear pair requests
  // from strangers, and gates them in hub.ino with Nodes::find/adopt instead.
  if (_kind != KIND_HUB && _hasHub && !macEq(mac, _hubMac)) return;
  // Any accepted frame counts as proof the downlink is alive, whatever it said.
  // tick() uses this to decide the hub has moved channel.
  _lastDown = millis();
  _cb(mac, f);
}

// ========== [Identity helpers] ==========
const uint8_t *SecMeshClass::selfMac() { return _mac; }

bool SecMeshClass::macEq(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

String SecMeshClass::macToStr(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}
