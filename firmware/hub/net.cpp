// =============================================================================
//  net.cpp — WiFi, time sync, alerts, command polling, on its own task
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

// ========== [Cross-thread messages] ==========
// Fixed-size structs so both queues copy by value. Nothing is shared by pointer
// between the network task and the main loop.
enum OutKind : uint8_t { OUT_ALERT = 0, OUT_DECIDE, OUT_ROSTER };
enum InKind  : uint8_t { IN_UNLOCK = 0, IN_LOCK, IN_PANIC, IN_ARM, IN_DISARM,
                         IN_APPROVE, IN_RENAME };

struct OutMsg {
  uint8_t  kind;
  bool     flag;
  uint32_t id;
  uint16_t value;
  char     what[16];
  char     event[40];
};

struct InMsg {
  uint8_t  kind;
  uint32_t id;
  uint16_t arg;
  char     action[20];
  char     text[40];
};

// ========== [Module state] ==========
static volatile bool g_online = false;
static uint32_t g_nextTry   = 0;
static uint32_t g_nextSync  = 0;
static uint32_t g_nextPoll  = 0;
static bool     g_started   = false;
static bool     g_rosterDue = true;

static QueueHandle_t g_out = nullptr;
static QueueHandle_t g_in  = nullptr;

bool online()  { return g_online; }
int  channel() { return WiFi.channel(); }

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
static void postAlert(const OutMsg &m) {
  const HubConfig &c = Store::config();
  HTTPClient http;
  if (!openTo(http, "/api/alert")) return;

  String body = "{\"silent\":";
  body += m.flag ? "true" : "false";
  body += ",\"kind\":\"";
  body += m.what;
  body += "\",\"event\":\"";
  body += m.event;
  body += "\",\"value\":";
  body += String(m.value);
  body += ",\"site\":\"";
  body += c.siteName;
  body += "\",\"epoch\":";
  body += String(Store::epoch());
  body += "}";

  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

void alert(bool silent, const char *kind, const char *event, uint16_t value) {
  if (!g_out) return;
  OutMsg m = {};
  m.kind  = OUT_ALERT;
  m.flag  = silent;
  m.value = value;
  strncpy(m.what, kind, sizeof(m.what) - 1);
  strncpy(m.event, event, sizeof(m.event) - 1);
  xQueueSend(g_out, &m, 0);
}

// ========== [Outbound: the answer to an approval] ==========
static void postDecision(const OutMsg &m) {
  HTTPClient http;
  if (!openTo(http, "/api/command")) return;
  String body = "{\"decision\":{\"id\":";
  body += String(m.id);
  body += ",\"allowed\":";
  body += m.flag ? "true" : "false";
  body += "}}";
  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

void decide(uint32_t id, bool allowed) {
  if (!g_out) return;
  OutMsg m = {};
  m.kind = OUT_DECIDE;
  m.id   = id;
  m.flag = allowed;
  xQueueSend(g_out, &m, 0);
}

// ========== [Outbound: the fingerprint roster] ==========
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

void publishRoster() {
  if (!g_out) return;
  OutMsg m = {};
  m.kind = OUT_ROSTER;
  xQueueSend(g_out, &m, 0);
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

  InMsg m = {};
  m.kind = IN_APPROVE;
  m.id   = (uint32_t)id;
  m.arg  = (uint16_t)(arg < 0 ? 0 : arg);
  strncpy(m.action, action.c_str(), sizeof(m.action) - 1);
  strncpy(m.text, text.c_str(), sizeof(m.text) - 1);
  xQueueSend(g_in, &m, 0);
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
    if (id > 0 && label.length()) {
      InMsg m = {};
      m.kind = IN_RENAME;
      m.arg  = (uint16_t)id;
      strncpy(m.text, label.c_str(), sizeof(m.text) - 1);
      xQueueSend(g_in, &m, 0);
    }
    from = close + 1;
  }
}

// ========== [Command polling] ==========
static void pushFlag(uint8_t kind) {
  InMsg m = {};
  m.kind = kind;
  xQueueSend(g_in, &m, 0);
}

static void pollCommands() {
  HTTPClient http;
  if (!openTo(http, "/api/command")) return;
  if (http.GET() == 200) {
    const String body = http.getString();
    if (flagTrue(body, "unlock")) pushFlag(IN_UNLOCK);
    if (flagTrue(body, "lock"))   pushFlag(IN_LOCK);
    if (flagTrue(body, "panic"))  pushFlag(IN_PANIC);
    if (flagTrue(body, "disarm")) pushFlag(IN_DISARM);
    if (flagTrue(body, "arm"))    pushFlag(IN_ARM);
    takeApproval(body);
    takeRenames(body);
  }
  http.end();
}

// ========== [The network task] ==========
// Everything that can block on a socket runs here, on core 0 with the WiFi stack,
// so a slow or missing server cannot stall the touch panel on core 1.
static void serviceLink() {
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
  }

  if (g_rosterDue) sendRoster();
}

static void netTask(void *) {
  for (;;) {
    OutMsg m;
    while (xQueueReceive(g_out, &m, 0) == pdTRUE) {
      switch (m.kind) {
        case OUT_ALERT:  postAlert(m);       break;
        case OUT_DECIDE: postDecision(m);    break;
        case OUT_ROSTER: g_rosterDue = true; break;
        default: break;
      }
    }
    serviceLink();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ========== [Bring-up] ==========
void begin() {
  const HubConfig &c = Store::config();
  if (c.wifiSsid.length() == 0) return;

  g_out = xQueueCreate(8, sizeof(OutMsg));
  g_in  = xQueueCreate(8, sizeof(InMsg));
  if (!g_out || !g_in) return;

  WiFi.begin(c.wifiSsid.c_str(), c.wifiPass.c_str());
  g_started = true;
  g_nextTry = millis() + 8000;

  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 1, nullptr, 0);
}

// ========== [Applying what the task fetched] ==========
// Runs on the main loop, so every Sec:: call still happens on one thread.
void tick() {
  if (!g_started || !g_in) return;

  InMsg m;
  while (xQueueReceive(g_in, &m, 0) == pdTRUE) {
    switch (m.kind) {
      case IN_UNLOCK:  Sec::clearLockdown("unlocked from the bot");  break;
      case IN_LOCK:    Sec::lockRemote();                            break;
      case IN_PANIC:   Sec::panic();                                 break;
      case IN_ARM:     Sec::requestArm(MODE_AWAY);                   break;
      case IN_DISARM:  Sec::requestArm(MODE_DISARMED);               break;
      case IN_RENAME:  Sec::renameFinger(m.arg, String(m.text));     break;
      case IN_APPROVE: Access::request(m.id, m.action, m.arg, m.text); break;
      default: break;
    }
  }
}

}
