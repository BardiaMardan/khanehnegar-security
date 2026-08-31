import { cookies } from 'next/headers'
import { COOKIE, checkPassword, makeToken } from '../../../lib/auth'

export async function POST(request) {
  let body
  try {
    body = await request.json()
  } catch {
    return Response.json({ ok: false }, { status: 400 })
  }

  await new Promise((r) => setTimeout(r, 400))

  if (!checkPassword(body.password)) {
    return Response.json({ ok: false, error: 'wrong password' }, { status: 401 })
  }

  const jar = await cookies()
  jar.set(COOKIE, makeToken(), {
    httpOnly: true,
    sameSite: 'lax',
    path: '/',
    maxAge: 60 * 60 * 12,
  })

  return Response.json({ ok: true })
}

export async function DELETE() {
  const jar = await cookies()
  jar.delete(COOKIE)
  return Response.json({ ok: true })
}
