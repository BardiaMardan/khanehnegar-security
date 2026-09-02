// =============================================================================
//  climate.h — room temperature and humidity, as seen from outside
// =============================================================================

#pragma once
#include <Arduino.h>

namespace Climate {
void  begin();
void  tick();
bool  valid();
float tempC();
float humidity();
}
