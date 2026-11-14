# 6 · Get alerts on Telegram and Bale

**You'll end with:** alarms on your phone, and the ability to arm, disarm, or lock the house by messaging a bot.
**Time:** 10 minutes.

← [5 · Dashboard](05-dashboard.md) · Next: [7 · First run](07-first-run.md) →

---

You can enable Telegram, [Bale](https://bale.ai), or both. Bale's Bot API mirrors Telegram's and works where Telegram is blocked. Alerts go to every configured bot.

## 1. Create a bot

**Telegram:** message [@BotFather](https://t.me/BotFather) → `/newbot` → pick a name → copy the **token**.

**Bale:** message `@botfather` inside Bale → create a bot → copy the **token**.

## 2. Find your chat ID

1. Open a chat with your new bot and send it any message, for example `hi`. A bot can't message you until you've written to it first.
2. Open this page in a browser, using your token:

   - Telegram: `https://api.telegram.org/bot<TOKEN>/getUpdates`
   - Bale: `https://tapi.bale.ai/bot<TOKEN>/getUpdates`

3. Find `"chat":{"id": 123456789, ...}`. That number is your **chat ID**.

For a group, add the bot to the group, send a message there, and use the group's (negative) ID.

## 3. Configure

In `dashboard/.env.local`:

```ini
TELEGRAM_BOT_TOKEN=123456:ABC-DEF...
TELEGRAM_CHAT_ID=123456789

BALE_BOT_TOKEN=...
BALE_CHAT_ID=...
```

Restart the dashboard.

## 4. Test

```bash
curl -s -X POST -H "X-Api-Key: $(grep HUB_API_KEY .env.local | cut -d= -f2)" \
  -H 'Content-Type: application/json' \
  -d '{"silent":true,"kind":"DOOR","event":"handle touched","value":0,"site":"Test","epoch":0}' \
  http://localhost:3000/api/alert
# {"ok":true,"notified":1}
```

Your phone receives:

```
SILENT ALARM
site: Test
source: DOOR
event: handle touched
when: time unknown

reply /unlock to clear a lockdown
```

`notified` counts the bots that accepted the message.

## 5. Control the house from the chat

| Command | Effect |
|---|---|
| `/arm` | Arm AWAY, after the exit delay |
| `/disarm` | Disarm and stop the siren |
| `/panic` | Sound the alarm now |
| `/lock` | Lock the keypad, so codes are refused |
| `/unlock` | Clear a lockdown |

Commands are accepted **only from the configured chat ID**, and are picked up when the hub next polls (within about 5 s). The dashboard can't receive them while the hub is offline. They wait until it's back.

> Anyone in that chat can disarm your house. Keep it a private chat, or a closed group. See [security.md](../security.md#the-dashboard-can-disarm).

## Which events are sent

| Sent | Not sent |
|---|---|
| Every alarm, loud or silent | Disarmed-mode node events (they're only journaled on the hub) |
| Lockdown, and lockdown cleared | Heartbeats |
| Code changed; finger added or removed | Repeats of the same event within 60 s |
| Hub case opened | |
| Commands sent from the dashboard | |

---

Next: [7 · First run](07-first-run.md) →
