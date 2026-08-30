#pragma once
#include <Arduino.h>

struct HubConfig {
  String   wifiSsid;
  String   wifiPass;
  String   serverUrl;
  String   apiKey;
  String   siteName;
  String   timezone;
  uint16_t duressFinger;
};

namespace Store {
bool ready();
bool begin();
const HubConfig &config();

String fingerLabel(uint16_t id);
bool   fingerKnown(uint16_t id);
bool   fingerIsDuress(uint16_t id);
int    fingerCount();
bool   addFinger(uint16_t id, const String &label, bool duress);

void appendEvent(uint32_t epoch, const char *kind, const char *event,
                 uint16_t value, bool silent);

void setEpoch(uint32_t epoch);
uint32_t epoch();
bool haveTime();
}
