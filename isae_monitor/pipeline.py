"""
Run orchestration: fetch, diff, classify, deliver, persist.

The two-phase tracking from the original is preserved, because it was a good
idea: an announcement can have reached the general channel but still be
awaiting classification, and the next run retries only the missing half.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .classify import Classifier
from .config import Settings
from .departments import GENERAL, OTHER, label_for
from .feed import fetch
from .models import Announcement
from .notify.telegram import Telegram, TelegramError, format_message
from .state import State


@dataclass
class RunReport:
    fetched: int = 0
    pending: int = 0
    general_sent: int = 0
    department_sent: int = 0
    classified: Dict[str, int] = field(default_factory=dict)
    fallback_used: int = 0
    errors: List[str] = field(default_factory=list)

    def render(self) -> str:
        lines = [
            f"fetched {self.fetched} entries, {self.pending} needed work",
            f"general channel: {self.general_sent} sent",
            f"department channels: {self.department_sent} sent",
        ]
        if self.classified:
            spread = ", ".join(f"{k}={v}" for k, v in sorted(self.classified.items()))
            lines.append(f"categories: {spread}")
        if self.fallback_used:
            lines.append(f"keyword fallback used {self.fallback_used} time(s)")
        for err in self.errors:
            lines.append(f"! {err}")
        return "\n".join(lines)


def select_pending(announcements: List[Announcement], state: State) -> List[Announcement]:
    """Annotate announcements with what still needs doing, keeping only those that do."""
    pending = []
    for item in announcements:
        item.needs_general = state.needs_general(item.id)
        item.needs_classification = state.needs_classification(item.id)
        if item.needs_general or item.needs_classification:
            pending.append(item)
    return pending


def run(settings: Settings, *, dry_run: bool = False,
        bootstrap: bool = False, verbose: bool = True) -> RunReport:
    report = RunReport()
    announcements = fetch(settings)
    report.fetched = len(announcements)

    with State(settings.state_file, settings.state_history) as state:
        if state.recovered_from_corruption:
            report.errors.append(
                "state file was corrupt and has been moved aside "
                "(.corrupt); re-run with --bootstrap to avoid re-sending everything"
            )

        if bootstrap:
            for item in announcements:
                state.mark_all_seen(item.id)
            state.save()
            report.pending = 0
            if verbose:
                print(f"Bootstrapped: {len(announcements)} announcement(s) "
                      f"marked as seen without notifying.")
            return report

        pending = select_pending(announcements, state)
        report.pending = len(pending)
        if not pending:
            return report

        telegram = Telegram(settings, dry_run=dry_run)
        classifier = Classifier(settings)

        for item in pending:
            if verbose:
                print(f"\n  {item.title[:80]}")

            if item.needs_general and settings.general_channel:
                try:
                    telegram.send(settings.general_channel, format_message(item))
                    state.mark_general_sent(item.id)
                    report.general_sent += 1
                except TelegramError as exc:
                    report.errors.append(f"general send failed: {exc}")

            if item.needs_classification:
                result = classifier.classify(item)
                item.category = result.category
                report.classified[result.category] = report.classified.get(result.category, 0) + 1
                if not result.is_ai:
                    report.fallback_used += 1
                if verbose:
                    print(f"    -> {label_for(result.category)} (via {result.source})")

                delivered = True
                chat_id = settings.department_channels.get(result.category)
                if chat_id:
                    try:
                        telegram.send(chat_id, format_message(item, result.category))
                        report.department_sent += 1
                    except TelegramError as exc:
                        report.errors.append(
                            f"{result.category} send failed: {exc}")
                        delivered = False

                # Only record the classification once its routing succeeded,
                # so a failed department send is retried next run.
                if delivered:
                    state.mark_classified(item.id, result.category)

            # Persist after every announcement: an interrupted run then loses
            # at most one item's progress rather than all of it.
            state.save()

        for note in classifier.notes:
            report.errors.append(note)

    return report
