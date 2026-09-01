import { checkApiKey } from '../../../lib/apikey'

export async function GET(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  const tz = process.env.SITE_TIMEZONE || 'Asia/Tehran'

  try {
    const res = await fetch(`https://utctime.app/api/now/${encodeURIComponent(tz)}`, {
      cache: 'no-store',
      signal: AbortSignal.timeout(5000),
    })
    if (res.ok) {
      const data = await res.json()
      return Response.json({
        unix: data.unix,
        offset: data.utc_offset_minutes,
        source: 'utctime',
      })
    }
  } catch {}

  const now = new Date()
  const local = new Date(now.toLocaleString('en-US', { timeZone: tz }))
  const utc = new Date(now.toLocaleString('en-US', { timeZone: 'UTC' }))
  const offset = Math.round((local - utc) / 60000)

  return Response.json({
    unix: Math.floor(now.getTime() / 1000),
    offset,
    source: 'server',
  })
}
