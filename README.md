# ISAE Announcements Monitor

A small tool that watches the ISAE announcements page (isae.edu.lb) of CNAM Liban and notifies students when a new announcement is posted, instead of having to check the site manually.

## How it works

The ISAE announcements site runs on Blogger, so the tool reads its Atom feed directly instead of scraping HTML:

```
http://annonces.isae.edu.lb/feeds/posts/default
```

On each run, the tool:

1. Fetches the feed and compares entries against a local list of already-seen announcements (`seen.json`).
2. For each new announcement, asks an AI model to classify it as:
   - `general` – relevant to all students
   - `cs` – relevant to Computer Science students specifically
   - `other` – relevant to a different department or not academic
3. Sends a Telegram notification for `general` and `cs` announcements, with the title and link.

## Requirements

- Python 3
- Dependencies listed in `requirements.txt`

```
pip install -r requirements.txt
```

## Configuration

The tool needs two things to run, set as environment variables (never hardcoded):

- `AI_API_KEYS` -> one or more API keys for the AI classification model, comma-separated (`key1,key2,key3`). If a key hits its quota, the tool automatically rotates to the next one. `AI_API_KEY` (single key) is still supported for backward compatibility.
- `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` -> for sending notifications

```
export AI_API_KEYS="key1,key2,key3"
export TELEGRAM_BOT_TOKEN="your_bot_token"
export TELEGRAM_CHAT_ID="your_chat_id"
```

## Running

```
python3 monitor.py
```

Meant to run on a schedule (cron, GitHub Actions, or similar), not continuously.

## Testing

`test_local.py` simulates the feed with fake entries to check the "detect new announcement" logic without hitting the network.

`test_classify.py` tests the AI classification on a few real sample announcements.

## Status

Work in progress. Currently built for a single recipient. Multi-department and multi-subscriber support is planned.# ISAE Announcements Monitor

A small tool that watches the ISAE announcements page (isae.edu.lb) of CNAM Liban and notifies students when a new announcement is posted, instead of having to check the site manually.

## How it works

The ISAE announcements site runs on Blogger, so the tool reads its Atom feed directly instead of scraping HTML:

```
http://annonces.isae.edu.lb/feeds/posts/default
```

On each run, the tool:

1. Fetches the feed and compares entries against a local list of already-seen announcements (`seen.json`).
2. For each new announcement, asks an AI model to classify it as:
   - `general` – relevant to all students
   - `cs` – relevant to Computer Science students specifically
   - `other` – relevant to a different department or not academic
3. Sends a Telegram notification for `general` and `cs` announcements, with the title and link.

## Requirements

- Python 3
- Dependencies listed in `requirements.txt`

```
pip install -r requirements.txt
```

## Configuration

The tool needs two things to run, set as environment variables (never hardcoded):

- `AI_API_KEYS` -> one or more API keys for the AI classification model, comma-separated (`key1,key2,key3`). If a key hits its quota, the tool automatically rotates to the next one. `AI_API_KEY` (single key) is still supported for backward compatibility.
- `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` -> for sending notifications

```
export AI_API_KEYS="key1,key2,key3"
export TELEGRAM_BOT_TOKEN="your_bot_token"
export TELEGRAM_CHAT_ID="your_chat_id"
```

## Running

```
python3 monitor.py
```

Meant to run on a schedule (cron, GitHub Actions, or similar), not continuously.

## Testing

`test_local.py` simulates the feed with fake entries to check the "detect new announcement" logic without hitting the network.

`test_classify.py` tests the AI classification on a few real sample announcements.

## Status

Work in progress. Currently built for a single recipient. Multi-department and multi-subscriber support is planned.
