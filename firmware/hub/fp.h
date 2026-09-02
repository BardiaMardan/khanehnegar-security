// =============================================================================
//  fp.h — the fingerprint sensor, as seen from outside
// =============================================================================

#pragma once
#include <Arduino.h>

namespace FP {
enum Result : uint8_t {
  RES_NONE = 0,
  RES_MATCH,
  RES_NOMATCH,
  RES_ENROLLED,
  RES_ENROLL_FAIL,
};

void begin();
bool present();
int  templateCount();
void tick();

// ========== [The one-shot result] ==========
Result   take();
uint16_t matchedId();
uint16_t confidence();

// ========== [Enrolment] ==========
void     startEnroll();
void     cancelEnroll();
bool     enrolling();
uint8_t  enrollStep();
uint16_t lastEnrolledId();

// ========== [Template database] ==========
bool erase(uint16_t id);
void emptyAll();
int  adoptExisting();
}
