import { betterAuth } from 'better-auth'
import { authConfig } from '../lib/auth-config.js'

const email = process.env.ADMIN_EMAIL
const password = process.env.ADMIN_PASSWORD

if (!email || !password) {
  console.error('set ADMIN_EMAIL and ADMIN_PASSWORD in .env.local first')
  process.exit(1)
}

const seeder = betterAuth({
  ...authConfig,
  emailAndPassword: { ...authConfig.emailAndPassword, disableSignUp: false },
  rateLimit: { enabled: false },
})

const result = await seeder.api
  .signUpEmail({ body: { email, password, name: 'admin' } })
  .catch((err) => ({ error: err }))

if (result?.error) {
  const message = result.error.message || result.error.body?.message || String(result.error)
  if (/exist/i.test(message)) {
    console.log(`admin account already exists: ${email}`)
    process.exit(0)
  }
  console.error('could not create the account:', message)
  process.exit(1)
}

console.log(`admin account ready: ${email}`)
