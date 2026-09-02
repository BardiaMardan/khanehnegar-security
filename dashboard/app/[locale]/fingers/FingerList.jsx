'use client'

import { useCallback, useEffect, useRef, useState } from 'react'
import Link from 'next/link'
import Tabs from '../Tabs'

const POLL_MS = 1500

export default function FingerList({ initial, locale, t, other }) {
  const [fingers, setFingers] = useState(initial)
  const [busyId, setBusyId] = useState(0)
  const [editId, setEditId] = useState(0)
  const [draft, setDraft] = useState('')
  const [status, setStatus] = useState('')
  const timer = useRef(null)

  const reload = useCallback(async () => {
    const res = await fetch('/api/fingers', { cache: 'no-store' })
    if (res.ok) setFingers((await res.json()).fingers)
  }, [])

  useEffect(() => () => clearInterval(timer.current), [])

  function startEdit(finger) {
    setEditId(finger.id)
    setDraft(finger.label)
    setStatus('')
  }

  async function saveEdit(id) {
    const label = draft.trim()
    if (!label) return
    setEditId(0)
    setFingers((list) => list.map((f) => (f.id === id ? { ...f, label } : f)))
    const res = await fetch('/api/fingers/rename', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id, label }),
    })
    setStatus(res.ok ? t.renamed : t.failed)
    if (res.ok) reload()
  }

  function watch(approval, target) {
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
        setFingers((list) => list.filter((f) => f.id !== target))
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
    watch(body.approval, id)
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
              {editId === f.id ? (
                <span className="rename">
                  <input
                    autoFocus
                    value={draft}
                    maxLength={16}
                    onChange={(e) => setDraft(e.target.value)}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter') saveEdit(f.id)
                      if (e.key === 'Escape') setEditId(0)
                    }}
                  />
                  <button onClick={() => saveEdit(f.id)}>{t.save}</button>
                  <button onClick={() => setEditId(0)}>{t.cancel}</button>
                </span>
              ) : (
                <span className="what">
                  <span className="event">{f.label}</span>
                  <br />
                  <span className="kind">id {f.id}</span>
                </span>
              )}
              {editId === f.id ? (
                <span />
              ) : (
                <span className="rename">
                  <button onClick={() => startEdit(f)}>{t.rename}</button>
                  <button className="danger" disabled={busyId !== 0} onClick={() => remove(f.id)}>
                    {busyId === f.id ? t.waiting : t.remove}
                  </button>
                </span>
              )}
            </div>
          ))}
        </div>
      )}

      <p className="note">{status || t.approvalHint}</p>
      <p className="note">{t.enrolHint}</p>
    </>
  )
}
