#include <WiFi.h>
#include <SecMesh.h>
#include "config.h"
#include "store.h"
#include "nodes.h"
#include "state.h"
#include "io.h"
#include "src/ui/ui.h"
#include "fp.h"
#include "climate.h"
#include "net.h"

#define RXQ_LEN 12
struct RxItem { uint8_t mac[6]; SecFrame f; };
static RxItem  rxq[RXQ_LEN];
static volatile uint8_t rxHead = 0, rxTail = 0;

static bool tamperLast = false;

static void onMeshRx(const uint8_t mac[6], const SecFrame &f) {
  const uint8_t next = (rxHead + 1) % RXQ_LEN;
  if (next == rxTail) return;
  memcpy(rxq[rxHead].mac, mac, 6);
  rxq[rxHead].f = f;
  rxHead = next;
}

static bool knownKind(uint8_t k) {
  return k == KIND_DOOR || k == KIND_WINDOW || k == KIND_LASER || k == KIND_PANEL;
}

static void handleFrame(const uint8_t mac[6], const SecFrame &f) {
  if (f.code == EV_PAIR_REQUEST) {
    if (!knownKind(f.kind)) return;
    NodeRec *n = Nodes::adopt(mac, f.kind, f.arg == 1);
    if (n) {
      SecMesh.sendTo(mac, CMD_PAIR_ACCEPT, Sec::mode());
      IO::beep(2, 60, 60);
      UI::invalidate();
    }
    return;
  }

  NodeRec *n = Nodes::find(mac);
  if (!n) return;
  if (!Nodes::acceptCounter(n, f.counter)) return;
  Nodes::touchSeen(n, f);
  Sec::onNodeEvent(n, f);
  UI::invalidate();
}

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
  Serial.printf("fingerprint: %s, %d templates\n",
                FP::present() ? "ok" : "NOT RESPONDING", FP::templateCount());

  UI::boot("sensors");
  Climate::begin();

  pinMode(PIN_TAMPER, INPUT_PULLUP);
  tamperLast = digitalRead(PIN_TAMPER) == HIGH;

  UI::boot("network");
  Net::begin();

  UI::boot("ready");
  delay(400);
  UI::ready();
}

static void handleFingerprint() {
  switch (FP::take()) {
    case FP::RES_MATCH:
      Serial.printf("finger %u score %u\n", FP::matchedId(), FP::confidence());
      Sec::onFinger(FP::matchedId(), FP::confidence());
      UI::invalidate();
      break;
    case FP::RES_NOMATCH:
      Serial.println("finger not recognised");
      Sec::onUnknownFinger();
      UI::invalidate();
      break;
    case FP::RES_ENROLLED: {
      const uint16_t id = FP::lastEnrolledId();
      const bool dur = FP::lastEnrollDuress();
      Store::addFinger(id, String("finger ") + String(id), dur);
      Serial.printf("enrolled id %u%s\n", id, dur ? " (duress)" : "");
      IO::beep(2, 80, 80);
      UI::invalidate();
      break;
    }
    case FP::RES_ENROLL_FAIL:
      Serial.println("enrol failed");
      IO::reject();
      UI::invalidate();
      break;
    default:
      break;
  }
}

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
  if (tamper != tamperLast) {
    tamperLast = tamper;
    Sec::onHubTamper(tamper);
    UI::invalidate();
  }

  handleFingerprint();
}
