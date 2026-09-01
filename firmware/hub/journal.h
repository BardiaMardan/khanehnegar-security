#pragma once
#include <Arduino.h>

struct LogEntry {
  uint32_t epoch;
  uint32_t at;
  uint8_t  kind;
  uint8_t  code;
  uint16_t value;
  uint8_t  silent;
};

namespace Journal {
void begin();
void tick();

void append(uint8_t kind, uint8_t code, uint16_t value, bool silent);

int             count();
const LogEntry &at(int i);
void            clear();
}
