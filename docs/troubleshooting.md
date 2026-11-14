# Troubleshooting & FAQ

Open the serial monitor at **115200 baud** first. Every board explains its boot there.

## Hub

**`sd: not mounted`**
- Is the card FAT32? exFAT cards (most over 32 GB) won't mount. Reformat as FAT32.
- Is SD MISO on GPIO **32**? GPIO 34–39 have no pull-up and the card stays silent.
- Is the module on its own pins (14/13/32/27)? Don't share the display's SD slot or bus.
- The hub tries 400 kHz, 1 MHz, and 4 MHz. If none work, it's wiring or power.
- The hub still runs without a card, using `secrets.h`.

**`fingerprint: NOT RESPONDING`**
- TX and RX are crossed: the sensor's TX goes to GPIO 16, and its RX to GPIO 17.
- The sensor needs 3.3 V or 5 V depending on the model. Check its label.
- Keep `FP_TOUCH_GATE 0` unless the touch wire is connected. An unwired touch pin disables the sensor.

**The touch screen responds in the wrong place**
Touch calibration is stored once. To redo it, erase flash (`arduino-cli upload ... --erase` or *Tools → Erase All Flash* in the IDE) or change `UI_ROTATION`.

**White screen**
`TFT_eSPI` is using its own `User_Setup.h`. Copy [`TFT_eSPI_User_Setup.h`](../TFT_eSPI_User_Setup.h) over the library's `User_Setup.h`. See [tutorial 1](tutorials/01-hub.md#3-configure-tft_espi).

**LINK shows DOWN**
- The hub supports **2.4 GHz** Wi-Fi only.
- Check `wifi_ssid` and `wifi_pass` on the card. Watch the serial log at boot.

**LINK shows NET, but nothing reaches the dashboard**
- `server_url` must be reachable *from the hub*: use the computer's LAN IP, not `localhost`.
- The dashboard must listen on all interfaces. `npm run dev` and `npm start` already use `-H 0.0.0.0`.
- `api_key` must match `HUB_API_KEY` exactly. A mismatch shows as `401` in the dashboard's terminal.
- Check the computer's firewall allows port 3000.

**The clock shows `--:--`**
The time comes from the dashboard's `/api/time`. It appears once the hub reaches the dashboard.

**"LOCKED, USE FINGER"**
5 wrong codes. Clear it with a known finger, the dashboard's **unlock**, or `/unlock` in the bot. Power cycling doesn't clear it, by design.

**I forgot the code and have no finger enrolled**
Erase the hub's flash and reflash. That resets the code to `1234`, and also forgets nodes, fingers, and the touch calibration.

## Nodes

**A node never pairs**
1. Open **MANAGE → PAIR MODE** on the hub. It closes after 2 minutes.
2. Make sure every board was built with the **same** `SecMeshKeys.h`.
3. The node hops channels every 400 ms. Give it a few seconds.
4. The hub holds 6 nodes at most. Use **FORGET NODES** if it's full.

**Modules show OFFLINE right after the hub boots**
That's expected. Adopted nodes start offline and go ONLINE on their first frame.

**A node flaps between ONLINE and OFFLINE**
It's at the edge of radio range, or on a different channel from the hub's Wi-Fi. Move it closer, or reboot it so it hunts again.

**Door node: `ADXL345 NOT FOUND`**
Check SDA 21 and SCL 22, and that CS is tied to 3V3 so the chip is in I²C mode.

**Door node: movement isn't detected**
It learns "closed" after 4 s of stillness at boot. Power it with the door shut and don't touch it. Lower `MOVE_DEG` for doors that open only slightly.

**Door node: the handle touch fires on its own, or never**
Set `TOUCH_TRACE 1`, flash it, and watch `touch <value> base <baseline> trip <threshold>`. Long or unshielded wires pick up noise. Shorten the wire, or raise `TOUCH_MIN_DROP`.

**Laser node: `WARNING: weak beam`**
Aim the laser directly onto the LDR, and shade the LDR from room light with a short tube. Contrast should be well over 300.

**Laser node LED**
Slow blink means looking for the hub. Solid means paired and the beam is intact. Fast blink means the beam is broken. Off means no power.

**Re-pairing a laser node with a different hub**
Hold **BOOT** (GPIO 0) while it powers up. The LED flickers and the claim is cleared.

## Dashboard

**`npm ci` fails building `better-sqlite3`**
You're on a Node.js version with no prebuilt binary. Use **Node 22 LTS** (`nvm use` in `dashboard/` reads `.nvmrc`).

**Login says wrong email or password**
Run `npm run setup` once. It creates the tables and the account from `ADMIN_EMAIL` and `ADMIN_PASSWORD`. To change the password, delete `data/auth.db` and run it again.

**Logged out immediately in production**
In production, cookies are `Secure` and need HTTPS. Either serve over HTTPS or run `npm run dev` on a trusted LAN. See [deployment.md](deployment.md).

**No Telegram messages**
- Send your bot any message first. Bots can't start a chat.
- Check the chat ID. See [tutorial 6](tutorials/06-alerts.md).
- Telegram may be blocked on your network. Bale is there for that case.

**Fingerprint removal says "the terminal did not answer"**
The popup appears on the hub only while the **MANAGE** tab is unlocked. Unlock it with the code, then press remove again on the web.

## FAQ

**Why not Home Assistant / ESPHome?**
They're great for automation, but a security system has different priorities. Alarms are decided on the hub with no server involved. Silent and loud evidence are treated differently. Credentials can't be changed remotely without consent at the panel. It works with the internet down. See [architecture.md](architecture.md#design-decisions).

**Can I add more sensors?**
Yes. Up to 6 nodes, and the protocol already reserves codes for windows, PIR, glass break, and RF decoys. See [protocol.md](protocol.md#writing-a-new-node).

**Does it need the internet?**
No. Detection, the siren, and the panel work offline. The dashboard can run on your LAN, and only the bot alerts need the internet.

**Why is the dashboard Persian by default?**
It was built for Persian-speaking users. Switch language with the button in the header, or open `/en`.
