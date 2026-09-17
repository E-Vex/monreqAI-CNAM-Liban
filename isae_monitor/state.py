"""
Persistent "what have I already handled" store.

Fixes three problems with the original seen.json handling:
  1. writes were not atomic, so an interrupted run left corrupt JSON that
     killed every subsequent run;
  2. every mark_* call re-read and re-wrote the whole file;
  3. membership was a list scan against a file that grew forever.

Format v2:
    {"version": 2, "seen": {"<id>": {"g": bool, "c": "<category>|null", "t": int}}}
"""

from __future__ import annotations

import json
import os
import tempfile
import time
from typing import Dict, Optional

VERSION = 2


class State:
    def __init__(self, path: str, history: int = 1000):
        self.path = path
        self.history = history
        self._seen: Dict[str, dict] = {}
        self._dirty = False
        self.recovered_from_corruption = False
        self.load()

    # --- Loading & migration -------------------------------------------------

    def load(self) -> None:
        if not os.path.exists(self.path):
            self._seen = {}
            return
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError):
            # A corrupt state file must never be silently discarded: dropping
            # it re-notifies every announcement in the feed. Keep a copy and
            # start empty, but flag it so the caller can warn (and so the
            # operator can choose --bootstrap instead of being spammed).
            self._quarantine()
            self._seen = {}
            self.recovered_from_corruption = True
            return

        self._seen = self._migrate(data)

    def _quarantine(self) -> None:
        try:
            os.replace(self.path, self.path + ".corrupt")
        except OSError:
            pass

    @staticmethod
    def _migrate(data) -> Dict[str, dict]:
        now = int(time.time())

        # v0: a plain list of ids.
        if isinstance(data, list):
            return {str(i): {"g": True, "c": None, "t": now} for i in data}

        if not isinstance(data, dict):
            return {}

        if data.get("version") == VERSION and isinstance(data.get("seen"), dict):
            return {str(k): v for k, v in data["seen"].items() if isinstance(v, dict)}

        # v1: {"general_sent": [...], "classified": [...]}
        if "general_sent" in data or "classified" in data:
            out: Dict[str, dict] = {}
            for i in data.get("general_sent", []) or []:
                out.setdefault(str(i), {"g": False, "c": None, "t": now})["g"] = True
            for i in data.get("classified", []) or []:
                entry = out.setdefault(str(i), {"g": False, "c": None, "t": now})
                # v1 recorded that it was classified but not as what.
                entry["c"] = entry.get("c") or "unknown"
            return out

        return {}

    # --- Queries -------------------------------------------------------------

    def needs_general(self, announcement_id: str) -> bool:
        return not self._seen.get(announcement_id, {}).get("g", False)

    def needs_classification(self, announcement_id: str) -> bool:
        return self._seen.get(announcement_id, {}).get("c") is None

    def category_of(self, announcement_id: str) -> Optional[str]:
        return self._seen.get(announcement_id, {}).get("c")

    def __len__(self) -> int:
        return len(self._seen)

    # --- Mutations -----------------------------------------------------------

    def _entry(self, announcement_id: str) -> dict:
        return self._seen.setdefault(
            announcement_id, {"g": False, "c": None, "t": int(time.time())}
        )

    def mark_general_sent(self, announcement_id: str) -> None:
        self._entry(announcement_id)["g"] = True
        self._dirty = True

    def mark_classified(self, announcement_id: str, category: str) -> None:
        self._entry(announcement_id)["c"] = category
        self._dirty = True

    def mark_all_seen(self, announcement_id: str, category: str = "bootstrap") -> None:
        entry = self._entry(announcement_id)
        entry["g"] = True
        entry["c"] = category
        self._dirty = True

    # --- Persistence ---------------------------------------------------------

    def _prune(self) -> None:
        if len(self._seen) <= self.history:
            return
        ordered = sorted(self._seen.items(), key=lambda kv: kv[1].get("t", 0), reverse=True)
        self._seen = dict(ordered[: self.history])

    def save(self, force: bool = False) -> None:
        """Atomically persist. Write to a temp file, fsync, then rename."""
        if not self._dirty and not force:
            return
        self._prune()
        payload = {"version": VERSION, "seen": self._seen}

        directory = os.path.dirname(os.path.abspath(self.path)) or "."
        os.makedirs(directory, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=directory, prefix=".seen-", suffix=".tmp")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(payload, f, ensure_ascii=False, indent=2)
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp, self.path)   # atomic on POSIX and Windows
            self._dirty = False
        except BaseException:
            if os.path.exists(tmp):
                os.unlink(tmp)
            raise

    def __enter__(self) -> "State":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        # Persist whatever progress was made, even on the way out of an error.
        try:
            self.save()
        except OSError:
            pass
