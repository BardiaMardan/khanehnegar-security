#include "internal.h"
#include "../../state.h"
#include "../../nodes.h"
#include "../../io.h"
#include "../../config.h"
#include "../../store.h"
#include "../../climate.h"
#include "../../net.h"
#include "../../fp.h"

namespace UI {

static const Btn kAction = {6, 208, 228, 56, "ARM"};

static const char *actionLabel() {
  if (Sec::arming()) return "CANCEL";
  if (Sec::mode() == MODE_DISARMED) return "ARM";
  return "DISARM";
}

void drawStatus(bool full) {
  const uint8_t  m = Sec::displayMode();
  const uint16_t c = modeColour(m);

  if (full) {
    tft.fillRect(0, BANNER_H, SCR_W, NAV_TOP - BANNER_H, COL_BG);
    tft.drawRoundRect(6, 84, 228, 12, 3, COL_PANEL);
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

  label(8, 58, Sec::message(), COL_TEXT, 2, 168);
  const uint32_t cd = Sec::countdownMs();
  label(182, 58, cd ? String(cd / 1000) + "s" : String(""), COL_WARN, 2, 50);

  const int r = constrain(Sec::risk(), 0, RISK_MAX);
  const int w = map(r, 0, RISK_MAX, 0, 224);
  const uint16_t rc = r >= RISK_ALARM_THRESHOLD ? COL_BAD : r > 0 ? COL_WARN : COL_BG;
  if (w) tft.fillRect(8, 86, w, 8, rc);
  if (w < 224) tft.fillRect(8 + w, 86, 224 - w, 8, COL_BG);

  String aux = String(Nodes::liveCount()) + " mod";
  if (Nodes::deadCount()) aux += "/" + String(Nodes::deadCount()) + " lost";
  if (Climate::valid()) {
    aux += "  " + String((int)Climate::tempC()) + "C " +
                  String((int)Climate::humidity()) + "%";
  } else {
    aux += "  --C --%";
  }
  aux += Net::online() ? "  net" : "  no net";
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

bool statusTouch(int16_t x, int16_t y) {
  if (!hit(kAction, x, y)) return false;
  if (Sec::arming())                { Sec::requestArm(MODE_DISARMED); return true; }
  if (Sec::mode() == MODE_DISARMED) { Sec::requestArm(MODE_AWAY);     return true; }
  go(PAGE_KEYPAD);
  return true;
}

static const char *kKeys[12] = {"1","2","3","4","5","6","7","8","9","CLR","0","OK"};

static Btn keyAt(int i) {
  const int col = i % 3, row = i / 3;
  return {(int16_t)(6 + col * 78), (int16_t)(52 + row * 54), 72, 48, kKeys[i]};
}

void drawKeypad(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    for (int i = 0; i < 12; i++)
      drawBtn(keyAt(i), i == 11 ? COL_OK : i == 9 ? COL_PANEL : COL_PANEL,
              i == 11 ? COL_BG : COL_TEXT, 4);
  }

  String masked;
  for (uint16_t i = 0; i < g_pin.length(); i++) masked += "*";
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(210);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString(masked.length() ? masked : String("enter code"), SCR_W / 2, 26, 4);
  tft.setTextPadding(0);
}

bool keypadTouch(int16_t x, int16_t y) {
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

void drawLog(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    label(8, 8, "event log", COL_DIM, 2, 0);
  }

  for (int i = 0; i < 12; i++) {
    const int y = 30 + i * 20;
    if (i >= Sec::logCount()) {
      label(8, y, "", COL_TEXT, 2, 224);
      continue;
    }
    const LogEntry &e = Sec::logAt(i);
    label(8, y, String(secKindName(e.kind)) + "  " + secEventName(e.code),
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

void drawNodes(bool full) {
  if (full) {
    tft.fillRect(0, 0, SCR_W, NAV_TOP, COL_BG);
    label(8, 6, "modules", COL_DIM, 2, 0);
  }

  int row = 0;
  for (int i = 0; i < Nodes::capacity() && row < 6; i++) {
    NodeRec *n = Nodes::at(i);
    if (!n || !n->used) continue;
    const int y = 28 + row * 22;
    char tail[8];
    snprintf(tail, sizeof(tail), "%02X%02X", n->mac[4], n->mac[5]);
    label(8,   y, secKindName(n->kind), n->dead ? COL_BAD : COL_OK, 2, 52);
    label(64,  y, tail, COL_TEXT, 2, 40);
    label(108, y, n->encrypted ? "AES" : "PLAIN", n->encrypted ? COL_ACCENT : COL_WARN, 2, 52);
    label(164, y, n->dead ? "LOST" : ((n->flags & SF_TAMPER) ? "OPEN" : "ok"),
          n->dead ? COL_BAD : COL_DIM, 2, 68);
    row++;
  }
  if (row == 0) label(8, 28, "none paired yet", COL_DIM, 2, 224);
  for (int i = row; i < 6; i++) label(8, 28 + i * 22, "", COL_TEXT, 2, 224);

  String line;
  if (!FP::present())       line = "fingerprint offline";
  else if (FP::enrolling()) line = FP::enrollStep() == 1 ? "lift finger" : "place finger";
  else line = String(Store::fingerCount()) + " fingers   sd " +
              (Store::ready() ? "ok" : "MISSING");
  label(8, 168, line, FP::enrolling() ? COL_WARN : COL_DIM, 2, 224);

  drawBtn(kPair,    Nodes::pairingMode() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kForget,  COL_PANEL, COL_TEXT);
  drawBtn(kEnroll,  FP::enrolling() ? COL_WARN : COL_PANEL, COL_TEXT);
  drawBtn(kEnrollD, COL_PANEL, COL_TEXT);
}

bool nodesTouch(int16_t x, int16_t y) {
  if (hit(kPair, x, y))    { Nodes::setPairingMode(!Nodes::pairingMode()); g_full = true; return true; }
  if (hit(kForget, x, y))  { Nodes::forgetAll(); g_full = true; return true; }
  if (hit(kEnroll, x, y))  { FP::startEnroll(false); g_full = true; return true; }
  if (hit(kEnrollD, x, y)) { FP::startEnroll(true);  g_full = true; return true; }
  return false;
}

}
