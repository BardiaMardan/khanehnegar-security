#pragma once
#include <TFT_eSPI.h>
#include "ui.h"
#include "theme.h"

namespace UI {

struct Btn { int16_t x, y, w, h; const char *label; };

extern TFT_eSPI tft;
extern Page     g_page;
extern bool     g_full;
extern bool     g_dirty;
extern uint8_t  g_lastMode;
extern String   g_pin;

bool     hit(const Btn &b, int16_t x, int16_t y);
void     drawBtn(const Btn &b, uint16_t fill, uint16_t text, uint8_t font = 2);
void     label(int16_t x, int16_t y, const String &s, uint16_t fg, uint8_t font, uint16_t pad);
void     panel(int16_t x, int16_t y, int16_t w, int16_t h);
void     caption(int16_t x, int16_t y, const char *s);
void     rightLabel(int16_t x, int16_t y, const String &s, uint16_t fg, uint8_t font, uint16_t pad);
uint16_t modeColour(uint8_t m);

Btn  keyAt(int i);
int  keyHit(int16_t x, int16_t y);
void drawKeyGrid();
void drawEntry(const char *prompt, uint16_t width);

void drawStatus(bool full);
bool statusTouch(int16_t x, int16_t y);
void drawKeypad(bool full);
bool keypadTouch(int16_t x, int16_t y);
void drawLog(bool full);
void drawManage(bool full);
bool manageTouch(int16_t x, int16_t y);
void resetKeypad();
}
