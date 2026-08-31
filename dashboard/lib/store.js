import fs from 'fs/promises'
import path from 'path'

const DIR = path.join(process.cwd(), 'data')
const FILE = path.join(DIR, 'events.json')
const MAX = 500

export async function readEvents() {
  try {
    const raw = await fs.readFile(FILE, 'utf8')
    const parsed = JSON.parse(raw)
    return Array.isArray(parsed) ? parsed : []
  } catch {
    return []
  }
}

export async function addEvent(event) {
  const list = await readEvents()
  list.unshift(event)
  const trimmed = list.slice(0, MAX)
  await fs.mkdir(DIR, { recursive: true })
  await fs.writeFile(FILE, JSON.stringify(trimmed, null, 2), 'utf8')
  return trimmed.length
}
