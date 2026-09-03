'use client'

import { useEffect, useState } from 'react'
import { useRouter } from 'next/navigation'
import Link from 'next/link'
import { signOut } from '../../lib/auth-client'
import Tabs from './Tabs'
import Controls from './Controls'

function stamp(event, locale, fallback) {
  const seconds = event.epoch || event.receivedAt
  if (!seconds) return fallback
  return new Date(seconds * 1000).toLocaleString(locale === 'fa' ? 'fa-IR' : 'en-GB', {
    day: '2-digit',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
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
            {events.length} {t.events}
          </p>
        </div>
        <div className="actions">
          <Link className="btn" href={`/${other}`}>
            {t.lang}
          </Link>
          <button onClick={leave}>{t.signOut}</button>
        </div>
      </header>

      <Tabs locale={locale} t={t} active="events" />

      <Controls t={t} />

      {events.length === 0 ? (
        <p className="empty">{t.empty}</p>
      ) : (
        <div className="card">
          {events.map((e) => (
            <div className="row" key={e.id}>
              <span className={`dot ${e.silent ? 'silent' : ''}`} />
              <span className="what">
                <span className="event">{e.event}</span>
                <br />
                <span className="kind">{e.kind}</span>
              </span>
              <span className="time">{stamp(e, locale, t.unknown)}</span>
            </div>
          ))}
        </div>
      )}
    </>
  )
}
