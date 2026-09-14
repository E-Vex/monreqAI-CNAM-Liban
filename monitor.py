"""
ISAE Announcements Monitor
Step 1: Read the Atom Feed and detect new announcements
Step 2: Classify announcements using the Gemini AI API
"""

import feedparser
import html
import json
import os
import re
import urllib.request
import urllib.error
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


def mark_seen(announcement_id: str) -> None:
    """
    Mark a single announcement as seen and persist immediately.

    Called only after an announcement has been fully classified. This way,
    if classification fails partway through a batch, the unprocessed
    announcements are NOT marked as seen and will be picked up again on
    the next run instead of being silently dropped forever.
    """
    seen_ids = load_seen()
    if announcement_id not in seen_ids:
        seen_ids.append(announcement_id)
        save_seen(seen_ids)


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
    3. Return only the new announcements

    Note: this no longer marks announcements as seen. That now happens
    per-item, via mark_seen(), only after each one is successfully
    classified - see the __main__ block below.
    """
    seen_ids = load_seen()
    entries  = fetch_feed()

    new_announcements = []

    for entry in entries:
        info = extract_entry_info(entry)

        if info["id"] not in seen_ids:
            new_announcements.append(info)

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


# --- AI Classification --------------------------------------------------------
def _load_api_keys() -> list:
    """
    Load one or more Gemini API keys.

    Preferred: AI_API_KEYS="key1,key2,key3" (comma-separated).
    Still supported for backward compatibility: a single AI_API_KEY.
    """
    raw = os.environ.get("AI_API_KEYS") or os.environ.get("AI_API_KEY") or ""
    return [k.strip() for k in raw.split(",") if k.strip()]


AI_API_KEYS = _load_api_keys()
AI_MODEL    = "gemini-3.6-flash"   # fast, free-tier model, enough for simple classification

CATEGORY_GENERAL = "general"   # relevant to all students
CATEGORY_CS      = "cs"        # relevant to Computer Science / Informatique students
CATEGORY_OTHER   = "other"     # not relevant to me

# round-robin starting point across calls, so load spreads evenly over keys
_key_cursor = 0


def _call_gemini(prompt: str, api_key: str) -> str:
    """Make a single classification request to the Gemini API using one key."""
    body = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}]
    }).encode("utf-8")

    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{AI_MODEL}:generateContent?key={api_key}"
    )

    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/json"},
    )

    with urllib.request.urlopen(req, timeout=20) as response:
        result = json.loads(response.read())

    return result["candidates"][0]["content"]["parts"][0]["text"].strip().lower()


def classify_announcement(info: dict) -> str:
    """
    Ask the AI model to classify the announcement into: general / cs / other.

    Rotates across AI_API_KEYS: if a key returns 429 (quota exceeded),
    the next key is tried instead of failing the whole classification.
    """
    global _key_cursor

    if not AI_API_KEYS:
        raise RuntimeError("AI_API_KEYS / AI_API_KEY environment variable is not set.")

    prompt = f"""You classify university announcements for an engineering institute.

Title: {info['title']}
Summary: {info['summary']}

Classify this announcement into exactly one category:
- general: relevant to all students in general (deadlines, holidays, registration, general exams...)
- cs: specifically relevant to Computer Science / Informatique students
- other: relevant to another department (civil, electrical...) or unrelated to studies (generic job offers, etc.)

Reply with exactly one word: general or cs or other"""

    key_count  = len(AI_API_KEYS)
    last_error = None

    for attempt in range(key_count):
        key_index = (_key_cursor + attempt) % key_count
        api_key   = AI_API_KEYS[key_index]

        try:
            answer = _call_gemini(prompt, api_key)
        except urllib.error.HTTPError as e:
            error_body = e.read().decode("utf-8")
            if e.code == 429:
                last_error = f"key #{key_index + 1}/{key_count}: quota exceeded (429)"
                continue  # this key is exhausted, try the next one
            raise RuntimeError(f"API request failed ({e.code}): {error_body}")

        # this key worked - start from the next one next time, spreads load evenly
        _key_cursor = (key_index + 1) % key_count

        if "cs" in answer:
            return CATEGORY_CS
        if "general" in answer:
            return CATEGORY_GENERAL
        return CATEGORY_OTHER

    raise RuntimeError(
        f"All {key_count} API key(s) exhausted their quota. Last error: {last_error}"
    )



# --- Telegram Notifications ---------------------------------------------------
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN")
TELEGRAM_CHAT_ID   = os.environ.get("TELEGRAM_CHAT_ID")

# categories that are worth notifying about
NOTIFY_CATEGORIES = (CATEGORY_GENERAL, CATEGORY_CS)


def format_telegram_message(info: dict, category: str) -> str:
    """Build the plain-text Telegram message for one announcement."""
    label = "General" if category == CATEGORY_GENERAL else "CS"
    lines = [
        f"[{label}] {info['title']}",
        f"Date: {info['published']}",
    ]
    if info["summary"]:
        lines.append(info["summary"][:200])
    lines.append(info["link"])
    return "\n".join(lines)


def send_telegram_message(text: str) -> None:
    """
    Send a plain-text message to the configured Telegram chat
    using the Bot API sendMessage endpoint.
    """
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID:
        raise RuntimeError(
            "TELEGRAM_BOT_TOKEN / TELEGRAM_CHAT_ID environment variables are not set."
        )

    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    body = json.dumps({
        "chat_id": TELEGRAM_CHAT_ID,
        "text": text,
        "disable_web_page_preview": True,
    }).encode("utf-8")

    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/json"},
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            result = json.loads(response.read())
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8")
        raise RuntimeError(f"Telegram request failed ({e.code}): {error_body}")

    if not result.get("ok"):
        raise RuntimeError(f"Telegram API returned an error: {result}")


# --- Entry point -------------------------------------------------------------
if __name__ == "__main__":
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"\n[{timestamp}] Checking ISAE announcements...")

    try:
        new_items = check_new_announcements()

        if new_items:
            print(f"\nFound {len(new_items)} new announcement(s):\n")
            for i, item in enumerate(new_items, start=1):
                try:
                    category = classify_announcement(item)
                    # Only mark as seen once classification succeeds - a
                    # failure here means this announcement gets retried on
                    # the next run instead of being silently dropped.
                    mark_seen(item["id"])
                except RuntimeError as e:
                    category = f"(classification unavailable: {e})"

                print_announcement(item, index=i)
                print(f"Category:  {category}")

                if category in NOTIFY_CATEGORIES:
                    try:
                        send_telegram_message(format_telegram_message(item, category))
                        print("Telegram:  sent")
                    except RuntimeError as e:
                        print(f"Telegram:  failed ({e})")
        else:
            print("No new announcements since last check.\n")

    except ConnectionError as e:
        print(f"\nConnection error: {e}\n")
    except Exception as e:
        print(f"\nUnexpected error: {e}\n")
        raise