#include <SPI.h>
#include <SD.h>

#define SD_CS    27
#define SD_SCK   14
#define SD_MOSI  13
#define SD_MISO  32

#define TFT_SCK  18
#define TFT_MOSI 23
#define TFT_MISO 19

static void banner(const char *s) {
  Serial.println();
  Serial.println("========================================");
  Serial.println(s);
  Serial.println("========================================");
}

static void pinReport(int sck, int mosi, int miso, int cs) {
  Serial.printf("pins  sck=%d mosi=%d miso=%d cs=%d\n", sck, mosi, miso, cs);

  pinMode(cs, OUTPUT);
  pinMode(miso, INPUT_PULLUP);
  delay(5);

  digitalWrite(cs, HIGH);
  delay(5);
  const int idleHigh = digitalRead(miso);

  digitalWrite(cs, LOW);
  delay(5);
  const int selLow = digitalRead(miso);
  digitalWrite(cs, HIGH);

  Serial.printf("miso with cs high = %d   (expect 1)\n", idleHigh);
  Serial.printf("miso with cs low  = %d\n", selLow);

  if (idleHigh == 0) {
    Serial.println("VERDICT: miso is stuck LOW.");
    Serial.println("  Either miso is not connected to the card at all,");
    Serial.println("  or it is shorted to ground, or the card has no power.");
  } else {
    Serial.println("miso idles high, so the line is at least pulled up.");
  }

  pinMode(mosi, INPUT_PULLUP);
  pinMode(sck, INPUT_PULLUP);
  delay(5);
  Serial.printf("mosi idle = %d   sck idle = %d   (both expect 1)\n",
                digitalRead(mosi), digitalRead(sck));

  pinMode(sck, OUTPUT);
  digitalWrite(sck, LOW);
  delay(2);
  const int misoAfterSckLow = digitalRead(miso);
  const int mosiAfterSckLow = digitalRead(mosi);
  digitalWrite(sck, HIGH);
  if (misoAfterSckLow == 0 || mosiAfterSckLow == 0) {
    Serial.println("WARNING: driving sck low pulled another line low.");
    Serial.println("  Those two pins are shorted together.");
  }
}

static bool tryMount(SPIClass &bus, int sck, int mosi, int miso, int cs, uint32_t hz) {
  bus.end();
  bus.begin(sck, miso, mosi, cs);
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  delay(20);

  const bool ok = SD.begin(cs, bus, hz);
  Serial.printf("  mount at %7lu Hz : %s\n", (unsigned long)hz, ok ? "OK" : "fail");
  if (!ok) return false;

  const uint8_t type = SD.cardType();
  const char *name = type == CARD_NONE ? "NONE"
                   : type == CARD_MMC  ? "MMC"
                   : type == CARD_SD   ? "SDSC"
                   : type == CARD_SDHC ? "SDHC" : "UNKNOWN";
  Serial.printf("  card type  : %s\n", name);
  Serial.printf("  card size  : %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  Serial.printf("  used bytes : %llu\n", SD.usedBytes());

  File root = SD.open("/");
  if (root) {
    Serial.println("  root listing:");
    File e = root.openNextFile();
    int n = 0;
    while (e && n < 12) {
      Serial.printf("    %s%s  %u\n", e.isDirectory() ? "[dir] " : "      ",
                    e.name(), (unsigned)e.size());
      e = root.openNextFile();
      n++;
    }
    if (n == 0) Serial.println("    (empty)");
    root.close();
  }

  File w = SD.open("/sdtest.txt", FILE_WRITE);
  if (w) {
    w.println("write ok");
    w.close();
    Serial.println("  write test : OK");
    SD.remove("/sdtest.txt");
  } else {
    Serial.println("  write test : FAILED (card may be locked or full)");
  }

  SD.end();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(600);

  banner("SD DIAGNOSTIC");
  Serial.println("If every mount fails, read the electrical section above the");
  Serial.println("attempts. That tells you wiring versus card versus module.");

  banner("ELECTRICAL CHECK  (HSPI pins)");
  pinReport(SD_SCK, SD_MOSI, SD_MISO, SD_CS);

  banner("MOUNT ATTEMPTS  HSPI 14/13/32 cs 27");
  SPIClass hspi(HSPI);
  bool any = false;
  any |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 400000);
  if (!any) any |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 1000000);
  if (!any) any |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 4000000);

  if (!any) {
    banner("MOUNT ATTEMPTS  shared TFT bus 18/23/19 cs 27");
    Serial.println("This checks whether the board bridges SD to the TFT bus.");
    SPIClass vspi(VSPI);
    any |= tryMount(vspi, TFT_SCK, TFT_MOSI, TFT_MISO, SD_CS, 400000);
    if (!any) any |= tryMount(vspi, TFT_SCK, TFT_MOSI, TFT_MISO, SD_CS, 4000000);
  }

  banner(any ? "RESULT: a working configuration was found"
             : "RESULT: nothing mounted");

  if (!any) {
    Serial.println("Work through these in order:");
    Serial.println(" 1. Card format. 64GB and larger ship as exFAT, which is");
    Serial.println("    not supported. Reformat as FAT32.");
    Serial.println(" 2. Card seated fully, and the slot has 3.3V.");
    Serial.println(" 3. Try a different card. Very old and very new cards both fail.");
    Serial.println(" 4. Check the four jumpers against the board silkscreen again.");
    Serial.println(" 5. If miso read 0 in the electrical check, the fault is");
    Serial.println("    wiring or power, not software.");
  }
}

void loop() {
  delay(5000);
}
