import { Vazirmatn } from 'next/font/google'
import { locales, isRtl, dict } from '../../lib/i18n'
import '../globals.css'

const vazir = Vazirmatn({
  subsets: ['arabic', 'latin'],
  weight: ['400', '500', '600', '700'],
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
  const t = dict(locale)

  return (
    <html lang={locale} dir={isRtl(locale) ? 'rtl' : 'ltr'}>
      <body className={vazir.className}>
        <main>
          {children}
          <footer>
            <div>{t.madeBy}</div>
            <div>{t.forOrg}</div>
            <div>
              <a href="https://kibordevents.com" rel="noreferrer">
                kibordevents.com
              </a>
              {' · '}
              <a href="https://bardiamardan.xyz" rel="noreferrer">
                bardiamardan.xyz
              </a>
            </div>
          </footer>
        </main>
      </body>
    </html>
  )
}
