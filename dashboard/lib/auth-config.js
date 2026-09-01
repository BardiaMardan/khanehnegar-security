import fs from 'fs'
import path from 'path'
import Database from 'better-sqlite3'

const dir = path.join(process.cwd(), 'data')
fs.mkdirSync(dir, { recursive: true })

export const dbPath = path.join(dir, 'auth.db')

export const authConfig = {
  appName: 'khanehnegar',
  database: new Database(dbPath),
  emailAndPassword: {
    enabled: true,
    disableSignUp: true,
    minPasswordLength: 8,
  },
  session: {
    expiresIn: 60 * 60 * 12,
    updateAge: 60 * 60,
  },
  rateLimit: {
    enabled: true,
    window: 60,
    max: 10,
  },
  advanced: {
    useSecureCookies: process.env.NODE_ENV === 'production',
  },
}
