#include "internal.h"
#include "../../state.h"
#include "../../access.h"
#include "../../config.h"
#include <Preferences.h>

namespace UI {

TFT_eSPI tft;
Page     g_page     = PAGE_STATUS;
bool     g_full     = true;
bool     g_dirty    = true;
uint8_t  g_lastMode = 255;
String   g_pin      = "";

static Page     g_drawn       = (Page)255;
static uint32_t g_lastPaint   = 0;
static uint32_t g_lastTouch   = 0;
static uint32_t g_touchGuard  = 0;
static bool     g_toastShown  = false;
static bool     g_popupShown  = false;
static uint8_t  g_bootStep    = 0;
static bool     g_booting     = true;

bool hit(const Btn &b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void drawBtn(const Btn &b, uint16_t fill, uint16_t text, uint8_t font) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.setTextPadding(0);
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, font);
}

void label(int16_t x, int16_t y, const String &s, uint16_t fg, uint8_t font, uint16_t pad) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(pad);
  tft.setTextColor(fg, COL_BG);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

uint16_t modeColour(uint8_t m) {
  switch (m) {
    case MODE_DISARMED: return COL_OK;
    case MODE_HOME:     return COL_ACCENT;
    case MODE_AWAY:     return COL_WARN;
    case MODE_ENTRY:    return COL_WARN;
    case MODE_ALARM:    return COL_BAD;
    case MODE_LOCKDOWN: return COL_BAD;
    default:            return COL_DIM;
  }
}

static void splashFrame() {
  tft.fillScreen(COL_BG);
  tft.setTextPadding(0);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString("khanehnegar", SCR_W / 2, 96, 4);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString("security terminal", SCR_W / 2, 126, 2);
  tft.drawRoundRect(28, 186, 184, 10, 3, COL_PANEL);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(CREDIT_NAME, SCR_W / 2, 246, 2);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(CREDIT_ORG, SCR_W / 2, 264, 2);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(CREDIT_URL1, SCR_W / 2, 286, 2);
  tft.drawString(CREDIT_URL2, SCR_W / 2, 304, 2);
}

void boot(const char *step) {
  if (!g_booting) return;
  if (g_bootStep < 6) g_bootStep++;
  const int w = (180 * g_bootStep) / 6;
  tft.fillRect(30, 188, w, 6, COL_ACCENT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(220);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(step, SCR_W / 2, 216, 2);
  tft.setTextPadding(0);
}

static void calibrate() {
  Preferences p;
  uint16_t cal[5] = {0};
  char key[8];
  snprintf(key, sizeof(key), "tcal%d", UI_ROTATION);

  p.begin("khaneh", true);
  const size_t got = p.getBytes(key, cal, sizeof(cal));
  p.end();

  if (got == sizeof(cal) && cal[4] != 0) {
    tft.setTouch(cal);
    return;
  }

  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("touch each corner", SCR_W / 2, 140, 2);
  tft.drawString("marker in turn", SCR_W / 2, 162, 2);
  delay(1200);
  tft.calibrateTouch(cal, COL_TEXT, COL_BG, 15);

  p.begin("khaneh", false);
  p.putBytes(key, cal, sizeof(cal));
  p.end();
  tft.setTouch(cal);
}

void begin() {
  tft.init();
  tft.setRotation(UI_ROTATION);
  tft.fillScreen(COL_BG);
  calibrate();
  splashFrame();
  g_booting = true;
  g_bootStep = 0;
  boot("starting");
}

void ready() {
  g_booting = false;
  tft.fillScreen(COL_BG);
  g_full = true;
  g_dirty = true;
}

void invalidate() { g_dirty = true; }
Page page() { return g_page; }

void go(Page p) {
  if (g_page != p) {
    g_page = p;
    g_pin = "";
    resetKeypad();
  }
  g_dirty = true;
}

static const char *kTabs[4] = {"STATUS", "CODE", "LOG", "MANAGE"};

static void drawNav() {
  for (int i = 0; i < 4; i++) {
    const bool on = (g_page == (Page)i);
    tft.fillRect(i * TAB_W, NAV_TOP, TAB_W - 1, SCR_H - NAV_TOP, on ? COL_ACCENT : COL_PANEL);
    tft.setTextPadding(0);
    tft.setTextColor(on ? COL_BG : COL_DIM, on ? COL_ACCENT : COL_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(kTabs[i], i * TAB_W + TAB_W / 2, NAV_TOP + 24, 1);
  }
}

static bool navTouch(int16_t x, int16_t y) {
  if (y < NAV_TOP) return false;
  go((Page)constrain(x / TAB_W, 0, 3));
  return true;
}

static void drawToast() {
  const uint8_t k = Sec::toastKind();
  const uint16_t c = (k == 1) ? COL_OK : (k == 2) ? COL_WARN : COL_BAD;
  tft.fillRoundRect(8, TOAST_Y, 224, TOAST_H, 6, c);
  tft.drawRoundRect(8, TOAST_Y, 224, TOAST_H, 6, COL_TEXT);
  tft.setTextPadding(0);
  tft.setTextColor(COL_BG, c);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(Sec::toastText(), SCR_W / 2, TOAST_Y + TOAST_H / 2, 2);
}

static const Btn kAllow = {16, 194, 98, 40, "ALLOW"};
static const Btn kDeny  = {126, 194, 98, 40, "DENY"};

static bool popupLive() { return Access::pending() && Access::granted(); }

static void popupCountdown() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(90);
  tft.setTextColor(COL_DIM, COL_PANEL);
  tft.drawString(String(Access::approvalMsLeft() / 1000) + "s to answer",
                 SCR_W / 2, 174, 2);
  tft.setTextPadding(0);
}

static void drawPopup() {
  const Access::Approval &a = Access::approval();

  tft.fillRoundRect(8, 92, 224, 152, 8, COL_PANEL);
  tft.drawRoundRect(8, 92, 224, 152, 8, COL_WARN);
  tft.setTextPadding(0);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_WARN, COL_PANEL);
  tft.drawString("THE WEB ASKS TO", SCR_W / 2, 112, 2);

  String text = a.text;
  int cut = -1;
  if (text.length() > 26) {
    cut = text.lastIndexOf(' ', 26);
    if (cut < 8) cut = 26;
  }
  tft.setTextColor(COL_TEXT, COL_PANEL);
  if (cut < 0) {
    tft.drawString(text, SCR_W / 2, 140, 2);
  } else {
    tft.drawString(text.substring(0, cut), SCR_W / 2, 132, 2);
    tft.drawString(text.substring(cut + 1), SCR_W / 2, 152, 2);
  }

  popupCountdown();
  drawBtn(kAllow, COL_OK, COL_BG);
  drawBtn(kDeny, COL_BAD, COL_BG);
}

void tick() {
  if (g_booting) return;
  const uint32_t now = millis();

  if (now - g_lastTouch >= 40) {
    g_lastTouch = now;
    uint16_t rx, ry;
    if (now > g_touchGuard && tft.getTouch(&rx, &ry)) {
      const int16_t tx = (int16_t)rx;
      const int16_t ty = (int16_t)ry;
      g_touchGuard = now + 220;

      if (popupLive()) {
        if      (hit(kAllow, tx, ty)) Sec::decideApproval(true);
        else if (hit(kDeny, tx, ty))  Sec::decideApproval(false);
        g_dirty = true;
      } else {
        bool handled = navTouch(tx, ty);
        if (!handled) {
          switch (g_page) {
            case PAGE_STATUS: handled = statusTouch(tx, ty); break;
            case PAGE_KEYPAD: handled = keypadTouch(tx, ty); break;
            case PAGE_LOG:    handled = false;               break;
            case PAGE_MANAGE: handled = manageTouch(tx, ty); break;
          }
        }
        if (handled) g_dirty = true;
      }
    }
  }

  if (popupLive()) {
    if (!g_popupShown) {
      g_popupShown = true;
      g_lastPaint = now;
      drawPopup();
    } else if (now - g_lastPaint > 400) {
      g_lastPaint = now;
      popupCountdown();
    }
    return;
  }

  if (g_popupShown) {
    g_popupShown = false;
    g_full = true;
  }

  const bool toastLive = Sec::toastAge() < TOAST_MS;

  if (toastLive && g_toastShown) return;

  if (!toastLive && g_toastShown) {
    g_toastShown = false;
    g_full = true;
  }

  if (g_drawn != g_page) {
    g_drawn = g_page;
    g_full = true;
  }

  const bool periodic = (g_page != PAGE_KEYPAD) && (now - g_lastPaint > 400);
  if (!g_full && !g_dirty && !periodic && !toastLive) return;

  const bool full = g_full;
  g_full = false;
  g_dirty = false;
  g_lastPaint = now;

  switch (g_page) {
    case PAGE_STATUS: drawStatus(full); break;
    case PAGE_KEYPAD: drawKeypad(full); break;
    case PAGE_LOG:    drawLog(full);    break;
    case PAGE_MANAGE: drawManage(full); break;
  }
  if (full) drawNav();

  if (toastLive) {
    drawToast();
    g_toastShown = true;
  }
}

}
