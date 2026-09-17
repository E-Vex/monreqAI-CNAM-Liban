"""
Keyword classification: the last line of defence when every AI provider fails.

Upgraded from the original in three ways:
  - covers all nine departments rather than just Informatique;
  - matches accent-folded French *and* Arabic, since the announcements page
    publishes in both (the old list was French/English only, so every Arabic
    announcement fell through to "other");
  - scores every department and picks the strongest match instead of
    returning the first list that happened to contain a hit, which made the
    result depend on dictionary ordering.

Still deliberately conservative: when nothing scores, return "general" rather
than guessing a department. A missed department routing is an inconvenience;
a wrong one sends students to the wrong channel.
"""

from __future__ import annotations

from typing import Dict, Optional, Tuple

from ..departments import DEPARTMENTS, GENERAL, OTHER
from ..models import Announcement, normalize

#: Terms that signal an institute-wide notice regardless of department.
GENERAL_MARKERS: Tuple[str, ...] = (
    "tous les auditeurs", "tous les etudiants", "a tous", "all students",
    "inscription", "registration", "frais", "fees", "rentree", "calendrier",
    "vacance", "conge", "holiday", "ferme", "closure", "transport",
    "horaire", "schedule", "deadline", "delai", "attestation", "diplome",
    "الى جميع الطلاب", "جميع الطلاب", "كافة المراكز", "التسجيل", "الرسوم",
    "عطلة", "المواعيد", "اعلان عام",
)

#: Terms that mean "not aimed at students at all".
OTHER_MARKERS: Tuple[str, ...] = (
    "offre d'emploi", "job offer", "recrute", "recruitment", "vacancy",
    "appel d'offres", "tender", "فرص عمل", "وظيفة", "مناقصة",
)

# Titles routinely name the department followed by a course code, so a match
# in the title is a much stronger signal than one buried in the summary.
TITLE_WEIGHT = 3
SUMMARY_WEIGHT = 1


def score_departments(announcement: Announcement) -> Dict[str, int]:
    """Return a per-department match score. Exposed for testing and tuning."""
    title = normalize(announcement.title)
    summary = normalize(announcement.summary)
    scores: Dict[str, int] = {}

    for dept in DEPARTMENTS:
        score = 0
        for keyword in dept.keywords:
            folded = normalize(keyword)
            if not folded:
                continue
            if folded in title:
                # Multi-word keywords are far more specific than single words.
                score += TITLE_WEIGHT * (2 if " " in folded else 1)
            elif folded in summary:
                score += SUMMARY_WEIGHT * (2 if " " in folded else 1)
        if score:
            scores[dept.key] = score
    return scores


def classify(announcement: Announcement) -> str:
    text = announcement.search_text

    if any(normalize(m) in text for m in OTHER_MARKERS):
        return OTHER

    scores = score_departments(announcement)
    if scores:
        best = max(scores.values())
        winners = [k for k, v in scores.items() if v == best]
        # An ambiguous tie is not a routing decision worth making.
        if len(winners) == 1:
            return winners[0]

    if any(normalize(m) in text for m in GENERAL_MARKERS):
        return GENERAL

    return GENERAL
