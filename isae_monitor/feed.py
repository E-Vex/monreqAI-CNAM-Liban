"""Atom feed fetching."""

from __future__ import annotations

from typing import List

import feedparser

from . import httpclient
from .config import Settings
from .models import Announcement


class FeedError(RuntimeError):
    pass


def fetch(settings: Settings) -> List[Announcement]:
    """
    Fetch and parse the announcements feed.

    Returns announcements oldest-first, so notifications arrive in the order
    the institute published them.
    """
    try:
        raw = httpclient.request(
            settings.feed_url,
            timeout=settings.request_timeout,
            retries=settings.max_retries,
        )
    except httpclient.HttpError as exc:
        raise FeedError(f"could not reach the feed: {exc}") from exc

    parsed = feedparser.parse(raw)
    if parsed.bozo and not parsed.entries:
        raise FeedError(f"could not parse the feed: {parsed.bozo_exception}")

    announcements = [Announcement.from_entry(e) for e in parsed.entries]
    announcements = [a for a in announcements if a.id]
    announcements.reverse()  # feed is newest-first
    return announcements
