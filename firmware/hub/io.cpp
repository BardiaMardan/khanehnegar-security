// =============================================================================
//  io.cpp — the buzzer: patterns, and the siren
// =============================================================================

#include "io.h"
#include "config.h"

namespace IO {

// ========== [Module state] ==========
static uint8_t  q_count   = 0;
static uint16_t q_on      = 0;
static uint16_t q_off     = 0;
static bool     q_high    = false;
static uint32_t q_next    = 0;
static bool     siren     = false;
static uint32_t sirenNext = 0;
static bool     sirenHigh = false;

void begin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

// ========== [Patterns and what they mean] ==========
void beep(uint8_t count, uint16_t onMs, uint16_t offMs) {
  if (siren) return;
  q_count = count;
  q_on    = onMs;
  q_off   = offMs;
  q_high  = false;
  q_next  = millis();
}

void ack()      { beep(1, 90, 0); }
void reject()   { beep(3, 70, 70); }
void chime()    { beep(2, 50, 60); }
void tickTock() { beep(1, 25, 0); }

// ========== [Siren] ==========
void sirenOn() {
  siren = true;
  sirenNext = millis();
  sirenHigh = false;
  q_count = 0;
}

void sirenOff() {
  siren = false;
  digitalWrite(PIN_BUZZER, LOW);
}

bool sirenActive() { return siren; }

// ========== [The state machine] ==========
void tick() {
  const uint32_t now = millis();

  if (siren) {
    if (now >= sirenNext) {
      sirenHigh = !sirenHigh;
      digitalWrite(PIN_BUZZER, sirenHigh ? HIGH : LOW);
      sirenNext = now + (sirenHigh ? 350 : 200);
    }
    return;
  }

  if (q_count > 0 && now >= q_next) {
    if (!q_high) {
      digitalWrite(PIN_BUZZER, HIGH);
      q_high = true;
      q_next = now + q_on;
    } else {
      digitalWrite(PIN_BUZZER, LOW);
      q_high = false;
      q_count--;
      q_next = now + q_off;
    }
  }
}

}
