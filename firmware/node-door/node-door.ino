// =============================================================================
//  node-door.ino — door node: ADXL345 motion and the ESP32's own touch channel
//
//  The accelerometer is the door sensor and the touch pad is the
//  silent trigger.
// =============================================================================

#include <Wire.h>
#include <math.h>
#include <SecMesh.h>

// ========== [Pins] ==========
#define PIN_SDA           21
#define PIN_SCL           22
#define PIN_TOUCH         27

// The node keeps no record of the hub's WiFi channel, so a restored claim starts on channel 1
// and only hops after SECMESH_SILENT_MS. Starting unclaimed hunts every 400 ms instead, and the
// hub re-adopts a MAC it already knows without PAIR MODE, so rejoining after a reboot is fast.
#define ALWAYS_UNCLAIM     1
#define TOUCH_TRACE        0

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
#define IMPACT_G         1.30f
#define AGITATE_G        0.09f
#define AGITATE_WINDOW  3000UL
#define AGITATE_COUNT     10
#define MOVE_DEG         10.0f
#define MOVE_REST_DEG     4.0f
#define MOVE_HOLD_MS     400UL
#define MOVE_REARM_MS   8000UL
#define IMPACT_HOLDOFF  1500UL
#define REST_G           0.04f
#define REST_SETTLE_MS  4000UL
#define SAMPLE_MS          20UL

// ========== [Touch tuning] ==========
#define TOUCH_SAMPLE_MS     20UL
#define TOUCH_DEBOUNCE_MS   50UL
#define TOUCH_REARM_MS    4000UL
#define TOUCH_STUCK_MS   30000UL
#define TOUCH_CALIB_MS     600UL
#define TOUCH_DRIFT_MS     500UL
#define TOUCH_DROP_PCT      75
#define TOUCH_MIN_DROP       4

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

static bool     moved       = false;
static uint32_t moveSince   = 0;
static uint32_t moveSent    = 0;

static bool     touchRaw    = false;
static bool     touchStable = false;
static uint32_t touchEdge   = 0;
static uint32_t touchSince  = 0;
static uint32_t touchSent   = 0;
static uint16_t touchBase   = 0;
static uint32_t touchAt     = 0;
static uint32_t touchDrift  = 0;

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
      break;
    default:
      break;
  }
}

// ========== [Handle touch — the silent trigger] ==========
static uint16_t touchSample() {
  uint32_t sum = 0;
  for (int i = 0; i < 3; i++) sum += touchRead(PIN_TOUCH);
  return (uint16_t)(sum / 3);
}

static void touchCalibrate() {
  uint32_t sum = 0, n = 0;
  const uint32_t until = millis() + TOUCH_CALIB_MS;
  while ((int32_t)(until - millis()) > 0) {
    sum += touchSample();
    n++;
    delay(10);
  }
  touchBase = n ? (uint16_t)(sum / n) : 0;
}

static uint16_t touchTrip() {
  uint16_t trip = (uint16_t)((uint32_t)touchBase * TOUCH_DROP_PCT / 100);
  if (touchBase - trip < TOUCH_MIN_DROP) {
    trip = (touchBase > TOUCH_MIN_DROP) ? (uint16_t)(touchBase - TOUCH_MIN_DROP) : 0;
  }
  return trip;
}

static void sampleTouch() {
  const uint32_t now = millis();
  if (!touchBase || now - touchAt < TOUCH_SAMPLE_MS) return;
  touchAt = now;

  const uint16_t v = touchSample();
  const uint16_t trip = touchTrip();
  const bool raw = v < trip;

#if TOUCH_TRACE
  static uint32_t traceAt = 0;
  if (now - traceAt > 400) {
    traceAt = now;
    DBG("touch %u base %u trip %u%s", v, touchBase, trip, raw ? "  <<<" : "");
  }
#endif

  if (raw != touchRaw) {
    touchRaw = raw;
    touchEdge = now;
    return;
  }
  if (now - touchEdge < TOUCH_DEBOUNCE_MS) return;

  if (raw == touchStable) {
    if (!raw && now - touchDrift > TOUCH_DRIFT_MS) {
      touchDrift = now;
      const int32_t d = (int32_t)v - (int32_t)touchBase;
      if      (d > 0) touchBase += (uint16_t)((d + 7) / 8);
      else if (d < 0) touchBase -= (uint16_t)((-d + 7) / 8);
    }
    if (raw && now - touchSince > TOUCH_STUCK_MS) {
      touchBase = v;
      touchRaw = touchStable = false;
      DBG("touch held past %lus, baseline relearned at %u",
          (unsigned long)(TOUCH_STUCK_MS / 1000), touchBase);
    }
    return;
  }

  touchStable = raw;
  if (!raw) return;

  touchSince = now;
  if (now - touchSent < TOUCH_REARM_MS) return;
  touchSent = now;
  DBG("handle touched, %u under %u", v, trip);
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

  if (ang < MOVE_REST_DEG) {
    moved = false;
    moveSince = 0;
    return;
  }

  if (ang < MOVE_DEG) return;

  if (moveSince == 0) moveSince = now;
  if (moved || now - moveSince < MOVE_HOLD_MS) return;
  if (moveSent && now - moveSent < MOVE_REARM_MS) return;

  moved = true;
  moveSent = now;
  DBG("movement %.1f deg", ang);
  SecMesh.sendUrgent(EV_TILT, 0, (uint16_t)ang);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("khanehnegar door node");

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

  touchCalibrate();

  Serial.print("door node mac: ");
  Serial.println(SecMeshClass::macToStr(SecMesh.selfMac()));
  DBG("touch GPIO %d baseline %u, trips under %u", PIN_TOUCH, touchBase, touchTrip());
}

void loop() {
  SecMesh.tick();
  sampleTouch();
  sampleAccel();
}
