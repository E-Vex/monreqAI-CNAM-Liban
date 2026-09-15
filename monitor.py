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
        its department channel). Only added once classification succeeds
        AND (the department-channel send succeeds OR no department channel
        is configured for that category), so a failure at any step gets
        retried on the next run instead of being silently dropped forever.

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
            info["pending_general"]         = pending_general
            info["pending_classification"]  = pending_classification
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


# --- AI Classification & Smart Key Pooling -----------------------------------
import time
from typing import List, Optional, Dict, Any

CATEGORY_GENERAL = "general"
CATEGORY_CS      = "cs"
CATEGORY_OTHER   = "other"

class APIKeyManager:
    """
    manages API keys with round-robin, failure tracking, and cooldowns.
    Designed to be instantiated per run. If run via cron, cooldowns reset,
    but the priority sorting ensures consistently failing keys are tried last,
    minimizing latency before falling back to Groq.
    """
    def __init__(self):
        self.gemini_keys = [k.strip() for k in os.environ.get("GEMINI_API_KEYS", "").split(",") if k.strip()]
        self.groq_key = os.environ.get("GROQ_API_KEY", "").strip()
        self.groq_model = os.environ.get("GROQ_MODEL", "llama3-8b-8192")
        
        # Track state: { "key_string": {"failures": int, "cooldown_until": float} }
        self.state: Dict[str, Dict[str, Any]] = {}
        for key in self.gemini_keys + ([self.groq_key] if self.groq_key else []):
            self.state[key] = {"failures": 0, "cooldown_until": 0.0}

    def get_available_keys(self, provider: str) -> List[str]:
        """Returns available keys sorted by lowest failure count (best first)."""
        keys = self.gemini_keys if provider == "gemini" else ([self.groq_key] if self.groq_key else [])
        now = time.time()
        
        available = [
            k for k in keys 
            if self.state[k]["cooldown_until"] < now
        ]
        # Sort by failures ascending (healthiest keys first)
        available.sort(key=lambda k: self.state[k]["failures"])
        return available

    def record_success(self, key: str):
        if key in self.state:
            self.state[key]["failures"] = 0
            self.state[key]["cooldown_until"] = 0.0

    def record_failure(self, key: str, error_code: int):
        if key not in self.state:
            return
        self.state[key]["failures"] += 1
        # Cool down for 15 minutes on quota/server errors to save time in current run
        if error_code in [429, 500, 502, 503, 504]:
            self.state[key]["cooldown_until"] = time.time() + 900


# Initialize manager globally for this run
key_manager = APIKeyManager()

def _call_gemini(prompt: str, api_key: str) -> str:
    """Make a classification request to Gemini API."""
    body = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}]
    }).encode("utf-8")

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={api_key}"
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})

    with urllib.request.urlopen(req, timeout=15) as response:
        result = json.loads(response.read())
    
    return result["candidates"][0]["content"]["parts"][0]["text"].strip().lower()

def _call_groq(prompt: str, api_key: str) -> str:
    """Make a classification request to Groq API (OpenAI compatible)."""
    body = json.dumps({
        "model": key_manager.groq_model,
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.0,
        "max_tokens": 10
    }).encode("utf-8")

    url = "https://api.groq.com/openai/v1/chat/completions"
    req = urllib.request.Request(
        url, 
        data=body, 
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}"
        }
    )

    with urllib.request.urlopen(req, timeout=15) as response:
        result = json.loads(response.read())
    
    return result["choices"][0]["message"]["content"].strip().lower()

def _parse_ai_response(answer: str) -> str:
    """Safely extract the category from the AI's text response."""
    answer = answer.lower()
    if "cs" in answer or "computer" in answer or "informatique" in answer:
        return CATEGORY_CS
    if "general" in answer:
        return CATEGORY_GENERAL
    return CATEGORY_OTHER

def classify_announcement(info: dict) -> str:
    """
    Classifies the announcement using a resilient multi-provider strategy:
    1. Tries available Gemini keys (sorted by health).
    2. If all Gemini keys fail (e.g., all hit 429), falls back to Groq.
    3. Raises RuntimeError only if ALL providers are exhausted, allowing 
       the main loop to mark it as pending for the next run.
    """
    if not key_manager.gemini_keys and not key_manager.groq_key:
        raise RuntimeError("No AI API keys configured (GEMINI_API_KEYS or GROQ_API_KEY).")

    prompt = f"""You classify university announcements for an engineering institute.

Title: {info['title']}
Summary: {info['summary']}

Classify this announcement into exactly one category:
- general: relevant to all students (deadlines, holidays, registration, general exams...)
- cs: specifically relevant to Computer Science / Informatique students
- other: relevant to another department or unrelated to studies.

Reply with exactly one word: general or cs or other"""

    last_error = None

    # --- Phase 1: Try Gemini Keys ---
    for key in key_manager.get_available_keys("gemini"):
        try:
            answer = _call_gemini(prompt, key)
            key_manager.record_success(key)
            return _parse_ai_response(answer)
        except urllib.error.HTTPError as e:
            key_manager.record_failure(key, e.code)
            last_error = f"Gemini key failed ({e.code})"
            continue # Move to next Gemini key
        except urllib.error.URLError as e:
            key_manager.record_failure(key, 500)
            last_error = f"Gemini network error: {e}"
            continue

    # --- Phase 2: Fallback to Groq ---
    groq_keys = key_manager.get_available_keys("groq")
    if groq_keys:
        groq_key = groq_keys[0]
        try:
            answer = _call_groq(prompt, groq_key)
            key_manager.record_success(groq_key)
            return _parse_ai_response(answer)
        except urllib.error.HTTPError as e:
            key_manager.record_failure(groq_key, e.code)
            last_error = f"Groq fallback failed ({e.code})"
        except urllib.error.URLError as e:
            key_manager.record_failure(groq_key, 500)
            last_error = f"Groq network error: {e}"

    # --- Phase 3: Total Failure ---
    raise RuntimeError(f"All AI providers exhausted. Last error: {last_error}")



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

    Both HTTP-level errors (4xx/5xx) and network-level errors (DNS failure,
    timeout, connection refused) are wrapped as RuntimeError so the main
    loop can catch them uniformly and decide whether to retry on the next
    run. HTTPError is a subclass of URLError, so the order of the except
    clauses matters: HTTPError is matched first.
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
    except urllib.error.URLError as e:
        raise RuntimeError(f"Telegram network error: {e}")

    if not result.get("ok"):
        raise RuntimeError(f"Telegram API returned an error: {result}")


# --- Startup sanity checks ---------------------------------------------------
def _warn_missing_config() -> None:
    """
    Print one-time warnings for misconfigurations instead of failing per-item
    on every single announcement. The script keeps running so items that
    don't depend on the missing config (e.g. classification without a
    general channel) still make progress.
    """
    missing = []
    if not TELEGRAM_BOT_TOKEN:
        missing.append("TELEGRAM_BOT_TOKEN")
    if not GENERAL_CHANNEL_CHAT_ID:
        missing.append("TELEGRAM_CHANNEL_GENERAL")
    if not AI_API_KEYS:
        missing.append("AI_API_KEYS / AI_API_KEY")

    if missing:
        print("  Configuration warnings:")
        for var in missing:
            print(f"    - {var} is not set (related features will be skipped)")
        print()


# --- Entry point -------------------------------------------------------------
if __name__ == "__main__":
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"\n[{timestamp}] Checking ISAE announcements...")

    _warn_missing_config()

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
                #    Only marked done once classification succeeds AND (the
                #    department send succeeds OR no department channel is
                #    configured for this category). A failure at any step
                #    means the whole step is retried on the next run instead
                #    of being silently dropped forever.
                if item["pending_classification"]:
                    category = None
                    try:
                        category = classify_announcement(item)
                    except RuntimeError as e:
                        print(f"Classification:   unavailable ({e})")

                    if category:
                        print(f"Category:         {category}")
                        dept_chat_id = DEPARTMENT_CHANNELS.get(category)
                        dept_routed = True  # assume success / no routing needed
                        if dept_chat_id:
                            try:
                                send_telegram_message(
                                    dept_chat_id, format_department_message(item, category)
                                )
                                print(f"{category} channel:  sent")
                            except RuntimeError as e:
                                print(f"{category} channel:  failed ({e})")
                                dept_routed = False

                        # Only mark as classified if the department send
                        # succeeded (or no department channel was needed).
                        # Otherwise leave it pending so classification +
                        # routing both retry on the next run.
                        if dept_routed:
                            mark_classified(item["id"])
        else:
            print("No new announcements since last check.\n")

    except ConnectionError as e:
        print(f"\nConnection error: {e}\n")
    except Exception as e:
        print(f"\nUnexpected error: {e}\n")
        raise
