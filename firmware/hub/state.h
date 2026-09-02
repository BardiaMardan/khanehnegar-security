// =============================================================================
//  state.h — the hub's decision layer, as seen from outside
//
//  Everything the rest of the firmware is allowed to know about the security state.
//  The pattern is deliberate: inputs are `on*` notifications from hub.ino, requests
//  are verbs that may be refused, and state is read through functions rather than
//  extern variables, so no other module can write the mode or the risk. Implemented
//  in state.cpp.
// =============================================================================

#pragma once
#include <Arduino.h>
#include "nodes.h"

namespace Sec {
void begin();
void tick();

// ========== [Read-only state for the UI and the network layer] ==========
// displayMode() is mode() with lockdown merged in; only the UI should use it.
uint8_t  mode();
uint8_t  displayMode();
bool     arming();
bool     silentLatched();
int      risk();
uint32_t countdownMs();

// ========== [Inputs — called from hub.ino as things happen] ==========
void onNodeEvent(NodeRec *n, const SecFrame &f);
void onHubTamper(bool open);
void onFinger(uint16_t id, uint16_t score);
void onUnknownFinger();

// ========== [Requests — may be refused] ==========
// The bool returns mean "the request was accepted", not "the action finished".
bool submitPin(const String &pin);
bool openManage(const String &pin);
bool changePin(const String &current, const String &next);
void requestArm(uint8_t m);
void panic();

// ========== [Lockdown] ==========
// The latch lives in access.cpp; these are the announced transitions.
bool lockdown();
void enterLockdown();
void lockRemote();
void clearLockdown(const char *how);

// ========== [Fingerprint management] ==========

bool enrolFinger();
void onEnrolled(uint16_t id);
bool removeFinger(uint16_t id);
bool renameFinger(uint16_t id, const String &label);
void clearFingers();

// ========== [Web approvals] ==========
// Answer for the pending request in access.cpp: true is Allow on the terminal.
void decideApproval(bool allow);

// ========== [Messaging] ==========
// message() is the persistent status line; a toast is the transient banner. Both are
// polled by the UI — this layer never draws.
const char *message();

const char *toastText();
uint8_t     toastKind();
uint32_t    toastAge();
void        showToast(const char *text, uint8_t kind);
}
