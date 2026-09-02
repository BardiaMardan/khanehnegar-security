import fs from 'fs/promises'
import path from 'path'

const DIR = path.join(process.cwd(), 'data')
const FILE = path.join(DIR, 'commands.json')
const APPROVAL_TTL_MS = 45000
const NAMES = ['unlock', 'lock', 'panic', 'arm', 'disarm']

const EMPTY = {
  pending: { unlock: false, lock: false, panic: false, arm: false, disarm: false },
  renames: [],
  approval: null,
  decided: null,
  nextId: 1,
}

let chain = Promise.resolve()

async function load() {
  try {
    const raw = await fs.readFile(FILE, 'utf8')
    return { ...EMPTY, ...JSON.parse(raw) }
  } catch {
    return structuredClone(EMPTY)
  }
}

async function save(state) {
  await fs.mkdir(DIR, { recursive: true })
  await fs.writeFile(FILE, JSON.stringify(state, null, 2), 'utf8')
}

function sweep(state) {
  if (state.approval && Date.now() - state.approval.at > APPROVAL_TTL_MS) {
    state.decided = { id: state.approval.id, reason: 'expired' }
    state.approval = null
  }
}

function withState(mutate) {
  const run = chain.then(async () => {
    const state = await load()
    sweep(state)
    const result = await mutate(state)
    await save(state)
    return result
  })
  chain = run.catch(() => {})
  return run
}

export function parse(text) {
  const word = text.replace(/^\//, '')
  return NAMES.includes(word) ? word : null
}

export function queue(names) {
  return withState((state) => {
    for (const name of names) {
      if (NAMES.includes(name)) state.pending[name] = true
    }
  })
}

export function drain() {
  return withState((state) => {
    const snapshot = { ...state.pending }
    for (const name of NAMES) state.pending[name] = false
    const renames = state.renames || []
    state.renames = []
    const approve = state.approval
      ? {
          id: state.approval.id,
          action: state.approval.action,
          arg: state.approval.arg,
          text: state.approval.text,
        }
      : null
    return { ...snapshot, renames, approve }
  })
}

export function queueRename(id, label) {
  return withState((state) => {
    if (!Array.isArray(state.renames)) state.renames = []
    state.renames = state.renames.filter((r) => r.id !== id)
    state.renames.push({ id, label })
    return true
  })
}

export function askApproval(action, arg, text) {
  return withState((state) => {
    if (state.approval) return { ok: false, reason: 'busy' }
    state.approval = { id: state.nextId, action, arg, text, at: Date.now() }
    state.decided = null
    state.nextId += 1
    return { ok: true, id: state.approval.id }
  })
}

export function settle(id, allowed) {
  return withState((state) => {
    if (!state.approval || state.approval.id !== id) return false
    state.approval = null
    state.decided = { id, reason: allowed ? 'allowed' : 'denied' }
    return true
  })
}

export function statusOf(id) {
  return withState((state) => {
    if (state.approval && state.approval.id === id) return 'pending'
    if (state.decided && state.decided.id === id) return state.decided.reason
    return 'unknown'
  })
}
