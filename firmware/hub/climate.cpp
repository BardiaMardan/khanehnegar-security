#include "climate.h"
#include "config.h"
#include <DHT.h>

namespace Climate {

static DHT      dht(PIN_DHT, DHT11);
static float    g_t     = 0;
static float    g_h     = 0;
static bool     g_valid = false;
static uint32_t g_next  = 0;

void begin() {
  dht.begin();
  g_next = millis() + 2000;
}

void tick() {
  const uint32_t now = millis();
  if (now < g_next) return;
  g_next = now + CLIMATE_PERIOD_MS;

  const float t = dht.readTemperature();
  const float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    g_valid = false;
    return;
  }
  g_t = t;
  g_h = h;
  g_valid = true;
}

bool  valid()    { return g_valid; }
float tempC()    { return g_t; }
float humidity() { return g_h; }

}
