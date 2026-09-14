"""
ISAE Announcements Monitor
Step 1: Read the Atom Feed and detect new announcements
"""

import feedparser
import html
import json
import os
import re
import urllib.request
from datetime import datetime

# --- Settings ----------------------------------------------------------------
FEED_URL  = "http://annonces.isae.edu.lb/feeds/posts/default"
SEEN_FILE = "seen.json"   # stores IDs of already-processed announcements
# -----------------------------------------------------------------------------


def load_seen() -> list:
    """Load the list of already-seen announcement IDs."""
    if os.path.exists(SEEN_FILE):
        with open(SEEN_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    return []


def save_seen(seen_ids: list) -> None:
    """Save the list of seen announcement IDs."""
    with open(SEEN_FILE, "w", encoding="utf-8") as f:
        json.dump(seen_ids, f, ensure_ascii=False, indent=2)


def fetch_feed() -> list:
    """
    Fetch announcements from the Atom Feed.
    Uses urllib directly to set a proper User-Agent header,
    since some servers reject requests without one (returns 403).
    """
    req = urllib.request.Request(
        FEED_URL,
        headers={"User-Agent": "Mozilla/5.0 (compatible; ISAEMonitor/1.0)"},
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            raw_feed = response.read()
    except Exception as e:
        raise ConnectionError(f"Could not reach the feed: {e}")

    feed = feedparser.parse(raw_feed)

    if feed.bozo and not feed.entries:
        raise ConnectionError(f"Could not parse the feed: {feed.bozo_exception}")

    return feed.entries


def clean_html(raw: str) -> str:
    """
    Strip HTML tags and decode HTML entities (e.g. &nbsp; -> space).
    Returns plain text, collapsed to single spaces.
    """
    decoded = html.unescape(raw)                  # &nbsp; &amp; etc. -> plain chars
    no_tags = re.sub(r"<[^>]+>", " ", decoded)    # remove <tags>
    return " ".join(no_tags.split())              # collapse whitespace


def extract_entry_info(entry) -> dict:
    """Extract the essential fields from a single feed entry."""
    summary_clean = clean_html(entry.get("summary", ""))

    return {
        "id":        entry.get("id") or entry.get("link", ""),
        "title":     entry.get("title", "(no title)"),
        "link":      entry.get("link", ""),
        "published": entry.get("published", "unknown date"),
        "summary":   summary_clean[:300],
    }


def check_new_announcements() -> list:
    """
    Core logic:
    1. Fetch the feed
    2. Compare against saved IDs
    3. Return only new announcements and update the seen file
    """
    seen_ids = load_seen()
    entries  = fetch_feed()

    new_announcements = []

    for entry in entries:
        info = extract_entry_info(entry)

        if info["id"] not in seen_ids:
            new_announcements.append(info)
            seen_ids.append(info["id"])

    # Save first, before any further processing - avoids re-sending on later errors
    if new_announcements:
        save_seen(seen_ids)

    return new_announcements


def print_announcement(info: dict, index: int = None) -> None:
    """Pretty-print a single announcement."""
    prefix = f"[{index}] " if index is not None else ""
    print("\n" + "-" * 55)
    print(f"{prefix}{info['title']}")
    print(f"Date:      {info['published']}")
    print(f"Link:      {info['link']}")
    if info["summary"]:
        preview = info["summary"][:200]
        ellipsis = "..." if len(info["summary"]) > 200 else ""
        print(f"Summary:   {preview}{ellipsis}")
    print("-" * 55)


# --- Entry point -------------------------------------------------------------
if __name__ == "__main__":
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"\n[{timestamp}] Checking ISAE announcements...")

    try:
        new_items = check_new_announcements()

        if new_items:
            print(f"\nFound {len(new_items)} new announcement(s):\n")
            for i, item in enumerate(new_items, start=1):
                print_announcement(item, index=i)
        else:
            print("No new announcements since last check.\n")

    except ConnectionError as e:
        print(f"\nConnection error: {e}\n")
    except Exception as e:
        print(f"\nUnexpected error: {e}\n")
        raise
