'use client'

import { useState } from 'react'

const ACTIONS = ['arm', 'disarm', 'panic', 'lock', 'unlock']

export default function Controls({ t }) {
  const [busy, setBusy] = useState('')
  const [note, setNote] = useState('')

  async function send(action) {
    setBusy(action)
    setNote('')
    const res = await fetch('/api/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action }),
    })
    setBusy('')
    setNote(res.ok ? `${t[action]} — ${t.sent}` : t.failed)
  }

  return (
    <div className="card controls">
      <div className="controls-head">
        <span className="kind">{t.controls}</span>
        <span className="kind">{note}</span>
      </div>
      <div className="controls-row">
        {ACTIONS.map((action) => (
          <button
            key={action}
            className={action === 'panic' ? 'danger' : ''}
            disabled={busy !== ''}
            onClick={() => send(action)}
          >
            {t[action]}
          </button>
        ))}
      </div>
    </div>
  )
}
