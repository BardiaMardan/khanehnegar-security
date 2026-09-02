import { checkApiKey } from '../../../lib/apikey'

function offsetMinutes(tz, at) {
  const parts = new Intl.DateTimeFormat('en-US', {
    timeZone: tz,
    timeZoneName: 'longOffset',
  }).formatToParts(at)

  const name = parts.find((p) => p.type === 'timeZoneName')?.value || 'GMT+00:00'
  const match = name.match(/GMT([+-])(\d{2}):(\d{2})/)
  if (!match) return 0

  const sign = match[1] === '-' ? -1 : 1
  return sign * (Number(match[2]) * 60 + Number(match[3]))
}

export async function GET(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  const tz = process.env.SITE_TIMEZONE || 'Asia/Tehran'
  const now = new Date()

  return Response.json({
    ok: true,
    unix: Math.floor(now.getTime() / 1000),
    offset: offsetMinutes(tz, now),
    zone: tz,
    iso: now.toISOString(),
  })
}
