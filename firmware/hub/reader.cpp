#include "reader.h"
#include "config.h"
#include <SPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>

namespace Reader {

static SPIClass               hspi(HSPI);
static MFRC522DriverPinSimple ssPin(PIN_RFID_CS);
static SPISettings            settings(1000000, MSBFIRST, SPI_MODE0);
static MFRC522DriverSPI       driver(ssPin, hspi, settings);
static MFRC522                rc(driver);

static bool     g_present = false;
static bool     g_new     = false;
static String   g_uid     = "";
static String   g_lastUid = "";
static uint32_t g_lastAt  = 0;

void begin() {
  pinMode(PIN_RFID_RST, OUTPUT);
  digitalWrite(PIN_RFID_RST, HIGH);
  delay(50);

  hspi.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_CS);
  rc.PCD_Init();

  const byte v = (byte)rc.PCD_GetVersion();
  g_present = (v != 0x00 && v != 0xFF);
}

bool present() { return g_present; }

bool newCard() {
  const bool n = g_new;
  g_new = false;
  return n;
}

String uid() { return g_uid; }

void tick() {
  if (!g_present) return;
  if (!rc.PICC_IsNewCardPresent()) return;
  if (!rc.PICC_ReadCardSerial()) return;

  String s;
  for (byte i = 0; i < rc.uid.size; i++) {
    if (i) s += ":";
    if (rc.uid.uidByte[i] < 0x10) s += "0";
    s += String(rc.uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  rc.PICC_HaltA();

  const uint32_t now = millis();
  if (s == g_lastUid && now - g_lastAt < 2500) return;
  g_lastUid = s;
  g_lastAt  = now;
  g_uid     = s;
  g_new     = true;
}

}
