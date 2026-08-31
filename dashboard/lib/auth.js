import crypto from 'crypto'

const SECRET = process.env.SESSION_SECRET || 'insecure-default'
const TTL_MS = 1000 * 60 * 60 * 12

export const COOKIE = 'khaneh_session'

export function makeToken() {
  const exp = String(Date.now() + TTL_MS)
  const sig = crypto.createHmac('sha256', SECRET).update(exp).digest('hex')
  return `${exp}.${sig}`
}

export function validToken(token) {
  if (!token) return false
  const parts = token.split('.')
  if (parts.length !== 2) return false
  const [exp, sig] = parts
  if (!/^\d+$/.test(exp) || Number(exp) < Date.now()) return false
  const want = crypto.createHmac('sha256', SECRET).update(exp).digest('hex')
  if (sig.length !== want.length) return false
  return crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(want))
}

export function checkPassword(input) {
  const want = process.env.DASHBOARD_PASSWORD || ''
  if (!want || typeof input !== 'string' || input.length !== want.length) return false
  return crypto.timingSafeEqual(Buffer.from(input), Buffer.from(want))
}

export function checkApiKey(request) {
  const want = process.env.HUB_API_KEY || ''
  const got = request.headers.get('x-api-key') || ''
  if (!want || got.length !== want.length) return false
  return crypto.timingSafeEqual(Buffer.from(got), Buffer.from(want))
}
