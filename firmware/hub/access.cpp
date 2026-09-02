// =============================================================================
//  access.cpp — the keypad credential, the lockdown latch and the manage session
// =============================================================================

#include "access.h"
#include "config.h"
#include <Preferences.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

namespace Access {

// ========== [Storage keys] ==========
#define SALT_KEY  "psalt"
#define HASH_KEY  "phash"
#define LOCK_KEY  "lockdown"
#define SALT_LEN  8
#define HASH_LEN  32

// ========== [Module state] ==========
static uint8_t  g_salt[SALT_LEN];
static uint8_t  g_hash[HASH_LEN];
static uint8_t  g_fails    = 0;
static bool     g_lockdown = false;
static bool     g_latched  = false;

static uint32_t g_sessionUntil = 0;

static Approval g_approval  = {0, 0, "", ""};
static bool     g_pending   = false;
static uint32_t g_pendingAt = 0;

// ========== [Digest] ==========
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
// Accumulate every difference instead of returning early, so timing leaks no prefix.
static bool sameHash(const uint8_t *a, const uint8_t *b) {
  uint8_t diff = 0;
  for (uint8_t i = 0; i < HASH_LEN; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

// ========== [Persistence] ==========
static void persistPin() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes(SALT_KEY, g_salt, SALT_LEN);
  p.putBytes(HASH_KEY, g_hash, HASH_LEN);
  p.end();
}

static void setPin(const String &pin) {
  digest(pin, g_hash);
  persistPin();
}

// ========== [Lifecycle] ==========
void begin() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  const size_t gotSalt = p.getBytes(SALT_KEY, g_salt, SALT_LEN);
  const size_t gotHash = p.getBytes(HASH_KEY, g_hash, HASH_LEN);
  g_lockdown = p.getBool(LOCK_KEY, false);
  p.end();

  if (gotSalt == SALT_LEN && gotHash == HASH_LEN) return;

  esp_fill_random(g_salt, SALT_LEN);
  setPin(DEFAULT_PIN);
  Serial.println("access: code set to the factory default");
}

uint8_t failures()  { return g_fails; }
bool    lockdown()  { return g_lockdown; }

// ========== [Lockdown latch] ==========
static void persistLockdown(bool on) {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBool(LOCK_KEY, on);
  p.end();
}

// Persisted, so pulling the power does not hand back a fresh set of guesses.
static void latch() {
  if (g_lockdown) return;
  g_lockdown = true;
  g_latched = true;
  g_fails = 0;
  revoke();
  persistLockdown(true);
}

bool takeLatched() {
  const bool v = g_latched;
  g_latched = false;
  return v;
}

void latchRemote() {
  latch();
  g_latched = false;
}

bool clearLockdown() {
  if (!g_lockdown) return false;
  g_lockdown = false;
  g_fails = 0;
  persistLockdown(false);
  return true;
}

// ========== [Verify] ==========
Verdict verify(const String &pin) {
  if (g_lockdown) return V_LOCKED;

  uint8_t candidate[HASH_LEN];
  digest(pin, candidate);

  if (sameHash(candidate, g_hash)) {
    g_fails = 0;
    return V_OK;
  }

  g_fails++;
  if (g_fails >= MAX_PIN_ATTEMPTS) latch();
  return V_WRONG;
}

// ========== [PIN change] ==========
bool changePin(const String &current, const String &next) {
  if (next.length() < PIN_LENGTH) return false;
  if (verify(current) != V_OK) return false;
  setPin(next);
  keepAlive();
  return true;
}

// ========== [Manage session] ==========
void grant() { g_sessionUntil = millis() + MANAGE_SESSION_MS; }

void keepAlive() {
  if (g_sessionUntil == 0) return;
  g_sessionUntil = millis() + MANAGE_SESSION_MS;
}

// Signed difference, not a plain compare, so the window survives millis() rollover.
bool granted() {
  return g_sessionUntil != 0 && (int32_t)(g_sessionUntil - millis()) > 0;
}

uint32_t sessionMsLeft() {
  if (!granted()) return 0;
  return g_sessionUntil - millis();
}

void revoke() { g_sessionUntil = 0; }

// ========== [Approvals] ==========
bool pending() { return g_pending; }

const Approval &approval() { return g_approval; }

uint32_t approvalMsLeft() {
  if (!g_pending) return 0;
  const uint32_t age = millis() - g_pendingAt;
  return age >= APPROVAL_TIMEOUT_MS ? 0 : APPROVAL_TIMEOUT_MS - age;
}

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

void drop() { g_pending = false; }

// ========== [Tick] ==========
void tick() {
  if (g_sessionUntil != 0 && !granted()) revoke();
}

}
