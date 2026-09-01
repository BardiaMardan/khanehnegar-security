import fs from 'fs'
import path from 'path'
import Database from 'better-sqlite3'
import { betterAuth } from 'better-auth'

const email = process.env.ADMIN_EMAIL
const password = process.env.ADMIN_PASSWORD

if (!email || !password) {
  console.error('set ADMIN_EMAIL and ADMIN_PASSWORD in .env.local first')
  process.exit(1)
}

const dir = path.join(process.cwd(), 'data')
fs.mkdirSync(dir, { recursive: true })

const seeder = betterAuth({
  appName: 'khanehnegar',
  database: new Database(path.join(dir, 'auth.db')),
  emailAndPassword: { enabled: true, minPasswordLength: 10 },
})

const result = await seeder.api
  .signUpEmail({ body: { email, password, name: 'admin' } })
  .catch((err) => ({ error: err }))

if (result?.error) {
  console.error('could not create the account:', result.error.message || result.error)
  console.error('if it already exists, this is expected')
  process.exit(1)
}

console.log(`admin account ready: ${email}`)
