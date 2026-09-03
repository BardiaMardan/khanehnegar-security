#include <Wire.h>
#include <math.h>
#include <SecMesh.h>

#define PIN_SDA           21
#define PIN_SCL           22
#define PIN_DOOR          23
#define PIN_CASE          18
#define PIN_TOUCH         19
#define PIN_LED            5
#define PIN_UNCLAIM        0

#define ENABLE_CASE_TAMPER 1

// Switch wiring:
// CLOSED -> GND -> LOW
// OPEN   -> pull-up -> HIGH
#define DOOR_CLOSED_IS_LOW 1
#define CASE_CLOSED_IS_LOW 1

// 0 = report the edge on the next loop pass. Raise it only if a switch genuinely chatters:
// at 250 ms a real contact of ten milliseconds was swallowed whole.
#define SWITCH_DEBOUNCE_MS 0UL

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

// ============================================================
// DEBUG
// ============================================================

#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
  #define DBG(fmt, ...) Serial.printf("[%10lu] " fmt "\n", millis(), ##__VA_ARGS__)
#else
  #define DBG(fmt, ...)
#endif


// ============================================================
// STATE
// ============================================================

static uint8_t adxlAddr = ADXL_PRIMARY;
static bool adxlReady = false;

static bool doorOpen = false;
static bool doorRaw = false;
static uint32_t doorEdge = 0;

static uint8_t hubMode = MODE_DISARMED;

static uint32_t lastImpact = 0;
static uint32_t lastAgitate = 0;
static uint8_t agitations = 0;
static uint32_t agitateFrom = 0;

static uint32_t tiltSince = 0;
static bool tiltSent = false;

static uint32_t restSince = 0;

static float baseX = 0;
static float baseY = 0;
static float baseZ = 1;

static bool haveBase = false;
static uint32_t lastSample = 0;

static bool touchRaw = false;
static bool touchStable = false;
static uint32_t touchEdge = 0;
static uint32_t touchSince = 0;
static uint32_t touchSent = 0;
static bool touchStuck = false;


// ============================================================
// DOOR
// ============================================================

static bool doorIsOpen() {
  const int level = digitalRead(PIN_DOOR);

  return DOOR_CLOSED_IS_LOW
           ? (level == HIGH)
           : (level == LOW);
}


// ============================================================
// ADXL345
// ============================================================

static void adxlWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(adxlAddr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool adxlPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(ADXL_DEVID);

  if (Wire.endTransmission(false) != 0)
    return false;

  if (Wire.requestFrom(addr, (uint8_t)1) != 1)
    return false;

  return Wire.read() == 0xE5;
}

static bool adxlBegin() {

  if (adxlPing(ADXL_PRIMARY))
    adxlAddr = ADXL_PRIMARY;

  else if (adxlPing(ADXL_ALTERNATE))
    adxlAddr = ADXL_ALTERNATE;

  else
    return false;

  adxlWrite(ADXL_DATA_FORMAT, 0x0B);
  adxlWrite(ADXL_BW_RATE, 0x0C);
  adxlWrite(ADXL_POWER_CTL, 0x08);

  return true;
}

static bool adxlRead(float &x, float &y, float &z) {

  Wire.beginTransmission(adxlAddr);
  Wire.write(ADXL_DATAX0);

  if (Wire.endTransmission(false) != 0)
    return false;

  if (Wire.requestFrom(adxlAddr, (uint8_t)6) != 6)
    return false;

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


// ============================================================
// SEC MESH RX
// ============================================================

static void onRx(const uint8_t mac[6], const SecFrame &f) {

  DBG("RX code=%u arg=%u", f.code, f.arg);

  switch (f.code) {

    case CMD_PAIR_ACCEPT:

      DBG("RX: CMD_PAIR_ACCEPT");

      if (SecMesh.onPairAccept(mac, f)) {

        hubMode = f.arg;

        DBG("PAIR ACCEPTED, hubMode=%u", hubMode);

        SecMesh.send(EV_BOOT);

        DBG("sent EV_BOOT");
      }
      else {

        DBG("PAIR ACCEPT FAILED");
      }

      break;


    case CMD_STATE:

      DBG("RX: CMD_STATE %u -> %u",
          hubMode,
          f.arg);

      hubMode = f.arg;

      break;


    case CMD_PING:

      DBG("RX: CMD_PING arg=%u", f.arg);

      hubMode = f.arg;

      break;


    case CMD_IDENTIFY:

      DBG("RX: CMD_IDENTIFY");

      for (int i = 0; i < 6; i++) {
        digitalWrite(PIN_LED, i % 2);
        delay(120);
      }

      break;


    default:

      DBG("RX: unhandled code=%u arg=%u",
          f.code,
          f.arg);

      break;
  }
}


// ============================================================
// TOUCH
// ============================================================

static void sampleTouch() {

  const uint32_t now = millis();

  const bool raw =
      digitalRead(PIN_TOUCH) == HIGH;

  if (raw != touchRaw) {

    DBG("TOUCH RAW: %s -> %s",
        touchRaw ? "HIGH" : "LOW",
        raw ? "HIGH" : "LOW");

    touchRaw = raw;
    touchEdge = now;

    DBG("TOUCH debounce started");

    return;
  }

  if (now - touchEdge < TOUCH_DEBOUNCE_MS)
    return;

  if (raw == touchStable) {

    if (raw &&
        !touchStuck &&
        now - touchSince > TOUCH_STUCK_MS) {

      touchStuck = true;

      DBG("TOUCH WARNING: STUCK HIGH");
    }

    return;
  }

  touchStable = raw;

  DBG("TOUCH STABLE -> %s",
      raw ? "HIGH" : "LOW");

  if (!raw) {

    touchStuck = false;

    DBG("TOUCH released");

    return;
  }

  touchSince = now;

  if (now - touchSent < TOUCH_REARM_MS) {

    DBG("TOUCH suppressed: rearm timer");

    return;
  }

  touchSent = now;

  DBG("TOUCH EVENT -> EV_TOUCH");

  SecMesh.sendUrgent(EV_TOUCH);
}


// ============================================================
// DOOR SWITCH
// ============================================================

static void sampleDoor() {

  const uint32_t now = millis();

  const bool raw = doorIsOpen();

  if (raw != doorRaw) {

    DBG("DOOR RAW CHANGE: %s -> %s",
        doorRaw ? "OPEN" : "CLOSED",
        raw ? "OPEN" : "CLOSED");

    DBG("DOOR GPIO%d raw level=%d",
        PIN_DOOR,
        digitalRead(PIN_DOOR));

    doorRaw = raw;
    doorEdge = now;

    DBG("DOOR debounce started");

    return;
  }

  if (raw == doorOpen)
    return;

  if (now - doorEdge < SWITCH_DEBOUNCE_MS)
    return;

  doorOpen = raw;

  DBG("DOOR STABLE -> %s",
      doorOpen ? "OPEN" : "CLOSED");

  DBG("DOOR GPIO%d level=%d",
      PIN_DOOR,
      digitalRead(PIN_DOOR));

  if (doorOpen) {

    DBG("DOOR EVENT -> EV_DOOR_OPEN");

    SecMesh.sendUrgent(EV_DOOR_OPEN);
  }
  else {

    DBG("DOOR EVENT -> EV_DOOR_CLOSED");

    SecMesh.send(EV_DOOR_CLOSED);
  }
}


// ============================================================
// CASE DEBUG
// ============================================================
//
// SecMesh owns the actual tamper processing, so this function
// does NOT duplicate the tamper event. It only watches the
// physical GPIO and tells us exactly what the switch is doing.
//

static bool caseIsOpen() {

  const int level = digitalRead(PIN_CASE);

  return CASE_CLOSED_IS_LOW
           ? (level == HIGH)
           : (level == LOW);
}

static bool lastCaseOpen = false;
static bool caseRawState = false;
static uint32_t caseEdge = 0;

static void sampleCase() {

  const uint32_t now = millis();

  const int level = digitalRead(PIN_CASE);

  const bool rawOpen =
      CASE_CLOSED_IS_LOW
        ? (level == HIGH)
        : (level == LOW);


  if (rawOpen != caseRawState) {

    DBG("CASE RAW CHANGE: %s -> %s",
        caseRawState ? "OPEN" : "CLOSED",
        rawOpen ? "OPEN" : "CLOSED");

    DBG("CASE GPIO%d raw level=%d",
        PIN_CASE,
        level);

    caseRawState = rawOpen;
    caseEdge = now;

    DBG("CASE debounce started");

    return;
  }


  if (now - caseEdge < SWITCH_DEBOUNCE_MS)
    return;


  if (rawOpen != lastCaseOpen) {

    lastCaseOpen = rawOpen;

    DBG("CASE STABLE -> %s",
        lastCaseOpen ? "OPEN" : "CLOSED");

    DBG("CASE GPIO%d level=%d",
        PIN_CASE,
        digitalRead(PIN_CASE));

    if (lastCaseOpen) {
      DBG("CASE IS OPEN / TAMPER CONDITION");
    }
    else {
      DBG("CASE IS CLOSED");
    }
  }
}


// ============================================================
// ACCELEROMETER
// ============================================================
//
// NO SERIAL DEBUG HERE.
//

static void sampleAccel() {

  const uint32_t now = millis();

  if (!adxlReady)
    return;

  if (now - lastSample < 20)
    return;

  lastSample = now;

  float x, y, z;

  if (!adxlRead(x, y, z))
    return;

  const float mag =
      sqrtf(x * x + y * y + z * z);

  const float dev =
      fabsf(mag - 1.0f);


  if (dev > IMPACT_G) {

    if (now - lastImpact > IMPACT_HOLDOFF) {

      lastImpact = now;
      agitations = 0;

      SecMesh.sendUrgent(
          EV_IMPACT,
          0,
          (uint16_t)(mag * 100)
      );
    }

    return;
  }


  if (dev > AGITATE_G) {

    restSince = 0;

    if (now - lastAgitate > 40) {

      lastAgitate = now;

      if (agitations == 0 ||
          now - agitateFrom > AGITATE_WINDOW) {

        agitateFrom = now;
        agitations = 0;
      }

      agitations++;

      if (agitations >= AGITATE_COUNT) {

        agitations = 0;

        SecMesh.sendUrgent(
            EV_AGITATION,
            0,
            (uint16_t)(dev * 1000)
        );
      }
    }

  }
  else {

    if (agitations &&
        now - agitateFrom > AGITATE_WINDOW) {

      agitations = 0;
    }

    if (dev < REST_G) {

      if (restSince == 0)
        restSince = now;

      if (!haveBase &&
          now - restSince > REST_SETTLE_MS) {

        baseX = x / mag;
        baseY = y / mag;
        baseZ = z / mag;

        haveBase = true;
      }

    }
    else {

      restSince = 0;
    }
  }


  if (!haveBase)
    return;


  const float dot =
      (x * baseX +
       y * baseY +
       z * baseZ) / mag;

  const float ang =
      acosf(
        constrain(dot, -1.0f, 1.0f)
      ) * 57.2958f;


  if (ang > TILT_DEG) {

    if (tiltSince == 0)
      tiltSince = now;

    if (!tiltSent &&
        now - tiltSince > TILT_HOLD_MS) {

      tiltSent = true;

      SecMesh.sendUrgent(
          EV_TILT,
          0,
          (uint16_t)ang
      );
    }

  }
  else {

    tiltSince = 0;
    tiltSent = false;
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(200);

  Serial.println();
  Serial.println("========================================");
  Serial.println("       DOOR NODE SWITCH DEBUG");
  Serial.println("========================================");

  DBG("SDA       = GPIO %d", PIN_SDA);
  DBG("SCL       = GPIO %d", PIN_SCL);
  DBG("DOOR      = GPIO %d", PIN_DOOR);
  DBG("CASE      = GPIO %d", PIN_CASE);
  DBG("TOUCH     = GPIO %d", PIN_TOUCH);
  DBG("LED       = GPIO %d", PIN_LED);
  DBG("UNCLAIM   = GPIO %d", PIN_UNCLAIM);

  Serial.println();
  Serial.println("Switch wiring expected:");
  Serial.println("  DOOR: GPIO23 -> switch -> GND");
  Serial.println("  CASE: GPIO18 -> switch -> GND");
  Serial.println("  LED : GPIO5  -> 330R -> LED -> GND");
  Serial.println("  CLOSED = LOW");
  Serial.println("  OPEN   = HIGH");
  Serial.println();


  // ----------------------------------------------------------
  // GPIO
  // ----------------------------------------------------------

  pinMode(PIN_LED, OUTPUT);

  // Self-test: if this half second of light does not happen, the LED is on the wrong pin,
  // reversed, or has no series resistor to ground. Nothing after this can light it either.
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  digitalWrite(PIN_LED, LOW);
  delay(150);
  digitalWrite(PIN_LED, HIGH);
  DBG("LED self-test done on GPIO %d", PIN_LED);

  pinMode(PIN_DOOR, INPUT_PULLUP);

#if ENABLE_CASE_TAMPER
  pinMode(PIN_CASE, INPUT_PULLUP);
#endif

  pinMode(PIN_TOUCH, INPUT);

  pinMode(PIN_UNCLAIM, INPUT_PULLUP);


  // ----------------------------------------------------------
  // I2C / accelerometer
  // ----------------------------------------------------------

  Wire.begin(PIN_SDA, PIN_SCL);

  adxlReady = adxlBegin();

  DBG("ADXL345 = %s",
      adxlReady ? "OK" : "NOT FOUND");


  // ----------------------------------------------------------
  // SecMesh
  // ----------------------------------------------------------

  DBG("starting SecMesh");

  if (!SecMesh.begin(KIND_DOOR, onRx)) {
    DBG("FATAL: SecMesh failed to start");
  }
  else {
    DBG("SecMesh started");
  }


  // ----------------------------------------------------------
  // Claim
  // ----------------------------------------------------------

  if (digitalRead(PIN_UNCLAIM) == LOW) {

    DBG("UNCLAIM ACTIVE");

    SecMesh.clearClaim();

    for (int i = 0; i < 10; i++) {
      digitalWrite(PIN_LED, i % 2);
      delay(80);
    }

    DBG("claim cleared");

  }
  else {

    DBG("loading claim");

    SecMesh.loadClaim();

    if (SecMesh.hasHub()) {

      DBG("hub found -> sending EV_BOOT");

      SecMesh.send(EV_BOOT);

    }
    else {

      DBG("no hub claimed");
    }
  }


  // ----------------------------------------------------------
  // Case tamper
  // ----------------------------------------------------------

#if ENABLE_CASE_TAMPER

  DBG("configuring CASE tamper: GPIO%d",
      PIN_CASE);

  SecMesh.setTamperPin(
      PIN_CASE,
      CASE_CLOSED_IS_LOW ? true : false
  );

#endif


  // ----------------------------------------------------------
  // Initial switch states
  // ----------------------------------------------------------

  const int doorLevel = digitalRead(PIN_DOOR);
  const int caseLevel = digitalRead(PIN_CASE);
  const int touchLevel = digitalRead(PIN_TOUCH);

  doorOpen = doorRaw = doorIsOpen();

  doorEdge = millis();

  caseRawState = caseIsOpen();
  lastCaseOpen = caseRawState;
  caseEdge = millis();

  touchRaw =
  touchStable =
      touchLevel == HIGH;


  Serial.println();
  Serial.println("--------------- INITIAL PINS ---------------");

  DBG("DOOR  GPIO%d = %d -> %s",
      PIN_DOOR,
      doorLevel,
      doorOpen ? "OPEN" : "CLOSED");

  DBG("CASE  GPIO%d = %d -> %s",
      PIN_CASE,
      caseLevel,
      lastCaseOpen ? "OPEN" : "CLOSED");

  DBG("TOUCH GPIO%d = %d -> %s",
      PIN_TOUCH,
      touchLevel,
      touchStable ? "HIGH" : "LOW");

  Serial.println("--------------------------------------------");
  Serial.println();


  Serial.print("door node mac: ");
  Serial.println(
      SecMeshClass::macToStr(
          SecMesh.selfMac()
      )
  );

  Serial.println();
  Serial.println("READY - switch debugging active");
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  SecMesh.tick();

  // Solid on = claimed and healthy. Blinking = hunting for a hub. This was inverted: a
  // paired node drove the pin LOW, so a node that was working perfectly showed no light at
  // all and looked dead.
  digitalWrite(
      PIN_LED,
      SecMesh.hasHub()
        ? HIGH
        : ((millis() / 400) % 2)
  );

  sampleDoor();
  sampleCase();
  sampleTouch();

  // Still runs, but produces NO serial output.
  sampleAccel();
}