import { headers } from 'next/headers'
import { auth } from '../../../lib/auth'
import { checkApiKey } from '../../../lib/apikey'
import { readFingers, writeFingers } from '../../../lib/fingers'

export async function GET() {
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) {
    return Response.json({ ok: false }, { status: 401 })
  }
  return Response.json({ ok: true, fingers: await readFingers() })
}

export async function POST(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  let body
  try {
    body = await request.json()
  } catch {
    return Response.json({ ok: false }, { status: 400 })
  }

  if (!Array.isArray(body?.fingers)) {
    return Response.json({ ok: false }, { status: 400 })
  }

  const saved = await writeFingers(body.fingers)
  return Response.json({ ok: true, count: saved.length })
}
