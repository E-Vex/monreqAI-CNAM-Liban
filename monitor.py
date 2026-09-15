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


def load_seen() -> dict:
    """
    Load the sets of already-processed announcement IDs.

    Two independent sets, because the two notification paths no longer
    succeed or fail together:
      - "general_sent": already broadcast to the general channel. This can
        happen immediately, with no classification needed.
      - "classified":   already classified (and, if applicable, routed to
        its department channel). Only added once classification actually
        succeeds, so a classification failure gets retried on the next
        run instead of being silently dropped forever.

    Transparently upgrades the old plain-list format (from before general/
    department channels existed) by treating every id already in it as
    done for both.
    """
    if not os.path.exists(SEEN_FILE):
        return {"general_sent": [], "classified": []}

    with open(SEEN_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)

    if isinstance(data, list):
        return {"general_sent": list(data), "classified": list(data)}

    data.setdefault("general_sent", [])
    data.setdefault("classified", [])
    return data


def save_seen(seen: dict) -> None:
    """Save the sets of seen announcement IDs."""
    with open(SEEN_FILE, "w", encoding="utf-8") as f:
        json.dump(seen, f, ensure_ascii=False, indent=2)


def mark_general_sent(announcement_id: str) -> None:
    """Record that an announcement has been broadcast to the general channel."""
    seen = load_seen()
    if announcement_id not in seen["general_sent"]:
        seen["general_sent"].append(announcement_id)
        save_seen(seen)


def mark_classified(announcement_id: str) -> None:
    """Record that an announcement has been classified (and routed if applicable)."""
    seen = load_seen()
    if announcement_id not in seen["classified"]:
        seen["classified"].append(announcement_id)
        save_seen(seen)


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
    3. Return every announcement still missing at least one of the two
       notification steps (general broadcast, classification/department
       routing), tagged with which of the two it's still pending.

    Note: this no longer marks announcements as seen. That now happens
    per-item, via mark_general_sent() / mark_classified(), only once each
    step actually succeeds - see the __main__ block below.
    """
    seen    = load_seen()
    entries = fetch_feed()

    new_announcements = []

    for entry in entries:
        info = extract_entry_info(entry)

        pending_general        = info["id"] not in seen["general_sent"]
        pending_classification = info["id"] not in seen["classified"]

        if pending_general or pending_classification:
            info["pending_general"]        = pending_general
            info["pending_classification"] = pending_classification
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

# The public channel: every new announcement is posted here, unconditionally,
# regardless of category (including "other" and anything classification
# couldn't handle).
GENERAL_CHANNEL_CHAT_ID = os.environ.get("TELEGRAM_CHANNEL_GENERAL")

# One chat id per department channel, keyed by the classifier's category.
# Add an entry here (plus a matching env var) whenever a new department
# gets its own channel - "other" stays unmapped until it does.
DEPARTMENT_CHANNELS = {
    CATEGORY_CS: os.environ.get("TELEGRAM_CHANNEL_CS"),
}

# Display label per department, for the message prefix. Falls back to the
# category name (capitalized) if it's not listed here.
DEPARTMENT_LABELS = {
    CATEGORY_CS: "CS",
}


def format_general_message(info: dict) -> str:
    """Build the plain-text message for the general channel (no category label)."""
    lines = [
        info["title"],
        f"Date: {info['published']}",
    ]
    if info["summary"]:
        lines.append(info["summary"][:200])
    lines.append(info["link"])
    return "\n".join(lines)


def format_department_message(info: dict, category: str) -> str:
    """Build the plain-text message for a department channel, prefixed with its label."""
    label = DEPARTMENT_LABELS.get(category, category.capitalize())
    lines = [
        f"[{label}] {info['title']}",
        f"Date: {info['published']}",
    ]
    if info["summary"]:
        lines.append(info["summary"][:200])
    lines.append(info["link"])
    return "\n".join(lines)


def send_telegram_message(chat_id: str, text: str) -> None:
    """
    Send a plain-text message to the given Telegram chat/channel
    using the Bot API sendMessage endpoint.
    """
    if not TELEGRAM_BOT_TOKEN or not chat_id:
        raise RuntimeError(
            "TELEGRAM_BOT_TOKEN is not set, or no chat id was given for this channel."
        )

    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    body = json.dumps({
        "chat_id": chat_id,
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
                print_announcement(item, index=i)

                # 1. General channel: every new announcement goes here,
                #    unconditionally. Independent of classification, so it
                #    isn't held up if the AI step below fails.
                if item["pending_general"]:
                    try:
                        send_telegram_message(
                            GENERAL_CHANNEL_CHAT_ID, format_general_message(item)
                        )
                        mark_general_sent(item["id"])
                        print("General channel:  sent")
                    except RuntimeError as e:
                        print(f"General channel:  failed ({e})")

                # 2. Department channel: only for items the AI can classify.
                #    Only marked done once classification succeeds - a
                #    failure here means it gets retried on the next run
                #    instead of being silently dropped.
                if item["pending_classification"]:
                    try:
                        category = classify_announcement(item)
                        mark_classified(item["id"])
                    except RuntimeError as e:
                        category = None
                        print(f"Classification:   unavailable ({e})")

                    if category:
                        print(f"Category:         {category}")
                        dept_chat_id = DEPARTMENT_CHANNELS.get(category)
                        if dept_chat_id:
                            try:
                                send_telegram_message(
                                    dept_chat_id, format_department_message(item, category)
                                )
                                print(f"{category} channel:  sent")
                            except RuntimeError as e:
                                print(f"{category} channel:  failed ({e})")
        else:
            print("No new announcements since last check.\n")

    except ConnectionError as e:
        print(f"\nConnection error: {e}\n")
    except Exception as e:
        print(f"\nUnexpected error: {e}\n")
        raise
