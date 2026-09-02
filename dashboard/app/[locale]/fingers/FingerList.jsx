'use client'

import { useCallback, useEffect, useRef, useState } from 'react'
import Link from 'next/link'
import Tabs from '../Tabs'

const POLL_MS = 1500

export default function FingerList({ initial, locale, t, other }) {
  const [fingers, setFingers] = useState(initial)
  const [busyId, setBusyId] = useState(0)
  const [status, setStatus] = useState('')
  const timer = useRef(null)

  const reload = useCallback(async () => {
    const res = await fetch('/api/fingers', { cache: 'no-store' })
    if (res.ok) setFingers((await res.json()).fingers)
  }, [])

  useEffect(() => () => clearInterval(timer.current), [])

  function watch(approval) {
    clearInterval(timer.current)
    timer.current = setInterval(async () => {
      const res = await fetch(`/api/approval?id=${approval}`, { cache: 'no-store' })
      if (!res.ok) return
      const { status: state } = await res.json()
      if (state === 'pending') return

      clearInterval(timer.current)
      setBusyId(0)
      if (state === 'allowed') {
        setStatus(t.approved)
        reload()
      } else if (state === 'denied') {
        setStatus(t.denied)
      } else {
        setStatus(t.expired)
      }
    }, POLL_MS)
  }

  async function remove(id) {
    setStatus(t.waiting)
    setBusyId(id)
    const res = await fetch('/api/fingers/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id }),
    })
    const body = await res.json().catch(() => ({}))
    if (!res.ok) {
      setBusyId(0)
      setStatus(body.reason === 'busy' ? t.busy : t.expired)
      return
    }
    watch(body.approval)
  }

  return (
    <>
      <header>
        <div>
          <h1>{t.fingersTitle}</h1>
          <p className="sub">{t.fingersSub}</p>
        </div>
        <div className="actions">
          <Link className="btn" href={`/${other}/fingers`}>
            {t.lang}
          </Link>
        </div>
      </header>

      <Tabs locale={locale} t={t} active="fingers" />

      {fingers.length === 0 ? (
        <p className="empty">{t.noFingers}</p>
      ) : (
        <div className="card">
          {fingers.map((f) => (
            <div className="row" key={f.id}>
              <span className="dot silent" />
              <span className="what">
                <span className="event">{f.label}</span>
                <br />
                <span className="kind">id {f.id}</span>
              </span>
              <button className="danger" disabled={busyId !== 0} onClick={() => remove(f.id)}>
                {busyId === f.id ? t.waiting : t.remove}
              </button>
            </div>
          ))}
        </div>
      )}

      <p className="note">{status || t.approvalHint}</p>
      <p className="note">{t.enrolHint}</p>
    </>
  )
}
