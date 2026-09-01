#pragma once
#include <Arduino.h>

namespace Net {
void begin();
void tick();
bool online();
int  channel();

void alert(bool silent, const char *kind, const char *event, uint16_t value);
void decide(uint32_t id, bool allowed);
void publishRoster();
}
