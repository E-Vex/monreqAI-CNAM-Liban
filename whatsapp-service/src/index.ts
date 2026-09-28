import { timingSafeEqual } from 'node:crypto'
import express from 'express'
import makeWASocket, {
  DisconnectReason,
  fetchLatestBaileysVersion,
  jidNormalizedUser,
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
const GROUP_JID = process.env.WHATSAPP_GROUP_JID ?? ''
const PAIRING_PHONE = (process.env.WHATSAPP_PAIRING_PHONE ?? '').replace(/\D/g, '')
const AUTH_DIR = 'auth'
const MAX_TEXT = 8000

if (!SECRET) {
  log('ERROR', 'WHATSAPP_SHARED_SECRET is not set; refusing to start.')
  process.exit(1)
}
// Each target is independently optional: a missing target simply disables
// its endpoint with a 503; a *malformed* target refuses to start. This
// way an operator can configure channel-only, group-only, or both.
if (CHANNEL_JID && !/^[0-9]+@newsletter$/.test(CHANNEL_JID)) {
  log('ERROR', 'WHATSAPP_CHANNEL_JID must look like <id>@newsletter; refusing to start.')
  process.exit(1)
}
if (GROUP_JID && !/^[0-9]+(-[0-9]+)?@g\.us$/.test(GROUP_JID)) {
  log('ERROR', 'WHATSAPP_GROUP_JID must look like <id>@g.us (or <id>-<id>@g.us); refusing to start.')
  process.exit(1)
}
if (!CHANNEL_JID && !GROUP_JID) {
  log('ERROR', 'Neither WHATSAPP_CHANNEL_JID nor WHATSAPP_GROUP_JID is set; refusing to start.')
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
      // Diagnostic only: probe the group's metadata so the failure mode
      // ("account not a member" / "group is admins-only") is legible in
      // the log instead of surfacing as a bare Baileys error on the first
      // send. Never blocks sends -- metadata can be transiently
      // unavailable right after reconnecting.
      if (GROUP_JID) {
        s.groupMetadata(GROUP_JID).then((meta) => {
          if (!meta) {
            log('WARN', `group ${GROUP_JID}: no metadata returned (account may not be a participant)`)
            return
          }
          // s.user.id carries a device suffix ("123:5@s.whatsapp.net") and
          // participants may be listed in LID or phone-number form, so
          // normalize and compare across every identifier we have.
          const mine = new Set(
            [s.user?.id, s.user?.lid, s.user?.phoneNumber]
              .filter((x): x is string => !!x)
              .map((x) => jidNormalizedUser(x))
          )
          const participant = (meta.participants ?? []).some((p) =>
            [p.id, p.lid, p.phoneNumber]
              .filter((x): x is string => !!x)
              .some((x) => mine.has(jidNormalizedUser(x)))
          )
          const announce = meta.announce === true
          log('INFO', `group ${GROUP_JID}: subject="${meta.subject ?? ''}", participant=${participant ? 'yes' : 'NO'}, announce=${announce ? 'yes (admins-only)' : 'no'}`)
          if (!participant) {
            log('WARN', `group ${GROUP_JID}: the paired account is NOT a participant; sends will fail. Add the number to the group manually (Baileys cannot join on its own).`)
          }
        }).catch((e: unknown) => {
          log('WARN', `group ${GROUP_JID}: metadata lookup failed: ${(e as Error).message}`)
        })
      }
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

// Shared send handler. The JID is resolved up front (one of CHANNEL_JID /
// GROUP_JID), so the only target-specific thing here is the label used in
// log lines. Baileys' sendMessage is JID-type-agnostic, so the wire call
// is identical for channel vs group.
async function handleSend(
  req: express.Request,
  res: express.Response,
  target: string,
  jid: string
): Promise<void> {
  if (!secretMatches(req.header('X-Internal-Secret'))) {
    log('WARN', `send rejected: bad or missing X-Internal-Secret (${target})`)
    res.status(401).json({ success: false, error: 'unauthorized' })
    return
  }
  const text = req.body?.text
  if (typeof text !== 'string' || !text.trim() || text.length > MAX_TEXT) {
    log('WARN', `send rejected: invalid "text" (${target})`)
    res.status(400).json({ success: false, error: 'body must be {"text": non-empty string}' })
    return
  }
  if (!sock || !connected) {
    const error = loggedOut ? 'whatsapp session logged out; re-pair required' : 'whatsapp not connected'
    log('ERROR', `send failed (${target}): ${error}`)
    res.status(503).json({ success: false, error })
    return
  }
  try {
    const sent = await sock.sendMessage(jid, { text })
    log('INFO', `send OK to ${jid} (${text.length} chars, id ${sent?.key?.id ?? 'n/a'})`)
    res.json({ success: true, id: sent?.key?.id ?? null })
  } catch (err) {
    const error = (err as Error).message
    log('ERROR', `send failed (${target}): ${error}`)
    res.status(502).json({ success: false, error })
  }
}

app.post('/send-channel-message', (req, res) => {
  if (!CHANNEL_JID) {
    log('WARN', 'send rejected: whatsapp channel not configured')
    res.status(503).json({ success: false, error: 'whatsapp channel not configured' })
    return
  }
  void handleSend(req, res, 'channel', CHANNEL_JID)
})

app.post('/send-group-message', (req, res) => {
  if (!GROUP_JID) {
    log('WARN', 'send rejected: whatsapp group not configured')
    res.status(503).json({ success: false, error: 'whatsapp group not configured' })
    return
  }
  void handleSend(req, res, 'group', GROUP_JID)
})

app.listen(PORT, HOST, () => log('INFO', `listening on http://${HOST}:${PORT}`))
connect().catch((e) => {
  log('ERROR', `Initial connect failed: ${(e as Error).message}`)
  process.exit(1)
})
