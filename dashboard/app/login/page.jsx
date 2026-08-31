'use client'

import { useState } from 'react'
import { useRouter } from 'next/navigation'

export default function Login() {
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const router = useRouter()

  async function submit(e) {
    e.preventDefault()
    setBusy(true)
    setError('')
    const res = await fetch('/api/session', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password }),
    })
    setBusy(false)
    if (res.ok) router.replace('/')
    else setError('wrong password')
  }

  return (
    <>
      <h1>khanehnegar</h1>
      <p className="sub">sign in to view events</p>
      <form onSubmit={submit}>
        <input
          type="password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          placeholder="password"
          autoFocus
        />
        <div className="err">{error}</div>
        <button type="submit" disabled={busy}>
          {busy ? 'checking' : 'sign in'}
        </button>
      </form>
    </>
  )
}
