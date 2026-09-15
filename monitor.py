"""
ISAE Announcements Monitor
Step 1: Read the Atom Feed and detect new announcements
Step 2: Classify announcements using AI API (OpenRouter fallback)
"""

import feedparser
import html
import json
import os
import re
import urllib.request
import urllib.error
import time
from datetime import datetime
from typing import List, Optional, Dict, Any

# --- Settings ----------------------------------------------------------------
FEED_URL  = "http://annonces.isae.edu.lb/feeds/posts/default"
SEEN_FILE = "seen.json"
# -----------------------------------------------------------------------------

CATEGORY_GENERAL = "general"
CATEGORY_CS      = "cs"
CATEGORY_OTHER   = "other"

# --- AI Key Manager ----------------------------------------------------------
class APIKeyManager:
    """
    Manages API keys with round-robin, failure tracking, and cooldowns.
    Prioritizes Gemini, falls back to OpenRouter.
    """
    def __init__(self):
        self.gemini_keys = [k.strip() for k in os.environ.get("GEMINI_API_KEYS", "").split(",") if k.strip()]
        self.openrouter_key = os.environ.get("OPENROUTER_API_KEY", "").strip()
        self.openrouter_model = os.environ.get("OPENROUTER_MODEL", "meta-llama/llama-3.1-8b-instruct")
        
        self.state: Dict[str, Dict[str, Any]] = {}
        all_keys = self.gemini_keys + ([self.openrouter_key] if self.openrouter_key else [])
        for key in all_keys:
            self.state[key] = {"failures": 0, "cooldown_until": 0.0}

    def get_available_keys(self, provider: str) -> List[str]:
        """Returns available keys sorted by lowest failure count (best first)."""
        keys = self.gemini_keys if provider == "gemini" else ([self.openrouter_key] if self.openrouter_key else [])
        now = time.time()
        available = [k for k in keys if self.state[k]["cooldown_until"] < now]
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
        if error_code in [429, 500, 502, 503, 504]:
            self.state[key]["cooldown_until"] = time.time() + 900

key_manager = APIKeyManager()

def _call_gemini(prompt: str, api_key: str) -> str:
    """Make a classification request to Gemini API."""
    body = json.dumps({"contents": [{"parts": [{"text": prompt}]}]}).encode("utf-8")
    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={api_key}"
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as response:
        result = json.loads(response.read())
    return result["candidates"][0]["content"]["parts"][0]["text"].strip().lower()

def _call_openrouter(prompt: str, api_key: str) -> str:
    """Make a classification request to OpenRouter API (OpenAI compatible)."""
    body = json.dumps({
        "model": key_manager.openrouter_model,
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.0,
        "max_tokens": 20
    }).encode("utf-8")

    url = "https://openrouter.ai/api/v1/chat/completions"
    req = urllib.request.Request(
        url, 
        data=body, 
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
            "HTTP-Referer": "http://localhost",  # مطلوب من OpenRouter
            "X-Title": "monreqAI"
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
    """Classifies the announcement using a resilient multi-provider strategy."""
    if not key_manager.gemini_keys and not key_manager.openrouter_key:
        raise RuntimeError("No AI API keys configured (GEMINI_API_KEYS or OPENROUTER_API_KEY).")

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
            continue
        except urllib.error.URLError as e:
            key_manager.record_failure(key, 500)
            last_error = f"Gemini network error: {e}"
            continue

    # --- Phase 2: Fallback to OpenRouter ---
    openrouter_keys = key_manager.get_available_keys("openrouter")
    if openrouter_keys:
        or_key = openrouter_keys[0]
        try:
            answer = _call_openrouter(prompt, or_key)
            key_manager.record_success(or_key)
            return _parse_ai_response(answer)
        except urllib.error.HTTPError as e:
            key_manager.record_failure(or_key, e.code)
            last_error = f"OpenRouter fallback failed ({e.code})"
        except urllib.error.URLError as e:
            key_manager.record_failure(or_key, 500)
            last_error = f"OpenRouter network error: {e}"

    # --- Phase 3: Total Failure ---
    raise RuntimeError(f"All AI providers exhausted. Last error: {last_error}")


# --- Feed & Utility Functions ------------------------------------------------
def load_seen() -> dict:
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
    with open(SEEN_FILE, "w", encoding="utf-8") as f:
        json.dump(seen, f, ensure_ascii=False, indent=2)

def mark_general_sent(announcement_id: str) -> None:
    seen = load_seen()
    if announcement_id not in seen["general_sent"]:
        seen["general_sent"].append(announcement_id)
        save_seen(seen)

def mark_classified(announcement_id: str) -> None:
    seen = load_seen()
    if announcement_id not in seen["classified"]:
        seen["classified"].append(announcement_id)
        save_seen(seen)

def fetch_feed() -> list:
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
    decoded = html.unescape(raw)
    no_tags = re.sub(r"<[^>]+>", " ", decoded)
    return " ".join(no_tags.split())

def extract_entry_info(entry) -> dict:
    summary_clean = clean_html(entry.get("summary", ""))
    return {
        "id":        entry.get("id") or entry.get("link", ""),
        "title":     entry.get("title", "(no title)"),
        "link":      entry.get("link", ""),
        "published": entry.get("published", "unknown date"),
        "summary":   summary_clean[:300],
    }

def check_new_announcements() -> list:
    seen = load_seen()
    entries = fetch_feed()
    new_announcements = []
    for entry in entries:
        info = extract_entry_info(entry)
        pending_general = info["id"] not in seen["general_sent"]
        pending_classification = info["id"] not in seen["classified"]
        if pending_general or pending_classification:
            info["pending_general"] = pending_general
            info["pending_classification"] = pending_classification
            new_announcements.append(info)
    return new_announcements

def print_announcement(info: dict, index: int = None) -> None:
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


# --- Telegram Notifications ---------------------------------------------------
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN")
GENERAL_CHANNEL_CHAT_ID = os.environ.get("TELEGRAM_CHANNEL_GENERAL")

DEPARTMENT_CHANNELS = {
    CATEGORY_CS: os.environ.get("TELEGRAM_CHANNEL_CS"),
}

DEPARTMENT_LABELS = {
    CATEGORY_CS: "CS",
}

def format_general_message(info: dict) -> str:
    lines = [info["title"], f"Date: {info['published']}"]
    if info["summary"]:
        lines.append(info["summary"][:200])
    lines.append(info["link"])
    return "\n".join(lines)

def format_department_message(info: dict, category: str) -> str:
    label = DEPARTMENT_LABELS.get(category, category.capitalize())
    lines = [f"[{label}] {info['title']}", f"Date: {info['published']}"]
    if info["summary"]:
        lines.append(info["summary"][:200])
    lines.append(info["link"])
    return "\n".join(lines)

def send_telegram_message(chat_id: str, text: str) -> None:
    if not TELEGRAM_BOT_TOKEN or not chat_id:
        raise RuntimeError("TELEGRAM_BOT_TOKEN is not set, or no chat id was given.")

    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    body = json.dumps({
        "chat_id": chat_id,
        "text": text,
        "disable_web_page_preview": True,
    }).encode("utf-8")

    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
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
    missing = []
    if not TELEGRAM_BOT_TOKEN:
        missing.append("TELEGRAM_BOT_TOKEN")
    if not GENERAL_CHANNEL_CHAT_ID:
        missing.append("TELEGRAM_CHANNEL_GENERAL")
    
    gemini_keys = os.environ.get("GEMINI_API_KEYS", "").strip()
    openrouter_key = os.environ.get("OPENROUTER_API_KEY", "").strip()
    
    if not gemini_keys and not openrouter_key:
        missing.append("GEMINI_API_KEYS or OPENROUTER_API_KEY")

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

                if item["pending_general"]:
                    try:
                        send_telegram_message(GENERAL_CHANNEL_CHAT_ID, format_general_message(item))
                        mark_general_sent(item["id"])
                        print("General channel:  sent")
                    except RuntimeError as e:
                        print(f"General channel:  failed ({e})")

                if item["pending_classification"]:
                    category = None
                    try:
                        category = classify_announcement(item)
                    except RuntimeError as e:
                        print(f"Classification:   unavailable ({e})")

                    if category:
                        print(f"Category:         {category}")
                        dept_chat_id = DEPARTMENT_CHANNELS.get(category)
                        dept_routed = True
                        if dept_chat_id:
                            try:
                                send_telegram_message(dept_chat_id, format_department_message(item, category))
                                print(f"{category} channel:  sent")
                            except RuntimeError as e:
                                print(f"{category} channel:  failed ({e})")
                                dept_routed = False

                        if dept_routed:
                            mark_classified(item["id"])
        else:
            print("No new announcements since last check.\n")

    except ConnectionError as e:
        print(f"\nConnection error: {e}\n")
    except Exception as e:
        print(f"\nUnexpected error: {e}\n")
        raise