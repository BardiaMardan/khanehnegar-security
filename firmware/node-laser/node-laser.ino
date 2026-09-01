#include <SecMesh.h>

#define PIN_LDR         34
#define PIN_LASER       26
#define PIN_CASE        25
#define PIN_LED          2
#define PIN_UNCLAIM      0

#define ENABLE_CASE_TAMPER 1

#define SAMPLE_MS         40UL
#define SETTLE_US       4000
#define MIN_CONTRAST      350
#define BREAK_CONFIRM       3
#define CLEAR_CONFIRM       5

static uint8_t  hubMode  = MODE_DISARMED;
static uint32_t lastScan = 0;
static uint8_t  breaks   = 0;
static uint8_t  clears   = 0;
static bool     tripped  = false;
static int      baseline = 0;

static void onRx(const uint8_t mac[6], const SecFrame &f) {
  switch (f.code) {
    case CMD_PAIR_ACCEPT:
      SecMesh.setHub(mac);
      hubMode = f.arg;
      break;
    case CMD_STATE:
      hubMode = f.arg;
      break;
    case CMD_IDENTIFY:
      for (int i = 0; i < 6; i++) { digitalWrite(PIN_LED, i % 2); delay(120); }
      break;
    default:
      break;
  }
}

static int contrast() {
  digitalWrite(PIN_LASER, LOW);
  delayMicroseconds(SETTLE_US);
  const int dark = analogRead(PIN_LDR);

  digitalWrite(PIN_LASER, HIGH);
  delayMicroseconds(SETTLE_US);
  const int lit = analogRead(PIN_LDR);

  return lit - dark;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
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
    for (int i = 0; i < 10; i++) { digitalWrite(PIN_LED, i % 2); delay(80); }
    Serial.println("claim cleared");
  } else {
    SecMesh.loadClaim();
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

  digitalWrite(PIN_LED, SecMesh.hasHub() ? (tripped ? HIGH : LOW)
                                         : ((millis() / 400) % 2));

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
      SecMesh.sendUrgent(EV_LASER_CUT, 0, (uint16_t)c);
      Serial.printf("beam cut, contrast %d\n", c);
    }
  } else {
    breaks = 0;
    if (clears < 255) clears++;
    if (tripped && clears >= CLEAR_CONFIRM) {
      tripped = false;
      Serial.println("beam restored");
    }
  }
}
