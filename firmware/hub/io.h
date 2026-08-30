#pragma once
#include <Arduino.h>

namespace IO {
void begin();
void tick();

void beep(uint8_t count, uint16_t onMs = 60, uint16_t offMs = 90);
void ack();
void reject();
void chime();
void tickTock();

void sirenOn();
void sirenOff();
bool sirenActive();
}
