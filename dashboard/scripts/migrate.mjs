import { getMigrations } from 'better-auth/db'
import { authConfig, dbPath } from '../lib/auth-config.js'

const { toBeCreated, toBeAdded, runMigrations } = await getMigrations(authConfig)

if (!toBeCreated.length && !toBeAdded.length) {
  console.log(`schema already up to date: ${dbPath}`)
  process.exit(0)
}

for (const t of toBeCreated) console.log(`create table ${t.table}`)
for (const t of toBeAdded) console.log(`alter table ${t.table}`)

await runMigrations()

console.log(`schema applied: ${dbPath}`)
