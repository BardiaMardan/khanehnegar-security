#include "store.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

namespace Store {

#define CFG_PATH    "/khaneh/config.ini"
#define ACCESS_PATH "/khaneh/access.ini"
#define EVENT_PATH  "/khaneh/events.csv"
#define FP_SLOTS    16

static SPIClass  sdSpi(HSPI);
static bool      g_ready = false;
static HubConfig g_cfg;

static uint16_t g_ids[FP_SLOTS];
static String   g_labels[FP_SLOTS];
static bool     g_duress[FP_SLOTS];
static int      g_fingers = 0;

static uint32_t g_epochBase = 0;
static uint32_t g_epochAtMs = 0;
static int16_t  g_offsetMin = 0;
static bool     g_haveTime  = false;

bool ready() { return g_ready; }
const HubConfig &config() { return g_cfg; }

static void trim(String &s) {
  s.replace("\r", "");
  s.trim();
}

static bool splitPair(const String &line, String &key, String &val) {
  if (line.length() == 0 || line[0] == '#' || line[0] == ';') return false;
  const int eq = line.indexOf('=');
  if (eq <= 0) return false;
  key = line.substring(0, eq);
  val = line.substring(eq + 1);
  trim(key);
  trim(val);
  return key.length() > 0;
}

static void applyConfig(const String &k, const String &v) {
  if      (k == "wifi_ssid")     g_cfg.wifiSsid = v;
  else if (k == "wifi_pass")     g_cfg.wifiPass = v;
  else if (k == "server_url")    g_cfg.serverUrl = v;
  else if (k == "api_key")       g_cfg.apiKey = v;
  else if (k == "site_name")     g_cfg.siteName = v;
  else if (k == "timezone")      g_cfg.timezone = v;
  else if (k == "duress_finger") g_cfg.duressFinger = v.toInt();
}

static void readIni(const char *path, void (*sink)(const String &, const String &)) {
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    String k, v;
    if (splitPair(line, k, v)) sink(k, v);
  }
  f.close();
}

static void applyAccess(const String &k, const String &v) {
  if (g_fingers >= FP_SLOTS) return;
  const uint16_t id = (uint16_t)k.toInt();
  if (id == 0) return;
  String label = v;
  bool duress = false;
  const int bar = v.indexOf('|');
  if (bar >= 0) {
    label = v.substring(0, bar);
    duress = v.substring(bar + 1).indexOf("duress") >= 0;
    label.trim();
  }
  g_ids[g_fingers]    = id;
  g_labels[g_fingers] = label;
  g_duress[g_fingers] = duress;
  g_fingers++;
}

bool begin() {
  g_cfg.siteName     = "khanehnegar";
  g_cfg.timezone     = "Asia/Tehran";
  g_cfg.duressFinger = 0;

  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  g_ready = false;
  for (int attempt = 0; attempt < 3 && !g_ready; attempt++) {
    if (SD.begin(PIN_SD_CS, sdSpi, SD_CLOCK_HZ)) g_ready = true;
    else delay(250);
  }
  if (!g_ready) return false;

  if (!SD.exists("/khaneh")) SD.mkdir("/khaneh");
  readIni(CFG_PATH, applyConfig);
  readIni(ACCESS_PATH, applyAccess);
  return true;
}

String fingerLabel(uint16_t id) {
  for (int i = 0; i < g_fingers; i++) if (g_ids[i] == id) return g_labels[i];
  return String("");
}

bool fingerKnown(uint16_t id) {
  for (int i = 0; i < g_fingers; i++) if (g_ids[i] == id) return true;
  return false;
}

bool fingerIsDuress(uint16_t id) {
  if (g_cfg.duressFinger && id == g_cfg.duressFinger) return true;
  for (int i = 0; i < g_fingers; i++) if (g_ids[i] == id) return g_duress[i];
  return false;
}

int fingerCount() { return g_fingers; }

bool addFinger(uint16_t id, const String &label, bool duress) {
  if (g_fingers >= FP_SLOTS) return false;
  for (int i = 0; i < g_fingers; i++) if (g_ids[i] == id) return false;

  g_ids[g_fingers]    = id;
  g_labels[g_fingers] = label;
  g_duress[g_fingers] = duress;
  g_fingers++;

  if (!g_ready) return false;
  File f = SD.open(ACCESS_PATH, FILE_APPEND);
  if (!f) return false;
  f.printf("%u=%s%s\n", id, label.c_str(), duress ? "|duress" : "");
  f.close();
  return true;
}

void appendEvent(uint32_t ep, const char *kind, const char *event,
                 uint16_t value, bool silent) {
  if (!g_ready) return;
  File f = SD.open(EVENT_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%s,%s,%u,%d\n", (unsigned long)ep, kind, event, value, silent ? 1 : 0);
  f.close();
}

void setEpoch(uint32_t ep) {
  g_epochBase = ep;
  g_epochAtMs = millis();
  g_haveTime = true;
}

void setOffsetMinutes(int16_t minutes) { g_offsetMin = minutes; }

uint32_t epoch() {
  if (!g_haveTime) return 0;
  return g_epochBase + (millis() - g_epochAtMs) / 1000;
}

uint32_t localEpoch() {
  const uint32_t e = epoch();
  if (!e) return 0;
  return e + (int32_t)g_offsetMin * 60;
}

String clockHHMM() {
  const uint32_t e = localEpoch();
  if (!e) return String("--:--");
  const uint32_t day = e % 86400UL;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(day / 3600), (unsigned)((day % 3600) / 60));
  return String(buf);
}

bool haveTime() { return g_haveTime; }

}
