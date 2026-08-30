#include <Wire.h>
#include <math.h>
#include <SecMesh.h>

#define PIN_SDA          4
#define PIN_SCL          5
#define PIN_DOOR        14
#define PIN_CASE        12
#define PIN_LED          2
#define PIN_UNCLAIM      0

#define ENABLE_CASE_TAMPER 1

#define ADXL_ADDR       0x53
#define ADXL_BW_RATE    0x2C
#define ADXL_POWER_CTL  0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0     0x32
#define ADXL_SCALE_G    0.0039f

#define IMPACT_G         1.30f
#define AGITATE_G        0.09f
#define AGITATE_WINDOW  3000UL
#define AGITATE_COUNT     10
#define TILT_DEG         12.0f
#define TILT_HOLD_MS     400UL
#define IMPACT_HOLDOFF  1500UL
#define REST_G           0.04f
#define REST_SETTLE_MS  4000UL

static bool     doorOpen    = false;
static uint8_t  hubMode     = MODE_DISARMED;
static uint32_t lastImpact  = 0;
static uint32_t lastAgitate = 0;
static uint8_t  agitations  = 0;
static uint32_t agitateFrom = 0;
static uint32_t tiltSince   = 0;
static bool     tiltSent    = false;
static uint32_t restSince   = 0;
static float    baseX = 0, baseY = 0, baseZ = 1;
static bool     haveBase    = false;
static uint32_t lastSample  = 0;

static void adxlWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool adxlRead(float &x, float &y, float &z) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(ADXL_DATAX0);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADXL_ADDR, 6) != 6) return false;

  int16_t raw[3];
  for (int i = 0; i < 3; i++) {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }
  x = raw[0] * ADXL_SCALE_G;
  y = raw[1] * ADXL_SCALE_G;
  z = raw[2] * ADXL_SCALE_G;
  return true;
}

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

static void sampleAccel() {
  const uint32_t now = millis();
  if (now - lastSample < 20) return;
  lastSample = now;

  float x, y, z;
  if (!adxlRead(x, y, z)) return;

  const float mag = sqrtf(x * x + y * y + z * z);
  const float dev = fabsf(mag - 1.0f);

  if (dev > IMPACT_G) {
    if (now - lastImpact > IMPACT_HOLDOFF) {
      lastImpact = now;
      agitations = 0;
      SecMesh.sendUrgent(EV_IMPACT, 0, (uint16_t)(mag * 100));
    }
    return;
  }

  if (dev > AGITATE_G) {
    restSince = 0;
    if (now - lastAgitate > 40) {
      lastAgitate = now;
      if (agitations == 0 || now - agitateFrom > AGITATE_WINDOW) {
        agitateFrom = now;
        agitations = 0;
      }
      agitations++;
      if (agitations >= AGITATE_COUNT) {
        agitations = 0;
        SecMesh.sendUrgent(EV_AGITATION, 0, (uint16_t)(dev * 1000));
      }
    }
  } else {
    if (agitations && now - agitateFrom > AGITATE_WINDOW) agitations = 0;
    if (dev < REST_G) {
      if (restSince == 0) restSince = now;
      if (!haveBase && now - restSince > REST_SETTLE_MS) {
        baseX = x / mag; baseY = y / mag; baseZ = z / mag;
        haveBase = true;
      }
    } else {
      restSince = 0;
    }
  }

  if (!haveBase) return;

  const float dot = (x * baseX + y * baseY + z * baseZ) / mag;
  const float ang = acosf(constrain(dot, -1.0f, 1.0f)) * 57.2958f;

  if (ang > TILT_DEG) {
    if (tiltSince == 0) tiltSince = now;
    if (!tiltSent && now - tiltSince > TILT_HOLD_MS) {
      tiltSent = true;
      SecMesh.sendUrgent(EV_TILT, 0, (uint16_t)ang);
    }
  } else {
    tiltSince = 0;
    tiltSent = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_DOOR, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  adxlWrite(ADXL_DATA_FORMAT, 0x0B);
  adxlWrite(ADXL_BW_RATE, 0x0C);
  adxlWrite(ADXL_POWER_CTL, 0x08);

  if (!SecMesh.begin(KIND_DOOR, onRx)) {
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

  doorOpen = digitalRead(PIN_DOOR) == HIGH;
  Serial.print("door node mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));
}

void loop() {
  SecMesh.tick();

  digitalWrite(PIN_LED, SecMesh.hasHub() ? LOW : ((millis() / 400) % 2));

  const bool open = digitalRead(PIN_DOOR) == HIGH;
  if (open != doorOpen) {
    doorOpen = open;
    if (open) SecMesh.sendUrgent(EV_DOOR_OPEN);
    else      SecMesh.send(EV_DOOR_CLOSED);
    delay(60);
  }

  sampleAccel();
}
