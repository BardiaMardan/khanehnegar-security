# Dashboard API

All routes are under `dashboard/app/api/`. Every route checks its auth before it does anything else.

There are two kinds of caller:

| Caller | Auth | How |
|---|---|---|
| **Hub** | `X-Api-Key` header | Must equal `HUB_API_KEY`. Compared in constant time (`lib/apikey.js`). |
| **Browser** | Session cookie | better-auth email + password. Sessions last 12 h. Sign-up is disabled, and login is rate-limited to 10 tries per minute. |

A missing or wrong credential returns `401 {"ok":false}`.

---

## Hub routes

### `GET /api/time`
Sets the hub's clock, once at boot and then hourly.
```json
{ "ok": true, "unix": 1789000000, "offset": 210, "zone": "Asia/Tehran", "iso": "..." }
```
`offset` is minutes east of UTC for `SITE_TIMEZONE` at this moment, so daylight saving is handled.

### `POST /api/alert`
Records an event and forwards it to Telegram and Bale.
```json
{ "silent": true, "kind": "DOOR", "event": "handle touched", "value": 0, "site": "Home", "epoch": 1789000000 }
```
→ `{ "ok": true, "notified": 2 }`. Fields are truncated (kind 24, event 48, site 32). The dashboard keeps the latest 500 events.

### `GET /api/command`
The hub calls this every 5 s. It first pulls new bot messages from Telegram and Bale, turns `/arm`, `/disarm`, `/panic`, `/lock`, or `/unlock` from the configured chat into pending flags, then **drains** everything pending:
```json
{
  "ok": true,
  "unlock": false, "lock": false, "panic": false, "arm": true, "disarm": false,
  "renames": [ { "id": 3, "label": "Mom" } ],
  "approve": { "id": 12, "action": "finger_del", "arg": 3, "text": "remove Mom" }
}
```
Flags and renames are cleared once returned. `approve` stays until it's decided or expires after 45 s.

### `POST /api/command`
The hub's answer to an approval.
```json
{ "decision": { "id": 12, "allowed": true } }
```
→ `{ "ok": true, "settled": true }`

### `POST /api/fingers`
The hub publishes its full fingerprint roster after any change and on every reconnect.
```json
{ "fingers": [ { "id": 1, "label": "Bardia" }, { "id": 3, "label": "Mom" } ] }
```

---

## Browser routes

### `GET /api/events`
→ `{ "ok": true, "events": [ {id, receivedAt, epoch, silent, kind, event, value, site}, ... ] }`, newest first. The feed polls this every 4 s.

### `POST /api/control`
`{ "action": "arm" | "disarm" | "panic" | "lock" | "unlock" }`. Queues the command for the hub's next poll and posts "*action* requested from the dashboard" to the bots.

### `GET /api/fingers`
→ `{ "ok": true, "fingers": [...] }`, the last roster the hub published.

### `POST /api/fingers/rename`
`{ "id": 3, "label": "Mom" }` (label ≤ 16 chars). Takes effect at once on the dashboard and is queued for the hub. Renaming is not destructive, so it doesn't need approval.

### `POST /api/fingers/remove`
`{ "id": 3 }` → `{ "ok": true, "approval": 12 }`. **Doesn't remove anything.** It opens an approval request that someone must allow at the terminal. Returns `409 {"reason":"busy"}` if another request is already waiting, and `404` for an unknown ID.

### `GET /api/approval?id=12`
→ `{ "ok": true, "status": "pending" | "allowed" | "denied" | "expired" | "unknown" }`

### `/api/auth/*`
better-auth's own handler: sign-in, sign-out, session.

---

## Storage

Everything lives in `dashboard/data/`, which git ignores:

| File | Holds |
|---|---|
| `auth.db` | SQLite: users and sessions |
| `events.json` | last 500 events |
| `commands.json` | pending flags, renames, the open approval, the next approval ID |
| `fingers.json` | the hub's last published roster |

## Trying it with curl

```bash
KEY=$(grep HUB_API_KEY dashboard/.env.local | cut -d= -f2)

curl -s -H "X-Api-Key: $KEY" http://localhost:3000/api/time

curl -s -X POST -H "X-Api-Key: $KEY" -H 'Content-Type: application/json' \
  -d '{"silent":false,"kind":"DOOR","event":"DOOR MOVEMENT","value":14,"site":"Test","epoch":0}' \
  http://localhost:3000/api/alert
```
