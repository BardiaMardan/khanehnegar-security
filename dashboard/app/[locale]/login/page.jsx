import { dict, locales } from '../../../lib/i18n'
import LoginForm from './LoginForm'

export default async function Page({ params }) {
  const { locale } = await params
  const other = locales.find((l) => l !== locale) || locale
  return <LoginForm locale={locale} t={dict(locale)} other={other} />
}
