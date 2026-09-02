// =============================================================================
//  SecMeshPeers.cpp — peer table, claim, pairing, channel, tamper, heartbeat
//
//  Everything with memory or a clock. This translation unit owns who we are
//  allowed to talk to (the ESP-NOW peer table, including the keyed entries),
//  who we belong to (the hub claim in NVS or EEPROM), how that binding is
//  established (the two-frame pairing handshake), which channel the radio sits
//  on, and the periodic work a node does unattended — hunting for a hub,
//  heartbeating, and reporting its enclosure switch. It owns no framing and no
//  radio bring-up (SecMesh.cpp), and no alarm policy: it reports, the hub
//  decides.
// =============================================================================
#include "SecMesh.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
  #include <EEPROM.h>
#else
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <esp_now.h>
  #include <Preferences.h>
#endif

// Marker byte at EEPROM offset 0 on ESP8266. Erased flash reads as 0xFF and
// blank EEPROM as 0x00, so an arbitrary non-trivial value is what distinguishes
// "a claim was written here" from "never written". ESP32 needs no equivalent:
// NVS either has the key or it does not.
#define CLAIM_TAG 0xA5

// ========== [Peer table] ==========
// Add or replace one entry. Nothing can be unicast to, or received encrypted
// from, a MAC that is not in this table.
//
// Delete-then-add, rather than skipping an existing entry, is what makes this
// idempotent and makes it possible to *change* an entry: esp_now_add_peer()
// fails on a MAC that is already present, so a peer that exists in the wrong
// mode — plaintext when it should be encrypted — would otherwise be permanent.
//
// The ESP32 allows 20 peers in total but only 6 of them encrypted, which is the
// whole reason SECMESH_MAX_NODES is 6. The plaintext broadcast entry does not
// spend one of those six. SECMESH_LMK is one site-wide constant, so every link
// on the site shares a key (ADR-002).
bool SecMeshClass::addPeer(const uint8_t mac[6], bool encrypted) {
#if defined(ESP8266)
  if (esp_now_is_peer_exist((uint8_t *)mac)) esp_now_del_peer((uint8_t *)mac);
  return esp_now_add_peer((uint8_t *)mac, ESP_NOW_ROLE_COMBO, 0,
                          encrypted ? (uint8_t *)SECMESH_LMK : NULL,
                          encrypted ? 16 : 0) == 0;
#else
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  // 0 does not mean channel zero — it means "whatever channel the interface is
  // on right now". Pinning the entry to a number instead would break the moment
  // setChannel() hops, or the moment the hub's own WiFi association moves it,
  // and every peer would have to be rewritten after a hop. The ESP8266 branch
  // above passes 0 for the same reason.
  p.channel = 0;
  // ESP-NOW here rides the station interface, matching WiFi.mode(WIFI_STA) in
  // begin(). An entry on the wrong interface is silently unreachable.
  p.ifidx   = WIFI_IF_STA;
  p.encrypt = encrypted;
  if (encrypted) memcpy(p.lmk, SECMESH_LMK, 16);
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  return esp_now_add_peer(&p) == ESP_OK;
#endif
}

bool SecMeshClass::removePeer(const uint8_t mac[6]) {
#if defined(ESP8266)
  return esp_now_del_peer((uint8_t *)mac) == 0;
#else
  return esp_now_del_peer(mac) == ESP_OK;
#endif
}

// ========== [Claim persistence] ==========
// Six bytes of hub MAC, so a node comes back from a power cut still bound and
// the site does not re-enter a pairing window every time the lights flicker.
// ESP32: NVS via Preferences, namespace "secmesh", key "hub". ESP8266: EEPROM
// offset 0, tag byte then MAC, no checksum — a torn write yields a plausible
// but wrong MAC and the node has to be unclaimed by hand (ADR-004).
//
// writeClaim(nullptr, false) is safe: the ternaries never evaluate mac[i] on
// the invalid path. That is how clearClaim() reaches it.
static void writeClaim(const uint8_t mac[6], bool valid) {
#if defined(ESP8266)
  EEPROM.begin(16);
  EEPROM.write(0, valid ? CLAIM_TAG : 0);
  for (int i = 0; i < 6; i++) EEPROM.write(1 + i, valid ? mac[i] : 0);
  EEPROM.commit();
  EEPROM.end();
#else
  Preferences p;
  p.begin("secmesh", false);
  if (valid) p.putBytes("hub", mac, 6);
  else       p.remove("hub");
  p.end();
#endif
}

// Call once in setup(), after begin(). Restores the claim and, critically, the
// peer entry that goes with it.
void SecMeshClass::loadClaim() {
  uint8_t mac[6];
#if defined(ESP8266)
  EEPROM.begin(16);
  const uint8_t tag = EEPROM.read(0);
  for (int i = 0; i < 6; i++) mac[i] = EEPROM.read(1 + i);
  EEPROM.end();
  if (tag != CLAIM_TAG) return;
#else
  Preferences p;
  p.begin("secmesh", true);
  const size_t got = p.getBytes("hub", mac, 6);
  p.end();
  if (got != 6) return;
#endif
  memcpy(_hubMac, mac, 6);
  _hasHub = true;
  _flags |= SF_PAIRED;
  // Encrypted, matching setHub() and matching what the hub did to us. The two
  // peer tables have to agree on the mode or the link works in one direction
  // only. This argument was `false` once, and every reboot silently downgraded
  // a working pair into a mute node.
  addPeer(_hubMac, true);
}

// The physical unclaim gesture (BOOT held at power-up on the door node).
void SecMeshClass::clearClaim() {
  writeClaim(nullptr, false);
  forgetHub();
}

// ========== [Pairing handshake] ==========
// The low two bytes of a MAC, packed big-endian into the 16-bit `value` field.
// This is how a broadcast accept names one recipient without the frame carrying
// a MAC — there is no room for six more bytes and no version bump worth
// spending on one.
uint16_t SecMeshClass::macTail(const uint8_t mac[6]) {
  return (uint16_t)((mac[4] << 8) | mac[5]);
}

// Node side of the claim: bind to this hub, in RAM, in the peer table, and in
// flash, in that order.
//
// addPeer() here is not optional. Without it the node holds a hub MAC it is
// structurally unable to talk to: its unicast sends go to an unknown peer and
// fail, the hub's encrypted replies are dropped, and because _hasHub is set the
// tick() pair hunt never runs again either. That exact omission produced an
// endless pair-request loop and a hub beeping on every request.
void SecMeshClass::setHub(const uint8_t mac[6]) {
  memcpy(_hubMac, mac, 6);
  _hasHub = true;
  _flags |= SF_PAIRED;
  addPeer(_hubMac, true);
  writeClaim(mac, true);
}

// Hub side of the claim, and the answer to the pairing deadlock.
//
// The obvious implementation — add the node, then unicast it an encrypted
// accept — cannot work. ESP-NOW drops an encrypted frame from a peer that is
// not in the receiver's table, and the node cannot add the hub as a peer
// because the hub's MAC is exactly what the accept was supposed to deliver.
// Each side is waiting for the other to be addressable first.
//
// So the accept goes out as an unencrypted broadcast, which needs no peer entry
// on the receiving side, carrying macTail(node) in `value` as the address and
// the hub's current mode in `arg`. Every unclaimed node in earshot hears it and
// only the addressed one acts. The frame carries no secret — just "the hub at
// this source MAC accepts the node whose MAC ends in these two bytes" — so the
// LMK is never on the air. The cost is that the accept is unauthenticated;
// what bounds that is pairing mode being open for at most 120 s, the claim
// being first-come, and _onRaw going deaf to other sources afterwards
// (ADR-003).
void SecMeshClass::acceptPairing(const uint8_t mac[6], uint8_t mode) {
  addPeer(mac, true);
  broadcast(CMD_PAIR_ACCEPT, mode, macTail(mac));
}

// Node side of that broadcast. Two guards, and they are the entire access
// control on this path: an already-claimed node ignores accepts outright, so
// the claim is first-come and cannot be stolen while the node is up; and the
// tail must match our own MAC, so an accept meant for a different node in the
// same room is ignored. Returns true only if the claim was taken, which is the
// caller's cue to announce itself (node-door sends EV_BOOT).
bool SecMeshClass::onPairAccept(const uint8_t mac[6], const SecFrame &f) {
  if (_hasHub) return false;
  if (f.value != macTail(_mac)) return false;
  setHub(mac);
  return true;
}

// RAM only — the flash claim is untouched, so this alone does not unclaim a
// node across a reboot. clearClaim() is the one that does.
void SecMeshClass::forgetHub() {
  _hasHub = false;
  _flags &= ~SF_PAIRED;
  memset(_hubMac, 0, 6);
}

// arg = 1 means "I want an encrypted link", and it is hardcoded because both
// chip families must ask for the same thing: the hub adds the node with
// `encrypted = (arg == 1)`, while the node always adds the hub encrypted. When
// the ESP8266 branch asked for 0 and the ESP32 for 1, the two peer tables
// disagreed and the link died in one direction.
void SecMeshClass::requestPairing() {
  broadcast(EV_PAIR_REQUEST, 1, 0);
}

// ========== [Channel control] ==========
// 1..13 only; anything else is ignored rather than clamped, so a bad caller
// leaves the radio where it was instead of moving it somewhere useless.
//
// On ESP32 the channel change is bracketed by promiscuous mode for the same
// reason as in begin(): an unassociated station does not own its channel and
// esp_wifi_set_channel() will not stick. Promiscuous is entered only for the
// duration of the call. Peers survive the hop because they are registered with
// channel 0, "current channel".
//
// Note the hub never calls this. Its channel is wherever its own WiFi
// association put it, which is exactly why nodes have to hunt.
void SecMeshClass::setChannel(uint8_t ch) {
  if (ch < 1 || ch > 13) return;
  _channel = ch;
#if defined(ESP8266)
  wifi_set_channel(ch);
#else
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
#endif
}

// ========== [Tamper pin] ==========
// Enclosure switch, wired to close when the case is shut. INPUT_PULLUP means an
// unplugged or cut switch line reads open, which is the alarming direction —
// cutting the wire is itself tamper. The initial state is sampled here rather
// than assumed, so a node that boots with its case already off says so on its
// first frame instead of waiting for an edge that never comes.
void SecMeshClass::setTamperPin(int pin, bool openIsHigh) {
  _tamperPin = pin;
  _tamperActiveHigh = openIsHigh;
  pinMode(pin, INPUT_PULLUP);
  _tamperLast = (digitalRead(pin) == (openIsHigh ? HIGH : LOW));
  if (_tamperLast) _flags |= SF_TAMPER;
}

// ========== [Tick: tamper edge, pair hunt, heartbeat] ==========
// Call from loop() on every board. Cheap, and does nothing at all on the hub
// beyond the tamper pin.
void SecMeshClass::tick() {
  const uint32_t now = millis();

  // Edge-triggered, not level: the flag bit rides on every subsequent frame, so
  // the event only has to be announced once. There is no debounce here — a
  // chattering enclosure switch will send a burst, and each send blocks tick()
  // for ~120 ms inside sendUrgent(). The hub debounces its own tamper pin in
  // hub.ino; a node's is trusted because a real case switch is a slow contact.
  if (_tamperPin >= 0) {
    const bool open = (digitalRead(_tamperPin) == (_tamperActiveHigh ? HIGH : LOW));
    if (open != _tamperLast) {
      _tamperLast = open;
      if (open) _flags |= SF_TAMPER;
      else      _flags &= ~SF_TAMPER;
      sendUrgent(open ? EV_TAMPER_OPEN : EV_TAMPER_CLOSED);
    }
  }

  // The hub is the thing being paired *to*, it never heartbeats, and it must not
  // move its own channel. Everything below is node-only.
  if (_kind == KIND_HUB) return;

  // Orphan hunt. The node cannot assume channel 1: the hub's ESP-NOW channel
  // follows its WiFi association to the local server, so it may be anywhere in
  // 1..13. ESP-NOW does not scan and does not hop, so the only way to find a hub
  // is to stand on each channel in turn and shout. One channel every 400 ms is a
  // full sweep in ~5.2 s, which is fast enough that a first-time pairing looks
  // instant to whoever is holding the board.
  // (PROTOCOL.md still says two seconds; the code is the 400 ms above.)
  if (!_hasHub) {
    if (now - _lastPairTry > 400) {
      _lastPairTry = now;
      _hopChannel++;
      if (_hopChannel > 13) _hopChannel = 1;
      setChannel(_hopChannel);
      requestPairing();
    }
    return;
  }

  // Steady state. One heartbeat per SECMESH_HEARTBEAT_MS; the hub declares the
  // node dead after SECMESH_DEAD_MS, i.e. three missed beats, and treats that as
  // an alarm rather than a warning (ADR-006).
  if (now - _lastHeartbeat > SECMESH_HEARTBEAT_MS) {
    _lastHeartbeat = now;
    send(EV_HEARTBEAT, 0, 0);
    // Claimed but nothing heard for 90 s: most likely the hub reassociated and
    // dragged its channel with it, so resume hopping — one channel per beat,
    // slowly, because the claim is still valid and we are only looking for the
    // one hub we already belong to. _lastDown is set by _onRaw on any accepted
    // frame; the zero guard keeps a node that has never heard anything (e.g. a
    // claim restored from flash on a hub that is off) out of this path.
    if (_lastDown && now - _lastDown > 90000UL) {
      _hopChannel++;
      if (_hopChannel > 13) _hopChannel = 1;
      setChannel(_hopChannel);
    }
  }
}
