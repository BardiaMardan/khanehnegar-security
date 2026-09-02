// =============================================================================
//  net.cpp — WiFi, time sync, alerts, command polling
// =============================================================================

#include "net.h"
#include "store.h"
#include "state.h"
#include "access.h"
#include "fingers.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

namespace Net {

// ========== [Module state] ==========
static bool     g_online   = false;
static uint32_t g_nextTry  = 0;
static uint32_t g_nextSync = 0;
static uint32_t g_nextPoll = 0;
static bool     g_started  = false;
static bool     g_rosterDue = true;

bool online()  { return g_online; }
int  channel() { return WiFi.channel(); }

// ========== [Bring-up] ==========
void begin() {
  const HubConfig &c = Store::config();
  if (c.wifiSsid.length() == 0) return;
  WiFi.begin(c.wifiSsid.c_str(), c.wifiPass.c_str());
  g_started = true;
  g_nextTry = millis() + 8000;
}

// ========== [The one place a request is opened] ==========
static bool openTo(HTTPClient &http, const char *path) {
  const HubConfig &c = Store::config();
  if (!g_online || c.serverUrl.length() == 0) return false;
  http.setConnectTimeout(1200);
  http.setTimeout(1800);
  if (!http.begin(c.serverUrl + path)) return false;
  http.addHeader("X-Api-Key", c.apiKey);
  return true;
}

// ========== [JSON, scanned rather than parsed] ==========

static long findNumber(const String &body, const char *key, int from = 0) {
  const int k = body.indexOf(key, from);
  if (k < 0) return -1;
  int i = body.indexOf(':', k);
  if (i < 0) return -1;
  i++;
  bool neg = false;
  while (i < (int)body.length() && (body[i] == ' ' || body[i] == '"')) i++;
  if (i < (int)body.length() && body[i] == '-') { neg = true; i++; }
  long v = 0;
  bool any = false;
  while (i < (int)body.length() && isDigit(body[i])) {
    v = v * 10 + (body[i] - '0');
    i++;
    any = true;
  }
  if (!any) return -1;
  return neg ? -v : v;
}

static String findString(const String &body, const char *key, int from = 0) {
  const int k = body.indexOf(key, from);
  if (k < 0) return String("");
  int i = body.indexOf(':', k);
  if (i < 0) return String("");
  i = body.indexOf('"', i + 1);
  if (i < 0) return String("");
  const int end = body.indexOf('"', i + 1);
  if (end < 0) return String("");
  return body.substring(i + 1, end);
}

static bool flagTrue(const String &body, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\":true";
  return body.indexOf(needle) >= 0;
}

// ========== [Time sync] ==========
static void syncTime() {
  HTTPClient http;
  if (!openTo(http, "/api/time")) return;
  if (http.GET() == 200) {
    const String body = http.getString();
    const long unix = findNumber(body, "\"unix\"");
    if (unix > 1000000000L) Store::setEpoch((uint32_t)unix);
    if (body.indexOf("\"offset\"") >= 0) {
      Store::setOffsetMinutes((int16_t)findNumber(body, "\"offset\""));
    }
  }
  http.end();
}

// ========== [Outbound: alerts] ==========
void alert(bool silent, const char *kind, const char *event, uint16_t value) {
  const HubConfig &c = Store::config();
  HTTPClient http;
  if (!openTo(http, "/api/alert")) return;

  String body = "{\"silent\":";
  body += silent ? "true" : "false";
  body += ",\"kind\":\"";
  body += kind;
  body += "\",\"event\":\"";
  body += event;
  body += "\",\"value\":";
  body += String(value);
  body += ",\"site\":\"";
  body += c.siteName;
  body += "\",\"epoch\":";
  body += String(Store::epoch());
  body += "}";

  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

// ========== [Outbound: the answer to an approval] ==========
void decide(uint32_t id, bool allowed) {
  HTTPClient http;
  if (!openTo(http, "/api/command")) return;
  String body = "{\"decision\":{\"id\":";
  body += String(id);
  body += ",\"allowed\":";
  body += allowed ? "true" : "false";
  body += "}}";
  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

// ========== [Outbound: the fingerprint roster] ==========
static void sendRoster();

void publishRoster() {
  g_rosterDue = true;
  if (g_online) sendRoster();
}

static void sendRoster() {
  HTTPClient http;
  if (!openTo(http, "/api/fingers")) return;

  String body = "{\"fingers\":[";
  for (int i = 0; i < Fingers::count(); i++) {
    if (i) body += ",";
    body += "{\"id\":";
    body += String(Fingers::idAt(i));
    body += ",\"label\":\"";
    body += Fingers::labelAt(i);
    body += "\"}";
  }
  body += "]}";

  http.addHeader("Content-Type", "application/json");
  if (http.POST(body) == 200) g_rosterDue = false;
  http.end();
}

// ========== [Inbound: an approval request] ==========
static void takeApproval(const String &body) {
  const int k = body.indexOf("\"approve\"");
  if (k < 0) return;
  const long id = findNumber(body, "\"id\"", k);
  if (id <= 0) return;
  const String action = findString(body, "\"action\"", k);
  const String text   = findString(body, "\"text\"", k);
  const long arg      = findNumber(body, "\"arg\"", k);
  if (!action.length()) return;
  Access::request((uint32_t)id, action.c_str(),
                  (uint16_t)(arg < 0 ? 0 : arg), text.c_str());
}

// ========== [Inbound: fingerprint renames] ==========
static void takeRenames(const String &body) {
  const int arr = body.indexOf("\"renames\":[");
  if (arr < 0) return;
  const int end = body.indexOf(']', arr);
  if (end < 0) return;

  int from = arr;
  while (true) {
    const int open = body.indexOf('{', from);
    if (open < 0 || open > end) return;
    const int close = body.indexOf('}', open);
    if (close < 0 || close > end) return;

    const String item = body.substring(open, close + 1);
    const long id = findNumber(item, "\"id\"");
    const String label = findString(item, "\"label\"");
    if (id > 0 && label.length()) Sec::renameFinger((uint16_t)id, label);
    from = close + 1;
  }
}

// ========== [Command polling] ==========
static void pollCommands() {
  HTTPClient http;
  if (!openTo(http, "/api/command")) return;
  if (http.GET() == 200) {
    const String body = http.getString();
    if (flagTrue(body, "unlock")) Sec::clearLockdown("unlocked from the bot");
    if (flagTrue(body, "lock"))   Sec::lockRemote();
    if (flagTrue(body, "panic"))  Sec::panic();
    if (flagTrue(body, "disarm")) Sec::requestArm(MODE_DISARMED);
    if (flagTrue(body, "arm"))    Sec::requestArm(MODE_AWAY);
    takeApproval(body);
    takeRenames(body);
  }
  http.end();
}

// ========== [Scheduler] ==========
void tick() {
  if (!g_started) return;
  const uint32_t now = millis();

  const bool up = WiFi.status() == WL_CONNECTED;
  if (up != g_online) {
    g_online = up;
    if (up) {
      g_nextSync = 0;
      g_rosterDue = true;
    }
  }

  if (!g_online) {
    if (now > g_nextTry) {
      g_nextTry = now + 15000;
      WiFi.reconnect();
    }
    return;
  }

  if (now >= g_nextSync) {
    g_nextSync = now + TIME_SYNC_MS;
    syncTime();
  }

  if (now >= g_nextPoll) {
    g_nextPoll = now + COMMAND_POLL_MS;
    pollCommands();
    if (g_rosterDue) sendRoster();
  }
}

}
