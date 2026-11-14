# 5 · Run the dashboard

**You'll end with:** a web page in Persian or English showing every event the hub reports, with buttons to arm, disarm, panic, and lock.
**Time:** 10 minutes.

← [4 · Laser node](04-laser-node.md) · Next: [6 · Alerts](06-alerts.md) →

---

## 1. Requirements

- **Node.js 22 LTS.** Other majors may have no prebuilt `better-sqlite3` and fail to install. With nvm: `cd dashboard && nvm install && nvm use`.
- A computer on the **same network as the hub**, with a fixed LAN IP (set a DHCP reservation in your router).

## 2. Install

```bash
cd dashboard
npm ci
```

## 3. Configure

```bash
cp .env.example .env.local
```

Fill it in:

```ini
HUB_API_KEY=<the same value you put in api_key on the SD card>
SITE_TIMEZONE=Asia/Tehran

BETTER_AUTH_SECRET=<output of: openssl rand -base64 32>
BETTER_AUTH_URL=http://192.168.1.50:3000

ADMIN_EMAIL=you@example.com
ADMIN_PASSWORD=<at least 8 characters>

TELEGRAM_BOT_TOKEN=
TELEGRAM_CHAT_ID=
BALE_BOT_TOKEN=
BALE_CHAT_ID=
```

- `BETTER_AUTH_URL` is the address you'll type into the browser.
- `SITE_TIMEZONE` sets the hub's clock as well as alert timestamps.
- Leave the bot variables empty for now. [Tutorial 6](06-alerts.md) fills them in.

Every variable is explained in [configuration.md](../configuration.md#dashboardenvlocal).

## 4. Create the database and your account

```bash
npm run setup
```

```
create table user
create table session
create table account
create table verification
schema applied: .../dashboard/data/auth.db
admin account ready: you@example.com
```

This is the only account. Sign-up is disabled. It's safe to run again.

## 5. Start

```bash
npm run dev          # while you're setting up
# or
npm run build && npm start   # for daily use
```

Both listen on all interfaces, on port **3000**. Open `BETTER_AUTH_URL` in a browser and sign in.

> **`npm start` and logins:** in production mode the login cookie needs HTTPS. On plain `http://`, use `npm run dev`, or follow [deployment.md](../deployment.md) to add HTTPS.

## 6. Connect the hub

Within a few seconds of the dashboard starting:

- the hub's clock changes from `--:--` to the real time
- the **fingerprints** tab lists the hub's roster (empty for now)

The feed shows **alerts**: alarms, lockdowns, code and fingerprint changes. Opening a door while disarmed is logged on the hub's LOG tab, but it isn't an alert, so it won't appear here. To see an event arrive, change the keypad code or arm and trip a sensor ([tutorial 7](07-first-run.md)). It shows up within 4 s.

Quick check without the hub:

```bash
curl -s -H "X-Api-Key: $(grep HUB_API_KEY .env.local | cut -d= -f2)" http://localhost:3000/api/time
```

## What's on the page

| Section | Does |
|---|---|
| **terminal** | arm · disarm · panic · lock · unlock. Each is queued and picked up by the hub's next poll, within 5 s. |
| **events** | Every alert, newest first, marked *silent* or *loud* |
| **fingerprints** | Rename (immediate) or remove (needs approval at the terminal) |
| **فارسی / English** | Language switch. Persian is the default and lays out right-to-left. |

---

Next: [6 · Get alerts on Telegram and Bale](06-alerts.md) →
