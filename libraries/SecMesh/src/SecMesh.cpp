// =============================================================================
//  SecMesh.cpp — ESP-NOW transport: radio bring-up, frame build, send paths
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
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ========== [Receive callback shims] ==========
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
bool SecMeshClass::begin(NodeKind kind, SecMeshRxCb cb) {
  _kind = kind;
  _cb = cb;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

#if defined(ESP8266)
  wifi_set_channel(SECMESH_CHANNEL);
#else
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(SECMESH_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
#endif

  if (esp_now_init() != 0) return false;

#if defined(ESP8266)
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_set_kok((uint8_t *)SECMESH_PMK, 16);
#else
  esp_now_set_pmk((const uint8_t *)SECMESH_PMK);
#endif

  esp_now_register_recv_cb(secmeshRecv);
  WiFi.macAddress(_mac);

  addPeer(kBroadcast, false);
  _bootAt = millis();
  return true;
}

// ========== [Frame build] ==========
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

bool SecMeshClass::rawSend(const uint8_t mac[6], const SecFrame &f) {
  return esp_now_send((uint8_t *)mac, (uint8_t *)&f, sizeof(SecFrame)) == 0;
}

// ========== [Send paths] ==========
bool SecMeshClass::send(uint8_t code, uint8_t arg, uint16_t value) {
  if (!_hasHub) return false;
  return sendTo(_hubMac, code, arg, value);
}

// Three copies of the *same* frame, so a lost one cannot lose an alarm while the hub's
// counter check drops the duplicates. Building inside the loop scored a touch three times.
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
void SecMeshClass::_onRaw(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(SecFrame) || !_cb) return;
  SecFrame f;
  memcpy(&f, data, sizeof(SecFrame));
  if (f.magic != SECMESH_MAGIC || f.version != SECMESH_VERSION) return;
  if (_kind != KIND_HUB && _hasHub && !macEq(mac, _hubMac)) return;
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
