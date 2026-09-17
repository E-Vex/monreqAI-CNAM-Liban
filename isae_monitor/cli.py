"""Command line entry point."""

from __future__ import annotations

import argparse
import sys
from datetime import datetime

from . import __version__
from .config import Settings
from .departments import DEPARTMENTS
from .feed import FeedError


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="isae-monitor",
        description="Watch the ISSAE / Cnam Liban announcements feed and "
                    "route each notice to the right Telegram channel.",
    )
    p.add_argument("--dry-run", action="store_true",
                   help="classify and print, but send nothing")
    p.add_argument("--bootstrap", action="store_true",
                   help="mark everything currently in the feed as seen without "
                        "notifying; use this on first install so you are not "
                        "flooded with the whole backlog")
    p.add_argument("--list-departments", action="store_true",
                   help="show every department and its channel variable")
    p.add_argument("--check", action="store_true",
                   help="print resolved configuration and exit")
    p.add_argument("--quiet", action="store_true", help="only print the summary")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    return p


def list_departments(settings: Settings) -> int:
    print(f"{len(DEPARTMENTS)} departments at ISSAE / Cnam Liban:\n")
    width = max(len(d.name_fr) for d in DEPARTMENTS)
    for dept in DEPARTMENTS:
        state = "configured" if dept.key in settings.department_channels else "-"
        print(f"  {dept.name_fr:<{width}}  {dept.env_var:<32} {state}")
    print("\n  plus the pseudo-categories 'general' (TELEGRAM_CHANNEL_GENERAL) "
          "and 'other' (never routed).")
    return 0


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    settings = Settings.from_env()

    if args.list_departments:
        return list_departments(settings)

    if args.check:
        print(settings.summary())
        problems = settings.problems()
        if problems:
            print("\nWarnings:")
            for problem in problems:
                print(f"  - {problem}")
        return 0 if settings.has_telegram else 1

    verbose = not args.quiet
    if verbose:
        stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"[{stamp}] checking ISSAE announcements...")
        for problem in settings.problems():
            print(f"  warning: {problem}")

    # Import here so --list-departments works without feedparser installed.
    from .pipeline import run

    try:
        report = run(settings, dry_run=args.dry_run,
                     bootstrap=args.bootstrap, verbose=verbose)
    except FeedError as exc:
        print(f"feed error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130

    print("\n" + report.render())
    return 1 if report.errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
