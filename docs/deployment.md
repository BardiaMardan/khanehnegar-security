# Deployment: moving the dashboard off your laptop

The tutorials run the dashboard with `npm run dev` on your computer. That's fine for setting up. For a system that guards a house, the dashboard should run on something that stays on: a Raspberry Pi, an old PC, a NAS, or a VPS.

This guide moves an existing install without losing history or accounts.

## Choose where it runs

| Option | Pros | Cons |
|---|---|---|
| **Raspberry Pi / home server on your LAN** (recommended) | Nothing exposed to the internet. Works when the internet is down. Bots still work outbound. | You need the browser on the same network, or a VPN, to see it |
| **VPS** | Reachable from anywhere | Must be HTTPS. The hub's API key crosses the internet. One more thing to patch. |

The hub always **calls out** to the dashboard, so the dashboard never needs to reach the hub. A VPS works without opening ports at home.

## 1. Prepare the new machine

You need Node.js **22 LTS** and build tools for `better-sqlite3`:

```bash
# Debian / Ubuntu / Raspberry Pi OS
sudo apt update && sudo apt install -y git build-essential python3
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt install -y nodejs
node -v   # v22.x
```

## 2. Copy the code and your data

On the new machine:

```bash
sudo useradd --system --create-home --home-dir /opt/khanehnegar khaneh
sudo -u khaneh git clone https://github.com/BardiaMardan/khanehnegar-security.git /opt/khanehnegar/app
```

**Stop the old dashboard first**, so `auth.db` isn't half-written. Then copy the state from the old machine:

```bash
# from the old machine
scp dashboard/.env.local  newhost:/tmp/
scp -r dashboard/data     newhost:/tmp/
```

```bash
# on the new machine
sudo mv /tmp/.env.local /opt/khanehnegar/app/dashboard/
sudo mv /tmp/data       /opt/khanehnegar/app/dashboard/
sudo chown -R khaneh: /opt/khanehnegar/app/dashboard
```

The `data/` folder holds everything stateful:

| File | Lose it and… |
|---|---|
| `auth.db` | you must re-run `npm run setup` to recreate the login |
| `events.json` | the event history is gone (the hub's SD card still has `events.csv`) |
| `fingers.json` | it's rebuilt the next time the hub reconnects |
| `commands.json` | at most, one pending command is lost |

Starting fresh instead? Skip copying `data/`, and run `npm run setup` after step 3.

## 3. Build

```bash
cd /opt/khanehnegar/app/dashboard
sudo -u khaneh npm ci
sudo -u khaneh npm run build
```

Update `.env.local` for the new address:

```ini
BETTER_AUTH_URL=http://192.168.1.60:3000     # or https://alarm.example.com
```

## 4. Run it as a service

`/etc/systemd/system/khanehnegar.service`:

```ini
[Unit]
Description=khanehnegar dashboard
After=network-online.target
Wants=network-online.target

[Service]
User=khaneh
WorkingDirectory=/opt/khanehnegar/app/dashboard
EnvironmentFile=/opt/khanehnegar/app/dashboard/.env.local
Environment=NODE_ENV=production
ExecStart=/usr/bin/npm start
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now khanehnegar
journalctl -u khanehnegar -f
```

## 5. HTTPS

In production, login cookies are `Secure`, so **browsers only keep them over HTTPS**. Pick one:

- **Caddy** (the simplest, with automatic certificates for a public domain):
  ```
  alarm.example.com {
      reverse_proxy 127.0.0.1:3000
  }
  ```
- **LAN only, no domain**: put it behind Tailscale/WireGuard and use `tailscale serve`, or a reverse proxy with a local certificate.

Set `BETTER_AUTH_URL` to the exact `https://` URL you open in the browser.

> **About the hub:** the ESP32's `HTTPClient` is used without certificate checks, so keep the hub talking to the dashboard over the **LAN address** (`http://192.168.x.x:3000`), even if browsers use HTTPS through the proxy. On a VPS that isn't possible, so the hub's traffic, including the API key, crosses the internet. Run a VPN, or accept that risk knowingly. See [security.md](security.md#hub--dashboard-over-plain-http).

## 6. Point the hub at the new address

Edit `/khaneh/config.ini` on the hub's SD card:

```ini
server_url=http://192.168.1.60:3000
```

Put the card back and reset the hub. On the status page, **LINK** should show **NET**, and the clock should set itself within a few seconds.

## 7. Check it

- [ ] The browser opens the new URL and you can log in
- [ ] The old events are in the feed
- [ ] Pressing **arm** on the dashboard arms the hub within 5 s
- [ ] A test alarm reaches Telegram/Bale
- [ ] The service comes back after `sudo reboot`

## Updating later

```bash
cd /opt/khanehnegar/app
sudo -u khaneh git pull
cd dashboard
sudo -u khaneh npm ci
sudo -u khaneh npm run auth:migrate      # applies any new auth tables
sudo -u khaneh npm run build
sudo systemctl restart khanehnegar
```

Back up `dashboard/data/` and `.env.local` before every update.
