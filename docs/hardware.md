# Hardware

Everything here is cheap, common, and solderless. The reference build was assembled on breadboards.

## Bill of materials

Prices are rough single-unit figures from hobby marketplaces. Use them for scale, not as a quote.

### Hub

| Part | Notes | Approx. cost |
|---|---|---|
| ESP32 DevKit (ESP32-WROOM-32, 4 MB) | Any 38-pin or 30-pin board with GPIO 2, 4, 5, 13–19, 21–23, 25, 27, 32, 33 broken out | $5 |
| 2.8" ILI9341 SPI TFT with XPT2046 resistive touch, 240×320 | The red "SPI TFT" boards. Its SD slot is **not** used. | $9 |
| R307 / R308 optical fingerprint sensor (UART, 57600 baud) | Any Adafruit-Fingerprint-compatible module | $10 |
| microSD module (SPI) + FAT32 card, any size | A separate module on its own bus | $1 |
| DHT11 temperature/humidity sensor | | $1 |
| Active buzzer, 5 V or 3.3 V | Doubles as the siren. Drive it through a transistor for a louder one. | $0.50 |
| Tamper switch (optional) | Microswitch on the case lid. Disabled by default (`ENABLE_HUB_TAMPER 0`). | $0.50 |

### Door node

| Part | Notes |
|---|---|
| ESP32 DevKit | Must be an ESP32. The touch pad uses its built-in capacitive touch channel. |
| ADXL345 accelerometer (I²C) | Address 0x53 or 0x1D, found automatically |
| A wire or metal plate on the door handle | Connected to GPIO 27, the silent touch trigger |

### Laser node

| Part | Notes |
|---|---|
| LilyGO T-Display (ESP32) | Any ESP32 works. The pinout was chosen so every signal sits on one header row of the T-Display. |
| 5 V laser diode module (KY-008 type) | Switched directly from GPIO 16 |
| LDR (photoresistor) + 10 kΩ resistor | Voltage divider into GPIO 39 |
| Tamper switch (optional) | GPIO 4 to GND, closed when the case is shut |
| LED + resistor | Status, on GPIO 13 |

---

## Hub wiring

The screen and touch controller share the VSPI bus. The SD card gets its own HSPI bus, because sharing one bus with the display was unreliable.

| Peripheral | Signal | ESP32 GPIO |
|---|---|---|
| **TFT (ILI9341)** | MOSI | 23 |
| | MISO | 19 |
| | SCLK | 18 |
| | CS | 2 |
| | DC | 4 |
| | RST | 15 |
| | LED (backlight) | 3V3 |
| **Touch (XPT2046)** | T_CS | 5 |
| | T_DIN / T_DO / T_CLK | shared with TFT MOSI / MISO / SCLK |
| **microSD (HSPI)** | SCK | 14 |
| | MOSI | 13 |
| | MISO | **32** |
| | CS | 27 |
| **Fingerprint (UART2)** | sensor TX → ESP32 RX | 16 |
| | sensor RX ← ESP32 TX | 17 |
| | touch/wake (unused by default) | 22 |
| **DHT11** | DATA | 33 |
| **Buzzer** | + | 21 |
| **Tamper switch** | to GND | 25 |

Display pins come from [`TFT_eSPI_User_Setup.h`](../TFT_eSPI_User_Setup.h). Everything else comes from [`firmware/hub/config.h`](../firmware/hub/config.h).

> **Why SD MISO is on 32 and not 35:** GPIO 34–39 have no internal pull-up, and SD MISO needs one.
>
> **Why the fingerprint touch pin is ignored:** with `FP_TOUCH_GATE 0` the hub polls the sensor every 120 ms. An unwired touch pin reads low and silently disabled the sensor, so polling is the default.

## Door node wiring

| Peripheral | Signal | ESP32 GPIO |
|---|---|---|
| ADXL345 | SDA | 21 |
| | SCL | 22 |
| | VCC / GND | 3V3 / GND |
| | CS | 3V3 (selects I²C) |
| Handle pad | wire to the metal handle | 27 (touch channel T7) |

Mount the accelerometer **on the door leaf** and power it up with the door shut. It learns its "closed" orientation after 4 seconds of stillness. A 10° tilt held for 400 ms counts as the door moving.

## Laser node wiring

| Peripheral | Signal | ESP32 GPIO |
|---|---|---|
| LDR divider | midpoint | 39 (SVN, ADC1) |
| Laser module | S | 16 |
| Status LED | anode via resistor | 13 |
| Case tamper | to GND | 4 |
| Unpair button | to GND, held at boot | 0 (the BOOT button) |

> **Why the LDR is on an ADC1 pin:** ADC2 stops working while Wi-Fi or ESP-NOW is running.

The LDR goes from 3V3 to GPIO 39, and the 10 kΩ resistor from GPIO 39 to GND. Then aim the laser so it lands on the LDR. At boot, the node measures the beam contrast and prints `beam contrast baseline: N`. Anything under 300 prints a warning.

## Power

Each board runs from USB 5 V. The reference build has **no battery backup**. See [security.md](security.md#what-it-does-not-defend-against).
