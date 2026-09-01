const pending = {
  unlock: false,
  panic: false,
  arm: false,
  disarm: false,
}

const KNOWN = Object.keys(pending)

export function queue(name) {
  if (!KNOWN.includes(name)) return false
  pending[name] = true
  return true
}

export function drain() {
  const snapshot = { ...pending }
  for (const key of KNOWN) pending[key] = false
  return snapshot
}

export function parse(text) {
  const word = text.replace(/^\//, '')
  if (word === 'unlock') return 'unlock'
  if (word === 'panic') return 'panic'
  if (word === 'arm') return 'arm'
  if (word === 'disarm') return 'disarm'
  return null
}
