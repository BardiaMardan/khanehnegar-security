#include "ui.h"
#include "state.h"
#include "nodes.h"
#include "io.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Preferences.h>

namespace UI {

static TFT_eSPI tft;
static Page     g_page       = PAGE_STATUS;
static bool     g_dirty      = true;
static uint32_t g_lastPaint  = 0;
static uint32_t g_touchGuard = 0;
static String   g_pin        = "";
static bool     g_enroll     = false;
static bool     g_enrollDur  = false;
static String   g_lastUid    = "";

#define COL_BG      0x0000
#define COL_PANEL   0x18C3
#define COL_TEXT    0xFFFF
#define COL_DIM     0x8410
#define COL_OK      0x0660
#define COL_WARN    0xFD20
#define COL_BAD     0xF800
#define COL_ACCENT  0x04FF

#define NAV_TOP     272
#define TAB_W       60

struct Btn { int16_t x, y, w, h; const char *label; };

static bool hit(const Btn &b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void drawBtn(const Btn &b, uint16_t fill, uint16_t text) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 4, fill);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

static uint16_t modeColour(uint8_t m) {
  switch (m) {
    case MODE_DISARMED: return COL_OK;
    case MODE_HOME:     return COL_ACCENT;
    case MODE_AWAY:     return COL_WARN;
    case MODE_ENTRY:    return COL_WARN;
    case MODE_ALARM:    return COL_BAD;
    default:            return COL_DIM;
  }
}

static void calibrate() {
  Preferences p;
  uint16_t cal[5] = {0};
  p.begin("khaneh", true);
  const size_t got = p.getBytes("tcal", cal, sizeof(cal));
  p.end();

  if (got == sizeof(cal) && cal[4] != 0) {
    tft.setTouch(cal);
    return;
  }

  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("touch each corner", 120, 140, 2);
  tft.drawString("marker in turn", 120, 162, 2);
  delay(1200);
  tft.calibrateTouch(cal, COL_TEXT, COL_BG, 15);

  p.begin("khaneh", false);
  p.putBytes("tcal", cal, sizeof(cal));
  p.end();
  tft.setTouch(cal);
}

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  calibrate();
  g_dirty = true;
}

void invalidate() { g_dirty = true; }
Page page() { return g_page; }

void go(Page p) {
  if (g_page != p) { g_page = p; g_pin = ""; }
  g_dirty = true;
}

bool enrollArmed()    { return g_enroll; }
bool enrollAsDuress() { return g_enrollDur; }
void clearEnroll()    { g_enroll = false; g_enrollDur = false; g_dirty = true; }

void flashCardSeen(const String &uid) {
  g_lastUid = uid;
  g_dirty = true;
}

static const char *kTabs[4] = {"STAT", "CODE", "LOG", "NODE"};

static void drawNav() {
  for (int i = 0; i < 4; i++) {
    const bool on = (g_page == (Page)i);
    tft.fillRect(i * TAB_W, NAV_TOP, TAB_W - 1, 320 - NAV_TOP, on ? COL_ACCENT : COL_PANEL);
    tft.setTextColor(on ? COL_BG : COL_DIM, on ? COL_ACCENT : COL_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(kTabs[i], i * TAB_W + TAB_W / 2, NAV_TOP + 24, 2);
  }
}

static bool navTouch(int16_t x, int16_t y) {
  if (y < NAV_TOP) return false;
  go((Page)constrain(x / TAB_W, 0, 3));
  return true;
}

static const Btn kArmAway = {6, 224, 110, 40, "ARM AWAY"};
static const Btn kArmHome = {124, 224, 110, 40, "ARM HOME"};

static void drawStatus() {
  const uint8_t m = Sec::displayMode();
  tft.fillRect(0, 0, 240, 46, modeColour(m));
  tft.setTextColor(COL_BG, modeColour(m));
  tft.setTextDatum(MC_DATUM);
  tft.drawString(secModeName(m), 120, 23, 4);

  tft.fillRect(0, 46, 240, NAV_TOP - 46, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(Sec::message(), 8, 54, 2);

  const uint32_t cd = Sec::countdownMs();
  if (cd) {
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString(String(cd / 1000) + "s", 190, 54, 2);
  }

  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("risk", 8, 78, 1);
  tft.drawRect(40, 76, 192, 10, COL_DIM);
  const int w = map(constrain(Sec::risk(), 0, RISK_MAX), 0, RISK_MAX, 0, 190);
  const uint16_t rc = Sec::risk() >= RISK_ALARM_THRESHOLD ? COL_BAD
                    : Sec::risk() > 0                     ? COL_WARN : COL_PANEL;
  tft.fillRect(41, 77, w, 8, rc);

  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("modules " + String(Nodes::liveCount()) + " live / " +
                 String(Nodes::deadCount()) + " lost", 8, 94, 2);

  int row = 0;
  for (int i = 0; i < Sec::logCount() && row < 5; i++, row++) {
    const LogEntry &e = Sec::logAt(i);
    tft.setTextColor(e.silent ? COL_ACCENT : COL_TEXT, COL_BG);
    tft.drawString(String(secKindName(e.kind)) + " " + secEventName(e.code),
                   8, 118 + row * 20, 2);
  }

  drawBtn(kArmAway, COL_PANEL, COL_TEXT);
  drawBtn(kArmHome, COL_PANEL, COL_TEXT);
}

static bool statusTouch(int16_t x, int16_t y) {
  if (hit(kArmAway, x, y)) { Sec::requestArm(MODE_AWAY); return true; }
  if (hit(kArmHome, x, y)) { Sec::requestArm(MODE_HOME); return true; }
  return false;
}

static const char *kKeys[12] = {"1","2","3","4","5","6","7","8","9","CLR","0","OK"};

static Btn keyAt(int i) {
  const int col = i % 3, row = i / 3;
  return {(int16_t)(6 + col * 78), (int16_t)(52 + row * 54), 72, 48, kKeys[i]};
}

static void drawKeypad() {
  tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  String masked;
  for (uint16_t i = 0; i < g_pin.length(); i++) masked += "*";
  tft.drawString(masked.length() ? masked : String("enter code"), 120, 26, 4);
  for (int i = 0; i < 12; i++) drawBtn(keyAt(i), i == 11 ? COL_OK : COL_PANEL, COL_TEXT);
}

static bool keypadTouch(int16_t x, int16_t y) {
  for (int i = 0; i < 12; i++) {
    if (!hit(keyAt(i), x, y)) continue;
    if (i == 9) {
      g_pin = "";
    } else if (i == 11) {
      Sec::submitPin(g_pin);
      g_pin = "";
      go(PAGE_STATUS);
    } else {
      if (g_pin.length() < 8) g_pin += kKeys[i];
      IO::beep(1, 15, 0);
    }
    return true;
  }
  return false;
}

static void drawLog() {
  tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("event log", 8, 8, 2);
  for (int i = 0; i < Sec::logCount() && i < 12; i++) {
    const LogEntry &e = Sec::logAt(i);
    tft.setTextColor(e.silent ? COL_ACCENT : COL_TEXT, COL_BG);
    tft.drawString(String(secKindName(e.kind)) + " " + secEventName(e.code),
                   8, 30 + i * 20, 2);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String((millis() - e.at) / 1000) + "s", 232, 30 + i * 20, 2);
    tft.setTextDatum(TL_DATUM);
  }
}

static const Btn kPair    = {6, 196, 110, 34, "PAIR MODE"};
static const Btn kForget  = {124, 196, 110, 34, "FORGET ALL"};
static const Btn kEnroll  = {6, 234, 110, 34, "ADD CARD"};
static const Btn kEnrollD = {124, 234, 110, 34, "ADD DURESS"};

static void drawNodes() {
  tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("modules", 8, 6, 2);

  int row = 0;
  for (int i = 0; i < Nodes::capacity() && row < 7; i++) {
    NodeRec *n = Nodes::at(i);
    if (!n || !n->used) continue;
    const int y = 28 + row * 22;
    tft.setTextColor(n->dead ? COL_BAD : COL_OK, COL_BG);
    tft.drawString(secKindName(n->kind), 8, y, 2);
    tft.setTextColor(COL_TEXT, COL_BG);
    char tail[8];
    snprintf(tail, sizeof(tail), "%02X%02X", n->mac[4], n->mac[5]);
    tft.drawString(tail, 56, y, 2);
    tft.setTextColor(n->encrypted ? COL_ACCENT : COL_WARN, COL_BG);
    tft.drawString(n->encrypted ? "AES" : "PLAIN", 100, y, 2);
    tft.setTextColor(n->dead ? COL_BAD : COL_DIM, COL_BG);
    tft.drawString(n->dead ? "LOST" : ((n->flags & SF_TAMPER) ? "OPEN" : "ok"), 160, y, 2);
    row++;
  }
  if (row == 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("none paired yet", 8, 28, 2);
  }

  if (g_lastUid.length()) {
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.drawString(g_lastUid, 8, 174, 1);
  }

  drawBtn(kPair,    Nodes::pairingMode() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kForget,  COL_PANEL, COL_TEXT);
  drawBtn(kEnroll,  (g_enroll && !g_enrollDur) ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kEnrollD, (g_enroll && g_enrollDur)  ? COL_WARN : COL_PANEL, COL_TEXT);
}

static bool nodesTouch(int16_t x, int16_t y) {
  if (hit(kPair, x, y))    { Nodes::setPairingMode(!Nodes::pairingMode()); return true; }
  if (hit(kForget, x, y))  { Nodes::forgetAll(); return true; }
  if (hit(kEnroll, x, y))  { g_enroll = true; g_enrollDur = false; return true; }
  if (hit(kEnrollD, x, y)) { g_enroll = true; g_enrollDur = true;  return true; }
  return false;
}

void tick() {
  const uint32_t now = millis();

  uint16_t rx, ry;
  if (now > g_touchGuard && tft.getTouch(&rx, &ry)) {
    const int16_t tx = (int16_t)rx;
    const int16_t ty = (int16_t)ry;
    g_touchGuard = now + 220;
    bool handled = navTouch(tx, ty);
    if (!handled) {
      switch (g_page) {
        case PAGE_STATUS: handled = statusTouch(tx, ty); break;
        case PAGE_KEYPAD: handled = keypadTouch(tx, ty); break;
        case PAGE_LOG:    handled = false;               break;
        case PAGE_NODES:  handled = nodesTouch(tx, ty);  break;
      }
    }
    if (handled) g_dirty = true;
  }

  const bool periodic = (g_page != PAGE_KEYPAD) && (now - g_lastPaint > 500);
  if (!g_dirty && !periodic) return;

  g_dirty = false;
  g_lastPaint = now;

  switch (g_page) {
    case PAGE_STATUS: drawStatus(); break;
    case PAGE_KEYPAD: drawKeypad(); break;
    case PAGE_LOG:    drawLog();    break;
    case PAGE_NODES:  drawNodes();  break;
  }
  drawNav();
}

}
