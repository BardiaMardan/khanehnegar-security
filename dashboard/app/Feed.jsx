'use client'

import { useEffect, useState } from 'react'
import { useRouter } from 'next/navigation'

function when(event) {
  const seconds = event.epoch || event.receivedAt
  if (!seconds) return 'unknown'
  return new Date(seconds * 1000).toLocaleString('en-GB', {
    day: '2-digit',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  })
}

export default function Feed({ initial }) {
  const [events, setEvents] = useState(initial)
  const router = useRouter()

  useEffect(() => {
    const id = setInterval(async () => {
      const res = await fetch('/api/events', { cache: 'no-store' })
      if (res.status === 401) {
        router.replace('/login')
        return
      }
      if (res.ok) {
        const data = await res.json()
        setEvents(data.events)
      }
    }, 4000)
    return () => clearInterval(id)
  }, [router])

  async function signOut() {
    await fetch('/api/session', { method: 'DELETE' })
    router.replace('/login')
  }

  return (
    <>
      <div className="bar">
        <div>
          <h1>khanehnegar</h1>
          <p className="sub" style={{ margin: 0 }}>
            {events.length} events, refreshing every 4s
          </p>
        </div>
        <button className="logout" onClick={signOut}>
          sign out
        </button>
      </div>

      {events.length === 0 && (
        <p className="empty">no events yet, the terminal has not reported anything</p>
      )}

      {events.map((e) => (
        <div key={e.id} className={`row ${e.silent ? 'silent' : 'loud'}`}>
          <span className="time">{when(e)}</span>
          <span className="kind">{e.kind}</span>
          <span className="event">{e.event}</span>
          <span className={`tag ${e.silent ? 'silent' : 'loud'}`}>
            {e.silent ? 'silent' : 'loud'}
          </span>
        </div>
      ))}
    </>
  )
}
