// =============================================================================
//  access.h — keypad credential, lockdown latch, manage session, pending approval
//
//  The credential layer's public surface. Every function here answers a question or
//  changes a permission; none of them produce sound, log lines or notifications, which
//  is what lets state.cpp own all the consequences. Implemented in access.cpp.
// =============================================================================

#pragma once
#include <Arduino.h>

namespace Access {

// V_LOCKED is not "wrong": the code was never checked, because the panel is latched.
enum Verdict : uint8_t { V_WRONG = 0, V_OK, V_LOCKED };

// One pending web action. `action` is matched as a string by Sec::decideApproval, `arg`
// is its parameter (a fingerprint id today), and `text` is what the popup shows the
// person being asked. Fixed buffers, because this arrives from the network.
struct Approval {
  uint32_t id;
  uint16_t arg;
  char     action[20];
  char     text[40];
};

void begin();
void tick();

// ========== [The code] ==========
// verify() is the only path to a verdict; takeLatched() is a read-and-clear edge that is
// true exactly once, on the attempt that latched lockdown.
Verdict verify(const String &pin);
bool    takeLatched();
uint8_t failures();

// ========== [Lockdown] ==========
// Persisted in NVS, so a power cycle does not hand back a fresh set of guesses.
bool lockdown();
bool clearLockdown();
void latchRemote();

bool changePin(const String &current, const String &next);

// ========== [Manage session] ==========
// grant() opens the window, keepAlive() slides it, granted() is the gate every
// credential-changing screen must pass.
void     grant();
void     keepAlive();
bool     granted();
uint32_t sessionMsLeft();
void     revoke();

// ========== [Approvals] ==========
// request() returns false when the same id is already in flight. approvalMsLeft() == 0
// means timed out, and a timeout is a refusal.
bool            pending();
const Approval &approval();
uint32_t        approvalMsLeft();
bool            request(uint32_t id, const char *action, uint16_t arg, const char *text);
void            drop();
}
