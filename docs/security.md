# Security model

This page sets out what khanehnegar defends against, how it does it, and where it stops. It's written to be argued with. If you find a gap that isn't listed here, see [SECURITY.md](../SECURITY.md).

## Assets

1. **The house.** Detect an intrusion and respond: siren, alert, or both.
2. **The right to disarm.** The keypad code, the enrolled fingers, and the dashboard login.
3. **The integrity of what the hub hears.** A node's report must be genuine and fresh.

## Adversaries considered

| Adversary | Capability |
|---|---|
| **Opportunistic intruder** | Physical access to doors and windows. No knowledge of the system. |
| **Informed intruder** | Knows it's an ESP32 system. Has an ESP32 and a laptop. Can sniff and transmit 2.4 GHz. |
| **Insider without the code** | Can stand at the panel and try codes and fingers. |
| **Remote attacker** | Can reach the dashboard, if you exposed it. |

Out of scope: a determined attacker with professional RF gear, nation-state anything, and supply-chain attacks on the boards.

---

## What it defends against, and how

| Threat | Defence | Where |
|---|---|---|
| Door opened or forced | Accelerometer tilt, impact, and agitation. Each one reaches the alarm threshold on its own. | `node-door.ino`, `state.cpp` |
| Beam crossed | A strobed laser: the beam is measured lit *minus* dark, so a torch aimed at the sensor can't mask a break | `node-laser.ino` |
| Intruder learning they were seen | **Silent** evidence (handle touch, sabotage, code attacks) alerts your phone but can never sound the siren | `scoreFor()` |
| Sniffing node traffic | ESP-NOW CCMP encryption on every unicast frame (ESP32) | SecMesh |
| Forged node frames | Encryption, plus the hub accepts frames only from adopted MACs | `Nodes::find` |
| Replaying a captured "all clear" or heartbeat | Strictly increasing per-node counter | `Nodes::acceptCounter` |
| Rogue node joining | Adoption only while PAIR MODE is open: 2 minutes, behind the code | `Nodes::adopt` |
| Destroying or unplugging a node | Heartbeat every 5 s, *MODULE LOST* alarm after 13 s | `Nodes::tick` |
| A lost radio packet hiding an alarm | Every urgent frame is sent 3×, de-duplicated by counter | `sendUrgent` |
| Guessing the keypad code | 5 tries, then **lockdown**. The keypad is refused until a known finger or `/unlock` clears it, and the latch survives power cycling. | `Access::verify` |
| Reading the code out of flash | Only a salted SHA-256 is stored | `access.cpp` |
| Timing side-channel on the code | Constant-time comparison | `sameHash` |
| Trying random fingers | 3 unknown in 60 s, or **1** while armed AWAY, raises a silent code-attack alert | `onUnknownFinger` |
| Power cut to disarm | The armed state is persisted and restored as AWAY | `Sec::begin` |
| Pulling the SD card | Codes, roster, mode, and lockdown all live in NVS. The card holds only network config and an archive. | `fingers.cpp` |
| Stolen dashboard password removing your fingerprint | Destructive web actions must be **approved at the panel** by someone who has the code | `/api/fingers/remove` |
| Dashboard brute force | Sign-up disabled, 10 attempts/min, 12 h sessions, secure cookies in production | `auth-config.js` |
| Forged hub calls to the dashboard | `X-Api-Key`, compared in constant time | `apikey.js` |
| A stranger messaging your bot | Commands are accepted only from the configured chat ID | `notify.js` |
| Siren timer revealing how long to wait | The UI shows countdowns for the exit and entry delays only, never for the alarm | `countdownMs()` |

---

## What it does **not** defend against

Stated plainly so nobody relies on it for more than it gives.

### RF jamming
A jammer on 2.4 GHz silences every node. The hub notices within **13 seconds** (MODULE LOST, a silent alert), but it can only tell you if its own Wi-Fi isn't jammed too. Event codes for jam and deauth detection are reserved (`EV_RF_JAM`, `EV_RF_DEAUTH`), but no reference node emits them yet.

### Power loss
The reference build has no battery. Cutting the house's power silences everything, and the next boot restores AWAY. Use a power bank or UPS on the hub and router.

### Internet or Wi-Fi outage
Local detection and the siren keep working. Alerts and remote commands don't, and there is no cellular fallback. Alerts raised while offline are journaled on the hub but **not re-sent** later.

### ESP8266 nodes
ESP8266 ESP-NOW doesn't interoperate reliably with ESP32 encrypted unicast. Pairing frames get through, but every encrypted frame after that was dropped, which left the node paired and mute. So ESP8266 nodes run **plaintext unicast**. Those frames are still counter-checked and MAC-filtered, so they can't be replayed or injected from an unknown MAC, but they **can be read** off the air. Both reference nodes are ESP32, so this applies only if you build an ESP8266 node.

### Shared keys
Every board on a site shares one PMK/LMK pair. Extracting the flash of *one* node (they sit on doors) reveals the keys for the whole site. Changing them means reflashing everything.

### MAC spoofing during PAIR MODE
While PAIR MODE is open, any device that sends a pair request is adopted. Open it only while you're pairing; it closes itself after 2 minutes. The hub re-accepts a MAC it already knows at any time, but a spoofed known MAC would still need the encryption keys to send anything the hub accepts.

### Replay right after a reboot
To keep a rebooted node from being locked out, counters of 8 or less are accepted. See [protocol.md](protocol.md#replay-protection). That's a window of 8 frames.

### The dashboard can disarm
Anyone logged in, and anyone in the configured bot chat, can arm, disarm, and clear a lockdown. Only credential removal needs the panel. Treat the dashboard password and the bot chat like a house key.

### Hub ↔ dashboard over plain HTTP
By default the API key and alerts cross your LAN unencrypted. Someone on your Wi-Fi could read the key. If the dashboard is anywhere but your own LAN, use HTTPS. See [deployment.md](deployment.md).

### Default code
The factory code is `1234`. Change it on day one. See [first run](tutorials/07-first-run.md#2-change-the-code).

### The hub's own tamper switch is off by default
`ENABLE_HUB_TAMPER 0`. Fit a switch and enable it if the panel is reachable.

---

## Hardening checklist

- [ ] Create `SecMeshKeys.h` with your own random keys (no `#warning` during the build)
- [ ] Change the keypad code from `1234`
- [ ] Use long random values for `HUB_API_KEY` and `BETTER_AUTH_SECRET`
- [ ] Use a strong dashboard password, and keep the bot chat private
- [ ] Keep the dashboard on your LAN, or behind HTTPS and a VPN
- [ ] Fit a hub tamper switch and set `ENABLE_HUB_TAMPER 1`
- [ ] Put the hub and router on a UPS
- [ ] Enrol at least two fingers per person: lockdown is cleared by a finger
- [ ] Mount nodes where they can't be reached without triggering another one
