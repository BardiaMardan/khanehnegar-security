// =============================================================================
//  node-laser.ino — strobed laser tripwire on an ESP32 (LilyGO T-Display)
// =============================================================================

#include <SecMesh.h>

// ========== [Pins] ==========
// Every signal sits on one side of the header — 16 / SVN / 5 / 4 / 2 / 0 / 14 / 12 / 13 / 15
// / RX / TX — so the whole node is one ribbon to one row. Power is the exception: 3V3 and GND
// only exist on the other side.
//
// SVN is GPIO39, an ADC1 channel. The LDR has to be on ADC1: ADC2 stops answering the moment
// WiFi or ESP-NOW is running. Input-only with no pull-up is fine for a divider.
#define PIN_LDR         39
#define PIN_LASER       16
#define PIN_CASE         4
#define PIN_LED         13
#define PIN_UNCLAIM      0

#define ENABLE_CASE_TAMPER 1

// ========== [Detection tuning] ==========
// A CdS photoresistor needs tens of milliseconds to respond, so each half of the strobe
// settles before it is read. One scan is 30 ms, three confirm a break: ~180 ms to detect.
#define SAMPLE_MS         60UL
#define SETTLE_MS         15UL
#define MIN_CONTRAST      300
#define BREAK_CONFIRM       3
#define CLEAR_CONFIRM       5

static uint8_t  hubMode  = MODE_DISARMED;
static uint32_t lastScan = 0;
static uint8_t  breaks   = 0;
static uint8_t  clears   = 0;
static bool     tripped  = false;
static int      baseline = 0;

static void showLed(bool on) { digitalWrite(PIN_LED, on ? HIGH : LOW); }

// ========== [Downlink] ==========
static void onRx(const uint8_t mac[6], const SecFrame &f) {
  switch (f.code) {
    case CMD_PAIR_ACCEPT:
      if (SecMesh.onPairAccept(mac, f)) {
        hubMode = f.arg;
        SecMesh.send(EV_BOOT);
      }
      break;
    case CMD_STATE:
    case CMD_PING:
      hubMode = f.arg;
      break;
    case CMD_IDENTIFY:
      for (int i = 0; i < 6; i++) { showLed(i % 2); delay(120); }
      break;
    default:
      break;
  }
}

// ========== [The strobe] ==========
// Not "is it dark" but "is the beam there". Ambient light appears in both samples and
// cancels, so a torch aimed at the sensor cannot mask a broken beam.
static int contrast() {
  digitalWrite(PIN_LASER, LOW);
  delay(SETTLE_MS);
  const int dark = analogRead(PIN_LDR);

  digitalWrite(PIN_LASER, HIGH);
  delay(SETTLE_MS);
  const int lit = analogRead(PIN_LDR);

  return lit - dark;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_LED, OUTPUT);
  showLed(true);
  delay(500);
  showLed(false);
  delay(150);
  DBG("LED self-test done on GPIO %d", PIN_LED);
  pinMode(PIN_LASER, OUTPUT);
  digitalWrite(PIN_LASER, HIGH);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);

  if (!SecMesh.begin(KIND_LASER, onRx)) {
    Serial.println("FATAL: ESP-NOW would not start");
  }

  pinMode(PIN_UNCLAIM, INPUT_PULLUP);
  if (digitalRead(PIN_UNCLAIM) == LOW) {
    SecMesh.clearClaim();
    for (int i = 0; i < 10; i++) { showLed(i % 2); delay(80); }
    Serial.println("claim cleared");
  } else {
    SecMesh.loadClaim();
    if (SecMesh.hasHub()) SecMesh.send(EV_BOOT);
  }

#if ENABLE_CASE_TAMPER
  SecMesh.setTamperPin(PIN_CASE, true);
#endif

  delay(500);
  int sum = 0;
  for (int i = 0; i < 8; i++) { sum += contrast(); delay(30); }
  baseline = sum / 8;

  Serial.print("laser node mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));
  Serial.printf("beam contrast baseline: %d\n", baseline);
  if (baseline < MIN_CONTRAST) {
    Serial.println("WARNING: weak beam. aim the laser at the sensor.");
  }
}

void loop() {
  SecMesh.tick();

  // Slow blink = hunting for a hub. Solid = claimed and the beam is intact. Fast blink = beam
  // broken. Solid-on for the healthy case matters: an unlit LED should mean a dead node, not
  // a working one.
  if (!SecMesh.hasHub()) showLed((millis() / 400) % 2);
  else if (tripped)      showLed((millis() / 100) % 2);
  else                   showLed(true);

  const uint32_t now = millis();
  if (now - lastScan < SAMPLE_MS) return;
  lastScan = now;

  const int c = contrast();
  const int limit = baseline / 2;

  if (c < limit) {
    clears = 0;
    if (breaks < 255) breaks++;
    if (!tripped && breaks >= BREAK_CONFIRM) {
      tripped = true;
      SecMesh.sendUrgent(EV_LASER_CUT, 0, (uint16_t)(c < 0 ? 0 : c));
      DBG("beam cut, contrast %d of %d", c, baseline);
    }
  } else {
    breaks = 0;
    if (clears < 255) clears++;
    if (tripped && clears >= CLEAR_CONFIRM) {
      tripped = false;
      DBG("beam restored, contrast %d", c);
    }
  }
}
