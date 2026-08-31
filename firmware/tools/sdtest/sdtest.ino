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

static void lineCheck(int sck, int mosi, int miso, int cs) {
  Serial.printf("pins  sck=%d mosi=%d miso=%d cs=%d\n", sck, mosi, miso, cs);
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);

  pinMode(miso, INPUT);
  delay(5);
  const int floatRead = digitalRead(miso);
  pinMode(miso, INPUT_PULLDOWN);
  delay(5);
  const int pulledDown = digitalRead(miso);
  pinMode(miso, INPUT);

  Serial.printf("miso floating        = %d\n", floatRead);
  Serial.printf("miso with pulldown   = %d\n", pulledDown);
  if (pulledDown == 1) {
    Serial.println("  miso is held high by something external.");
    Serial.println("  That means a card or module is driving it. Good sign.");
  } else {
    Serial.println("  miso follows our pulldown, so nothing external is on it.");
    Serial.println("  Either the wire is missing or the slot is unpowered.");
  }
}

static uint8_t sdCmd(SPIClass &bus, uint8_t cmd, uint32_t arg, uint8_t crc) {
  bus.transfer(0xFF);
  bus.transfer(0x40 | cmd);
  bus.transfer(arg >> 24);
  bus.transfer(arg >> 16);
  bus.transfer(arg >> 8);
  bus.transfer(arg);
  bus.transfer(crc);
  for (int i = 0; i < 12; i++) {
    const uint8_t r = bus.transfer(0xFF);
    if (!(r & 0x80)) return r;
  }
  return 0xFF;
}

static bool rawProbe(SPIClass &bus, int sck, int mosi, int miso, int cs) {
  bus.end();
  bus.begin(sck, miso, mosi, -1);
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);

  bus.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 12; i++) bus.transfer(0xFF);
  digitalWrite(cs, LOW);
  const uint8_t r1 = sdCmd(bus, 0, 0, 0x95);
  digitalWrite(cs, HIGH);
  bus.endTransaction();

  Serial.printf("CMD0 reply = 0x%02X   ", r1);
  if (r1 == 0x01) {
    Serial.println("CARD RESPONDED. The card and wiring are fine.");
    return true;
  }
  if (r1 == 0xFF) {
    Serial.println("silence. No card is reachable on these pins.");
    return false;
  }
  Serial.println("something answered but not idle state.");
  return false;
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

  File root = SD.open("/");
  if (root) {
    Serial.println("  root listing:");
    File e = root.openNextFile();
    int n = 0;
    while (e && n < 12) {
      Serial.printf("    %s%s\n", e.isDirectory() ? "[dir] " : "      ", e.name());
      e = root.openNextFile();
      n++;
    }
    if (n == 0) Serial.println("    (empty)");
    root.close();
  }
  SD.end();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(600);

  banner("SD DIAGNOSTIC");
  Serial.println("ALL FOUR WIRES MUST BE CONNECTED for this to mean anything:");
  Serial.printf("  SD_SCK->%d  SD_MOSI->%d  SD_MISO->%d  SD_CS->%d\n",
                SD_SCK, SD_MOSI, SD_MISO, SD_CS);

  SPIClass hspi(HSPI);
  SPIClass vspi(VSPI);

  banner("A. LINE CHECK, dedicated pins");
  lineCheck(SD_SCK, SD_MOSI, SD_MISO, SD_CS);

  banner("B. RAW CARD PROBE, dedicated pins");
  Serial.println("Sends CMD0 by hand. This is the real health test.");
  const bool aliveA = rawProbe(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS);

  bool aliveB = false;
  if (!aliveA) {
    banner("C. RAW CARD PROBE, shared TFT bus");
    aliveB = rawProbe(vspi, TFT_SCK, TFT_MOSI, TFT_MISO, SD_CS);
  } else {
    Serial.println();
    Serial.println("Skipping the shared bus probe: the dedicated pins work and");
    Serial.println("probing another bus on the same CS would disturb the card.");
  }

  bool mounted = false;

  if (aliveA) {
    banner("D. MOUNT, dedicated pins");
    hspi.end();
    delay(300);
    mounted |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 400000);
    if (!mounted) {
      delay(300);
      mounted |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 1000000);
    }
    if (!mounted) {
      delay(300);
      mounted |= tryMount(hspi, SD_SCK, SD_MOSI, SD_MISO, SD_CS, 4000000);
    }
  }
  if (!mounted && aliveB) {
    banner("D. MOUNT, shared TFT bus");
    mounted |= tryMount(vspi, TFT_SCK, TFT_MOSI, TFT_MISO, SD_CS, 400000);
    if (!mounted) mounted |= tryMount(vspi, TFT_SCK, TFT_MOSI, TFT_MISO, SD_CS, 4000000);
  }

  banner("VERDICT");
  if (mounted) {
    Serial.println("Working. Note which bus succeeded above.");
  } else if (aliveA || aliveB) {
    Serial.println("The card ANSWERS but will not mount.");
    Serial.println("That is a filesystem problem, not wiring.");
    Serial.println("Reformat as FAT32 with a 32KB allocation unit.");
  } else {
    Serial.println("The card never answered CMD0 on either bus.");
    Serial.println("Nothing electrical is reaching a card. Check, in order:");
    Serial.println(" 1. Are all four wires actually connected right now?");
    Serial.println(" 2. Is the slot getting 3.3V? Measure it.");
    Serial.println(" 3. Are you on the correct silkscreen pads for the SD slot,");
    Serial.println("    not the T_ touch pads?");
    Serial.println(" 4. Is the card fully seated? These push-push slots");
    Serial.println("    latch, and a half-insert looks seated.");
    Serial.println(" 5. Try the card in a computer to confirm it is readable.");
  }
}

void loop() {
  delay(10000);
}
