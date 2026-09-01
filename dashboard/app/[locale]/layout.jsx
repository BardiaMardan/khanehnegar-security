import { Vazirmatn } from 'next/font/google'
import { locales, isRtl } from '../../lib/i18n'
import '../globals.css'

const vazir = Vazirmatn({
  subsets: ['arabic', 'latin'],
  weight: ['400', '700', '900'],
  display: 'swap',
})

export const metadata = {
  title: 'khanehnegar',
  description: 'local security terminal',
}

export function generateStaticParams() {
  return locales.map((locale) => ({ locale }))
}

export default async function LocaleLayout({ children, params }) {
  const { locale } = await params
  return (
    <html lang={locale} dir={isRtl(locale) ? 'rtl' : 'ltr'}>
      <body className={vazir.className}>
        <main>{children}</main>
      </body>
    </html>
  )
}
