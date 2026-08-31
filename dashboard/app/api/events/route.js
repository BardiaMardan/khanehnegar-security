import { cookies } from 'next/headers'
import { COOKIE, validToken } from '../../../lib/auth'
import { readEvents } from '../../../lib/store'

export async function GET() {
  const jar = await cookies()
  if (!validToken(jar.get(COOKIE)?.value)) {
    return Response.json({ ok: false }, { status: 401 })
  }
  const events = await readEvents()
  return Response.json({ ok: true, events })
}
