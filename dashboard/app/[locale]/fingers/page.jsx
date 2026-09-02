import { headers } from 'next/headers'
import { redirect } from 'next/navigation'
import { auth } from '../../../lib/auth'
import { readFingers } from '../../../lib/fingers'
import { dict, locales } from '../../../lib/i18n'
import FingerList from './FingerList'

export const dynamic = 'force-dynamic'

export default async function Page({ params }) {
  const { locale } = await params
  const session = await auth.api.getSession({ headers: await headers() })
  if (!session) redirect(`/${locale}/login`)

  const fingers = await readFingers()
  const other = locales.find((l) => l !== locale) || locale

  return <FingerList initial={fingers} locale={locale} t={dict(locale)} other={other} />
}
