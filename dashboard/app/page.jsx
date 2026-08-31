import { cookies } from 'next/headers'
import { redirect } from 'next/navigation'
import { COOKIE, validToken } from '../lib/auth'
import { readEvents } from '../lib/store'
import Feed from './Feed'

export const dynamic = 'force-dynamic'

export default async function Page() {
  const jar = await cookies()
  if (!validToken(jar.get(COOKIE)?.value)) redirect('/login')
  const events = await readEvents()
  return <Feed initial={events} />
}
