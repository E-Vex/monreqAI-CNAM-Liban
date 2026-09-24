// Helper: turn a WhatsApp Channel invite link into its <id>@newsletter JID.
// Usage: npm run resolve-jid -- https://whatsapp.com/channel/<invite-code>
// Requires an already-paired session in auth/ (run the service once first).
import makeWASocket, { fetchLatestBaileysVersion, useMultiFileAuthState } from 'baileys'
import pino from 'pino'

const arg = process.argv[2] ?? ''
const code = arg.split('/').filter(Boolean).pop()?.split('?')[0]
if (!code) {
  console.error('Usage: npm run resolve-jid -- https://whatsapp.com/channel/<invite-code>')
  process.exit(1)
}

const { state, saveCreds } = await useMultiFileAuthState('auth')
if (!state.creds.registered) {
  console.error('No paired session in auth/. Start the service once and pair first.')
  process.exit(1)
}
const { version } = await fetchLatestBaileysVersion()
const sock = makeWASocket({ version, auth: state, logger: pino({ level: 'silent' }) })
sock.ev.on('creds.update', saveCreds)
sock.ev.on('connection.update', async ({ connection }) => {
  if (connection !== 'open') return
  try {
    const meta = await sock.newsletterMetadata('invite', code)
    if (!meta) throw new Error('channel not found')
    console.log(`${meta.id}   (${meta.name ?? 'unnamed channel'})`)
    process.exit(0)
  } catch (e) {
    console.error(`Lookup failed: ${(e as Error).message}`)
    process.exit(1)
  }
})
