import { checkApiKey } from '../../../lib/apikey'
import { drain, queue, parse } from '../../../lib/commands'
import { pollCommands } from '../../../lib/notify'

export async function GET(request) {
  if (!checkApiKey(request)) {
    return Response.json({ ok: false }, { status: 401 })
  }

  const texts = await pollCommands()
  for (const text of texts) {
    const name = parse(text)
    if (name) queue(name)
  }

  return Response.json({ ok: true, ...drain() })
}
