// =============================================================================
//  node-door.ino — door node: ADXL345 motion and a capacitive handle pad
//
//  No switches. The accelerometer is the door sensor and the touch pad is the
//  silent trigger. Everything else was removed.
// =============================================================================

#include <Wire.h>
#include <math.h>
#include <SecMesh.h>

// ========== [Pins] ==========
#define PIN_SDA           21
#define PIN_SCL           22
#define PIN_TOUCH         19
#define PIN_LED            5

// ========== [Development] ==========
// 1 = forget the hub on every boot, so a fresh upload always comes up hunting and can be
// re-adopted from the MANAGE tab. Set to 0 for the demo: with it on, any power blip leaves
// the node orphaned until someone opens pairing mode again.
#define ALWAYS_UNCLAIM     1

// ========== [ADXL345 registers] ==========
#define ADXL_PRIMARY     0x53
#define ADXL_ALTERNATE   0x1D
#define ADXL_DEVID       0x00
#define ADXL_BW_RATE     0x2C
#define ADXL_POWER_CTL   0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0      0x32
#define ADXL_SCALE_G     0.0039f

// ========== [Motion tuning] ==========
// The door is "open" once it has swung TILT_OPEN_DEG off the orientation it was resting at,
// and "closed" again below TILT_SHUT_DEG. The gap between the two is hysteresis: a single
// threshold would flap for as long as the door hovered on it.
#define IMPACT_G         1.30f
#define AGITATE_G        0.09f
#define AGITATE_WINDOW  3000UL
#define AGITATE_COUNT     10
#define TILT_OPEN_DEG    12.0f
#define TILT_SHUT_DEG     6.0f
#define TILT_HOLD_MS     350UL
#define IMPACT_HOLDOFF  1500UL
#define REST_G           0.04f
#define REST_SETTLE_MS  4000UL
#define SAMPLE_MS          20UL

// ========== [Touch tuning] ==========
#define TOUCH_DEBOUNCE_MS   50UL
#define TOUCH_REARM_MS    4000UL
#define TOUCH_STUCK_MS   30000UL

// ========== [State] ==========
static uint8_t  adxlAddr    = ADXL_PRIMARY;
static bool     adxlReady   = false;
static uint8_t  hubMode     = MODE_DISARMED;

static uint32_t lastSample  = 0;
static uint32_t lastImpact  = 0;
static uint32_t lastAgitate = 0;
static uint8_t  agitations  = 0;
static uint32_t agitateFrom = 0;

static float    baseX = 0, baseY = 0, baseZ = 1;
static bool     haveBase    = false;
static uint32_t restSince   = 0;

static bool     doorOpen    = false;
static uint32_t tiltSince   = 0;
static uint32_t shutSince   = 0;

static bool     touchRaw    = false;
static bool     touchStable = false;
static uint32_t touchEdge   = 0;
static uint32_t touchSince  = 0;
static uint32_t touchSent   = 0;
static bool     touchStuck  = false;

// ========== [ADXL345] ==========
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
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  return Wire.read() == 0xE5;
}

static bool adxlBegin() {
  if      (adxlPing(ADXL_PRIMARY))   adxlAddr = ADXL_PRIMARY;
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
  if (Wire.requestFrom(adxlAddr, (uint8_t)6) != 6) return false;

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

// ========== [Downlink] ==========
static void onRx(const uint8_t mac[6], const SecFrame &f) {
  switch (f.code) {
    case CMD_PAIR_ACCEPT:
      if (SecMesh.onPairAccept(mac, f)) {
        hubMode = f.arg;
        SecMesh.send(EV_BOOT);
        DBG("paired, mode %u", hubMode);
      }
      break;
    case CMD_STATE:
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

// ========== [Handle touch — the silent trigger] ==========
// Rising edge only, so it behaves the same whether the pad is jumpered momentary or latching.
// If the output stays high past TOUCH_STUCK_MS it is treated as stuck and waits for a real
// release rather than sitting on a permanent alarm.
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
    if (raw && !touchStuck && now - touchSince > TOUCH_STUCK_MS) {
      touchStuck = true;
      DBG("touch stuck high");
    }
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
  DBG("handle touched");
  SecMesh.sendUrgent(EV_TOUCH);
}

// ========== [Motion — impact, agitation, and the door itself] ==========
static void sampleAccel() {
  const uint32_t now = millis();
  if (!adxlReady || now - lastSample < SAMPLE_MS) return;
  lastSample = now;

  float x, y, z;
  if (!adxlRead(x, y, z)) return;

  const float mag = sqrtf(x * x + y * y + z * z);
  const float dev = fabsf(mag - 1.0f);

  if (dev > IMPACT_G) {
    if (now - lastImpact > IMPACT_HOLDOFF) {
      lastImpact = now;
      agitations = 0;
      DBG("impact %.2fg", mag);
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
        DBG("agitation");
        SecMesh.sendUrgent(EV_AGITATION, 0, (uint16_t)(dev * 1000));
      }
    }
  } else {
    if (agitations && now - agitateFrom > AGITATE_WINDOW) agitations = 0;
    // The resting orientation is learned once, after the board has been still for a few
    // seconds. Mount it with the door shut and leave it alone while it powers up.
    if (dev < REST_G) {
      if (restSince == 0) restSince = now;
      if (!haveBase && now - restSince > REST_SETTLE_MS) {
        baseX = x / mag; baseY = y / mag; baseZ = z / mag;
        haveBase = true;
        DBG("closed-door reference captured");
      }
    } else {
      restSince = 0;
    }
  }

  if (!haveBase) return;

  const float dot = (x * baseX + y * baseY + z * baseZ) / mag;
  const float ang = acosf(constrain(dot, -1.0f, 1.0f)) * 57.2958f;

  if (!doorOpen) {
    shutSince = 0;
    if (ang > TILT_OPEN_DEG) {
      if (tiltSince == 0) tiltSince = now;
      if (now - tiltSince > TILT_HOLD_MS) {
        doorOpen = true;
        tiltSince = 0;
        DBG("door OPEN at %.1f deg", ang);
        SecMesh.sendUrgent(EV_DOOR_OPEN, 0, (uint16_t)ang);
      }
    } else {
      tiltSince = 0;
    }
  } else {
    tiltSince = 0;
    if (ang < TILT_SHUT_DEG) {
      if (shutSince == 0) shutSince = now;
      if (now - shutSince > TILT_HOLD_MS) {
        doorOpen = false;
        shutSince = 0;
        DBG("door closed at %.1f deg", ang);
        SecMesh.send(EV_DOOR_CLOSED, 0, (uint16_t)ang);
      }
    } else {
      shutSince = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("khanehnegar door node");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  digitalWrite(PIN_LED, LOW);
  delay(150);
  DBG("LED self-test done on GPIO %d", PIN_LED);

  pinMode(PIN_TOUCH, INPUT);

  Wire.begin(PIN_SDA, PIN_SCL);
  adxlReady = adxlBegin();
  if (adxlReady) DBG("ADXL345 found at 0x%02X", adxlAddr);
  else           DBG("ADXL345 NOT FOUND — check SDA 21 / SCL 22");

  if (!SecMesh.begin(KIND_DOOR, onRx)) {
    DBG("FATAL: ESP-NOW would not start");
  }

#if ALWAYS_UNCLAIM
  SecMesh.clearClaim();
  DBG("claim cleared on boot, open PAIR MODE on the terminal");
#else
  SecMesh.loadClaim();
  if (SecMesh.hasHub()) SecMesh.send(EV_BOOT);
#endif

  touchRaw = touchStable = digitalRead(PIN_TOUCH) == HIGH;

  Serial.print("door node mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));
  DBG("touch GPIO %d reads %d", PIN_TOUCH, digitalRead(PIN_TOUCH));
}

void loop() {
  SecMesh.tick();

  // Solid = claimed and healthy. Blinking = hunting for a hub.
  digitalWrite(PIN_LED, SecMesh.hasHub() ? HIGH : ((millis() / 400) % 2));

  sampleTouch();
  sampleAccel();
}
