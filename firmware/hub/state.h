#pragma once
#include <Arduino.h>
#include "nodes.h"

namespace Sec {
void begin();
void tick();

uint8_t  mode();
uint8_t  displayMode();
bool     arming();
bool     silentLatched();
int      risk();
uint32_t countdownMs();

void onNodeEvent(NodeRec *n, const SecFrame &f);
void onHubTamper(bool open);
void onFinger(uint16_t id, uint16_t score);
void onUnknownFinger();

bool submitPin(const String &pin);
bool openManage(const String &pin);
bool changePin(const String &current, const String &next);
void requestArm(uint8_t m);
void panic();

bool lockdown();
void enterLockdown();
void lockRemote();
void clearLockdown(const char *how);

bool enrolFinger();
void onEnrolled(uint16_t id);
bool removeFinger(uint16_t id);
bool renameFinger(uint16_t id, const String &label);
void clearFingers();

void decideApproval(bool allow);

const char *message();

const char *toastText();
uint8_t     toastKind();
uint32_t    toastAge();
void        showToast(const char *text, uint8_t kind);
}
