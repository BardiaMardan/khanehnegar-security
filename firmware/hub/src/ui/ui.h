// =============================================================================
//  ui.h — the display layer as the rest of the firmware sees it
// =============================================================================

#pragma once
#include <Arduino.h>

namespace UI {
enum Page : uint8_t { PAGE_STATUS = 0, PAGE_KEYPAD, PAGE_LOG, PAGE_MANAGE };

void begin();
void boot(const char *step);
void ready();
void tick();

void invalidate();
void wake();
bool standby();
void go(Page p);
Page page();
}
