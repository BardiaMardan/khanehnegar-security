const PROVIDERS = [
  {
    name: 'telegram',
    base: 'https://api.telegram.org/bot',
    token: () => process.env.TELEGRAM_BOT_TOKEN,
    chat: () => process.env.TELEGRAM_CHAT_ID,
  },
  {
    name: 'bale',
    base: 'https://tapi.bale.ai/bot',
    token: () => process.env.BALE_BOT_TOKEN,
    chat: () => process.env.BALE_CHAT_ID,
  },
]

function configured() {
  return PROVIDERS.filter((p) => p.token() && p.chat())
}

async function call(provider, method, body) {
  const res = await fetch(`${provider.base}${provider.token()}/${method}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(8000),
  })
  const data = await res.json().catch(() => ({ ok: false }))
  return data
}

export async function broadcast(text) {
  const targets = configured()
  if (targets.length === 0) return { sent: 0, providers: [] }

  const results = await Promise.allSettled(
    targets.map((p) => call(p, 'sendMessage', { chat_id: p.chat(), text }))
  )

  const providers = targets.map((p, i) => ({
    name: p.name,
    ok: results[i].status === 'fulfilled' && results[i].value?.ok === true,
  }))

  return { sent: providers.filter((p) => p.ok).length, providers }
}

const offsets = new Map()

export async function pollCommands() {
  const commands = []

  for (const p of configured()) {
    try {
      const offset = offsets.get(p.name) ?? 0
      const data = await call(p, 'getUpdates', {
        offset,
        limit: 20,
        timeout: 0,
      })
      if (!data?.ok || !Array.isArray(data.result)) continue

      for (const update of data.result) {
        offsets.set(p.name, update.update_id + 1)
        const message = update.message || update.edited_message
        const text = message?.text
        const from = String(message?.chat?.id ?? '')
        if (!text || from !== String(p.chat())) continue
        commands.push(text.trim().toLowerCase().replace(/@\S+$/, ''))
      }
    } catch {}
  }

  return commands
}

export function formatAlert(event) {
  const tz = process.env.SITE_TIMEZONE || 'Asia/Tehran'
  const when = event.epoch
    ? new Date(event.epoch * 1000).toLocaleString('en-GB', { timeZone: tz })
    : 'time unknown'

  return [
    event.silent ? 'SILENT ALARM' : 'ALARM',
    `site: ${event.site || 'unknown'}`,
    `source: ${event.kind}`,
    `event: ${event.event}`,
    event.value ? `value: ${event.value}` : null,
    `when: ${when}`,
    '',
    'reply /unlock to clear a lockdown',
  ]
    .filter((line) => line !== null)
    .join('\n')
}
