# whatsapp-service

A small Node.js/TypeScript sidecar that posts **plain-text** messages to **one WhatsApp Channel**.
The C monitor calls it over loopback HTTP for announcements classified as `general`; Telegram
delivery is untouched and keeps working even if this service is down.

WhatsApp's official Business Cloud API cannot post to Channels, so this uses
[Baileys](https://github.com/WhiskeySockets/Baileys) (`baileys` on npm), an **unofficial** client for
the WhatsApp Web multi-device protocol. Read the [risks](#risks) before relying on it.

```
isae_monitor (C) --POST /send-channel-message--> whatsapp-service (127.0.0.1) --Baileys--> WhatsApp Channel
                     X-Internal-Secret header
```

## Install

Requires Node.js **20.6+** (uses `node --env-file`).

```bash
cd whatsapp-service
npm install
npm run build
```

## Configure

The service reads the **same `.env` as the monitor** (repo root, `../.env`):

| Variable | Required | Purpose |
|---|---|---|
| `WHATSAPP_SHARED_SECRET` | yes | Must equal the value the monitor uses; sent as `X-Internal-Secret`. |
| `WHATSAPP_CHANNEL_JID` | yes | Target channel, `<id>@newsletter`. |
| `WHATSAPP_SERVICE_URL` | monitor only | e.g. `http://127.0.0.1:3100`. |
| `PORT` | no | Default `3100`. |
| `WHATSAPP_PAIRING_PHONE` | no | Digits only with country code (e.g. `96170123456`) to pair by code instead of QR. |

Generate a secret with `openssl rand -hex 32`.

## First run: pair the WhatsApp number (QR)

Use the WhatsApp account that **owns/admins the channel**.

```bash
npm start
```

1. A QR code prints in the terminal.
2. On the phone: **WhatsApp → Settings → Linked devices → Link a device**, scan it.
3. Wait for `connection.update: open` in the log.

The session is stored in `whatsapp-service/auth/` (git-ignored). Restarting the service reconnects
without scanning again. If you would rather not scan, set `WHATSAPP_PAIRING_PHONE` and choose
**Link with phone number instead** in WhatsApp; an 8-character code is logged.

If WhatsApp logs the device out (code 401), the service stops reconnecting and answers `503`.
Stop it, delete `auth/`, and pair again. Any other disconnect reconnects automatically after 3 s.

## Get the channel JID

The JID is not shown in the WhatsApp app. With a paired session, resolve it from the channel's link
(channel → share/copy link, `https://whatsapp.com/channel/<code>`):

```bash
npm run resolve-jid -- https://whatsapp.com/channel/<code>
# 120363012345678901@newsletter   (My Channel)
```

Put the result in `WHATSAPP_CHANNEL_JID`. Stop the running service first so two sockets don't use the
same session at once. You can only post to a channel your linked account administers.

## API

`POST /send-channel-message` on `127.0.0.1` only.

```bash
curl -s -X POST http://127.0.0.1:3100/send-channel-message \
  -H 'Content-Type: application/json' \
  -H "X-Internal-Secret: $WHATSAPP_SHARED_SECRET" \
  -d '{"text":"Test message"}'
```

| Status | Body |
|---|---|
| 200 | `{"success":true,"id":"..."}` |
| 400 | invalid body (`text` must be a non-empty string) |
| 401 | missing/wrong secret |
| 502 | send failed inside Baileys |
| 503 | not connected / logged out |

Every attempt is logged to stdout/stderr with an ISO timestamp.

## Keep it running

The monitor is a one-shot job (cron); this service must run continuously.

### pm2

```bash
npm install -g pm2
cd whatsapp-service && npm run build
pm2 start npm --name whatsapp-service -- start
pm2 save
pm2 startup        # run the command it prints, so it survives reboots
```

### systemd

`/etc/systemd/system/whatsapp-service.service` (adjust paths and user):

```ini
[Unit]
Description=WhatsApp Channel sidecar
After=network-online.target
Wants=network-online.target

[Service]
User=monreq
WorkingDirectory=/opt/monreqAI-CNAM-Liban/whatsapp-service
EnvironmentFile=/opt/monreqAI-CNAM-Liban/.env
ExecStart=/usr/bin/node dist/index.js
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now whatsapp-service
journalctl -u whatsapp-service -f
```

With systemd, keep `.env` values unquoted or rely on `EnvironmentFile` quote handling; both work.
The working directory matters: `auth/` is created relative to it.

## Behavior notes

- Only `general`-classified announcements are sent, once each, when they are marked classified.
  This relies on `TELEGRAM_CHANNEL_GENERAL` being configured (as in the standard setup).
- A failed WhatsApp send is logged and **not retried** and never affects Telegram or the run's exit code.
- `isae_monitor --dry-run` prints the WhatsApp message instead of sending it.

## Risks

- Baileys is unofficial; using it can violate WhatsApp's terms and the number **may be banned**.
  Use a dedicated number you can afford to lose.
- Protocol changes can break it; update the pinned `baileys` version when that happens.
- Treat `auth/` like a password: anyone holding it can act as the linked account.

## Out of scope

Receiving messages, groups, direct messages, and media.
