"""
Classification engine.

Tries Gemini keys, then OpenRouter, then keyword matching. Key health is
tracked for the duration of a run so a dead key is not retried once per
announcement; it is deliberately *not* persisted across runs, because the
process is short-lived and the HTTP layer's backoff already covers the
cross-run case. (The original code kept cooldown timestamps in memory and
ran from cron, so its 15-minute cooldowns were discarded before they could
ever apply.)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set

from ..config import Settings
from ..departments import GENERAL, OTHER, describe_for_prompt, resolve
from ..models import Announcement
from . import keywords
from .providers import ProviderError, _extract_category, call_gemini, call_openrouter

PROMPT_TEMPLATE = """You route announcements for ISSAE / Cnam Liban, a Lebanese \
higher-education institute whose notices are published in French and Arabic.

Announcement title: {title}
Announcement summary: {summary}

Assign exactly one category:
{categories}

Rules:
- Departments are distinct. Something being "engineering" does not make it \
Informatique; Génie Civil, Génie Électrique, Génie Mécanique and Génie des \
Procédés are all engineering too.
- If a notice names a department explicitly, use that department.
- If it applies to the whole institute (fees, registration, closures, \
institute-wide exam calendars), use "general".
- If it is not aimed at students at all (job adverts, tenders), use "other".
- Arabic notices follow the same rules.

Respond with JSON only: {{"category": "<one of the labels above>"}}"""


@dataclass
class Result:
    category: str
    source: str                    # "gemini" | "openrouter" | "keywords"
    detail: str = ""

    @property
    def is_ai(self) -> bool:
        return self.source in ("gemini", "openrouter")


@dataclass
class Classifier:
    settings: Settings
    _dead_keys: Set[str] = field(default_factory=set)
    _notes: List[str] = field(default_factory=list)

    def build_prompt(self, announcement: Announcement) -> str:
        return PROMPT_TEMPLATE.format(
            title=announcement.title,
            summary=announcement.summary or "(no summary)",
            categories=describe_for_prompt(),
        )

    def classify(self, announcement: Announcement) -> Result:
        prompt = self.build_prompt(announcement)
        last_error = ""

        for key in self.settings.gemini_keys:
            if key in self._dead_keys:
                continue
            try:
                raw = call_gemini(prompt, key, self.settings.gemini_model,
                                  self.settings.request_timeout,
                                  self.settings.max_retries)
            except ProviderError as exc:
                last_error = f"gemini: {exc}"
                # 401/403 = bad key, 404 = wrong model: retrying is pointless.
                if exc.status in (400, 401, 403, 404, 429):
                    self._dead_keys.add(key)
                    self._note(f"gemini key disabled for this run ({exc})")
                continue

            category = _extract_category(raw)
            if category:
                return Result(category, "gemini")
            last_error = f"gemini: unparseable answer {raw[:80]!r}"

        if self.settings.openrouter_key and self.settings.openrouter_key not in self._dead_keys:
            try:
                raw = call_openrouter(prompt, self.settings.openrouter_key,
                                      self.settings.openrouter_model,
                                      self.settings.request_timeout,
                                      self.settings.max_retries)
                category = _extract_category(raw)
                if category:
                    return Result(category, "openrouter")
                last_error = f"openrouter: unparseable answer {raw[:80]!r}"
            except ProviderError as exc:
                last_error = f"openrouter: {exc}"
                if exc.status in (401, 402, 403):
                    self._dead_keys.add(self.settings.openrouter_key)

        return Result(keywords.classify(announcement), "keywords", last_error)

    def _note(self, message: str) -> None:
        if message not in self._notes:
            self._notes.append(message)

    @property
    def notes(self) -> List[str]:
        return list(self._notes)


__all__ = ["Classifier", "Result", "keywords"]
