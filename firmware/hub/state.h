// =============================================================================
//  state.h — the hub's decision layer, as seen from outside
// =============================================================================

#pragma once
#include <Arduino.h>
#include "nodes.h"

namespace Sec {
void begin();
void tick();

// ========== [Read-only state for the UI and the network layer] ==========
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
bool submitPin(const String &pin);
bool openManage(const String &pin);
bool changePin(const String &current, const String &next);
void requestArm(uint8_t m);
void panic();

// ========== [Lockdown] ==========
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
void decideApproval(bool allow);

// ========== [Messaging] ==========
const char *message();

const char *toastText();
uint8_t     toastKind();
uint32_t    toastAge();
void        showToast(const char *text, uint8_t kind);
}
