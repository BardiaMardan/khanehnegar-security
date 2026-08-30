#include "fp.h"
#include "config.h"
#include "store.h"
#include <Adafruit_Fingerprint.h>

namespace FP {

static HardwareSerial      port(2);
static Adafruit_Fingerprint sensor(&port);

static bool     g_present   = false;
static int      g_count     = 0;
static Result   g_result    = RES_NONE;
static uint16_t g_matchedId = 0;
static uint16_t g_score     = 0;

static bool     g_enroll     = false;
static bool     g_enrollDur  = false;
static uint8_t  g_step       = 0;
static uint16_t g_enrollId   = 0;
static uint16_t g_lastId     = 0;
static bool     g_lastDuress = false;
static uint32_t g_guard      = 0;

void begin() {
  pinMode(PIN_FP_TOUCH, INPUT);
  port.begin(FP_BAUD, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
  delay(400);
  g_present = sensor.verifyPassword();
  if (g_present) {
    sensor.getTemplateCount();
    g_count = sensor.templateCount;
  }
}

bool present() { return g_present; }
int  templateCount() { return g_count; }

Result take() {
  const Result r = g_result;
  g_result = RES_NONE;
  return r;
}

uint16_t matchedId()  { return g_matchedId; }
uint16_t confidence() { return g_score; }

static uint16_t nextFreeId() {
  for (uint16_t id = 1; id <= FP_MAX_TEMPLATES; id++) {
    if (!Store::fingerKnown(id)) return id;
  }
  return 0;
}

void startEnroll(bool duress) {
  if (!g_present) return;
  g_enrollId = nextFreeId();
  if (g_enrollId == 0) return;
  g_enroll = true;
  g_enrollDur = duress;
  g_step = 0;
  g_guard = 0;
}

void cancelEnroll() {
  g_enroll = false;
  g_step = 0;
}

bool     enrolling()        { return g_enroll; }
uint8_t  enrollStep()       { return g_step; }
uint16_t lastEnrolledId()   { return g_lastId; }
bool     lastEnrollDuress() { return g_lastDuress; }

void emptyAll() {
  if (!g_present) return;
  sensor.emptyDatabase();
  sensor.getTemplateCount();
  g_count = sensor.templateCount;
}

static void tickEnroll() {
  const uint8_t img = sensor.getImage();

  if (g_step == 0) {
    if (img != FINGERPRINT_OK) return;
    if (sensor.image2Tz(1) != FINGERPRINT_OK) return;
    g_step = 1;
    return;
  }

  if (g_step == 1) {
    if (img != FINGERPRINT_NOFINGER) return;
    g_step = 2;
    return;
  }

  if (g_step == 2) {
    if (img != FINGERPRINT_OK) return;
    if (sensor.image2Tz(2) != FINGERPRINT_OK) return;
    if (sensor.createModel() != FINGERPRINT_OK) {
      g_enroll = false;
      g_step = 0;
      g_result = RES_ENROLL_FAIL;
      return;
    }
    if (sensor.storeModel(g_enrollId) != FINGERPRINT_OK) {
      g_enroll = false;
      g_step = 0;
      g_result = RES_ENROLL_FAIL;
      return;
    }
    sensor.getTemplateCount();
    g_count = sensor.templateCount;
    g_lastId = g_enrollId;
    g_lastDuress = g_enrollDur;
    g_enroll = false;
    g_step = 0;
    g_result = RES_ENROLLED;
  }
}

void tick() {
  if (!g_present) return;

  const uint32_t now = millis();
  if (now < g_guard) return;

  if (g_enroll) {
    tickEnroll();
    return;
  }

  if (digitalRead(PIN_FP_TOUCH) == LOW) return;

  if (sensor.getImage() != FINGERPRINT_OK) return;
  if (sensor.image2Tz() != FINGERPRINT_OK) return;

  g_guard = now + 1500;

  if (sensor.fingerFastSearch() != FINGERPRINT_OK) {
    g_matchedId = 0;
    g_score = 0;
    g_result = RES_NOMATCH;
    return;
  }

  if (sensor.confidence < FP_MIN_SCORE) {
    g_matchedId = 0;
    g_score = sensor.confidence;
    g_result = RES_NOMATCH;
    return;
  }

  g_matchedId = sensor.fingerID;
  g_score = sensor.confidence;
  g_result = RES_MATCH;
}

}
