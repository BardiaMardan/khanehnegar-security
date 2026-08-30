#pragma once
#include <Arduino.h>

namespace Climate {
void  begin();
void  tick();
bool  valid();
float tempC();
float humidity();
}
