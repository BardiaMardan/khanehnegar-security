#include <SPI.h>
#include <SD.h>

#define SD_CS    27
#define SD_MOSI  13
#define SD_MISO  32
#define SD_SCK   14

static SPIClass bus(HSPI);

static void banner(const char *s) {
  Serial.println();
  Serial.println("----------------------------------------");
  Serial.println(s);
  Serial.println("----------------------------------------");
}

static void lineCheck() {
  Serial.printf("cs=%d mosi=%d miso=%d sck=%d\n", SD_CS, SD_MOSI, SD_MISO, SD_SCK);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(SD_MISO, INPUT_PULLDOWN);
  delay(10);
  const int held = digitalRead(SD_MISO);
  pinMode(SD_MISO, INPUT);

  Serial.printf("miso against a pulldown = %d\n", held);
  if (held) {
    Serial.println("something external is driving miso high, module has power");
  } else {
    Serial.println("nothing is driving miso");
    Serial.println("check the miso wire and that 3V3 reaches the module");
  }
}

static uint8_t cmd(uint8_t index, uint32_t arg, uint8_t crc) {
  bus.transfer(0xFF);
  bus.transfer(0x40 | index);
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

static bool probe() {
  bus.end();
  bus.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  bus.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 12; i++) bus.transfer(0xFF);
  digitalWrite(SD_CS, LOW);
  const uint8_t r1 = cmd(0, 0, 0x95);
  digitalWrite(SD_CS, HIGH);
  bus.endTransaction();

  Serial.printf("CMD0 reply = 0x%02X   ", r1);
  if (r1 == 0x01) { Serial.println("card answered, wiring is good"); return true; }
  if (r1 == 0xFF) { Serial.println("silence, nothing reachable"); return false; }
  Serial.println("unexpected reply");
  return false;
}

static bool mountAt(uint32_t hz) {
  bus.end();
  delay(250);
  bus.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(20);

  const bool ok = SD.begin(SD_CS, bus, hz);
  Serial.printf("mount %8lu Hz : %s\n", (unsigned long)hz, ok ? "OK" : "fail");
  return ok;
}

static void report() {
  const uint8_t t = SD.cardType();
  Serial.printf("type      : %s\n", t == CARD_NONE ? "NONE"
                                  : t == CARD_MMC  ? "MMC"
                                  : t == CARD_SD   ? "SDSC"
                                  : t == CARD_SDHC ? "SDHC" : "UNKNOWN");
  Serial.printf("size      : %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  Serial.printf("used      : %llu MB\n", SD.usedBytes() / (1024ULL * 1024ULL));

  File root = SD.open("/");
  if (root) {
    Serial.println("root      :");
    File e = root.openNextFile();
    int n = 0;
    while (e && n < 16) {
      Serial.printf("            %s%s\n", e.isDirectory() ? "[dir] " : "      ", e.name());
      e = root.openNextFile();
      n++;
    }
    if (!n) Serial.println("            (empty)");
    root.close();
  }

  if (SD.exists("/khaneh/config.ini")) Serial.println("config    : /khaneh/config.ini found");
  else Serial.println("config    : /khaneh/config.ini NOT found, copy sdcard-example");

  File w = SD.open("/sdtest.txt", FILE_WRITE);
  if (!w) { Serial.println("write     : FAILED"); return; }
  w.println("khanehnegar write test");
  w.close();

  File r = SD.open("/sdtest.txt", FILE_READ);
  if (r) {
    Serial.printf("readback  : %s", r.readString().c_str());
    r.close();
  }
  SD.remove("/sdtest.txt");
  Serial.println("write     : OK");
}

void setup() {
  Serial.begin(115200);
  delay(600);

  banner("EXTERNAL SD MODULE TEST");
  Serial.println("power the module from 3V3, leave its 5V pin unconnected");

  banner("1. LINE CHECK");
  lineCheck();

  banner("2. RAW PROBE");
  const bool alive = probe();

  banner("3. MOUNT");
  bool ok = false;
  if (alive) {
    const uint32_t rates[4] = {400000, 1000000, 4000000, 8000000};
    for (uint8_t i = 0; i < 4 && !ok; i++) ok = mountAt(rates[i]);
  } else {
    Serial.println("skipped, card never answered");
  }

  banner("4. RESULT");
  if (ok) {
    report();
    Serial.println();
    Serial.println("working, flash firmware/hub next");
    SD.end();
  } else if (alive) {
    Serial.println("card answers but will not mount");
    Serial.println("reformat FAT32 with an MBR partition table");
  } else {
    Serial.println("no card reachable");
    Serial.println(" 1. module on 3V3, not 5V");
    Serial.println(" 2. all four signal wires seated");
    Serial.println(" 3. card pushed in until it clicks");
    Serial.println(" 4. try 5V if 3V3 gives nothing");
  }
}

void loop() {
  delay(10000);
}
