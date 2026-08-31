const BASE = 'https://botapi.rubika.ir/v3'

export async function sendRubika(text) {
  const token = process.env.RUBIKA_TOKEN
  const chatId = process.env.RUBIKA_CHAT_ID
  if (!token || !chatId) return { sent: false, reason: 'not configured' }

  try {
    const res = await fetch(`${BASE}/${token}/sendMessage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ chat_id: chatId, text }),
      signal: AbortSignal.timeout(8000),
    })
    if (!res.ok) return { sent: false, reason: `http ${res.status}` }
    return { sent: true }
  } catch (err) {
    return { sent: false, reason: String(err.message || err) }
  }
}

export function formatAlert(event) {
  const when = event.epoch
    ? new Date(event.epoch * 1000).toLocaleString('en-GB', {
        timeZone: process.env.SITE_TIMEZONE || 'Asia/Tehran',
      })
    : 'time unknown'

  const head = event.silent ? 'SILENT ALARM' : 'ALARM'
  return [
    head,
    `site: ${event.site || 'unknown'}`,
    `source: ${event.kind}`,
    `event: ${event.event}`,
    event.value ? `value: ${event.value}` : null,
    `when: ${when}`,
  ]
    .filter(Boolean)
    .join('\n')
}
