// =============================================================================
//  hub.ino — the sketch: boot order, the main loop, and the ESP-NOW handoff
// =============================================================================

#include <WiFi.h>
#include <SecMesh.h>
#include "config.h"
#include "store.h"
#include "journal.h"
#include "access.h"
#include "fingers.h"
#include "nodes.h"
#include "state.h"
#include "io.h"
#include "src/ui/ui.h"
#include "fp.h"
#include "climate.h"
#include "net.h"

// ========== [ESP-NOW receive queue] ==========
#define RXQ_LEN 12
struct RxItem { uint8_t mac[6]; SecFrame f; };
static RxItem  rxq[RXQ_LEN];
static volatile uint8_t rxHead = 0, rxTail = 0;

static bool tamperLast = false;
static bool tamperRaw = false;
static uint32_t tamperEdge = 0;
static uint32_t lastAccept = 0;

static void onMeshRx(const uint8_t mac[6], const SecFrame &f) {
  const uint8_t next = (rxHead + 1) % RXQ_LEN;
  if (next == rxTail) return;
  memcpy(rxq[rxHead].mac, mac, 6);
  rxq[rxHead].f = f;
  rxHead = next;
}

// ========== [Frame handling] ==========
static bool knownKind(uint8_t k) {
  return k == KIND_DOOR || k == KIND_WINDOW || k == KIND_LASER || k == KIND_PANEL;
}

static void handleFrame(const uint8_t mac[6], const SecFrame &f) {
  if (f.code == EV_PAIR_REQUEST) {
    if (!knownKind(f.kind)) return;
    bool fresh = false;
    NodeRec *n = Nodes::adopt(mac, f.kind, f.arg == 1, fresh);
    if (!n) return;

    const uint32_t now = millis();
    if (fresh || now - lastAccept > 2000) {
      lastAccept = now;
      SecMesh.acceptPairing(mac, Sec::mode());
    }
    if (fresh) {
      IO::beep(2, 60, 60);
      Sec::showToast("MODULE PAIRED", 1);
      UI::invalidate();
    }
    return;
  }

  NodeRec *n = Nodes::find(mac);
  if (!n) return;

  // A node announces EV_BOOT after a restart, when its own counter is back at 1. Clearing
  // our stored value here is what lets it rejoin immediately instead of having every frame
  // read as a replay.
  if (f.code == EV_BOOT) Nodes::resetCounter(n);

  if (!Nodes::acceptCounter(n, f.counter)) return;

  const bool wasDead = n->dead;
  Nodes::touchSeen(n, f);

  // Tell it the current mode the moment it reappears, so a node that missed an arm while it
  // was away does not sit there disarmed.
  if (f.code == EV_BOOT || wasDead) SecMesh.sendTo(mac, CMD_STATE, Sec::mode());

  Sec::onNodeEvent(n, f);
  if (f.code != EV_HEARTBEAT) UI::wake();
  UI::invalidate();
}

// ========== [Boot] ==========
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("khanehnegar hub booting");

  IO::begin();
  UI::begin();

  UI::boot("storage");
  const bool sd = Store::begin();
  Serial.printf("sd card: %s\n", sd ? "ok" : "MISSING");

  UI::boot("radio");
  if (!SecMesh.begin(KIND_HUB, onMeshRx)) {
    Serial.println("FATAL: ESP-NOW would not start");
  }
  Serial.print("hub mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));

  Nodes::begin();
  Sec::begin();

  UI::boot("fingerprint");
  FP::begin();
  if (Fingers::count() == 0) {
    const int adopted = FP::adoptExisting();
    if (adopted) Serial.printf("fingers: adopted %d already in the sensor\n", adopted);
  }
  Serial.printf("fingerprint: %s, %d templates, %d enrolled\n",
                FP::present() ? "ok" : "NOT RESPONDING",
                FP::templateCount(), Fingers::count());

  UI::boot("sensors");
  Climate::begin();

  pinMode(PIN_TAMPER, INPUT_PULLUP);
  tamperLast = tamperRaw = digitalRead(PIN_TAMPER) == HIGH;

  UI::boot("network");
  Net::begin();

  UI::boot("ready");
  delay(400);
  UI::ready();
}

// ========== [Fingerprint results] ==========
static void handleFingerprint() {
  switch (FP::take()) {
    case FP::RES_MATCH:
      Serial.printf("finger %u score %u\n", FP::matchedId(), FP::confidence());
      Sec::onFinger(FP::matchedId(), FP::confidence());
      UI::wake();
      UI::invalidate();
      break;
    case FP::RES_NOMATCH:
      Serial.println("finger not recognised");
      Sec::onUnknownFinger();
      UI::wake();
      UI::invalidate();
      break;
    case FP::RES_ENROLLED:
      Sec::onEnrolled(FP::lastEnrolledId());
      Serial.printf("enrolled id %u\n", FP::lastEnrolledId());
      UI::wake();
      UI::invalidate();
      break;
    case FP::RES_ENROLL_FAIL:
      Serial.println("enrol failed");
      Sec::showToast("ENROL FAILED", 3);
      IO::reject();
      UI::wake();
      UI::invalidate();
      break;
    default:
      break;
  }
}

// ========== [Main loop] ==========
void loop() {
  IO::tick();
  FP::tick();
  Climate::tick();
  Nodes::tick();
  Sec::tick();
  Net::tick();
  UI::tick();

  while (rxTail != rxHead) {
    const RxItem item = rxq[rxTail];
    rxTail = (rxTail + 1) % RXQ_LEN;
    handleFrame(item.mac, item.f);
  }

  const bool tamper = digitalRead(PIN_TAMPER) == HIGH;
  if (tamper != tamperRaw) {
    tamperRaw = tamper;
    tamperEdge = millis();
  } else if (tamper != tamperLast && millis() - tamperEdge > 250) {
    tamperLast = tamper;
    Sec::onHubTamper(tamper);
    UI::wake();
    UI::invalidate();
  }

  handleFingerprint();
}
