"""
ai_classifier.py

Tier 2 fallback ONLY.

monitor.py's own APIKeyManager already handles Tier 1 (multiple Gemini
keys + OpenRouter, round-robin, per-key failure tracking, cooldowns) --
that part doesn't need duplicating here.

This file's only job: when every AI provider/key in monitor.py has been
tried and failed, give a rough keyword-based classification so the
service keeps running (degraded) instead of stopping completely.

Kept conservative on purpose: defaults to "other" rather than guess wrong
and notify the wrong students. Extend these lists from real announcement
samples over time.

TODO: log when this fallback disagrees with what the AI classified the
same announcement as on a previous run (if you ever re-classify), to spot
weak spots in the keyword lists.
"""

KEYWORDS_CS = [
    "informatique", "computer science", "genie informatique",
    "programmation", "algorithm", " cs ",
]
KEYWORDS_GENERAL = [
    "examen", "exam", "inscription", "registration", "horaire",
    "vacance", "conge", "deadline", "rentree", "calendrier",
]


def keyword_classify(info: dict) -> str:
    text = f"{info.get('title', '')} {info.get('summary', '')}".lower()
    if any(k in text for k in KEYWORDS_CS):
        return "cs"
    if any(k in text for k in KEYWORDS_GENERAL):
        return "general"
    return "other"