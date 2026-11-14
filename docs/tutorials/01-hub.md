# 1 · Build and flash the hub

**You'll end with:** the touch screen showing the status page, the fingerprint sensor answering, and the SD card mounted.
**Time:** about 1 hour, including wiring.

← [Docs index](../../README.md#documentation) · Next: [2 · SD card](02-sd-card.md) →

---

## 1. Wire it

Follow the [hub wiring table](../hardware.md#hub-wiring). Before powering up, check that:

- the display's touch chip select goes to GPIO **5**, and the display's own CS to GPIO **2**
- SD MISO goes to GPIO **32**
- the fingerprint TX/RX are crossed (sensor TX → GPIO 16)

## 2. Install the toolchain

You can use the **Arduino IDE 2** or **arduino-cli**. Both use the same versions:

| Component | Version |
|---|---|
| ESP32 core (Espressif) | 3.3.x (CI uses 3.3.11) |
| TFT_eSPI | 2.5.43 |
| Adafruit Fingerprint Sensor Library | 2.1.4 |
| DHT sensor library | 1.4.7 |
| Adafruit Unified Sensor | 1.1.15 |

**Make the repository your sketchbook.** This lets the IDE find `libraries/SecMesh` with no copying, and keeps third-party libraries inside the repo (git ignores them).

<details open>
<summary><b>Arduino IDE 2</b></summary>

1. **File → Preferences → Sketchbook location** → the cloned `khanehnegar-security` folder.
2. In the same window, add this to *Additional boards manager URLs*:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Boards Manager** → install **esp32 by Espressif Systems**.
4. **Library Manager** → install the four libraries in the table above.
5. Restart the IDE.

</details>

<details>
<summary><b>arduino-cli</b></summary>

```bash
cd khanehnegar-security
arduino-cli config set directories.user "$PWD"
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "TFT_eSPI@2.5.43" "Adafruit Fingerprint Sensor Library@2.1.4" \
                        "DHT sensor library@1.4.7" "Adafruit Unified Sensor@1.1.15"
```

</details>

## 3. Configure TFT_eSPI

TFT_eSPI takes its pins from a header inside the library. Replace it with this project's:

```bash
cp TFT_eSPI_User_Setup.h libraries/TFT_eSPI/User_Setup.h
```

Repeat this after every TFT_eSPI update. If you skip it, the screen stays white.

## 4. Create your secrets

```bash
cp firmware/hub/secrets.example.h firmware/hub/secrets.h
cp libraries/SecMesh/src/SecMeshKeys.example.h libraries/SecMesh/src/SecMeshKeys.h
```

- `secrets.h`: fallback Wi-Fi and server values, used only when the SD card is missing. You can leave them empty.
- `SecMeshKeys.h`: **replace both keys** with your own 16-character strings. Every board on your site must be flashed with this same file.

  ```bash
  openssl rand -base64 12   # prints exactly 16 characters, run it twice
  ```

Both files are git-ignored.

## 5. Flash

<details open>
<summary><b>Arduino IDE 2</b></summary>

Open `firmware/hub/hub.ino`. Select **Tools → Board → ESP32 Dev Module** and your port, then press **Upload**.

</details>

<details>
<summary><b>arduino-cli</b></summary>

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/hub
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/hub   # macOS: /dev/cu.usbserial-*
```

</details>

If the build prints `#warning "SecMesh: using the example keys"`, step 4 didn't work.

## 6. First boot

1. **Touch calibration** (first boot only): touch each corner marker as it appears.
2. **Splash screen**: it walks through *storage → radio → fingerprint → sensors → network → ready*.
3. **Status page**: it shows `DISARMED` and `system ready`.

Open the serial monitor at 115200 baud and check:

```
khanehnegar hub booting
sd: 400000 Hz ... mounted
sd card: ok
hub mac: 24:6F:28:XX:XX:XX
fingerprint: ok, 0 templates, 0 enrolled
```

`sd card: MISSING` is fine for now. The next tutorial covers it. For anything else, see [troubleshooting](../troubleshooting.md#hub).

## The four pages

| Tab | What it does |
|---|---|
| **STATUS** | Mode, message, modules online, Wi-Fi link, temperature, humidity, and the ARM / DISARM / CANCEL button |
| **CODE** | Keypad to disarm. **CODE** in the corner changes the code. |
| **LOG** | The last 12 events. Silent ones are highlighted. |
| **MANAGE** | Behind the code: PAIR MODE, FORGET NODES, ADD FINGER, WIPE FINGERS |

After 60 s idle, the screen switches to a standby clock. Touch it to wake.

---

Next: [2 · Prepare the SD card](02-sd-card.md) →
