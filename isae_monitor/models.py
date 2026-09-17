"""Core data types and text normalisation."""

from __future__ import annotations

import html
import re
import unicodedata
from dataclasses import dataclass
from typing import Optional

_TAG_RE = re.compile(r"<[^>]+>")
_ARABIC_DIACRITICS = re.compile(r"[\u064B-\u0652\u0640]")


def strip_html(raw: str) -> str:
    """Turn a Blogger summary blob into flat text."""
    if not raw:
        return ""
    return " ".join(_TAG_RE.sub(" ", html.unescape(raw)).split())


def normalize(text: str) -> str:
    """
    Fold text for keyword matching.

    Lowercases, strips French accents (so "génie" matches "genie"), removes
    Arabic diacritics and normalises alef variants, and collapses whitespace.
    Without this the fallback classifier misses most real announcements,
    which are written in accented French and unvocalised Arabic.
    """
    if not text:
        return ""
    text = text.lower()
    # Decompose, drop combining marks, recompose. Handles é -> e.
    text = "".join(
        ch for ch in unicodedata.normalize("NFKD", text)
        if not unicodedata.combining(ch)
    )
    text = _ARABIC_DIACRITICS.sub("", text)
    text = text.replace("أ", "ا").replace("إ", "ا").replace("آ", "ا")
    text = text.replace("ة", "ه").replace("ى", "ي")
    return " ".join(text.split())


@dataclass
class Announcement:
    id: str
    title: str
    link: str
    published: str
    summary: str

    #: set by the pipeline from state, not from the feed
    needs_general: bool = True
    needs_classification: bool = True
    category: Optional[str] = None

    @classmethod
    def from_entry(cls, entry) -> "Announcement":
        """Build from a feedparser entry (or anything with .get())."""
        link = entry.get("link", "") or ""
        return cls(
            id=entry.get("id", "") or link,
            title=(entry.get("title", "") or "(sans titre)").strip(),
            link=link,
            published=entry.get("published", "") or "date inconnue",
            summary=strip_html(entry.get("summary", "") or "")[:500],
        )

    @property
    def search_text(self) -> str:
        """Normalised title + summary, for keyword matching."""
        return normalize(f"{self.title} {self.summary}")
