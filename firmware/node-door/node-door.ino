#include <Wire.h>
#include <math.h>
#include <SecMesh.h>

#define PIN_SDA           21
#define PIN_SCL           22
#define PIN_DOOR           5
#define PIN_CASE          18
#define PIN_TOUCH         19
#define PIN_LED           23
#define PIN_UNCLAIM        0

#define ENABLE_CASE_TAMPER 1

#define ADXL_PRIMARY     0x53
#define ADXL_ALTERNATE   0x1D
#define ADXL_DEVID       0x00
#define ADXL_BW_RATE     0x2C
#define ADXL_POWER_CTL   0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0      0x32
#define ADXL_SCALE_G     0.0039f

#define IMPACT_G         1.30f
#define AGITATE_G        0.09f
#define AGITATE_WINDOW  3000UL
#define AGITATE_COUNT     10
#define TILT_DEG         12.0f
#define TILT_HOLD_MS     400UL
#define IMPACT_HOLDOFF  1500UL
#define REST_G           0.04f
#define REST_SETTLE_MS  4000UL

#define TOUCH_DEBOUNCE_MS   50UL
#define TOUCH_REARM_MS    4000UL
#define TOUCH_STUCK_MS   30000UL

static uint8_t  adxlAddr    = ADXL_PRIMARY;
static bool     adxlReady   = false;

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

static bool     touchRaw    = false;
static bool     touchStable = false;
static uint32_t touchEdge   = 0;
static uint32_t touchSince  = 0;
static uint32_t touchSent   = 0;
static bool     touchStuck  = false;

static void adxlWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool adxlPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(ADXL_DEVID);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, 1) != 1) return false;
  return Wire.read() == 0xE5;
}

static bool adxlBegin() {
  if (adxlPing(ADXL_PRIMARY))        adxlAddr = ADXL_PRIMARY;
  else if (adxlPing(ADXL_ALTERNATE)) adxlAddr = ADXL_ALTERNATE;
  else return false;

  adxlWrite(ADXL_DATA_FORMAT, 0x0B);
  adxlWrite(ADXL_BW_RATE, 0x0C);
  adxlWrite(ADXL_POWER_CTL, 0x08);
  return true;
}

static bool adxlRead(float &x, float &y, float &z) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(ADXL_DATAX0);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(adxlAddr, 6) != 6) return false;

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
      if (SecMesh.onPairAccept(mac, f)) {
        hubMode = f.arg;
        SecMesh.send(EV_BOOT);
      }
      break;
    case CMD_STATE:
      hubMode = f.arg;
      break;
    case CMD_PING:
      hubMode = f.arg;
      break;
    case CMD_IDENTIFY:
      for (int i = 0; i < 6; i++) { digitalWrite(PIN_LED, i % 2); delay(120); }
      break;
    default:
      break;
  }
}

static void sampleTouch() {
  const uint32_t now = millis();
  const bool raw = digitalRead(PIN_TOUCH) == HIGH;

  if (raw != touchRaw) {
    touchRaw = raw;
    touchEdge = now;
    return;
  }

  if (now - touchEdge < TOUCH_DEBOUNCE_MS) return;
  if (raw == touchStable) {
    if (raw && !touchStuck && now - touchSince > TOUCH_STUCK_MS) touchStuck = true;
    return;
  }

  touchStable = raw;

  if (!raw) {
    touchStuck = false;
    return;
  }

  touchSince = now;
  if (now - touchSent < TOUCH_REARM_MS) return;
  touchSent = now;
  SecMesh.sendUrgent(EV_TOUCH);
}

static void sampleAccel() {
  const uint32_t now = millis();
  if (!adxlReady || now - lastSample < 20) return;
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
  pinMode(PIN_TOUCH, INPUT);

  Wire.begin(PIN_SDA, PIN_SCL);
  adxlReady = adxlBegin();
  Serial.printf("adxl345: %s", adxlReady ? "ok at 0x" : "NOT FOUND");
  if (adxlReady) Serial.println(adxlAddr, HEX);
  else Serial.println();

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
    if (SecMesh.hasHub()) SecMesh.send(EV_BOOT);
  }

#if ENABLE_CASE_TAMPER
  SecMesh.setTamperPin(PIN_CASE, true);
#endif

  doorOpen = digitalRead(PIN_DOOR) == HIGH;
  touchRaw = touchStable = digitalRead(PIN_TOUCH) == HIGH;
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

  sampleTouch();
  sampleAccel();
}
