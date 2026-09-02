#include "state.h"
#include "io.h"
#include "config.h"
#include "store.h"
#include "journal.h"
#include "access.h"
#include "fingers.h"
#include "fp.h"
#include "net.h"
#include <SecMesh.h>

namespace Sec {

struct Score { uint8_t weight; bool loud; };

static Score scoreFor(uint8_t code) {
  switch (code) {
    case EV_PRETOUCH:    return {15, false};
    case EV_TOUCH:       return {25, false};
    case EV_TILT:        return {35, false};
    case EV_AGITATION:   return {55, false};
    case EV_TAMPER_OPEN: return {80, false};
    case EV_RF_PROBE:    return {25, false};
    case EV_RF_DEAUTH:   return {80, false};
    case EV_RF_JAM:      return {80, false};
    case EV_NODE_LOST:   return {75, false};
    case EV_CODE_ATTACK: return {70, false};
    case EV_MOTION:      return {30, true};
    case EV_DOOR_OPEN:   return {40, true};
    case EV_LASER_CUT:   return {45, true};
    case EV_IMPACT:      return {55, true};
    case EV_GLASS:       return {70, true};
    default:             return {0, false};
  }
}

static uint8_t  g_mode        = MODE_DISARMED;
static bool     g_silent      = false;
static int      g_risk        = 0;
static int      g_loudRisk    = 0;
static uint32_t g_decayAt     = 0;
static uint32_t g_deadline    = 0;
static uint8_t  g_pendingMode = MODE_DISARMED;
static uint32_t g_lastTock    = 0;
static char     g_msg[48]     = "system ready";

static uint8_t     g_unknownHits  = 0;
static uint32_t    g_unknownFirst = 0;
static char        g_toast[28]    = "";
static uint8_t     g_toastKind    = 0;
static uint32_t    g_toastAt      = 0;

bool lockdown() { return Access::lockdown(); }

void showToast(const char *text, uint8_t kind) {
  strncpy(g_toast, text, sizeof(g_toast) - 1);
  g_toast[sizeof(g_toast) - 1] = 0;
  g_toastKind = kind;
  g_toastAt = millis();
}

const char *toastText() { return g_toast; }
uint8_t     toastKind() { return g_toastKind; }
uint32_t    toastAge()  { return g_toastAt ? millis() - g_toastAt : 0xFFFFFFFF; }

static void say(const char *m) { strncpy(g_msg, m, sizeof(g_msg) - 1); }

static void logEvent(uint8_t kind, uint8_t code, uint16_t value, bool silent) {
  Journal::append(kind, code, value, silent);
}

static void broadcastMode() {
  for (int i = 0; i < Nodes::capacity(); i++) {
    NodeRec *n = Nodes::at(i);
    if (n && n->used && !n->dead) SecMesh.sendTo(n->mac, CMD_STATE, g_mode);
  }
}

static void setMode(uint8_t m) {
  g_mode = m;
  g_deadline = 0;
  broadcastMode();
}

static void raise(bool loud, uint8_t kind, uint8_t code, uint16_t value) {
  if (loud) {
    g_mode = MODE_ALARM;
    g_deadline = millis() + ALARM_DURATION_MS;
    IO::sirenOn();
    say("ALARM");
    showToast("ALARM", 3);
    logEvent(kind, code, value, false);
    broadcastMode();
  } else {
    g_silent = true;
    logEvent(kind, code, value, true);
  }
  Net::alert(!loud, secKindName(kind), secEventName(code), value);
}

static void addRisk(uint8_t kind, uint8_t code, uint16_t value) {
  const Score s = scoreFor(code);
  if (s.weight == 0) { logEvent(kind, code, value, false); return; }

  g_risk += s.weight;
  if (s.loud) g_loudRisk += s.weight;
  if (g_risk > RISK_MAX) g_risk = RISK_MAX;
  if (g_loudRisk > RISK_MAX) g_loudRisk = RISK_MAX;

  if (g_risk < RISK_ALARM_THRESHOLD) {
    logEvent(kind, code, value, false);
    return;
  }
  raise(g_loudRisk >= RISK_ALARM_THRESHOLD, kind, code, value);
}

static bool armedNow() {
  return g_mode == MODE_HOME || g_mode == MODE_AWAY ||
         g_mode == MODE_ENTRY || g_mode == MODE_ALARM;
}

void onNodeEvent(NodeRec *n, const SecFrame &f) {
  const bool armed = armedNow();

  const bool alwaysArmed = (f.code == EV_RF_JAM || f.code == EV_RF_DEAUTH ||
                            f.code == EV_PANIC);

  if (f.code == EV_PANIC) { panic(); return; }
  if (f.code == EV_HEARTBEAT || f.code == EV_BOOT || f.code == EV_TAMPER_CLOSED) return;

  if (!armed && !alwaysArmed) {
    logEvent(f.kind, f.code, f.value, false);
    return;
  }

  if (g_mode == MODE_AWAY && f.code == EV_DOOR_OPEN) {
    g_mode = MODE_ENTRY;
    g_deadline = millis() + ENTRY_DELAY_MS;
    say("entry delay");
    IO::chime();
    logEvent(f.kind, f.code, f.value, false);
    return;
  }
  addRisk(f.kind, f.code, f.value);
}

void onHubTamper(bool open) {
  if (!open) {
    logEvent(KIND_HUB, EV_TAMPER_CLOSED, 0, false);
    return;
  }

  if (!armedNow()) {
    logEvent(KIND_HUB, EV_TAMPER_OPEN, 0, true);
    say("panel opened");
    showToast("PANEL OPENED", 2);
    Net::alert(true, "PANEL", "case opened while disarmed", 0);
    return;
  }

  g_risk = RISK_MAX;
  g_loudRisk = RISK_MAX;
  raise(true, KIND_HUB, EV_TAMPER_OPEN, 0);
  say("PANEL OPENED");
}

void panic() {
  g_risk = RISK_MAX;
  g_loudRisk = RISK_MAX;
  raise(true, KIND_HUB, EV_PANIC, 0);
}

void requestArm(uint8_t m) {
  if (m == MODE_DISARMED) { setMode(MODE_DISARMED); IO::ack(); showToast("DISARMED", 1); return; }
  g_pendingMode = m;
  g_mode = MODE_DISARMED;
  g_deadline = millis() + EXIT_DELAY_MS;
  g_risk = g_loudRisk = 0;
  say("arming, leave now");
  showToast("ARMING, LEAVE NOW", 2);
  IO::beep(1, 120, 0);
}

static void doDisarm() {
  IO::sirenOff();
  g_mode = MODE_DISARMED;
  g_pendingMode = MODE_DISARMED;
  g_deadline = 0;
  g_risk = g_loudRisk = 0;
  g_silent = false;
  say("disarmed");
  IO::ack();
  broadcastMode();
}

void enterLockdown() {
  say("LOCKED, unlock remotely");
  showToast("LOCKDOWN", 3);
  logEvent(KIND_HUB, EV_LOCKDOWN, 0, true);
  Net::alert(true, "PANEL", "lockdown after failed codes", 0);
}

void lockRemote() {
  if (Access::lockdown()) return;
  Access::latchRemote();
  say("LOCKED remotely");
  showToast("LOCKDOWN", 3);
  logEvent(KIND_HUB, EV_LOCKDOWN, 0, false);
  Net::alert(false, "PANEL", "locked from the bot", 0);
}

void clearLockdown(const char *how) {
  if (!Access::clearLockdown()) return;
  say("lockdown cleared");
  showToast("UNLOCKED", 1);
  logEvent(KIND_HUB, EV_UNLOCKED, 0, false);
  Net::alert(false, "PANEL", how, 0);
}

bool submitPin(const String &pin) {
  const Access::Verdict v = Access::verify(pin);

  if (v == Access::V_LOCKED) {
    IO::reject();
    showToast("LOCKED, USE FINGER", 3);
    return false;
  }

  if (v == Access::V_OK) {
    doDisarm();
    logEvent(KIND_HUB, EV_DISARMED, 0, false);
    showToast("DISARMED", 1);
    return true;
  }

  IO::reject();
  say("wrong code");
  showToast("WRONG CODE", 3);
  logEvent(KIND_HUB, EV_ACCESS_DENIED, Access::failures(), false);

  if (Access::takeLatched()) {
    raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
    enterLockdown();
  }
  return false;
}

bool openManage(const String &pin) {
  const Access::Verdict v = Access::verify(pin);

  if (v == Access::V_LOCKED) {
    IO::reject();
    showToast("LOCKED, USE FINGER", 3);
    return false;
  }

  if (v == Access::V_OK) {
    Access::grant();
    IO::ack();
    showToast("MANAGE UNLOCKED", 1);
    return true;
  }

  IO::reject();
  showToast("WRONG CODE", 3);
  logEvent(KIND_HUB, EV_ACCESS_DENIED, Access::failures(), false);

  if (Access::takeLatched()) {
    raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
    enterLockdown();
  }
  return false;
}

bool changePin(const String &current, const String &next) {
  if (!Access::changePin(current, next)) {
    IO::reject();
    showToast("CODE NOT CHANGED", 3);
    if (Access::takeLatched()) {
      raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
      enterLockdown();
    }
    return false;
  }
  IO::ack();
  say("code changed");
  showToast("CODE CHANGED", 1);
  logEvent(KIND_HUB, EV_PIN_CHANGED, 0, false);
  Net::alert(false, "PANEL", "keypad code changed", 0);
  return true;
}

void onFinger(uint16_t id, uint16_t score) {
  if (!Fingers::known(id)) {
    onUnknownFinger();
    return;
  }
  g_unknownHits = 0;
  Access::keepAlive();
  if (Access::lockdown()) clearLockdown("unlocked by fingerprint");
  doDisarm();
  logEvent(KIND_HUB, EV_ACCESS_OK, id, false);
  String who = Fingers::label(id);
  if (!who.length()) who = String("finger ") + String(id);
  showToast(("welcome " + who).c_str(), 1);
}

void onUnknownFinger() {
  const uint32_t now = millis();
  if (g_unknownHits == 0 || now - g_unknownFirst > UNKNOWN_FINGER_WINDOW_MS) {
    g_unknownFirst = now;
    g_unknownHits = 0;
  }
  g_unknownHits++;
  IO::reject();
  say("not recognised");
  showToast("FINGER NOT RECOGNISED", 3);
  logEvent(KIND_HUB, EV_ACCESS_DENIED, g_unknownHits, false);

  if (g_unknownHits >= UNKNOWN_FINGER_LIMIT || g_mode == MODE_AWAY) {
    g_unknownHits = 0;
    raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
  }
}

void begin() {
  Access::begin();
  Journal::begin();
  Fingers::begin();
  if (Access::lockdown()) say("LOCKED, unlock remotely");
  g_decayAt = millis();
}

void tick() {
  const uint32_t now = millis();

  Access::tick();
  Journal::tick();

  if (Access::pending() && Access::approvalMsLeft() == 0) {
    const Access::Approval a = Access::approval();
    Access::drop();
    logEvent(KIND_HUB, EV_DENIED, (uint16_t)a.id, false);
    Net::decide(a.id, false);
  }

  if (now - g_decayAt >= 1000) {
    g_decayAt = now;
    g_risk -= RISK_DECAY_PER_SEC;
    g_loudRisk -= RISK_DECAY_PER_SEC;
    if (g_risk < 0) g_risk = 0;
    if (g_loudRisk < 0) g_loudRisk = 0;
  }

  if (Nodes::justDied) {
    NodeRec *n = Nodes::justDied;
    Nodes::justDied = nullptr;
    raise(false, n->kind, EV_NODE_LOST, 0);
    say("module lost");
  }

  if (g_deadline == 0) return;

  if (g_mode == MODE_ENTRY) {
    if (now - g_lastTock > 1000) { g_lastTock = now; IO::tickTock(); }
    if (now >= g_deadline) raise(true, KIND_HUB, EV_DOOR_OPEN, 0);
  } else if (g_mode == MODE_ALARM) {
    if (now >= g_deadline) {
      IO::sirenOff();
      g_mode = MODE_AWAY;
      g_deadline = 0;
      say("alarm timed out, armed");
      broadcastMode();
    }
  } else if (g_pendingMode != MODE_DISARMED) {
    if (now - g_lastTock > 1000) { g_lastTock = now; IO::tickTock(); }
    if (now >= g_deadline) {
      const uint8_t m = g_pendingMode;
      g_pendingMode = MODE_DISARMED;
      setMode(m);
      say("armed");
      showToast("ARMED", 2);
      logEvent(KIND_HUB, EV_ARMED, 0, false);
      IO::beep(2, 100, 100);
    }
  }
}

bool enrolFinger() {
  if (!FP::present()) {
    showToast("SENSOR OFFLINE", 3);
    return false;
  }
  if (Fingers::count() >= FP_SLOTS) {
    showToast("NO FREE SLOTS", 3);
    return false;
  }
  Access::keepAlive();
  FP::startEnroll();
  say("place your finger");
  return true;
}

void onEnrolled(uint16_t id) {
  Fingers::add(id, String("finger ") + String(id));
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_ADDED, id, false);
  showToast("FINGER ADDED", 1);
  IO::beep(2, 80, 80);
  Net::publishRoster();
  Net::alert(false, "PANEL", "fingerprint added", id);
}

bool removeFinger(uint16_t id) {
  if (!Fingers::known(id)) return false;
  const String who = Fingers::label(id);
  FP::erase(id);
  Fingers::remove(id);
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_REMOVED, id, false);
  showToast(("removed " + who).c_str(), 2);
  Net::publishRoster();
  Net::alert(false, "PANEL", "fingerprint removed", id);
  return true;
}

bool renameFinger(uint16_t id, const String &label) {
  if (!Fingers::rename(id, label)) return false;
  logEvent(KIND_HUB, EV_FINGER_RENAMED, id, false);
  Net::publishRoster();
  return true;
}

void clearFingers() {
  FP::emptyAll();
  Fingers::clear();
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_REMOVED, 0, false);
  showToast("ALL FINGERS CLEARED", 2);
  Net::publishRoster();
  Net::alert(false, "PANEL", "all fingerprints cleared", 0);
}

void decideApproval(bool allow) {
  if (!Access::pending()) return;
  const Access::Approval a = Access::approval();
  Access::drop();
  Access::keepAlive();

  if (!allow) {
    logEvent(KIND_HUB, EV_DENIED, (uint16_t)a.id, false);
    showToast("REFUSED", 2);
    Net::decide(a.id, false);
    return;
  }

  const String action = String(a.action);
  if      (action == "finger_del")   removeFinger(a.arg);
  else if (action == "finger_clear") clearFingers();
  else if (action == "forget_nodes") { Nodes::forgetAll(); showToast("MODULES FORGOTTEN", 2); }

  logEvent(KIND_HUB, EV_APPROVED, (uint16_t)a.id, false);
  Net::decide(a.id, true);
}

const char *message() { return g_msg; }
uint8_t mode() { return g_mode; }
bool silentLatched() { return g_silent; }
int  risk() { return g_risk; }
uint8_t displayMode() { return Access::lockdown() ? MODE_LOCKDOWN : g_mode; }
bool arming() { return g_pendingMode != MODE_DISARMED; }

uint32_t countdownMs() {
  if (g_deadline == 0) return 0;
  const uint32_t now = millis();
  return (now >= g_deadline) ? 0 : (g_deadline - now);
}

}
