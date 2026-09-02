import Link from 'next/link'

export default function Tabs({ locale, t, active }) {
  return (
    <nav className="tabs">
      <Link className={active === 'events' ? 'on' : ''} href={`/${locale}`}>
        {t.eventsTab}
      </Link>
      <Link className={active === 'fingers' ? 'on' : ''} href={`/${locale}/fingers`}>
        {t.fingersTab}
      </Link>
    </nav>
  )
}
