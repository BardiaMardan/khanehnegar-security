#pragma once
#include <Arduino.h>

namespace Reader {
void   begin();
void   tick();
bool   present();
bool   newCard();
String uid();
}
