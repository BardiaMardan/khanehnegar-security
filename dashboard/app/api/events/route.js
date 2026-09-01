import { headers } from 'next/headers'
import { auth } from '../../../lib/auth'
import { readEvents } from '../../../lib/store'

export async function GET() {
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) {
    return Response.json({ ok: false }, { status: 401 })
  }
  const events = await readEvents()
  return Response.json({ ok: true, events })
}
