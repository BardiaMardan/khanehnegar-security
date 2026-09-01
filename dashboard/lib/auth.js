import fs from 'fs'
import path from 'path'
import Database from 'better-sqlite3'
import { betterAuth } from 'better-auth'

const dir = path.join(process.cwd(), 'data')
fs.mkdirSync(dir, { recursive: true })

export const auth = betterAuth({
  appName: 'khanehnegar',
  database: new Database(path.join(dir, 'auth.db')),
  emailAndPassword: {
    enabled: true,
    disableSignUp: true,
    minPasswordLength: 10,
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
})
