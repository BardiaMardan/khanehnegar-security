// =============================================================================
//  store.h — the SD card and the wall clock, as seen from outside
// =============================================================================

#pragma once
#include <Arduino.h>

struct HubConfig {
  String wifiSsid;
  String wifiPass;
  String serverUrl;
  String apiKey;
  String siteName;
  String timezone;
};

namespace Store {
bool ready();
bool begin();
const HubConfig &config();

// ========== [The roster mirror] ==========
String readAccess();
bool   writeAccess(const String &body);

void appendEvent(uint32_t epoch, const char *kind, const char *event,
                 uint16_t value, bool silent);

// ========== [The clock] ==========
void setEpoch(uint32_t epoch);
void setOffsetMinutes(int16_t minutes);
uint32_t epoch();
uint32_t localEpoch();
bool haveTime();
String clockHHMM();
String clockHHMMSS();
String dayStamp();
String hhmm(uint32_t epoch);
}
