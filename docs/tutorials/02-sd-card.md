# 2 · Prepare the SD card

**You'll end with:** the hub on your Wi-Fi, ready to talk to the dashboard.
**Time:** 5 minutes.

← [1 · Hub](01-hub.md) · Next: [3 · Door node](03-door-node.md) →

---

## 1. Format

Use any microSD card, formatted **FAT32**. Cards over 32 GB usually come as exFAT, so reformat them:

- **Windows**: use a tool like *guiformat*, because Windows won't format large cards as FAT32
- **macOS**: `diskutil eraseDisk FAT32 KHANEH MBRFormat /dev/diskN`
- **Linux**: `sudo mkfs.vfat -F 32 /dev/sdX1`

## 2. Copy the folder

Copy [`sdcard-example/khaneh/`](../../sdcard-example/khaneh/) to the **root** of the card, so you end up with:

```
/khaneh/config.ini
/khaneh/access.ini
```

## 3. Edit `config.ini`

```ini
wifi_ssid=YourHomeWiFi
wifi_pass=YourWiFiPassword

server_url=http://192.168.1.50:3000
api_key=paste-the-same-value-as-HUB_API_KEY

site_name=Home
timezone=Asia/Tehran
```

- `wifi_ssid`: must be a **2.4 GHz** network.
- `server_url`: the **LAN IP** of the computer that will run the dashboard, not `localhost`. Find it with `ipconfig getifaddr en0` (macOS), `hostname -I` (Linux), or `ipconfig` (Windows).
- `api_key`: make one now and keep it for [tutorial 5](05-dashboard.md):

  ```bash
  openssl rand -hex 32
  ```

Every key is explained in [configuration.md](../configuration.md#sd-card-khanehconfigini).

## 4. `access.ini`

Leave it as it is, or delete the example lines. It only matters if your fingerprint sensor already has enrolled templates. See [configuration.md](../configuration.md#sd-card-khanehaccessini).

## 5. Boot

Insert the card and reset the hub. The serial log should show `sd card: ok`. Once the hub is on Wi-Fi, the status page's **LINK** tile shows **NET**.

> **LINK shows NET but the clock stays at `--:--`?** That's expected until the dashboard is running. The hub gets its time from it.

The hub also writes its full event history to `/khaneh/events.csv`.

---

Next: [3 · Build the door node](03-door-node.md) →
