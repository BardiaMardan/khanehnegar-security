import { checkApiKey } from '../../../lib/apikey'
import { addEvent } from '../../../lib/store'
import { broadcast, formatAlert } from '../../../lib/notify'

export async function POST(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  let body
  try {
    body = await request.json()
  } catch {
    return Response.json({ ok: false, error: 'bad json' }, { status: 400 })
  }

  const event = {
    id: Date.now(),
    receivedAt: Math.floor(Date.now() / 1000),
    epoch: Number(body.epoch) || 0,
    silent: Boolean(body.silent),
    kind: String(body.kind || 'UNKNOWN').slice(0, 24),
    event: String(body.event || 'event').slice(0, 48),
    value: Number(body.value) || 0,
    site: String(body.site || '').slice(0, 32),
  }

  await addEvent(event)
  const delivery = await broadcast(formatAlert(event))

  return Response.json({ ok: true, notified: delivery.sent })
}
