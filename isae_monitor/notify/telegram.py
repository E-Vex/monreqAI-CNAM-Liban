"""
Telegram delivery.

Adds the guards the original lacked: the 4096-character limit is enforced
(a long title used to produce an HTTP 400 that stalled the announcement
forever), text is HTML-escaped so an apostrophe or ampersand in a French
title cannot break the markup, and sends are spaced out so a first run does
not trip Telegram's flood limits.
"""

from __future__ import annotations

import html
import time
from dataclasses import dataclass, field
from typing import Optional

from .. import httpclient
from ..config import Settings
from ..departments import label_for
from ..models import Announcement

API = "https://api.telegram.org/bot{token}/sendMessage"

#: Telegram's hard cap is 4096; leave room for the HTML wrapper.
MAX_MESSAGE = 3900
SUMMARY_LIMIT = 400
TITLE_LIMIT = 250


def _truncate(text: str, limit: int) -> str:
    text = text.strip()
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def format_message(announcement: Announcement, category: Optional[str] = None) -> str:
    """Build the HTML message body for an announcement."""
    title = html.escape(_truncate(announcement.title, TITLE_LIMIT))
    parts = []
    if category:
        parts.append(f"<b>[{html.escape(label_for(category))}]</b>")
    parts.append(f"<b>{title}</b>")

    body = [" ".join(parts), f"<i>{html.escape(announcement.published)}</i>"]
    if announcement.summary:
        body.append(html.escape(_truncate(announcement.summary, SUMMARY_LIMIT)))
    if announcement.link:
        body.append(announcement.link)

    message = "\n\n".join(body)
    return message if len(message) <= MAX_MESSAGE else message[:MAX_MESSAGE] + "…"


class TelegramError(RuntimeError):
    pass


@dataclass
class Telegram:
    settings: Settings
    dry_run: bool = False
    _last_send: float = field(default=0.0, repr=False)

    def _throttle(self, sleep=time.sleep) -> None:
        elapsed = time.monotonic() - self._last_send
        if self._last_send and elapsed < self.settings.send_interval:
            sleep(self.settings.send_interval - elapsed)
        self._last_send = time.monotonic()

    def send(self, chat_id: str, text: str) -> None:
        if self.dry_run:
            print(f"    [dry-run] -> {chat_id}\n{_indent(text)}")
            return
        if not self.settings.telegram_token:
            raise TelegramError("TELEGRAM_BOT_TOKEN is not set")
        if not chat_id:
            raise TelegramError("no chat id given")

        self._throttle()
        url = API.format(token=self.settings.telegram_token)
        payload = {
            "chat_id": chat_id,
            "text": text,
            "parse_mode": "HTML",
            "disable_web_page_preview": True,
        }
        try:
            result = httpclient.request_json(
                url, method="POST", json_body=payload,
                timeout=self.settings.request_timeout,
                retries=self.settings.max_retries,
            )
        except httpclient.HttpError as exc:
            raise TelegramError(f"{exc} {exc.body}".strip()) from exc

        if not result.get("ok"):
            raise TelegramError(f"API error: {result.get('description', result)}")


def _indent(text: str) -> str:
    return "\n".join("      " + line for line in text.splitlines())
