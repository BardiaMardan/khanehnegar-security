import { headers } from 'next/headers'
import { auth } from '../../../../lib/auth'
import { askApproval } from '../../../../lib/commands'
import { readFingers } from '../../../../lib/fingers'

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
  if (!Number.isInteger(id) || id <= 0) {
    return Response.json({ ok: false }, { status: 400 })
  }

  const roster = await readFingers()
  const target = roster.find((f) => f.id === id)
  if (!target) {
    return Response.json({ ok: false, reason: 'unknown' }, { status: 404 })
  }

  const asked = await askApproval('finger_del', id, `remove ${target.label}`)
  if (!asked.ok) {
    return Response.json({ ok: false, reason: asked.reason }, { status: 409 })
  }

  return Response.json({ ok: true, approval: asked.id })
}
