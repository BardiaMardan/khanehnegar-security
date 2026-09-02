import { headers } from 'next/headers'
import { auth } from '../../../lib/auth'
import { queue } from '../../../lib/commands'
import { broadcast } from '../../../lib/notify'

const ALLOWED = ['arm', 'disarm', 'panic', 'unlock', 'lock']

export async function POST(request) {
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) {
    return Response.json({ ok: false }, { status: 401 })
  }

  let body
  try {
    body = await request.json()
  } catch {
    return Response.json({ ok: false }, { status: 400 })
  }

  const action = String(body?.action || '')
  if (!ALLOWED.includes(action)) {
    return Response.json({ ok: false }, { status: 400 })
  }

  await queue([action])
  await broadcast(`${action} requested from the dashboard`)

  return Response.json({ ok: true, action })
}
