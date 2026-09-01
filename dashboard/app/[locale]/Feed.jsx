'use client'

import { useEffect, useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'
import { signOut } from '../../lib/auth-client'

function stamp(event, locale, fallback) {
  const seconds = event.epoch || event.receivedAt
  if (!seconds) return fallback
  return new Date(seconds * 1000).toLocaleString(locale === 'fa' ? 'fa-IR' : 'en-GB', {
    day: '2-digit',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  })
}

export default function Feed({ initial, locale, t, other }) {
  const [events, setEvents] = useState(initial)
  const router = useRouter()

  useEffect(() => {
    const id = setInterval(async () => {
      const res = await fetch('/api/events', { cache: 'no-store' })
      if (res.status === 401) {
        router.replace(`/${locale}/login`)
        return
      }
      if (res.ok) setEvents((await res.json()).events)
    }, 4000)
    return () => clearInterval(id)
  }, [router, locale])

  async function leave() {
    await signOut()
    router.replace(`/${locale}/login`)
  }

  return (
    <>
      <header>
        <div>
          <h1>{t.title}</h1>
          <p className="sub">
            {events.length} {t.events} — {t.refreshing}
          </p>
        </div>
        <div className="actions">
          <Link className="btn" href={`/${other}`}>
            {t.lang}
          </Link>
          <button onClick={leave}>{t.signOut}</button>
        </div>
      </header>

      {events.length === 0 ? (
        <p className="empty">{t.empty}</p>
      ) : (
        <div className="stack">
          {events.map((e) => (
            <div key={e.id} className={`row ${e.silent ? 'silent' : 'loud'}`}>
              <span className="time">{stamp(e, locale, t.unknown)}</span>
              <span className="kind">{e.kind}</span>
              <span className="event">{e.event}</span>
              <span className={`tag ${e.silent ? 'silent' : 'loud'}`}>
                {e.silent ? t.silent : t.loud}
              </span>
            </div>
          ))}
        </div>
      )}
    </>
  )
}
