import { NextResponse } from 'next/server'
import { locales, defaultLocale } from './lib/i18n'

function pick(request) {
  const header = request.headers.get('accept-language') || ''
  for (const part of header.split(',')) {
    const tag = part.split(';')[0].trim().toLowerCase()
    const base = tag.split('-')[0]
    if (locales.includes(base)) return base
  }
  return defaultLocale
}

export function middleware(request) {
  const { pathname } = request.nextUrl
  const hasLocale = locales.some(
    (l) => pathname === `/${l}` || pathname.startsWith(`/${l}/`)
  )
  if (hasLocale) return NextResponse.next()

  const url = request.nextUrl.clone()
  url.pathname = `/${pick(request)}${pathname === '/' ? '' : pathname}`
  return NextResponse.redirect(url)
}

export const config = {
  matcher: ['/((?!api|_next|favicon.ico).*)'],
}
