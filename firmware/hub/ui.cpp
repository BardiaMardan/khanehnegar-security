#include "ui.h"
#include "state.h"
#include "nodes.h"
#include "io.h"
#include "config.h"
#include "store.h"
#include "climate.h"
#include "net.h"
#include "fp.h"
#include <TFT_eSPI.h>
#include <Preferences.h>

namespace UI {

static TFT_eSPI tft;
static Page     g_page       = PAGE_STATUS;
static Page     g_drawn      = (Page)255;
static bool     g_full       = true;
static bool     g_dirty      = true;
static uint32_t g_lastPaint  = 0;
static uint32_t g_lastTouch  = 0;
static uint32_t g_touchGuard = 0;
static uint8_t  g_lastMode   = 255;
static String   g_pin        = "";

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

static void drawBtn(const Btn &b, uint16_t fill, uint16_t text, uint8_t font = 2) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.setTextPadding(0);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, font);
}

static void label(int16_t x, int16_t y, const String &s, uint16_t fg,
                  uint8_t font, uint16_t pad) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(pad);
  tft.setTextColor(fg, COL_BG);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
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
  tft.fillScreen(COL_BG);
  g_full = true;
  g_dirty = true;
}

void invalidate() { g_dirty = true; }
Page page() { return g_page; }

void go(Page p) {
  if (g_page != p) { g_page = p; g_pin = ""; }
  g_dirty = true;
}

static const char *kTabs[4] = {"STAT", "CODE", "LOG", "NODE"};

static void drawNav() {
  for (int i = 0; i < 4; i++) {
    const bool on = (g_page == (Page)i);
    tft.fillRect(i * TAB_W, NAV_TOP, TAB_W - 1, 320 - NAV_TOP, on ? COL_ACCENT : COL_PANEL);
    tft.setTextPadding(0);
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

static const Btn kAction = {6, 208, 228, 56, "ARM"};

static const char *actionLabel() {
  if (Sec::arming()) return "CANCEL";
  if (Sec::mode() == MODE_DISARMED) return "ARM";
  return "DISARM";
}

static void drawStatus(bool full) {
  const uint8_t  m = Sec::displayMode();
  const uint16_t c = modeColour(m);

  if (full) {
    tft.fillRect(0, 52, 240, NAV_TOP - 52, COL_BG);
    tft.drawRoundRect(6, 84, 228, 12, 3, COL_PANEL);
  }

  if (full || m != g_lastMode) {
    g_lastMode = m;
    tft.fillRect(0, 0, 240, 52, c);
    tft.setTextPadding(0);
    tft.setTextColor(COL_BG, c);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(secModeName(m), 10, 26, 4);
  }

  tft.setTextDatum(MR_DATUM);
  tft.setTextPadding(58);
  tft.setTextColor(COL_BG, c);
  tft.drawString(Store::clockHHMM(), 232, 27, 2);
  tft.setTextPadding(0);

  label(8, 58, Sec::message(), COL_TEXT, 2, 168);
  const uint32_t cd = Sec::countdownMs();
  label(182, 58, cd ? String(cd / 1000) + "s" : String(""), COL_WARN, 2, 50);

  const int r = constrain(Sec::risk(), 0, RISK_MAX);
  const int w = map(r, 0, RISK_MAX, 0, 224);
  const uint16_t rc = r >= RISK_ALARM_THRESHOLD ? COL_BAD : r > 0 ? COL_WARN : COL_BG;
  if (w) tft.fillRect(8, 86, w, 8, rc);
  if (w < 224) tft.fillRect(8 + w, 86, 224 - w, 8, COL_BG);

  String aux = String(Nodes::liveCount()) + " modules";
  if (Nodes::deadCount()) aux += ", " + String(Nodes::deadCount()) + " lost";
  if (Climate::valid()) aux += "   " + String((int)Climate::tempC()) + "C";
  aux += Net::online() ? "   net" : "   no net";
  label(8, 102, aux, Net::online() ? COL_DIM : COL_WARN, 2, 224);

  for (int row = 0; row < 4; row++) {
    const int y = 128 + row * 20;
    if (row < Sec::logCount()) {
      const LogEntry &e = Sec::logAt(row);
      label(8, y, String(secKindName(e.kind)) + "   " + secEventName(e.code),
            e.silent ? COL_ACCENT : COL_TEXT, 2, 224);
    } else {
      label(8, y, "", COL_TEXT, 2, 224);
    }
  }

  Btn b = kAction;
  b.label = actionLabel();
  const uint16_t fill = Sec::arming() ? COL_WARN
                      : Sec::mode() == MODE_DISARMED ? COL_OK : COL_BAD;
  drawBtn(b, fill, COL_BG, 4);
}

static bool statusTouch(int16_t x, int16_t y) {
  if (!hit(kAction, x, y)) return false;
  if (Sec::arming())               { Sec::requestArm(MODE_DISARMED); return true; }
  if (Sec::mode() == MODE_DISARMED) { Sec::requestArm(MODE_AWAY);    return true; }
  go(PAGE_KEYPAD);
  return true;
}

static const char *kKeys[12] = {"1","2","3","4","5","6","7","8","9","CLR","0","OK"};

static Btn keyAt(int i) {
  const int col = i % 3, row = i / 3;
  return {(int16_t)(6 + col * 78), (int16_t)(52 + row * 54), 72, 48, kKeys[i]};
}

static void drawKeypad(bool full) {
  if (full) {
    tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
    for (int i = 0; i < 12; i++) drawBtn(keyAt(i), i == 11 ? COL_OK : COL_PANEL, COL_TEXT);
  }

  String masked;
  for (uint16_t i = 0; i < g_pin.length(); i++) masked += "*";
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(200);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(masked.length() ? masked : String("enter code"), 120, 26, 4);
  tft.setTextPadding(0);
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

static void drawLog(bool full) {
  if (full) {
    tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
    label(8, 8, "event log", COL_DIM, 2, 0);
  }

  for (int i = 0; i < 12; i++) {
    const int y = 30 + i * 20;
    if (i >= Sec::logCount()) {
      label(8, y, "", COL_TEXT, 2, 224);
      continue;
    }
    const LogEntry &e = Sec::logAt(i);
    label(8, y, String(secKindName(e.kind)) + " " + secEventName(e.code),
          e.silent ? COL_ACCENT : COL_TEXT, 2, 178);
    tft.setTextDatum(TR_DATUM);
    tft.setTextPadding(44);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(String((millis() - e.at) / 1000) + "s", 232, y, 2);
    tft.setTextPadding(0);
  }
}

static const Btn kPair    = {6, 196, 110, 34, "PAIR MODE"};
static const Btn kForget  = {124, 196, 110, 34, "FORGET ALL"};
static const Btn kEnroll  = {6, 234, 110, 34, "ADD FINGER"};
static const Btn kEnrollD = {124, 234, 110, 34, "ADD DURESS"};

static void drawNodes(bool full) {
  if (full) {
    tft.fillRect(0, 0, 240, NAV_TOP, COL_BG);
    label(8, 6, "modules", COL_DIM, 2, 0);
  }

  int row = 0;
  for (int i = 0; i < Nodes::capacity() && row < 6; i++) {
    NodeRec *n = Nodes::at(i);
    if (!n || !n->used) continue;
    const int y = 28 + row * 22;
    char tail[8];
    snprintf(tail, sizeof(tail), "%02X%02X", n->mac[4], n->mac[5]);
    label(8,   y, secKindName(n->kind), n->dead ? COL_BAD : COL_OK, 2, 44);
    label(56,  y, tail, COL_TEXT, 2, 40);
    label(100, y, n->encrypted ? "AES" : "PLAIN", n->encrypted ? COL_ACCENT : COL_WARN, 2, 56);
    label(160, y, n->dead ? "LOST" : ((n->flags & SF_TAMPER) ? "OPEN" : "ok"),
          n->dead ? COL_BAD : COL_DIM, 2, 72);
    row++;
  }
  if (row == 0) label(8, 28, "none paired yet", COL_DIM, 2, 224);
  for (int i = row; i < 6; i++) label(8, 28 + i * 22, "", COL_TEXT, 2, 224);

  String fpLine;
  if (!FP::present()) {
    fpLine = "fingerprint: not responding";
  } else if (FP::enrolling()) {
    fpLine = FP::enrollStep() == 1 ? "lift finger" : "place finger";
  } else {
    fpLine = "fingers " + String(Store::fingerCount()) + " / sd " +
             String(Store::ready() ? "ok" : "MISSING");
  }
  label(8, 168, fpLine, FP::enrolling() ? COL_WARN : COL_DIM, 2, 224);

  drawBtn(kPair,    Nodes::pairingMode() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kForget,  COL_PANEL, COL_TEXT);
  drawBtn(kEnroll,  (FP::enrolling() && !FP::lastEnrollDuress()) ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kEnrollD, COL_PANEL, COL_TEXT);
}

static bool nodesTouch(int16_t x, int16_t y) {
  if (hit(kPair, x, y))    { Nodes::setPairingMode(!Nodes::pairingMode()); g_full = true; return true; }
  if (hit(kForget, x, y))  { Nodes::forgetAll(); g_full = true; return true; }
  if (hit(kEnroll, x, y))  { FP::startEnroll(false); g_full = true; return true; }
  if (hit(kEnrollD, x, y)) { FP::startEnroll(true);  g_full = true; return true; }
  return false;
}

static bool g_toastShown = false;

static void drawToast() {
  const uint8_t k = Sec::toastKind();
  const uint16_t c = (k == 1) ? COL_OK : (k == 2) ? COL_WARN : COL_BAD;
  tft.fillRoundRect(8, 118, 224, 46, 6, c);
  tft.drawRoundRect(8, 118, 224, 46, 6, COL_TEXT);
  tft.setTextPadding(0);
  tft.setTextColor(COL_BG, c);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(Sec::toastText(), 120, 141, 2);
}

void tick() {
  const uint32_t now = millis();

  if (now - g_lastTouch >= 40) {
    g_lastTouch = now;
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
  }

  if (g_drawn != g_page) {
    g_drawn = g_page;
    g_full = true;
  }

  const bool toastLive = Sec::toastAge() < 2200;
  const bool periodic = (now - g_lastPaint > 400) &&
                        (g_page != PAGE_KEYPAD || toastLive || g_toastShown);
  if (!g_full && !g_dirty && !periodic) return;

  const bool full = g_full;
  g_full = false;
  g_dirty = false;
  g_lastPaint = now;

  switch (g_page) {
    case PAGE_STATUS: drawStatus(full); break;
    case PAGE_KEYPAD: drawKeypad(full); break;
    case PAGE_LOG:    drawLog(full);    break;
    case PAGE_NODES:  drawNodes(full);  break;
  }
  if (full) drawNav();

  if (toastLive) {
    drawToast();
    g_toastShown = true;
  } else if (g_toastShown) {
    g_toastShown = false;
    g_full = true;
  }
}

}
