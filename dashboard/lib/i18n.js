export const locales = ['en', 'fa']
export const defaultLocale = 'fa'

export function isRtl(locale) {
  return locale === 'fa'
}

const dictionaries = {
  en: {
    title: 'khanehnegar',
    subtitle: 'local security terminal',
    signIn: 'sign in',
    signingIn: 'checking',
    email: 'email',
    password: 'password',
    wrongCredentials: 'wrong email or password',
    signOut: 'sign out',
    events: 'events',
    refreshing: 'refreshing every 4s',
    empty: 'no events yet, the terminal has not reported anything',
    silent: 'silent',
    loud: 'loud',
    source: 'source',
    event: 'event',
    time: 'time',
    lang: 'فارسی',
    unknown: 'unknown',
  },
  fa: {
    title: 'خانه‌نگار',
    subtitle: 'پایانه امنیتی محلی',
    signIn: 'ورود',
    signingIn: 'در حال بررسی',
    email: 'ایمیل',
    password: 'گذرواژه',
    wrongCredentials: 'ایمیل یا گذرواژه نادرست است',
    signOut: 'خروج',
    events: 'رویدادها',
    refreshing: 'بازآوری هر ۴ ثانیه',
    empty: 'هنوز رویدادی نیست، پایانه چیزی گزارش نکرده',
    silent: 'بی‌صدا',
    loud: 'بلند',
    source: 'منبع',
    event: 'رویداد',
    time: 'زمان',
    lang: 'English',
    unknown: 'نامشخص',
  },
}

export function dict(locale) {
  return dictionaries[locale] || dictionaries[defaultLocale]
}
