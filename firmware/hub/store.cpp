#include "store.h"
#include "config.h"
#include "secrets.h"
#include <SPI.h>
#include <SD.h>
#include <sys/time.h>
#include <time.h>

namespace Store {

#define CFG_PATH    "/khaneh/config.ini"
#define ACCESS_PATH "/khaneh/access.ini"
#define EVENT_PATH  "/khaneh/events.csv"

static bool      g_ready = false;
static SPIClass  sdSpi(HSPI);
static HubConfig g_cfg;

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
  if      (k == "wifi_ssid")  g_cfg.wifiSsid = v;
  else if (k == "wifi_pass")  g_cfg.wifiPass = v;
  else if (k == "server_url") g_cfg.serverUrl = v;
  else if (k == "api_key")    g_cfg.apiKey = v;
  else if (k == "site_name")  g_cfg.siteName = v;
  else if (k == "timezone")   g_cfg.timezone = v;
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

bool begin() {
  g_cfg.siteName  = "khanehnegar";
  g_cfg.timezone  = "Asia/Tehran";
  g_cfg.wifiSsid  = FALLBACK_WIFI_SSID;
  g_cfg.wifiPass  = FALLBACK_WIFI_PASS;
  g_cfg.serverUrl = FALLBACK_SERVER_URL;
  g_cfg.apiKey    = FALLBACK_API_KEY;

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  delay(20);

  const uint32_t rates[4] = {400000, 1000000, 4000000, SD_CLOCK_HZ};
  g_ready = false;

  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  for (uint8_t i = 0; i < 4 && !g_ready; i++) {
    Serial.printf("sd: %lu Hz ... ", (unsigned long)rates[i]);
    if (SD.begin(PIN_SD_CS, sdSpi, rates[i])) {
      g_ready = true;
      Serial.println("mounted");
    } else {
      Serial.println("no");
      delay(150);
    }
  }

  if (!g_ready) {
    Serial.println("sd: not mounted. run firmware/tools/sdtest");
    return false;
  }

  Serial.printf("sd: type=%u size=%lluMB\n", SD.cardType(),
                SD.cardSize() / (1024ULL * 1024ULL));

  if (!SD.exists("/khaneh")) SD.mkdir("/khaneh");
  readIni(CFG_PATH, applyConfig);
  return true;
}

String readAccess() {
  if (!g_ready) return String("");
  File f = SD.open(ACCESS_PATH, FILE_READ);
  if (!f) return String("");
  String body;
  body.reserve(f.size() + 1);
  while (f.available()) body += (char)f.read();
  f.close();
  return body;
}

bool writeAccess(const String &body) {
  if (!g_ready) return false;
  File f = SD.open(ACCESS_PATH, FILE_WRITE);
  if (!f) return false;
  f.print(body);
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
  struct timeval tv;
  tv.tv_sec  = (time_t)ep;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  g_haveTime = true;
}

void setOffsetMinutes(int16_t minutes) { g_offsetMin = minutes; }

uint32_t epoch() {
  if (!g_haveTime) return 0;
  return (uint32_t)time(nullptr);
}

uint32_t localEpoch() {
  const uint32_t e = epoch();
  if (!e) return 0;
  return e + (int32_t)g_offsetMin * 60;
}

static bool localParts(struct tm &out) {
  const uint32_t local = localEpoch();
  if (!local) return false;
  const time_t t = (time_t)local;
  gmtime_r(&t, &out);
  return true;
}

String hhmm(uint32_t ep) {
  if (!ep) return String("--:--");
  const time_t t = (time_t)(ep + (int32_t)g_offsetMin * 60);
  struct tm p;
  gmtime_r(&t, &p);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", p.tm_hour, p.tm_min);
  return String(buf);
}

String clockHHMM() {
  struct tm p;
  if (!localParts(p)) return String("--:--");
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", p.tm_hour, p.tm_min);
  return String(buf);
}

String clockHHMMSS() {
  struct tm p;
  if (!localParts(p)) return String("--:--:--");
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", p.tm_hour, p.tm_min, p.tm_sec);
  return String(buf);
}

String dayStamp() {
  static const char *kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  struct tm p;
  if (!localParts(p)) return String("");
  char buf[20];
  snprintf(buf, sizeof(buf), "%s %d %s", kDays[p.tm_wday % 7], p.tm_mday,
           kMon[p.tm_mon % 12]);
  return String(buf);
}

bool haveTime() { return g_haveTime; }

}
