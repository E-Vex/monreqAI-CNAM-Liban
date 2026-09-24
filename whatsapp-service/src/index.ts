import { timingSafeEqual } from 'node:crypto'
import express from 'express'
import makeWASocket, {
  DisconnectReason,
  fetchLatestBaileysVersion,
  useMultiFileAuthState
} from 'baileys'
import { Boom } from '@hapi/boom'
import pino from 'pino'
import qrcode from 'qrcode-terminal'
import { log } from './log.js'

const HOST = '127.0.0.1' // never exposed externally
const PORT = Number(process.env.PORT ?? 3100)
const SECRET = process.env.WHATSAPP_SHARED_SECRET ?? ''
const CHANNEL_JID = process.env.WHATSAPP_CHANNEL_JID ?? ''
const PAIRING_PHONE = (process.env.WHATSAPP_PAIRING_PHONE ?? '').replace(/\D/g, '')
const AUTH_DIR = 'auth'
const MAX_TEXT = 8000

if (!SECRET) {
  log('ERROR', 'WHATSAPP_SHARED_SECRET is not set; refusing to start.')
  process.exit(1)
}
if (!/^[0-9]+@newsletter$/.test(CHANNEL_JID)) {
  log('ERROR', 'WHATSAPP_CHANNEL_JID must look like <id>@newsletter; refusing to start.')
  process.exit(1)
}

type Sock = ReturnType<typeof makeWASocket>
let sock: Sock | null = null
let connected = false
let loggedOut = false
let pairingRequested = false

async function connect(): Promise<void> {
  const { state, saveCreds } = await useMultiFileAuthState(AUTH_DIR)
  const { version } = await fetchLatestBaileysVersion()
  const s = makeWASocket({
    version,
    auth: state,
    logger: pino({ level: 'warn' }),
    markOnlineOnConnect: false,
    syncFullHistory: false
  })
  sock = s

  s.ev.on('creds.update', saveCreds)

  s.ev.on('connection.update', async (update) => {
    const { connection, lastDisconnect, qr } = update

    if (qr && !PAIRING_PHONE) {
      log('INFO', 'Scan this QR in WhatsApp > Settings > Linked devices > Link a device:')
      qrcode.generate(qr, { small: true })
    }

    // Pairing-code flow: request once, while not yet registered.
    if ((connection === 'connecting' || qr) && PAIRING_PHONE && !state.creds.registered && !pairingRequested) {
      pairingRequested = true
      try {
        const code = await s.requestPairingCode(PAIRING_PHONE)
        log('INFO', `Pairing code: ${code}  (WhatsApp > Linked devices > Link with phone number)`)
      } catch (err) {
        pairingRequested = false
        log('ERROR', `Could not request pairing code: ${(err as Error).message}`)
      }
    }

    if (connection) log('INFO', `connection.update: ${connection}`)

    if (connection === 'open') {
      connected = true
      loggedOut = false
      pairingRequested = false
    } else if (connection === 'close') {
      connected = false
      const code = (lastDisconnect?.error as Boom | undefined)?.output?.statusCode
      if (code === DisconnectReason.loggedOut) {
        loggedOut = true
        log('ERROR', `Logged out (code ${code}). Delete ${AUTH_DIR}/ and restart to pair again. Not reconnecting.`)
      } else {
        log('WARN', `Disconnected (code ${code ?? 'unknown'}); reconnecting in 3s`)
        setTimeout(() => {
          connect().catch((e) => log('ERROR', `Reconnect failed: ${(e as Error).message}`))
        }, 3000)
      }
    }
  })
}

function secretMatches(header: string | undefined): boolean {
  if (!header) return false
  const a = Buffer.from(header)
  const b = Buffer.from(SECRET)
  return a.length === b.length && timingSafeEqual(a, b)
}

const app = express()
app.disable('x-powered-by')
app.use(express.json({ limit: '64kb' }))

app.post('/send-channel-message', async (req, res) => {
  if (!secretMatches(req.header('X-Internal-Secret'))) {
    log('WARN', 'send rejected: bad or missing X-Internal-Secret')
    return res.status(401).json({ success: false, error: 'unauthorized' })
  }
  const text = req.body?.text
  if (typeof text !== 'string' || !text.trim() || text.length > MAX_TEXT) {
    log('WARN', 'send rejected: invalid "text"')
    return res.status(400).json({ success: false, error: 'body must be {"text": non-empty string}' })
  }
  if (!sock || !connected) {
    const error = loggedOut ? 'whatsapp session logged out; re-pair required' : 'whatsapp not connected'
    log('ERROR', `send failed: ${error}`)
    return res.status(503).json({ success: false, error })
  }
  try {
    const sent = await sock.sendMessage(CHANNEL_JID, { text })
    log('INFO', `send OK to ${CHANNEL_JID} (${text.length} chars, id ${sent?.key?.id ?? 'n/a'})`)
    return res.json({ success: true, id: sent?.key?.id ?? null })
  } catch (err) {
    const error = (err as Error).message
    log('ERROR', `send failed: ${error}`)
    return res.status(502).json({ success: false, error })
  }
})

app.listen(PORT, HOST, () => log('INFO', `listening on http://${HOST}:${PORT}`))
connect().catch((e) => {
  log('ERROR', `Initial connect failed: ${(e as Error).message}`)
  process.exit(1)
})
