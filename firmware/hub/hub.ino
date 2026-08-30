#include <WiFi.h>
#include <SecMesh.h>
#include "config.h"
#include "nodes.h"
#include "state.h"
#include "io.h"
#include "ui.h"
#include "reader.h"

#define DECOY_AP    1
#define DECOY_SSID  "HOME-SECURITY-HUB"
#define DECOY_PASS  "guest12345"

#define RXQ_LEN 12
struct RxItem { uint8_t mac[6]; SecFrame f; };
static RxItem  rxq[RXQ_LEN];
static volatile uint8_t rxHead = 0, rxTail = 0;

static uint8_t lastStations = 0;
static bool    tamperLast   = false;

static void onMeshRx(const uint8_t mac[6], const SecFrame &f) {
  const uint8_t next = (rxHead + 1) % RXQ_LEN;
  if (next == rxTail) return;
  memcpy(rxq[rxHead].mac, mac, 6);
  rxq[rxHead].f = f;
  rxHead = next;
}

static void handleFrame(const uint8_t mac[6], const SecFrame &f) {
  if (f.code == EV_PAIR_REQUEST) {
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

  if (!SecMesh.begin(KIND_HUB, onMeshRx)) {
    Serial.println("FATAL: ESP-NOW would not start");
  }
  Serial.print("hub mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));

  Nodes::begin();
  Sec::begin();

  Reader::begin();
  Serial.printf("rfid reader: %s\n", Reader::present() ? "ok" : "NOT RESPONDING");

  pinMode(PIN_TAMPER, INPUT_PULLUP);
  tamperLast = digitalRead(PIN_TAMPER) == HIGH;

#if DECOY_AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(DECOY_SSID, DECOY_PASS, SECMESH_CHANNEL, 0, 4);
  Serial.println("decoy ap up");
#endif

  UI::invalidate();
}

void loop() {
  IO::tick();
  Reader::tick();
  Nodes::tick();
  Sec::tick();
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

  if (Reader::newCard()) {
    const String u = Reader::uid();
    Serial.print("card: ");
    Serial.println(u);
    if (UI::enrollArmed()) {
      Sec::enrollCard(u, UI::enrollAsDuress());
      UI::clearEnroll();
    } else {
      Sec::onCard(u);
    }
    UI::flashCardSeen(u);
    UI::invalidate();
  }

#if DECOY_AP
  const uint8_t st = WiFi.softAPgetStationNum();
  if (st > lastStations) {
    Sec::onDecoyProbe();
    UI::invalidate();
  }
  lastStations = st;
#endif
}
