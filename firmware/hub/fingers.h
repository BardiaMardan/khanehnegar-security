#pragma once
#include <Arduino.h>

namespace Fingers {
void begin();

int      count();
bool     known(uint16_t id);
String   label(uint16_t id);
uint16_t idAt(int i);
String   labelAt(int i);

bool     add(uint16_t id, const String &label);
bool     rename(uint16_t id, const String &label);
bool     remove(uint16_t id);
void     clear();

uint16_t nextFreeId();
String   asIni();
}
