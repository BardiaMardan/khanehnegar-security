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

#define CLAIM_TAG 0xA5

bool SecMeshClass::addPeer(const uint8_t mac[6], bool encrypted) {
#if defined(ESP8266)
  if (esp_now_is_peer_exist((uint8_t *)mac)) esp_now_del_peer((uint8_t *)mac);
  return esp_now_add_peer((uint8_t *)mac, ESP_NOW_ROLE_COMBO, 0,
                          encrypted ? (uint8_t *)SECMESH_LMK : NULL,
                          encrypted ? 16 : 0) == 0;
#else
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;
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
  addPeer(_hubMac, false);
}

void SecMeshClass::clearClaim() {
  writeClaim(nullptr, false);
  forgetHub();
}

void SecMeshClass::setHub(const uint8_t mac[6]) {
  memcpy(_hubMac, mac, 6);
  _hasHub = true;
  _flags |= SF_PAIRED;
  writeClaim(mac, true);
}

void SecMeshClass::forgetHub() {
  _hasHub = false;
  _flags &= ~SF_PAIRED;
  memset(_hubMac, 0, 6);
}

void SecMeshClass::requestPairing() {
#if defined(ESP8266)
  const uint8_t wantsEncryption = 0;
#else
  const uint8_t wantsEncryption = 1;
#endif
  broadcast(EV_PAIR_REQUEST, wantsEncryption, 0);
}

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

void SecMeshClass::setTamperPin(int pin, bool openIsHigh) {
  _tamperPin = pin;
  _tamperActiveHigh = openIsHigh;
  pinMode(pin, INPUT_PULLUP);
  _tamperLast = (digitalRead(pin) == (openIsHigh ? HIGH : LOW));
  if (_tamperLast) _flags |= SF_TAMPER;
}

void SecMeshClass::tick() {
  const uint32_t now = millis();

  if (_tamperPin >= 0) {
    const bool open = (digitalRead(_tamperPin) == (_tamperActiveHigh ? HIGH : LOW));
    if (open != _tamperLast) {
      _tamperLast = open;
      if (open) _flags |= SF_TAMPER;
      else      _flags &= ~SF_TAMPER;
      sendUrgent(open ? EV_TAMPER_OPEN : EV_TAMPER_CLOSED);
    }
  }

  if (_kind == KIND_HUB) return;

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

  if (now - _lastHeartbeat > SECMESH_HEARTBEAT_MS) {
    _lastHeartbeat = now;
    send(EV_HEARTBEAT, 0, 0);
    if (_lastDown && now - _lastDown > 90000UL) {
      _hopChannel++;
      if (_hopChannel > 13) _hopChannel = 1;
      setChannel(_hopChannel);
    }
  }
}
