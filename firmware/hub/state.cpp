// =============================================================================
//  state.cpp — the brain: evidence fusion, modes, access outcomes
//
//  This module owns every decision the hub makes and every piece of mutable
//  security state: the current mode, the two risk accumulators, the countdown
//  deadlines, and the outcome of each access attempt. It owns no hardware and no
//  storage. Credentials are delegated to access.cpp, the fingerprint roster to
//  fingers.cpp, history to journal.cpp, the sensor to fp.cpp, sound to io.cpp and
//  notifications to net.cpp. Those modules answer questions and keep records; they
//  never decide anything and never call back into here. The result is that every
//  side effect of a security event — siren, beep, log line, mesh broadcast, push
//  notification — is triggered from this file, so the whole behaviour of the system
//  can be read in one place.
// =============================================================================

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

// ========== [Scoring table] ==========
//
// Every event the hub can see is worth some number of points and belongs to one of
// two classes. `weight` is how much evidence the event represents; `loud` is
// whether that evidence is permitted to make a noise.
//
//   loud == true   The event has already announced itself. Glass breaking, a door
//                  standing open, a beam cut: whoever caused it knows they caused
//                  it, so a siren tells them nothing they did not already know.
//
//   loud == false  The event is reconnaissance, and the person causing it does not
//                  yet know they have been seen — a hand approaching the handle,
//                  the handle turned, the door worked, a case opened, the radio
//                  jammed. A siren here would hand the intruder the single most
//                  useful fact available: which sensor fired, and therefore which
//                  sensor to defeat on the next attempt. A door-handle touch must
//                  never tell the intruder they were detected.
//
// That asymmetry is the core idea of the product, which is why the class is carried
// with the weight in the table rather than derived somewhere downstream. Silent
// evidence is not weak evidence: it still accumulates, still logs, still latches
// and still sends the notification. It simply does all of that where only the owner
// can see it. Full rationale and the tuning history are in ADR-007.
struct Score { uint8_t weight; bool loud; };

static Score scoreFor(uint8_t code) {
  switch (code) {
    // Silent class. Approach, contact, interference and anything that only makes
    // sense to the attacker. These never sound the siren, alone or in combination.
    // EV_TAMPER_OPEN here is a *node* case being opened; the hub's own case is
    // handled by onHubTamper() below, which does not use this table.
    case EV_PRETOUCH:    return {15, false};
    case EV_TOUCH:       return {25, false};
    case EV_TILT:        return {35, false};
    case EV_AGITATION:   return {55, false};
    case EV_TAMPER_OPEN: return {80, false};
    case EV_RF_PROBE:    return {25, false};
    case EV_RF_DEAUTH:   return {80, false};
    case EV_RF_JAM:      return {80, false};
    // A lost node is a killed node, so it scores just under the threshold on its
    // own: silent, and enough that anything else at all tips it over. See ADR-006.
    case EV_NODE_LOST:   return {75, false};
    case EV_CODE_ATTACK: return {70, false};
    // Loud class. An intrusion that is already obvious to the person committing it.
    case EV_MOTION:      return {30, true};
    case EV_DOOR_OPEN:   return {40, true};
    case EV_LASER_CUT:   return {45, true};
    case EV_IMPACT:      return {70, true};
    // At exactly the threshold, so a glass break sounds the siren by itself with no
    // delay — there is no ambiguity in it to grade.
    case EV_GLASS:       return {70, true};
    // Weight 0 means "not evidence": heartbeats, pairing, low supply, and every
    // administrative event this hub generates about itself. addRisk() logs those and
    // leaves both accumulators alone.
    default:             return {0, false};
  }
}

// ========== [Module state] ==========
// The whole mutable state of the system, file-static so nothing outside this
// translation unit can write it. Everything the UI and the network layer need is
// exposed read-only through the accessors at the bottom of the file.

// Never holds MODE_LOCKDOWN. Lockdown is a latch owned by access.cpp and is only
// merged in for display, by displayMode() — see ADR-008 on modes the UI has to lie
// about.
static uint8_t  g_mode        = MODE_DISARMED;
// Latched when a silent alarm has fired. Cleared only by a real disarm, so it
// survives the evidence decaying back to zero.
static bool     g_silent      = false;
// The two accumulators: all evidence, and loud evidence only.
static int      g_risk        = 0;
static int      g_loudRisk    = 0;
static uint32_t g_decayAt     = 0;
// One deadline shared by all three countdowns — entry delay, exit delay and siren
// runtime — distinguished by g_mode and g_pendingMode. 0 means nothing is counting,
// and tick() returns early on it.
static uint32_t g_deadline    = 0;
// The mode to enter when the exit delay expires. Anything other than MODE_DISARMED
// here is what arming() reports to the UI.
static uint8_t  g_pendingMode = MODE_DISARMED;
static uint32_t g_lastTock    = 0;
// The persistent status line under the banner. 48 bytes, written by say().
static char     g_msg[48]     = "system ready";

// Unknown-finger window: hits so far, and when the current window opened. A wallet
// full of other people's fingers is not a thing, but a stranger trying repeatedly is.
static uint8_t     g_unknownHits  = 0;
static uint32_t    g_unknownFirst = 0;
static char        g_toast[28]    = "";
static uint8_t     g_toastKind    = 0;
static uint32_t    g_toastAt      = 0;

// Re-exported so the UI has exactly one place to ask about lockdown, even though the
// latch itself lives in access.cpp with the credential it protects.
bool lockdown() { return Access::lockdown(); }

// ========== [Toasts] ==========
// A toast is the transient banner: what just happened. g_msg (via say(), below) is
// the persistent status line: what is happening now. Both are strings the UI polls;
// neither draws anything itself, which is why this module never includes ui.h.
// kind: 1 = good, 2 = notice, 3 = bad.
void showToast(const char *text, uint8_t kind) {
  strncpy(g_toast, text, sizeof(g_toast) - 1);
  g_toast[sizeof(g_toast) - 1] = 0;
  g_toastKind = kind;
  g_toastAt = millis();
}

// 0xFFFFFFFF when no toast has ever been shown, so the UI's `toastAge() < TOAST_MS`
// test is false at boot without needing a separate "have I got one" flag.
const char *toastText() { return g_toast; }
uint8_t     toastKind() { return g_toastKind; }
uint32_t    toastAge()  { return g_toastAt ? millis() - g_toastAt : 0xFFFFFFFF; }

static void say(const char *m) { strncpy(g_msg, m, sizeof(g_msg) - 1); }

// ========== [Event log bridge] ==========
// Every log write in this file goes through here, so the journal is called from one
// place and the `silent` flag is never forgotten. `silent` marks an event that the
// house was not told about audibly — it is recorded so the LOG page and the CSV can
// show that the system saw something and deliberately said nothing.
static void logEvent(uint8_t kind, uint8_t code, uint16_t value, bool silent) {
  Journal::append(kind, code, value, silent);
}

// ========== [Mode transitions] ==========
// Nodes need the mode because it changes their own behaviour: a door node reports a
// handle touch as reconnaissance while armed and as nothing much while disarmed.
// Dead nodes are skipped — they get the mode from the pairing accept when they come
// back rather than from a unicast into the void.
static void broadcastMode() {
  for (int i = 0; i < Nodes::capacity(); i++) {
    NodeRec *n = Nodes::at(i);
    if (n && n->used && !n->dead) SecMesh.sendTo(n->mac, CMD_STATE, g_mode);
  }
}

// An immediate, deliberate mode change with no countdown attached: set it, cancel any
// deadline and any pending arm, tell the nodes. It intentionally does not touch the
// risk accumulators — the callers that need those cleared do it themselves.
static void setMode(uint8_t m) {
  g_mode = m;
  // Clearing the pending mode here is what makes cancel-an-arm and disarm the same
  // operation. Leaving it set kept arming() true forever: the action button stuck on
  // CANCEL, the exit delay never completed, standby never engaged, and — because the
  // CANCEL branch needs no credential — one tap on an armed panel disarmed it.
  g_pendingMode = MODE_DISARMED;
  g_deadline = 0;
  broadcastMode();
}

// ========== [Alarm raising] ==========
// The single point where an alarm becomes real, and the only place that starts the
// siren. `loud` has already been decided by the caller — either by the fusion layer
// comparing g_loudRisk against the threshold, or by an event that is loud by
// definition (panic, hub tamper while armed, an expired entry delay).
//
// Loud:   MODE_ALARM, siren on for ALARM_DURATION_MS, banner, toast, log, and the
//         new mode pushed to every live node. The event is logged with silent=false
//         because the house was told.
// Silent: nothing audible, nothing on the mode line. The g_silent latch is set and
//         the event is logged with silent=true. The mode does not change: a silent
//         alarm while disarmed leaves the system disarmed, and while armed leaves it
//         armed, because entering MODE_ALARM is itself a visible signal.
//
// Either way Net::alert() runs, with its silent flag inverted from `loud`, so the
// owner's phone is the one channel that always fires. A silent alarm is therefore
// only as strong as that path: with the network down it is a log line and a latched
// flag, which is the cost accepted in ADR-007.
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

// ========== [Risk accumulation] ==========
// Fusion, in fifteen lines. Evidence is additive rather than any-trigger-alarms,
// because a single sensor firing is nearly always ordinary life and three sensors
// agreeing inside a few seconds almost never is.
//
// Two totals are kept deliberately:
//   g_risk      every point of evidence, silent and loud together. This is what
//               decides *whether* there is an intrusion.
//   g_loudRisk  the loud points only. This decides whether the response is allowed
//               to be heard.
//
// So the alarm fires when g_risk crosses the threshold, and the siren sounds only
// when g_loudRisk has crossed it as well. Touch, then tilt, then agitation adds up
// to 115 points and raises a full alarm — logged, latched, owner notified — while
// g_loudRisk is still 0, so the door looks unwatched from the outside and the
// intruder is given no reason to change approach. One glass break is 70 loud points
// and sounds the siren on its own. A single total could not express that difference;
// deriving the class from the last event could not either, because it is the whole
// accumulated set that has to be classified, not the straw that broke it.
static void addRisk(uint8_t kind, uint8_t code, uint16_t value) {
  const Score s = scoreFor(code);
  // Not evidence: record it and leave the accumulators untouched.
  if (s.weight == 0) { logEvent(kind, code, value, false); return; }

  g_risk += s.weight;
  if (s.loud) g_loudRisk += s.weight;
  // Clamp both. Without a ceiling, a long burst banks risk that keeps re-alarming
  // for minutes after it should have expired.
  if (g_risk > RISK_MAX) g_risk = RISK_MAX;
  if (g_loudRisk > RISK_MAX) g_loudRisk = RISK_MAX;

  // Below the threshold there is no verdict yet, only a record. This is the normal
  // path for almost every event the system ever sees.
  if (g_risk < RISK_ALARM_THRESHOLD) {
    logEvent(kind, code, value, false);
    return;
  }
  raise(g_loudRisk >= RISK_ALARM_THRESHOLD, kind, code, value);
}

// Armed for the purposes of gating evidence. ENTRY counts because the entry delay is
// a grace period inside an armed system, not a disarmed one, and ALARM counts because
// more evidence during an alarm should still extend it. MODE_DISARMED is absent, and
// so is the exit delay — during EXIT_DELAY_MS g_mode is still MODE_DISARMED, which is
// what makes walking out of your own door harmless.
static bool armedNow() {
  return g_mode == MODE_HOME || g_mode == MODE_AWAY ||
         g_mode == MODE_ENTRY || g_mode == MODE_ALARM;
}

// ========== [Node events] ==========
// Every frame from every node lands here, already validated by SecMesh (MAC known,
// replay counter accepted) and already recorded as liveness by Nodes::touchSeen.
// This function decides only one thing: does this event count as evidence right now.
void onNodeEvent(NodeRec *n, const SecFrame &f) {
  const bool armed = armedNow();

  // Three events ignore the armed state entirely.
  //
  // Jamming and a deauth flood are attacks on the alarm system itself, not on the
  // house. Nobody jams a radio by accident, and the reason to jam one is to blind the
  // hub before entering — so the evidence has to be scored whether or not anyone
  // remembered to arm the panel. Gating them on armed state would mean the one attack
  // aimed at the disarmed system is the one attack the disarmed system ignores.
  //
  // Panic is a person deliberately asking for the siren. A request from a human can
  // never depend on what mode a machine happens to be in.
  const bool alwaysArmed = (f.code == EV_RF_JAM || f.code == EV_RF_DEAUTH ||
                            f.code == EV_PANIC);

  // Panic short-circuits everything, including the scoring table (see panic() below).
  // Its presence in alwaysArmed above is therefore belt and braces, kept so the list
  // reads as the complete set of mode-independent events.
  if (f.code == EV_PANIC) { panic(); return; }
  // Dropped without a log line. These are routine chatter, not history: heartbeats
  // arrive from every node continuously and would flush the 40-entry ring in under a
  // minute, taking the events worth reading with them.
  if (f.code == EV_HEARTBEAT || f.code == EV_BOOT || f.code == EV_TAMPER_CLOSED) return;

  // Disarmed: recorded, never scored. A disarmed system watching someone open their
  // own front door must stay quiet, and the log is what makes the event reviewable
  // afterwards anyway.
  if (!armed && !alwaysArmed) {
    logEvent(f.kind, f.code, f.value, false);
    return;
  }

  // The entry delay. A door opening while AWAY is the single ambiguous event in the
  // system — it is either the owner coming home or an intruder — so instead of
  // scoring it, the hub starts a countdown and chimes to say so. Note the early
  // return: those 40 loud points are deliberately never added, because the owner is
  // about to authenticate. If nobody does, tick() raises a loud alarm on the same
  // event code when the deadline passes.
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

// ========== [Tamper] ==========
// The hub's own case switch, debounced in hub.ino. Deliberately not routed through
// the scoring table: the panel is the thing that decides, so opening it is either
// maintenance or the beginning of an attack, with nothing in between to grade.
void onHubTamper(bool open) {
  // Closing the case is recorded so the log shows a maintenance window with a start
  // and an end, rather than an open case that was apparently never shut.
  if (!open) {
    logEvent(KIND_HUB, EV_TAMPER_CLOSED, 0, false);
    return;
  }

  // Disarmed: log it, show it, notify the owner — but no siren.
  //
  // The reason is that every legitimate reason to open this case happens while the
  // system is disarmed: a battery change, an enrolment session behind the panel, a
  // firmware update. A siren on each of those trains the owner to tape the switch
  // shut, and a taped switch protects nothing at all.
  //
  // The accepted downside, stated plainly: an attacker who reaches the panel while
  // the system is disarmed opens the case with no audible response. What they get is
  // one silent notification's worth of warning to the owner and a log line, and
  // nothing more. That was a product decision, not an oversight — while disarmed there
  // is almost always someone in the house, and that someone is who opens the case
  // nearly every time. What bounds the damage is that opening the case exposes no
  // secret: the PIN digest and the roster live in NVS behind the firmware, and an
  // attacker already holding the board is outside the threat model. See ADR-009.
  if (!armedNow()) {
    logEvent(KIND_HUB, EV_TAMPER_OPEN, 0, true);
    say("panel opened");
    showToast("PANEL OPENED", 2);
    Net::alert(true, "PANEL", "case opened while disarmed", 0);
    return;
  }

  // Armed: no grading, no delay. Both accumulators are pinned to RISK_MAX so that
  // anything else arriving in the next few seconds is already over the threshold, and
  // the alarm is raised loud immediately.
  g_risk = RISK_MAX;
  g_loudRisk = RISK_MAX;
  raise(true, KIND_HUB, EV_TAMPER_OPEN, 0);
  say("PANEL OPENED");
}

// A human asking for the siren, from the panic node or from the bot. Same mechanism
// as armed tamper: pin both accumulators, raise loud, no conditions attached.
void panic() {
  g_risk = RISK_MAX;
  g_loudRisk = RISK_MAX;
  raise(true, KIND_HUB, EV_PANIC, 0);
}

// ========== [Arming] ==========
// Called by the terminal's single action button and by the remote command poll.
void requestArm(uint8_t m) {
  // MODE_DISARMED here is "cancel the exit delay", and the same entry point serves the
  // authenticated remote /disarm. It is deliberately not the route a person at the
  // panel takes to disarm an armed system — that goes through the keypad or a finger,
  // so disarming always costs a credential.
  if (m == MODE_DISARMED) { setMode(MODE_DISARMED); IO::ack(); showToast("DISARMED", 1); return; }
  g_pendingMode = m;
  // The exit delay runs with the mode still DISARMED, not in the target mode. That is
  // the whole point: leaving your own house produces exactly the evidence an intruder
  // entering it would, so during EXIT_DELAY_MS armedNow() is false and node events are
  // logged without being scored.
  g_mode = MODE_DISARMED;
  g_deadline = millis() + EXIT_DELAY_MS;
  // Start from zero, so evidence collected before arming — someone walking around the
  // house all afternoon — cannot be inherited by the armed system.
  g_risk = g_loudRisk = 0;
  say("arming, leave now");
  showToast("ARMING, LEAVE NOW", 2);
  IO::beep(1, 120, 0);
}

// The one and only disarm. Every route in — correct PIN, recognised fingerprint, remote
// command — funnels through here, because disarming is six separate pieces of state
// that must all move together: siren off, mode, pending mode, deadline, both
// accumulators, and the silent latch. Duplicating that in three call sites guarantees
// that one of them eventually forgets one piece, and the piece most easily forgotten is
// the silent latch — which would leave a silent alarm still latched after a legitimate
// entry, or worse, the siren running after a valid code. Callers add their own log line
// (EV_DISARMED for a code, EV_ACCESS_OK for a finger) because the reason differs; the
// state change cannot.
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

// ========== [Lockdown] ==========
// The latch itself is owned by access.cpp and persisted to NVS; these three functions
// are only the announcement side of it. Lockdown blocks the keypad and nothing else —
// a fingerprint still works, which is what keeps five typos from being a real lockout.

// Called after access.cpp has already latched, on the way out of a failed-code burst.
// It reports; it does not decide.
void enterLockdown() {
  say("LOCKED, unlock remotely");
  showToast("LOCKDOWN", 3);
  logEvent(KIND_HUB, EV_LOCKDOWN, 0, true);
  Net::alert(true, "PANEL", "lockdown after failed codes", 0);
}

// Lock from the bot or dashboard. Silent flag is false here, unlike the line above: an
// owner who pressed lock knows the panel is locked, whereas a lockdown triggered by an
// attacker guessing codes is information the attacker should not be handed.
void lockRemote() {
  if (Access::lockdown()) return;
  Access::latchRemote();
  say("LOCKED remotely");
  showToast("LOCKDOWN", 3);
  logEvent(KIND_HUB, EV_LOCKDOWN, 0, false);
  Net::alert(false, "PANEL", "locked from the bot", 0);
}

// `how` is carried into the notification so the log distinguishes the two recovery
// paths: a fingerprint at the panel, or /unlock from the bot. The early return means a
// clear that was not needed is silent rather than a second confusing notification.
void clearLockdown(const char *how) {
  if (!Access::clearLockdown()) return;
  say("lockdown cleared");
  showToast("UNLOCKED", 1);
  logEvent(KIND_HUB, EV_UNLOCKED, 0, false);
  Net::alert(false, "PANEL", how, 0);
}

// ========== [Keypad] ==========
// Three entry points, one shape: ask access.cpp for a verdict, then decide what the
// system does about it. No hashing, no counting and no latching happens here.

// The disarm path.
bool submitPin(const String &pin) {
  const Access::Verdict v = Access::verify(pin);

  // Locked out. The message names the recovery path that actually works, because a
  // panel that only says "locked" to its owner is a panel they will take apart.
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

  // Wrong. The failure count goes into the log entry's value field, so the LOG page
  // shows a run of attempts as 1, 2, 3 rather than three identical lines.
  IO::reject();
  say("wrong code");
  showToast("WRONG CODE", 3);
  logEvent(KIND_HUB, EV_ACCESS_DENIED, Access::failures(), false);

  // takeLatched() is a one-shot edge: true exactly once, on the attempt that latched.
  // That keeps the scoring and the announcement to a single occurrence instead of
  // repeating on every further wrong code. EV_CODE_ATTACK is raised silent — 70 points
  // is the threshold, so guessing codes produces a full alarm with nothing audible.
  // Someone standing at the keypad working through combinations must not be told that
  // the system has started counting.
  if (Access::takeLatched()) {
    raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
    enterLockdown();
  }
  return false;
}

// The manage-tab gate. Same verdict handling as submitPin, but a correct code opens a
// 60 s session instead of disarming — managing credentials while the system is armed is
// legitimate, and conflating the two would mean every enrolment disarmed the house.
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

// A wrong current code counts toward the same five-attempt budget as a wrong disarm
// code and can latch lockdown, which is why this path honours takeLatched() too. The
// length rule and the digest write live in access.cpp; only the reaction is here.
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

// ========== [Fingerprint] ==========
// fp.cpp has already matched the finger against the sensor's own database and checked
// the confidence score; hub.ino passes the id here. The roster is consulted second,
// because a template in the sensor with no roster entry is not a credential.
void onFinger(uint16_t id, uint16_t score) {
  if (!Fingers::known(id)) {
    onUnknownFinger();
    return;
  }
  g_unknownHits = 0;
  Access::keepAlive();
  // A finger is the local way out of lockdown, and it clears it before disarming
  // rather than instead of disarming — the person is standing there and has proved who
  // they are, so both should happen on one press.
  if (Access::lockdown()) clearLockdown("unlocked by fingerprint");
  doDisarm();
  logEvent(KIND_HUB, EV_ACCESS_OK, id, false);
  String who = Fingers::label(id);
  if (!who.length()) who = String("finger ") + String(id);
  showToast(("welcome " + who).c_str(), 1);
}

// An unrecognised finger is not by itself an attack — a wrong finger, a wet finger, a
// child playing. It becomes one on repetition inside UNKNOWN_FINGER_WINDOW_MS, or
// immediately if the house is supposed to be empty.
void onUnknownFinger() {
  const uint32_t now = millis();
  // The window restarts rather than slides: the first hit after an expiry opens a fresh
  // count, so three attempts have to fall inside one minute of each other to escalate.
  if (g_unknownHits == 0 || now - g_unknownFirst > UNKNOWN_FINGER_WINDOW_MS) {
    g_unknownFirst = now;
    g_unknownHits = 0;
  }
  g_unknownHits++;
  IO::reject();
  say("not recognised");
  showToast("FINGER NOT RECOGNISED", 3);
  logEvent(KIND_HUB, EV_ACCESS_DENIED, g_unknownHits, false);

  // Escalation is silent for the same reason the keypad's is: the person at the sensor
  // learns nothing beyond the rejection they already got. The counter is reset so the
  // next burst has to earn its own escalation.
  if (g_unknownHits >= UNKNOWN_FINGER_LIMIT || g_mode == MODE_AWAY) {
    g_unknownHits = 0;
    raise(false, KIND_HUB, EV_CODE_ATTACK, 0);
  }
}

// ========== [Lifecycle] ==========
// Order matters: access.cpp first because a hub that boots into lockdown must know it
// before anything can ask, then the journal, then the roster. g_decayAt is seeded from
// millis() so the first decay tick is a full second away rather than immediate.
void begin() {
  Access::begin();
  Journal::begin();
  Fingers::begin();
  if (Access::lockdown()) say("LOCKED, unlock remotely");
  g_decayAt = millis();
}

// Everything time-driven in the security layer happens here, called once per pass of
// loop(). Nothing in this file uses delay(), so a countdown, a decay step and a siren
// timeout all have to be expressed as "has the deadline passed yet".
void tick() {
  const uint32_t now = millis();

  // Pumped from here rather than from hub.ino, so the main loop has one call per
  // subsystem and the ordering of the security modules stays this module's business.
  Access::tick();
  Journal::tick();

  // An approval nobody answered is a refusal. The hub decides this, not the server: the
  // server's TTL is deliberately longer, so it only ever acts as a backstop for a hub
  // that never replied. Net::decide() closes the request out so the dashboard stops
  // showing it as pending.
  if (Access::pending() && Access::approvalMsLeft() == 0) {
    const Access::Approval a = Access::approval();
    Access::drop();
    logEvent(KIND_HUB, EV_DENIED, (uint16_t)a.id, false);
    Net::decide(a.id, false);
  }

  // Evidence expiry, once per second. This is what stops the accumulators being
  // lifetime totals: without it, a touch this morning and motion this afternoon
  // eventually sum past the threshold and the system alarms on ordinary life. Both
  // totals decay at the same rate so their relationship — and therefore the silent /
  // loud decision — is preserved as they fall. The subtraction is written
  // `now - g_decayAt >= 1000` rather than `now >= g_decayAt + 1000` because unsigned
  // difference is correct across the millis() rollover and the sum is not.
  if (now - g_decayAt >= 1000) {
    g_decayAt = now;
    g_risk -= RISK_DECAY_PER_SEC;
    g_loudRisk -= RISK_DECAY_PER_SEC;
    if (g_risk < 0) g_risk = 0;
    if (g_loudRisk < 0) g_loudRisk = 0;
  }

  // A node that stopped answering heartbeats is treated as killed, not as flaky — that
  // is an alarm condition, not a warning (ADR-006). Silent, at 75 points: whoever cut
  // the power or jammed the link learns nothing, and the next event of any kind tips it
  // over the threshold. The flag is consumed before raising so one death raises once.
  if (Nodes::justDied) {
    NodeRec *n = Nodes::justDied;
    Nodes::justDied = nullptr;
    raise(false, n->kind, EV_NODE_LOST, 0);
    say("module lost");
  }

  // No deadline means no countdown of any kind is running, which is the common case.
  if (g_deadline == 0) return;

  // One deadline, three meanings, disambiguated in this order.
  if (g_mode == MODE_ENTRY) {
    // Audible countdown, then a loud alarm on the same event that started it. The door
    // opening was never scored (see onNodeEvent), so this is where those points would
    // have gone.
    if (now - g_lastTock > 1000) { g_lastTock = now; IO::tickTock(); }
    if (now >= g_deadline) raise(true, KIND_HUB, EV_DOOR_OPEN, 0);
  } else if (g_mode == MODE_ALARM) {
    // The siren has a runtime, not an off switch that expires into safety. When it ends
    // the hub returns to AWAY and stays armed, because an alarm timing out is not an
    // authenticated event and the intruder may still be inside.
    if (now >= g_deadline) {
      IO::sirenOff();
      g_mode = MODE_AWAY;
      g_deadline = 0;
      say("alarm timed out, armed");
      broadcastMode();
    }
  } else if (g_pendingMode != MODE_DISARMED) {
    // Exit delay completing. Clearing g_pendingMode first is what makes arming() stop
    // reporting true, and setMode() pushes the new mode to the nodes.
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

// ========== [Fingerprint management] ==========
// Enrolment and removal. Each one touches three things that have to stay in step: the
// sensor's template database (fp.cpp), the labelled roster in NVS (fingers.cpp) and the
// dashboard's copy of it (Net::publishRoster). Every one of them also refreshes the
// manage session, because a management action is proof the operator is still present.

// Refuses early and says why, rather than starting an enrolment that cannot finish. The
// slot limit is FP_SLOTS — the hub's roster — not the sensor's much larger capacity.
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

// The sensor stored the template and reported the id it used; the roster follows with a
// placeholder label that the dashboard can rename later.
void onEnrolled(uint16_t id) {
  Fingers::add(id, String("finger ") + String(id));
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_ADDED, id, false);
  showToast("FINGER ADDED", 1);
  IO::beep(2, 80, 80);
  Net::publishRoster();
  Net::alert(false, "PANEL", "fingerprint added", id);
}

// Reached only through decideApproval() when the request came from the web, or directly
// from the manage tab where the operator is already physically present. The label is read
// before the removal so the toast can name who was removed.
bool removeFinger(uint16_t id) {
  if (!Fingers::known(id)) return false;
  const String who = Fingers::label(id);

  // The template in the sensor is the thing that actually grants entry; the roster only
  // names it. Dropping the roster entry while the template survives would leave a finger
  // that still matches, reads as unknown, and feeds the intrusion escalation instead of
  // opening the door. So the sensor has to confirm the erase first.
  if (!FP::erase(id)) {
    showToast("SENSOR REFUSED", 3);
    return false;
  }

  Fingers::remove(id);
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_REMOVED, id, false);
  showToast(("removed " + who).c_str(), 2);
  Net::publishRoster();
  Net::alert(false, "PANEL", "fingerprint removed", id);
  return true;
}

// A rename changes no authority, so unlike removal it is not gated behind an approval —
// it arrives straight from the dashboard on the command poll. No session is refreshed
// either, because nobody had to be at the panel for it.
bool renameFinger(uint16_t id, const String &label) {
  if (!Fingers::rename(id, label)) return false;
  logEvent(KIND_HUB, EV_FINGER_RENAMED, id, false);
  Net::publishRoster();
  return true;
}

// Wipes the sensor and the roster together. Logged as EV_FINGER_REMOVED with value 0,
// where 0 means "all of them" rather than an id.
void clearFingers() {
  FP::emptyAll();
  Fingers::clear();
  Access::keepAlive();
  logEvent(KIND_HUB, EV_FINGER_REMOVED, 0, false);
  showToast("ALL FINGERS CLEARED", 2);
  Net::publishRoster();
  Net::alert(false, "PANEL", "all fingerprints cleared", 0);
}

// ========== [Web approvals] ==========
// Actions that change who can get in later are never executed by the network layer.
// net.cpp records them as a pending request; the UI draws a popup with the request text
// and a countdown; this function is what an Allow or a Deny on the glass runs. The point
// is that a stolen dashboard session cannot quietly delete a credential — it needs a
// person standing at the panel. Actions that only change the current state (arm, disarm,
// panic, lock, unlock) stay direct, because losing an evening is not losing access.
// See ADR-013.
void decideApproval(bool allow) {
  if (!Access::pending()) return;
  // Copied by value and dropped immediately: the decision is consumed once, so a double
  // tap or a second poll cannot execute the same action twice.
  const Access::Approval a = Access::approval();
  Access::drop();
  Access::keepAlive();

  if (!allow) {
    logEvent(KIND_HUB, EV_DENIED, (uint16_t)a.id, false);
    showToast("REFUSED", 2);
    Net::decide(a.id, false);
    return;
  }

  // The action vocabulary. It is matched on strings because the same names travel over
  // HTTP, and it is deliberately short — this list is the complete set of things a
  // remote request is allowed to change about who can get in.
  //
  // An unrecognised action is reported back as a refusal rather than an approval.
  // Answering "approved" for work that was never carried out would make the dashboard
  // show a fingerprint as deleted while it still opens the door.
  const String action = String(a.action);
  bool done = false;
  if      (action == "finger_del")   done = removeFinger(a.arg);
  else if (action == "finger_clear") { clearFingers(); done = true; }
  else if (action == "forget_nodes") { Nodes::forgetAll(); showToast("MODULES FORGOTTEN", 2); done = true; }

  if (!done) {
    logEvent(KIND_HUB, EV_DENIED, (uint16_t)a.id, false);
    Net::decide(a.id, false);
    return;
  }

  logEvent(KIND_HUB, EV_APPROVED, (uint16_t)a.id, false);
  Net::decide(a.id, true);
}

// ========== [Accessors] ==========
// Read-only windows for the UI and the network layer. Nothing outside this file can
// write the state, which is why these are functions and not extern variables.
const char *message() { return g_msg; }
uint8_t mode() { return g_mode; }
// Set by any silent alarm, cleared only by doDisarm(). Deliberately not drawn anywhere:
// a silent alarm that shows a badge on the panel is not silent to whoever is standing in
// front of the panel.
bool silentLatched() { return g_silent; }
int  risk() { return g_risk; }
// The real mode with lockdown merged in for display only. g_mode never holds
// MODE_LOCKDOWN, so an unlock does not have to reconstruct what the mode used to be.
uint8_t displayMode() { return Access::lockdown() ? MODE_LOCKDOWN : g_mode; }
bool arming() { return g_pendingMode != MODE_DISARMED; }

// Milliseconds left on whichever countdown is running, clamped at 0 so the UI never has
// to reason about an expired deadline it has not been told about yet.
uint32_t countdownMs() {
  if (g_deadline == 0) return 0;
  const uint32_t now = millis();
  return (now >= g_deadline) ? 0 : (g_deadline - now);
}

}
