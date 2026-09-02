'use client'

import { useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'
import { signIn } from '../../../lib/auth-client'

export default function LoginForm({ locale, t, other }) {
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const router = useRouter()

  async function submit(e) {
    e.preventDefault()
    setBusy(true)
    setError('')
    const { error: failed } = await signIn.email({ email, password })
    setBusy(false)
    if (failed) setError(t.wrongCredentials)
    else router.replace(`/${locale}`)
  }

  return (
    <div className="login">
      <h1>{t.title}</h1>
      <p className="sub">{t.subtitle}</p>
      <form onSubmit={submit}>
        <div>
          <label htmlFor="email">{t.email}</label>
          <input
            id="email"
            type="email"
            autoComplete="username"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            required
          />
        </div>
        <div>
          <label htmlFor="password">{t.password}</label>
          <input
            id="password"
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            required
          />
        </div>
        <div className="err">{error}</div>
        <button className="primary" type="submit" disabled={busy}>
          {busy ? t.signingIn : t.signIn}
        </button>
      </form>
      <div className="actions" style={{ marginTop: 18, justifyContent: 'center' }}>
        <Link className="btn" href={`/${other}/login`}>
          {t.lang}
        </Link>
      </div>
    </div>
  )
}
