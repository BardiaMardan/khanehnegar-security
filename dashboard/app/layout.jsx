import './globals.css'

export const metadata = {
  title: 'khanehnegar',
  description: 'local security dashboard',
}

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>
        <main>{children}</main>
      </body>
    </html>
  )
}
