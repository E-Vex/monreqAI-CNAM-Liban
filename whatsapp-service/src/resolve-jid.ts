// Helper: turn a WhatsApp Channel or Group invite link into its JID.
//
// Channel mode (default, or --channel):
//   npm run resolve-jid -- https://whatsapp.com/channel/<invite-code>
//   -> prints `<id>@newsletter   (channel name)`
//
// Group mode (--group):
//   npm run resolve-jid -- --group https://chat.whatsapp.com/<invite-code>
//   -> prints `<id>@g.us   (group name)`
//
// Requires an already-paired session in auth/ (run the service once first).
//
// IMPORTANT for groups: Baileys' groupGetInviteInfo() returns metadata
// for an invite link even when the paired account has NOT joined the
// group -- it is a "preview" of the invite. So this script does NOT
// auto-join the group; it just prints the JID. To actually post, the
// paired number must already be a member (someone has to add it, or it
// has to accept the invite via the WhatsApp app or via a separate,
// explicit `sock.groupAcceptInvite(code)` step that the operator runs
// on purpose).
import makeWASocket, { fetchLatestBaileysVersion, useMultiFileAuthState } from 'baileys'
import pino from 'pino'

const args = process.argv.slice(2)
let mode: 'channel' | 'group' = 'channel'
if (args[0] === '--channel') {
  mode = 'channel'
  args.shift()
} else if (args[0] === '--group') {
  mode = 'group'
  args.shift()
}

const arg = args[0] ?? ''
const code = arg.split('/').filter(Boolean).pop()?.split('?')[0]
if (!code) {
  console.error('Usage:')
  console.error('  npm run resolve-jid -- https://whatsapp.com/channel/<invite-code>')
  console.error('  npm run resolve-jid -- --group https://chat.whatsapp.com/<invite-code>')
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
    if (mode === 'channel') {
      const meta = await sock.newsletterMetadata('invite', code)
      if (!meta) throw new Error('channel not found')
      console.log(`${meta.id}   (${meta.name ?? 'unnamed channel'})`)
    } else {
      // groupGetInviteInfo returns the group metadata for an invite code
      // (works whether or not the paired account has joined -- it is a
      // preview of the invite). The metadata.id is the @g.us JID.
      const meta = await sock.groupGetInviteInfo(code)
      if (!meta) throw new Error('group not found')
      console.log(`${meta.id}   (${meta.subject ?? 'unnamed group'})`)
    }
    process.exit(0)
  } catch (e) {
    console.error(`Lookup failed: ${(e as Error).message}`)
    process.exit(1)
  }
})
