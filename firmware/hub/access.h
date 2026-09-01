#pragma once
#include <Arduino.h>

namespace Access {

enum Verdict : uint8_t { V_WRONG = 0, V_OK, V_LOCKED };

struct Approval {
  uint32_t id;
  uint16_t arg;
  char     action[20];
  char     text[40];
};

void begin();
void tick();

Verdict verify(const String &pin);
bool    takeLatched();
uint8_t failures();

bool lockdown();
bool clearLockdown();

bool changePin(const String &current, const String &next);

void     grant();
void     keepAlive();
bool     granted();
uint32_t sessionMsLeft();
void     revoke();

bool            pending();
const Approval &approval();
uint32_t        approvalMsLeft();
bool            request(uint32_t id, const char *action, uint16_t arg, const char *text);
void            drop();
}
