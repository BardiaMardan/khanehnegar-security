import { headers } from 'next/headers'
import { redirect } from 'next/navigation'
import { auth } from '../../lib/auth'
import { readEvents } from '../../lib/store'
import { dict, locales } from '../../lib/i18n'
import Feed from './Feed'

export const dynamic = 'force-dynamic'

export default async function Page({ params }) {
  const { locale } = await params
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) redirect(`/${locale}/login`)

  const events = await readEvents()
  const other = locales.find((l) => l !== locale) || locale

  return <Feed initial={events} locale={locale} t={dict(locale)} other={other} />
}
