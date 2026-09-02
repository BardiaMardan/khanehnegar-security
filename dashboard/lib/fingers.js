import fs from 'fs/promises'
import path from 'path'

const DIR = path.join(process.cwd(), 'data')
const FILE = path.join(DIR, 'fingers.json')

export async function readFingers() {
  try {
    const raw = await fs.readFile(FILE, 'utf8')
    const parsed = JSON.parse(raw)
    return Array.isArray(parsed) ? parsed : []
  } catch {
    return []
  }
}

export async function writeFingers(list) {
  const clean = list
    .filter((f) => Number.isInteger(f.id) && f.id > 0)
    .map((f) => ({ id: f.id, label: String(f.label || '').slice(0, 24) }))
  await fs.mkdir(DIR, { recursive: true })
  await fs.writeFile(FILE, JSON.stringify(clean, null, 2), 'utf8')
  return clean
}
