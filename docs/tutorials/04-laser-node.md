# 4 · Build the laser tripwire

**You'll end with:** a laser beam across a hallway that raises the alarm when someone crosses it.
**Time:** 30 minutes, plus aiming.

← [3 · Door node](03-door-node.md) · Next: [5 · Dashboard](05-dashboard.md) →

---

## How it works

The node doesn't ask whether it's dark. It asks whether the beam is there. Every 60 ms it reads the light sensor once with the laser **off** and once with it **on**, and uses the difference. Room light shows up in both readings and cancels out, so a torch shone at the sensor can't hide a broken beam.

At boot it measures the normal difference. A crossing is a difference under **half** of that, for 3 scans in a row (about 180 ms).

## 1. Wire it

| Part | Connection |
|---|---|
| LDR | 3V3 → LDR → **GPIO 39**, plus 10 kΩ from GPIO 39 → GND |
| Laser module | S → **GPIO 16**, + → 3V3/5V, − → GND |
| Status LED | **GPIO 13** → resistor → LED → GND |
| Case switch (optional) | **GPIO 4** → switch → GND, closed when the lid is shut |

The reference board is a LilyGO T-Display, but any ESP32 works.

No case switch? Set `ENABLE_CASE_TAMPER 0`, or GPIO 4 will float and report the case open.

## 2. Aim

Put the laser on one side and the LDR on the other, or use a mirror to fold the beam back next to the node. A short black tube (a straw or heat-shrink) over the LDR blocks most room light.

## 3. Flash

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/node-laser
arduino-cli upload  --fqbn esp32:esp32:esp32 -p <port> firmware/node-laser
```

Serial output:

```
laser node mac: 24:6F:28:DD:EE:FF
beam contrast baseline: 1840
```

Under 300 prints `WARNING: weak beam`. Re-aim, then reset.

## 4. Pair it

**MANAGE → PAIR MODE** on the hub, as for the door node.

The LED shows the state:

| LED | Meaning |
|---|---|
| Slow blink | looking for the hub |
| Solid on | paired, beam intact |
| Fast blink | beam broken |
| Off | no power |

To move the node to a different hub, hold **BOOT** while powering up. The LED flickers and the node forgets its hub.

## 5. Test

Wave a hand through the beam. **LOG** shows `BEAM  beam cut`. If the system is armed, a single cut raises the alarm.

---

Next: [5 · Run the dashboard](05-dashboard.md) →
