# Configuration reference

Configuration lives in four places:

| Where | What | Changed by |
|---|---|---|
| `/khaneh/config.ini` on the SD card | Wi-Fi, server, API key, site name | Editing the card |
| `firmware/hub/secrets.h` | Fallback values when there's no card | Reflashing the hub |
| `libraries/SecMesh/src/SecMeshKeys.h` | ESP-NOW keys | Reflashing **every** board |
| `firmware/hub/config.h`, node `#define`s | Pins, timings, thresholds | Reflashing |
| `dashboard/.env.local` | Dashboard secrets, bots, time zone | Restarting the dashboard |

---

## SD card: `/khaneh/config.ini`

Plain `key=value`, one per line. `#` and `;` start comments. A ready example is in [`sdcard-example/khaneh/`](../sdcard-example/khaneh/).

| Key | Example | Meaning |
|---|---|---|
| `wifi_ssid` | `HomeWiFi` | 2.4 GHz network for the hub. Leave it empty to run with no network at all. |
| `wifi_pass` | | |
| `server_url` | `http://192.168.1.50:3000` | The dashboard, with no trailing slash |
| `api_key` | | Must equal `HUB_API_KEY` |
| `site_name` | `Home` | Shown in every alert |
| `timezone` | `Asia/Tehran` | Informational only. The clock offset comes from the dashboard's `SITE_TIMEZONE`. |

If the card is missing, or a key is absent, the hub uses `FALLBACK_*` from `secrets.h`.

## SD card: `/khaneh/access.ini`

`<sensor id>=<label>`, one per line. It's read **once**: on the first boot, or after the hub's roster has been wiped. It's useful when moving a fingerprint sensor that already has enrolled templates. After that, the hub owns the roster in flash and rewrites this file as a mirror.

On first boot with an empty roster, the hub also *adopts* any templates already in the sensor, and labels them `finger N`.

## SD card: `/khaneh/events.csv`

Written by the hub: `epoch,kind,event,value,silent`. It's append-only and never rotated.

## `firmware/hub/secrets.h`

Copy `secrets.example.h`. Every value is a fallback for the matching `config.ini` key.

```c
#define FALLBACK_WIFI_SSID   ""
#define FALLBACK_WIFI_PASS   ""
#define FALLBACK_SERVER_URL  ""
#define FALLBACK_API_KEY     ""
```

## `libraries/SecMesh/src/SecMeshKeys.h`

Copy `SecMeshKeys.example.h`. Both keys must be **exactly 16 characters**; the build fails otherwise. Without this file, the build uses the public example keys and prints a warning.

```bash
# two random 16-character keys
openssl rand -base64 12; openssl rand -base64 12
```

---

## `firmware/hub/config.h`

| Group | Constant | Default | Meaning |
|---|---|---|---|
| Keypad | `PIN_LENGTH` | 4 | Minimum length for a new code |
| | `DEFAULT_PIN` | `"1234"` | Set on first boot only. **Change it.** |
| | `MAX_PIN_ATTEMPTS` | 5 | Wrong codes before lockdown |
| Sessions | `MANAGE_SESSION_MS` | 60 s | The MANAGE tab locks again after this long idle |
| | `APPROVAL_TIMEOUT_MS` | 30 s | Time to answer a web approval at the terminal |
| Fingerprint | `FP_MAX_TEMPLATES` | 120 | Sensor capacity |
| | `FP_MIN_SCORE` | 50 | Minimum match confidence |
| | `FP_SLOTS` | 16 | People in the roster |
| | `FP_LABEL_LEN` | 16 | Max label length, including the terminator |
| | `FP_TOUCH_GATE` | 0 | 1 = wait for the sensor's touch pin, 0 = poll |
| | `FP_POLL_MS` | 120 | Poll period |
| Delays | `EXIT_DELAY_MS` | 15 s | Time to leave after pressing ARM |
| | `ENTRY_DELAY_MS` | 20 s | Time to disarm after the door moves in AWAY |
| | `ALARM_DURATION_MS` | 3 min | Siren length, then back to AWAY |
| | `UNKNOWN_FINGER_LIMIT` / `_WINDOW_MS` | 3 / 60 s | Unknown fingers that count as an attack. In AWAY, the first one does. |
| Risk | `RISK_ALARM_THRESHOLD` | 70 | Score that raises an alarm |
| | `RISK_DECAY_PER_SEC` | 3 | |
| | `RISK_MAX` | 150 | Cap |
| | `ALERT_COOLDOWN_MS` | 60 s | Repeat alerts of the same event are suppressed |
| | `ENABLE_HUB_TAMPER` | 0 | Watch the case switch on GPIO 25 |
| Journal | `JOURNAL_CAPACITY` | 40 | Events kept in flash |
| | `JOURNAL_FLUSH_MS` | 3 s | Batches flash writes |
| Periods | `CLIMATE_PERIOD_MS` | 30 s | DHT11 read |
| | `TIME_SYNC_MS` | 1 h | `/api/time` |
| | `COMMAND_POLL_MS` | 5 s | `/api/command` |
| | `STANDBY_AFTER_MS` | 60 s | Idle time before the standby clock |
| | `UI_ROTATION` | 2 | Screen rotation (0–3). Changing it asks for touch calibration again. |
| Storage | `NVS_NAMESPACE` | `"khaneh"` | |

Event weights live in `scoreFor()` in `state.cpp`. See [architecture.md](architecture.md#risk-fusion).

## Door node (`node-door.ino`)

| Constant | Default | Meaning |
|---|---|---|
| `ALWAYS_UNCLAIM` | 1 | Forget the hub at boot and hunt for it. It rejoins in about a second without PAIR MODE. |
| `TOUCH_TRACE` | 0 | Print touch readings every 400 ms, for tuning |
| `IMPACT_G` | 1.30 | Deviation from 1 g that counts as an impact |
| `AGITATE_G` / `_COUNT` / `_WINDOW` | 0.09 / 10 / 3 s | Vibrations that count as forcing |
| `MOVE_DEG` / `MOVE_HOLD_MS` | 10° / 400 ms | Tilt that counts as the door moving |
| `MOVE_REST_DEG` | 4° | Back under this angle = closed again |
| `MOVE_REARM_MS` | 8 s | Minimum gap between two movement reports |
| `TOUCH_DROP_PCT` / `TOUCH_MIN_DROP` | 75 % / 4 | The touch reading must fall under 75 % of baseline, and by at least 4 |
| `TOUCH_REARM_MS` | 4 s | Minimum gap between two touch reports |
| `TOUCH_STUCK_MS` | 30 s | A touch held this long relearns the baseline |

## Laser node (`node-laser.ino`)

| Constant | Default | Meaning |
|---|---|---|
| `MIN_CONTRAST` | 300 | Warn at boot if the beam is weaker than this |
| `BREAK_CONFIRM` / `CLEAR_CONFIRM` | 3 / 5 | Consecutive scans to declare a break or a restore |
| `SAMPLE_MS` / `SETTLE_MS` | 60 / 15 ms | Scan period, and LDR settle time per strobe half |
| `ENABLE_CASE_TAMPER` | 1 | Watch GPIO 4 |

A break is a contrast under **half** the boot baseline.

## SecMesh (`SecMeshConfig.h`)

See [protocol.md](protocol.md#liveness).

---

## `dashboard/.env.local`

| Variable | Required | Meaning |
|---|---|---|
| `HUB_API_KEY` | yes | Shared secret with the hub's `api_key` |
| `SITE_TIMEZONE` | | IANA zone for the hub's clock and alert timestamps. Defaults to `Asia/Tehran`. |
| `BETTER_AUTH_SECRET` | yes | `openssl rand -base64 32` |
| `BETTER_AUTH_URL` | yes | The URL you open in the browser, e.g. `http://192.168.1.50:3000` |
| `ADMIN_EMAIL` / `ADMIN_PASSWORD` | for `npm run setup` | The single account. The password needs 8+ characters. |
| `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` | | Enable Telegram |
| `BALE_BOT_TOKEN` / `BALE_CHAT_ID` | | Enable Bale |

Generate a strong `HUB_API_KEY` with `openssl rand -hex 32`.
