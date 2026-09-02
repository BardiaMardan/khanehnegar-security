import { headers } from 'next/headers'
import { auth } from '../../../../lib/auth'
import { queueRename } from '../../../../lib/commands'
import { readFingers, writeFingers } from '../../../../lib/fingers'

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

  const id = Number(body?.id)
  const label = String(body?.label || '').trim().slice(0, 16)
  if (!Number.isInteger(id) || id <= 0 || !label) {
    return Response.json({ ok: false }, { status: 400 })
  }

  const roster = await readFingers()
  if (!roster.some((f) => f.id === id)) {
    return Response.json({ ok: false, reason: 'unknown' }, { status: 404 })
  }

  await writeFingers(roster.map((f) => (f.id === id ? { ...f, label } : f)))
  await queueRename(id, label)

  return Response.json({ ok: true })
}
