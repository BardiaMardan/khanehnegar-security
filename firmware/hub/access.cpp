// =============================================================================
//  access.cpp — the keypad credential, the lockdown latch and the manage session
//
//  This module owns the one secret the user types in the open: it hashes the code,
//  stores the digest, answers yes/no, counts failures, latches lockdown and keeps
//  it across reboots. It also owns the two short-lived permissions that hang off a
//  correct code — the manage session and the pending web approval. What it
//  deliberately does not own is consequences. It never beeps, never logs, never
//  notifies and never changes the alarm mode; it returns a verdict and lets
//  state.cpp decide what the system does about it. That separation is why the
//  security-critical part of the firmware is a file with no I/O in it.
// =============================================================================

#include "access.h"
#include "config.h"
#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

namespace Access {

// ========== [Storage keys] ==========
// Keys inside the shared NVS namespace. Names are short because NVS keys are capped
// at 15 characters, and these are matched by exact string.
#define SALT_KEY  "psalt"
#define HASH_KEY  "phash"
#define LOCK_KEY  "lockdown"
// 8 random bytes per device. Not a secret in itself — it is stored in the clear right
// next to the digest — its whole job is to make the digest device-specific.
#define SALT_LEN  8
#define HASH_LEN  32

// ========== [Module state] ==========
// Salt and digest are held in RAM after begin() so a keypad entry costs one hash and
// no flash read.
static uint8_t  g_salt[SALT_LEN];
static uint8_t  g_hash[HASH_LEN];
// Consecutive wrong codes. RAM only, and deliberately so: persisting a counter on
// every wrong attempt would put a flash write on a path an attacker controls. What
// gets persisted is the latch it produces, once.
static uint8_t  g_fails    = 0;
static bool     g_lockdown = false;
// A one-shot edge, not a state: true only between the attempt that latched lockdown
// and the first read by takeLatched(). It exists so the caller can score and announce
// the event exactly once instead of on every further wrong code.
static bool     g_latched  = false;

// Absolute expiry of the manage session, in millis(). 0 means no session at all,
// which is a distinct case from an expired one — see granted().
static uint32_t g_sessionUntil = 0;

// At most one web action may wait for a physical Allow at a time. A second request
// arriving while this one is pending is refused as busy rather than queued: a queue of
// pending permission changes is a queue nobody reads carefully.
static Approval g_approval  = {0, 0, "", ""};
static bool     g_pending   = false;
static uint32_t g_pendingAt = 0;

// ========== [Digest] ==========
// The code is never stored, logged or compared in the clear. What NVS holds is
// SHA-256(salt || code), so a dumped flash partition, a card read in a laptop or a
// serial log yields a digest and not a code. The salt goes in first and is per-device,
// which is what stops one precomputed table from opening every panel ever built from
// this design — identical hardware with an identical default code would otherwise all
// share one digest.
//
// mbedtls ships with the ESP-IDF, so this costs no dependency. The second argument to
// _starts() selects SHA-256 rather than SHA-224.
//
// Worth being honest about what this does not buy: four digits is 10,000
// possibilities and a single SHA-256 pass is fast, so the digest is not what protects
// the code from someone holding the flash. The five-attempt limit and the persistent
// latch below are. Hashing defends against casual disclosure, which is the realistic
// exposure. See ADR-011.
static void digest(const String &pin, uint8_t out[HASH_LEN]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, g_salt, SALT_LEN);
  mbedtls_sha256_update(&ctx, (const uint8_t *)pin.c_str(), pin.length());
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

// ========== [Constant-time compare] ==========
// The loop ORs the differences and tests once at the end instead of returning on the
// first mismatching byte, so it takes the same time for a code that is wrong in the
// first digit as for one that is wrong in the last. `memcmp` and an early-return loop
// both leak that difference as timing, and a leak of *where* the mismatch is turns
// guessing a 4-digit code from 10,000 attempts into roughly 40 measurements per digit.
// The keypad's own five-attempt limit already makes that attack impractical here, but
// this is a two-line habit and the alternative is a subtle bug waiting for the day the
// code becomes longer or a network path starts calling verify().
//
// `diff` accumulates: any byte that differs sets bits that no later byte can clear.
static bool sameHash(const uint8_t *a, const uint8_t *b) {
  uint8_t diff = 0;
  for (uint8_t i = 0; i < HASH_LEN; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

// ========== [Persistence] ==========
// Salt and digest are written together, because a digest saved without its salt is
// unverifiable and a salt saved without its digest locks the panel out of its own code.
static void persistPin() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes(SALT_KEY, g_salt, SALT_LEN);
  p.putBytes(HASH_KEY, g_hash, HASH_LEN);
  p.end();
}

// Note what is missing: setPin() never regenerates the salt. The salt is created once
// per device in begin() and kept for the life of the NVS namespace, so changing the code
// rewrites only the digest.
static void setPin(const String &pin) {
  digest(pin, g_hash);
  persistPin();
}

// ========== [Lifecycle] ==========
// Read-only Preferences handle (the `true`), because a boot that only reads should not
// be able to corrupt what it is reading.
void begin() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  const size_t gotSalt = p.getBytes(SALT_KEY, g_salt, SALT_LEN);
  const size_t gotHash = p.getBytes(HASH_KEY, g_hash, HASH_LEN);
  // The latch is restored here, which is the entire point of persisting it: a hub that
  // was locked when it lost power boots straight back into locked.
  g_lockdown = p.getBool(LOCK_KEY, false);
  p.end();

  // Both halves present and the right length: nothing to do. Any other outcome —
  // first boot, an erased namespace, a partial write — falls through to a fresh install.
  if (gotSalt == SALT_LEN && gotHash == HASH_LEN) return;

  // esp_fill_random() draws from the hardware RNG. random() would be seeded from uptime,
  // which on a device that boots to the same state every time is not a secret at all:
  // an attacker who knows the firmware knows the salt of every unit.
  esp_fill_random(g_salt, SALT_LEN);
  setPin(DEFAULT_PIN);
  Serial.println("access: code set to the factory default");
}

uint8_t failures()  { return g_fails; }
bool    lockdown()  { return g_lockdown; }

// ========== [Lockdown latch] ==========
// Written to NVS, not held in RAM. Held in RAM the attack is trivial and complete: five
// guesses, pull the power, the panel comes back clean, five more guesses. That is
// unlimited guessing against a four-digit code one power cycle at a time, and it would
// make the attempt limit decorative. Persisted, the guess budget is five for the life of
// the device rather than five per boot. The cost is that an owner can lock themselves
// out with five typos, which is only acceptable because the two recovery paths — an
// enrolled fingerprint at the panel, or /unlock from the bot — do not involve the keypad.
// See ADR-012.
static void persistLockdown(bool on) {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBool(LOCK_KEY, on);
  p.end();
}

// Idempotent: already locked means the flash write and the edge are both skipped, so a
// second /lock does not re-announce anything. Latching also revokes any open manage
// session — whoever was mid-session is no longer trusted — and zeroes the failure count,
// since the counter's job is done once the latch is set.
static void latch() {
  if (g_lockdown) return;
  g_lockdown = true;
  g_latched = true;
  g_fails = 0;
  revoke();
  persistLockdown(true);
}

// Read-and-clear. The caller uses it to score EV_CODE_ATTACK and announce the lockdown
// exactly once, on the attempt that caused it.
bool takeLatched() {
  const bool v = g_latched;
  g_latched = false;
  return v;
}

// A remote lock is the owner's deliberate act, so it latches but immediately eats the
// edge: no code attack is scored and no "lockdown after failed codes" alarm is raised for
// something the owner asked for.
void latchRemote() {
  latch();
  g_latched = false;
}

// Returns false when there was nothing to clear, so the caller can stay quiet instead of
// reporting an unlock that did not happen. Clearing also resets the failure count, giving
// a fresh five attempts after a legitimate recovery.
bool clearLockdown() {
  if (!g_lockdown) return false;
  g_lockdown = false;
  g_fails = 0;
  persistLockdown(false);
  return true;
}

// ========== [Verify] ==========
// The only place a code is checked. Three outcomes, no side effects beyond the counter:
// V_LOCKED is checked first so a locked panel never even hashes the entry, and never
// gives an attacker the one bit of feedback a wrong-versus-locked distinction would carry.
Verdict verify(const String &pin) {
  if (g_lockdown) return V_LOCKED;

  uint8_t candidate[HASH_LEN];
  digest(pin, candidate);

  if (sameHash(candidate, g_hash)) {
    // A correct code forgives the run of wrong ones before it: a typo followed by a
    // success is not an attack, and the counter only means anything as a consecutive run.
    g_fails = 0;
    return V_OK;
  }

  g_fails++;
  if (g_fails >= MAX_PIN_ATTEMPTS) latch();
  return V_WRONG;
}

// ========== [PIN change] ==========
// Length is checked before the current code, so a too-short replacement costs no attempt.
// verify() is reused rather than reimplemented, which means a wrong current code counts
// toward the same five-attempt budget and can latch lockdown from this screen too.
// keepAlive() extends the manage session because changing the code is proof of presence.
bool changePin(const String &current, const String &next) {
  if (next.length() < PIN_LENGTH) return false;
  if (verify(current) != V_OK) return false;
  setPin(next);
  keepAlive();
  return true;
}

// ========== [Manage session] ==========
// A correct code buys MANAGE_SESSION_MS of authority to change credentials, not a
// permanent one. The window slides: every management action calls keepAlive() and pushes
// the expiry out again, so an enrolment that needs several finger presses, or a rename
// typed slowly, cannot expire halfway through and leave the operator locked out of a job
// they are visibly in the middle of. A fixed 60 s window would either be too short for
// enrolment or too long to leave unattended; a sliding one is short *and* patient. It also
// means the session dies 60 s after the operator walks away, which is the case that
// matters.
void grant() { g_sessionUntil = millis() + MANAGE_SESSION_MS; }

// Extends an existing session but never creates one — that is what the zero check is for.
// So a recognised fingerprint or an approval keeps an open session alive without silently
// granting management rights to anyone who touched the sensor.
void keepAlive() {
  if (g_sessionUntil == 0) return;
  g_sessionUntil = millis() + MANAGE_SESSION_MS;
}

// The subtraction is the point. `millis() < g_sessionUntil` breaks once per 49.7 days,
// when millis() wraps to 0: an expiry computed just before the wrap is a huge number, the
// fresh millis() is tiny, and the comparison reports a session that expired as still live
// — the one failure mode you do not want in the function that guards credential changes.
// Casting the unsigned difference to int32_t gives a signed "time remaining" that is
// correct across the wrap, because the subtraction wraps in the same modulus. The
// g_sessionUntil != 0 test comes first so "no session" is never mistaken for "expired
// long ago", which would otherwise land on exactly the same arithmetic.
bool granted() {
  return g_sessionUntil != 0 && (int32_t)(g_sessionUntil - millis()) > 0;
}

uint32_t sessionMsLeft() {
  if (!granted()) return 0;
  return g_sessionUntil - millis();
}

void revoke() { g_sessionUntil = 0; }

// ========== [Approvals] ==========
// A request from the web, parked here until someone answers it on the glass. This module
// only stores and times it; the decision and its consequences are in state.cpp.
bool pending() { return g_pending; }

const Approval &approval() { return g_approval; }

// Counts down rather than up so the UI can draw the remaining seconds directly. Returning
// 0 is what Sec::tick() reads as "timed out", so a timeout is a refusal by construction
// and there is no separate expiry flag to keep in step.
uint32_t approvalMsLeft() {
  if (!g_pending) return 0;
  const uint32_t age = millis() - g_pendingAt;
  return age >= APPROVAL_TIMEOUT_MS ? 0 : APPROVAL_TIMEOUT_MS - age;
}

// Refuses a repeat of the request already in flight, so the dashboard re-sending the same
// id on its next poll does not restart the 30 s countdown under the operator's finger.
// Both strings are copied into fixed buffers and terminated by hand, because they arrive
// from the network and their length is not this module's to trust.
bool request(uint32_t id, const char *action, uint16_t arg, const char *text) {
  if (g_pending && g_approval.id == id) return false;
  g_approval.id  = id;
  g_approval.arg = arg;
  strncpy(g_approval.action, action, sizeof(g_approval.action) - 1);
  g_approval.action[sizeof(g_approval.action) - 1] = 0;
  strncpy(g_approval.text, text, sizeof(g_approval.text) - 1);
  g_approval.text[sizeof(g_approval.text) - 1] = 0;
  g_pending = true;
  g_pendingAt = millis();
  return true;
}

// Clears the flag only; g_approval keeps its contents so the caller can still read the
// action it just consumed.
void drop() { g_pending = false; }

// ========== [Tick] ==========
// Collapses an expired session to the "no session" state, so granted() stops depending on
// arithmetic once the answer is settled and the UI's own polling sees a clean zero.
void tick() {
  if (g_sessionUntil != 0 && !granted()) revoke();
}

}
