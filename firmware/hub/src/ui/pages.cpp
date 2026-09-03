// =============================================================================
//  pages.cpp — the pixels: five screens, one keypad, and their touch targets
// =============================================================================

#include "internal.h"
#include "../../state.h"
#include "../../nodes.h"
#include "../../io.h"
#include "../../config.h"
#include "../../store.h"
#include "../../journal.h"
#include "../../access.h"
#include "../../fingers.h"
#include "../../climate.h"
#include "../../net.h"
#include "../../fp.h"

namespace UI {

// ========== [Keypad grid] ==========
static const char *kKeys[12] = {"1","2","3","4","5","6","7","8","9","CLR","0","OK"};

Btn keyAt(int i) {
  const int col = i % 3, row = i / 3;
  return {(int16_t)(6 + col * 78), (int16_t)(46 + row * 54), 72, 48, kKeys[i]};
}

int keyHit(int16_t x, int16_t y) {
  for (int i = 0; i < 12; i++) if (hit(keyAt(i), x, y)) return i;
  return -1;
}

void drawKeyGrid() {
  for (int i = 0; i < 12; i++)
    drawBtn(keyAt(i), i == 11 ? COL_OK : COL_PANEL, i == 11 ? COL_BG : COL_TEXT, 4);
}

void drawEntry(const char *prompt, uint16_t width) {
  String masked;
  for (uint16_t i = 0; i < g_pin.length(); i++) masked += "*";
  tft.setTextDatum(ML_DATUM);
  tft.setTextPadding(width);
  tft.setTextColor(g_pin.length() ? COL_TEXT : COL_DIM, COL_BG);
  tft.drawString(masked.length() ? masked : String(prompt), 10, 24, 4);
  tft.setTextPadding(0);
}

// ========== [Status page] ==========
// Wall clock, never a relative age. "120s ago" changes every second, which forced a repaint
// of twelve rows at 2.5 Hz and made the whole panel feel laggy for no information gain.
// An entry with no epoch predates the first time sync and shows dashes.
static String stamp(const LogEntry &e) {
  return Store::hhmm(e.epoch);
}

static const Btn kAction = {6, 208, 228, 56, "ARM"};

static const char *actionLabel() {
  if (Sec::lockdown()) return "LOCKED";
  if (Sec::arming()) return "CANCEL";
  if (Sec::mode() == MODE_DISARMED) return "ARM";
  return "DISARM";
}

void drawStatus(bool full) {
  const uint8_t  m = Sec::displayMode();
  const uint16_t c = modeColour(m);

  if (full) {
    tft.fillRect(0, BANNER_H, SCR_W, NAV_TOP - BANNER_H, COL_BG);
    tft.drawRoundRect(6, 82, 228, 10, 3, COL_PANEL);
    panel(6, 100, 228, 46);
    panel(6, 152, 228, 46);
    caption(16, 106, "MODULES");
    caption(16, 158, "CLIMATE");
    caption(150, 106, "LINK");
    caption(150, 158, "HUMIDITY");
  }

  if (full || m != g_lastMode) {
    g_lastMode = m;
    tft.fillRect(0, 0, SCR_W, BANNER_H, c);
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

  const bool waiting = Access::pending() && !Access::granted();
  label(8, 58, waiting ? "WEB ACTION PENDING" : Sec::message(),
        waiting ? COL_WARN : COL_TEXT, 2, 168);

  const uint32_t cd = Sec::countdownMs();
  rightLabel(232, 58, cd ? String(cd / 1000) + "s" : String(""), COL_WARN, 2, 50);

  const int r = constrain(Sec::risk(), 0, RISK_MAX);
  const int w = map(r, 0, RISK_MAX, 0, 224);
  const uint16_t rc = r >= RISK_ALARM_THRESHOLD ? COL_BAD : r > 0 ? COL_WARN : COL_BG;
  if (w) tft.fillRect(8, 84, w, 6, rc);
  if (w < 224) tft.fillRect(8 + w, 84, 224 - w, 6, COL_BG);

  // One label per cell, one line each. Two labels stacked inside a 46px panel used to
  // overlap: a font-4 string is 26px tall, so the second row landed on top of the first.
  // And a padded label only erases as many pixels as the padding, so "NO NET" -> "NET" left
  // its tail on the glass until a full repaint. Both cells now use a fixed-width string and
  // padding wider than the longest one they can hold.
  const int lost = Nodes::deadCount();
  const int live = Nodes::liveCount();
  const String modules = lost ? String(live) + "/" + String(live + lost)
                             : String(live) + " up";
  label(16, 118, modules, lost ? COL_BAD : COL_OK, 4, 128);

  label(150, 118, Net::online() ? "NET" : "DOWN",
        Net::online() ? COL_OK : COL_BAD, 4, 80);

  if (Climate::valid()) {
    label(16, 170, String((int)Climate::tempC()) + "C", COL_TEXT, 4, 128);
    label(150, 170, String((int)Climate::humidity()) + "%", COL_TEXT, 4, 80);
  } else {
    label(16, 170, "--", COL_DIM, 4, 128);
    label(150, 170, "--", COL_DIM, 4, 80);
  }

  Btn b = kAction;
  b.label = actionLabel();
  const uint16_t fill = Sec::lockdown() ? COL_BAD
                      : Sec::arming() ? COL_WARN
                      : Sec::mode() == MODE_DISARMED ? COL_OK : COL_BAD;
  drawBtn(b, fill, COL_BG, 4);
}

bool statusTouch(int16_t x, int16_t y) {
  if (!hit(kAction, x, y)) return false;
  if (Sec::lockdown())              { go(PAGE_KEYPAD);                return true; }
  if (Sec::arming())                { Sec::requestArm(MODE_DISARMED); return true; }
  if (Sec::mode() == MODE_DISARMED) { Sec::requestArm(MODE_AWAY);     return true; }
  go(PAGE_KEYPAD);
  return true;
}

// ========== [Keypad page and the change-PIN state machine] ==========
enum KeyMode : uint8_t { KM_UNLOCK = 0, KM_CUR, KM_NEW, KM_CONFIRM };

static KeyMode g_km = KM_UNLOCK;
static String  g_cur;
static String  g_new;

static const Btn kChange = {172, 6, 62, 34, "CODE"};

void resetKeypad() {
  g_km = KM_UNLOCK;
  g_cur = "";
  g_new = "";
}

static const char *kmPrompt() {
  switch (g_km) {
    case KM_CUR:     return "current";
    case KM_NEW:     return "new code";
    case KM_CONFIRM: return "again";
    default:         return "enter code";
  }
}

void drawKeypad(bool full) {
  if (Sec::lockdown()) {
    if (full) tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(220);
    tft.setTextColor(COL_BAD, COL_BG);
    tft.drawString("LOCKED", SCR_W / 2, 96, 4);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString("too many wrong codes", SCR_W / 2, 136, 2);
    tft.drawString("use a fingerprint", SCR_W / 2, 160, 2);
    tft.drawString("or unlock from the bot", SCR_W / 2, 184, 2);
    tft.setTextPadding(0);
    return;
  }

  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    drawKeyGrid();
  }

  drawBtn(kChange, g_km == KM_UNLOCK ? COL_PANEL : COL_ACCENT,
          g_km == KM_UNLOCK ? COL_DIM : COL_BG);
  drawEntry(kmPrompt(), 156);
}

bool keypadTouch(int16_t x, int16_t y) {
  if (Sec::lockdown()) return false;

  if (hit(kChange, x, y)) {
    if (g_km == KM_UNLOCK) g_km = KM_CUR;
    else                   resetKeypad();
    g_pin = "";
    g_full = true;
    IO::beep(1, 20, 0);
    return true;
  }

  const int k = keyHit(x, y);
  if (k < 0) return false;

  if (k == 9) {
    g_pin = "";
    return true;
  }

  if (k != 11) {
    if (g_pin.length() < 8) g_pin += kKeys[k];
    IO::beep(1, 15, 0);
    return true;
  }

  const String entered = g_pin;
  g_pin = "";

  switch (g_km) {
    case KM_UNLOCK:
      Sec::submitPin(entered);
      go(PAGE_STATUS);
      break;
    case KM_CUR:
      g_cur = entered;
      g_km = KM_NEW;
      break;
    case KM_NEW:
      if (entered.length() < PIN_LENGTH) {
        Sec::showToast("CODE TOO SHORT", 3);
        resetKeypad();
      } else {
        g_new = entered;
        g_km = KM_CONFIRM;
      }
      break;
    case KM_CONFIRM:
      if (entered == g_new) Sec::changePin(g_cur, g_new);
      else                  Sec::showToast("CODES DID NOT MATCH", 3);
      resetKeypad();
      break;
  }
  g_full = true;
  return true;
}

// ========== [Log page] ==========
void drawLog(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    label(8, 8, "event log", COL_DIM, 2, 0);
  }

  for (int i = 0; i < 12; i++) {
    const int y = 30 + i * 20;
    if (i >= Journal::count()) {
      label(8, y, "", COL_TEXT, 2, 224);
      continue;
    }
    const LogEntry &e = Journal::at(i);
    label(8, y, String(secKindName(e.kind)) + "  " + secEventName(e.code),
          e.silent ? COL_ACCENT : eventColour(e.code), 2, 178);
    tft.setTextDatum(TR_DATUM);
    tft.setTextPadding(44);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(stamp(e), 232, y, 2);
    tft.setTextPadding(0);
  }
}

// ========== [Manage page] ==========
static const Btn kPair   = {6, 196, 110, 34, "PAIR MODE"};
static const Btn kForget = {124, 196, 110, 34, "FORGET NODES"};
static const Btn kEnroll = {6, 234, 110, 34, "ADD FINGER"};
static const Btn kWipe   = {124, 234, 110, 34, "WIPE FINGERS"};

static void drawGate(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    drawKeyGrid();
  }
  drawEntry("code to manage", 220);
}

void drawManage(bool full) {
  if (!Access::granted()) {
    drawGate(full);
    return;
  }

  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    label(8, 6, "manage", COL_DIM, 2, 80);
  }

  int row = 0;
  for (int i = 0; i < Nodes::capacity() && row < 5; i++) {
    NodeRec *n = Nodes::at(i);
    if (!n || !n->used) continue;
    const int y = 28 + row * 22;
    label(8, y, secKindName(n->kind), COL_TEXT, 2, 70);
    label(80, y, n->dead ? "OFFLINE" : "ONLINE", n->dead ? COL_BAD : COL_OK, 2, 72);
    label(156, y, n->dead ? "" : ((n->flags & SF_TAMPER) ? "CASE OPEN"
                                                         : secEventName(n->lastEvent)),
          (n->flags & SF_TAMPER) ? COL_BAD : eventColour(n->lastEvent), 1, 76);
    row++;
  }
  if (row == 0) label(8, 28, "none paired yet", COL_DIM, 2, 224);
  for (int i = row; i < 5; i++) label(8, 28 + i * 22, "", COL_TEXT, 2, 224);

  String line;
  if (!FP::present())       line = "fingerprint offline";
  else if (FP::enrolling()) line = FP::enrollStep() == 1 ? "lift finger" : "place finger";
  else line = String(Fingers::count()) + " fingers   sd " +
              (Store::ready() ? "ok" : "MISSING");
  label(8, 150, line, FP::enrolling() ? COL_WARN : COL_DIM, 2, 224);

  drawBtn(kPair,   Nodes::pairingMode() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kForget, COL_PANEL, COL_TEXT);
  drawBtn(kEnroll, FP::enrolling() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kWipe,   COL_PANEL, COL_TEXT);
}

static bool gateTouch(int16_t x, int16_t y) {
  const int k = keyHit(x, y);
  if (k < 0) return false;

  if (k == 9) {
    g_pin = "";
    return true;
  }

  if (k != 11) {
    if (g_pin.length() < 8) g_pin += kKeys[k];
    IO::beep(1, 15, 0);
    return true;
  }

  const String entered = g_pin;
  g_pin = "";
  Sec::openManage(entered);
  g_full = true;
  return true;
}

bool manageTouch(int16_t x, int16_t y) {
  if (!Access::granted()) return gateTouch(x, y);

  Access::keepAlive();

  if (hit(kPair, x, y)) {
    Nodes::setPairingMode(!Nodes::pairingMode());
    g_full = true;
    return true;
  }
  if (hit(kForget, x, y)) {
    Nodes::forgetAll();
    Sec::showToast("MODULES FORGOTTEN", 2);
    g_full = true;
    return true;
  }
  if (hit(kEnroll, x, y)) {
    Sec::enrolFinger();
    g_full = true;
    return true;
  }
  if (hit(kWipe, x, y)) {
    Sec::clearFingers();
    g_full = true;
    return true;
  }
  return false;
}

// ========== [Standby] ==========
void drawStandby(bool full) {
  const uint8_t  m = Sec::displayMode();
  const uint16_t c = modeColour(m);

  if (full) {
    tft.fillScreen(COL_BG);
    caption(96, 236, "TEMPERATURE");
    caption(96, 276, "HUMIDITY");
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(230);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(Store::clockHHMM(), SCR_W / 2, 108, 7);

  tft.setTextPadding(180);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(Store::dayStamp(), SCR_W / 2, 152, 2);

  tft.setTextPadding(200);
  tft.setTextColor(c, COL_BG);
  tft.drawString(secModeName(m), SCR_W / 2, 190, 4);
  tft.setTextPadding(0);

  const String t = Climate::valid() ? String((int)Climate::tempC()) + "C" : String("--");
  const String h = Climate::valid() ? String((int)Climate::humidity()) + "%" : String("--");
  rightLabel(86, 230, t, COL_TEXT, 4, 76);
  rightLabel(86, 270, h, COL_TEXT, 4, 76);
}

}
