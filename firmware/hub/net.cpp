#include "net.h"
#include "store.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

namespace Net {

static bool     g_online   = false;
static uint32_t g_nextTry  = 0;
static uint32_t g_nextSync = 0;
static bool     g_started  = false;

bool online()  { return g_online; }
int  channel() { return WiFi.channel(); }

void begin() {
  const HubConfig &c = Store::config();
  if (c.wifiSsid.length() == 0) return;
  WiFi.begin(c.wifiSsid.c_str(), c.wifiPass.c_str());
  g_started = true;
  g_nextTry = millis() + 8000;
}

static long findNumber(const String &body, const char *key) {
  const int k = body.indexOf(key);
  if (k < 0) return -1;
  int i = body.indexOf(':', k);
  if (i < 0) return -1;
  i++;
  while (i < (int)body.length() && (body[i] == ' ' || body[i] == '"')) i++;
  long v = 0;
  bool any = false;
  while (i < (int)body.length() && isDigit(body[i])) {
    v = v * 10 + (body[i] - '0');
    i++;
    any = true;
  }
  return any ? v : -1;
}

static void syncTime() {
  const HubConfig &c = Store::config();
  if (c.serverUrl.length() == 0) return;

  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2000);
  if (!http.begin(c.serverUrl + "/api/time")) return;
  http.addHeader("X-Api-Key", c.apiKey);
  if (http.GET() == 200) {
    const long unix = findNumber(http.getString(), "\"unix\"");
    if (unix > 1000000000L) Store::setEpoch((uint32_t)unix);
  }
  http.end();
}

void alert(bool silent, const char *kind, const char *event, uint16_t value) {
  const HubConfig &c = Store::config();
  if (!g_online || c.serverUrl.length() == 0) return;

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

  HTTPClient http;
  http.setConnectTimeout(1200);
  http.setTimeout(1500);
  if (!http.begin(c.serverUrl + "/api/alert")) return;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Api-Key", c.apiKey);
  http.POST(body);
  http.end();
}

void tick() {
  if (!g_started) return;
  const uint32_t now = millis();

  const bool up = WiFi.status() == WL_CONNECTED;
  if (up != g_online) {
    g_online = up;
    if (up) g_nextSync = 0;
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
}

}
