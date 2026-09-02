import { headers } from 'next/headers'
import { auth } from '../../../lib/auth'
import { statusOf } from '../../../lib/commands'

export async function GET(request) {
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) {
    return Response.json({ ok: false }, { status: 401 })
  }

  const id = Number(new URL(request.url).searchParams.get('id'))
  if (!Number.isInteger(id) || id <= 0) {
    return Response.json({ ok: false }, { status: 400 })
  }

  return Response.json({ ok: true, status: await statusOf(id) })
}
