import { checkApiKey } from '../../../lib/apikey'
import { drain, queue, parse, settle } from '../../../lib/commands'
import { pollCommands } from '../../../lib/notify'

export async function GET(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  const texts = await pollCommands()
  const names = texts.map(parse).filter(Boolean)
  if (names.length) await queue(names)

  return Response.json({ ok: true, ...(await drain()) })
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

  const id = Number(body?.decision?.id)
  const allowed = body?.decision?.allowed === true
  if (!Number.isInteger(id) || id <= 0) {
    return Response.json({ ok: false }, { status: 400 })
  }

  return Response.json({ ok: true, settled: await settle(id, allowed) })
}
