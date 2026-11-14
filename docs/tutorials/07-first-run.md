# 7 · First run

**You'll end with:** a secured system: your own code, your fingers enrolled, nodes paired, and one real alarm tested end to end.
**Time:** 15 minutes.

← [6 · Alerts](06-alerts.md) · [Back to the README](../../README.md)

---

## 1. Check the status page

| Tile | Should show |
|---|---|
| Mode banner | `DISARMED`, with `system ready` |
| MODULES | `2/2` once both nodes are paired |
| LINK | `NET` |
| CLIMATE / HUMIDITY | real values |
| Clock (top right) | the real time |

## 2. Change the code

The factory code is **1234**. Change it before anything else.

1. **CODE** tab → press **CODE** in the top right corner.
2. Enter the **current** code → **OK**.
3. Enter the **new code** (4–8 digits) → **OK**.
4. Enter it **again** → **OK**.

You'll see **CODE CHANGED**, and a notice reaches your bots.

Five wrong codes in a row lock the keypad. Only a known finger, or `/unlock`, clears that. So enrol a finger next.

## 3. Enrol fingers

1. **MANAGE** → enter the code.
2. **ADD FINGER** → place your finger, lift it, and place it again when asked.
3. **FINGER ADDED**. It shows up as `finger 1`.
4. Rename it on the dashboard: **fingerprints → rename**.

Enrol **two fingers per person**: one finger can be cut, dirty, or wet. The roster holds 16.

A known finger on the sensor at any time **disarms** the system and clears a lockdown.

## 4. Pair the nodes

If you haven't already: **MANAGE → PAIR MODE**, then power up each node. See tutorials [3](03-door-node.md) and [4](04-laser-node.md).

## 5. Arm and test

1. **STATUS → ARM.** It reads `ARMING, LEAVE NOW` and you have **15 s**.
2. After that: `AWAY · armed`, with two beeps.
3. **Cross the laser.** The siren sounds at once, the banner reads `ALARM`, and your phone gets `ALARM ... beam cut`.
4. **Disarm** with a finger or the code.

Then the door path:

1. ARM again and wait.
2. **Open the door.** That starts the **20 s entry delay**: the banner reads `ENTRY`.
3. Disarm within 20 s and nothing happens. Wait, and the alarm sounds.

And the silent path:

1. ARM, then **touch the door handle** without opening it.
2. Nothing happens at the panel, but your phone gets a `SILENT ALARM ... handle touched`.

## 6. Test remote control

- Dashboard → **disarm**: the hub disarms within 5 s.
- Send `/arm` to your bot: the hub starts the exit delay.

## 7. Test a removal approval

1. Dashboard → fingerprints → **remove** one finger. It reads *waiting for the terminal*.
2. At the hub: **MANAGE** → code. A popup reads **THE WEB ASKS TO · remove …**.
3. **DENY**: the dashboard shows *refused at the terminal*. The finger stays.

## You're done

Go through the [hardening checklist](../security.md#hardening-checklist), and once you're happy with it, move the dashboard to an always-on machine with the [deployment guide](../deployment.md).
