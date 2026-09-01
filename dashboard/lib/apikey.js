import crypto from 'crypto'

export function checkApiKey(request) {
  const want = process.env.HUB_API_KEY || ''
  const got = request.headers.get('x-api-key') || ''
  if (!want || got.length !== want.length) return false
  return crypto.timingSafeEqual(Buffer.from(got), Buffer.from(want))
}
