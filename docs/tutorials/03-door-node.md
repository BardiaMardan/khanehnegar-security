# 3 · Build the door node

**You'll end with:** a sensor on your door that reports movement, forcing, impacts, and, silently, a hand on the handle.
**Time:** 30 minutes.

← [2 · SD card](02-sd-card.md) · Next: [4 · Laser node](04-laser-node.md) →

---

## How it works

- An **ADXL345 accelerometer** on the door leaf learns which way is "down" while the door is shut. When the door swings open, gravity shifts relative to the sensor. More than **10°** held for 400 ms is reported as door movement.
- A sharp spike over 1.3 g is an **impact**. Ten small shakes within 3 s is **forcing**.
- A wire on the metal handle connects to the ESP32's built-in **capacitive touch** channel. A touch is reported as a **silent** event: you get an alert, and the siren stays quiet.

## 1. Wire it

| ADXL345 | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | 21 |
| SCL | 22 |
| CS | 3V3 |

Connect a wire from **GPIO 27** to the metal door handle, or to a strip of copper tape behind a plastic one. Keep it short.

## 2. Flash

Use the same toolchain as the hub and **the same `SecMeshKeys.h`**.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/node-door
arduino-cli upload  --fqbn esp32:esp32:esp32 -p <port> firmware/node-door
```

## 3. Mount and power up

1. Fix the board and sensor to the **door itself**, not the frame.
2. **Shut the door**, then power the node.
3. Leave it still for 5 seconds. It's learning what "closed" looks like. Keep your hand off the handle for the first second too, while it calibrates the touch pad.

Serial output:

```
khanehnegar door node
door node mac: 24:6F:28:AA:BB:CC
```

## 4. Pair it

On the hub: **MANAGE** → enter the code (`1234` until you change it) → **PAIR MODE**.

The hub beeps twice and shows **MODULE PAIRED**. On STATUS, **MODULES** changes to `1/1`.

PAIR MODE turns itself off after 2 minutes. You don't need to reopen it when this node reboots: the hub recognises a node it has already adopted.

## 5. Test

With the system **disarmed**, open the door and touch the handle, then look at the **LOG** tab:

```
DOOR  DOOR MOVEMENT
DOOR  handle touched
```

Disarmed events are only logged. They never alarm.

## Tuning

Every threshold is a `#define` at the top of `node-door.ino`. See [configuration.md](../configuration.md#door-node-node-doorino). To see what the touch pad reads, set `TOUCH_TRACE 1` and reflash.

---

Next: [4 · Build the laser tripwire](04-laser-node.md) →
