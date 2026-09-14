"""
Local test - simulates the feed without an internet connection.
Verifies that the "detect new" and "save seen" logic works correctly.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import monitor

# --- Fake feed entries -------------------------------------------------------
FAKE_ENTRIES = [
    {
        "id":        "tag:blogger.com,1999:blog-123.post-001",
        "title":     "Important: Final Exam Schedule",
        "link":      "http://annonces.isae.edu.lb/2025/01/exam-schedule.html",
        "published": "Mon, 06 Jan 2025 10:00:00 +0000",
        "summary":   "Students are informed that the final exam schedule for semester 1 is now available...",
    },
    {
        "id":        "tag:blogger.com,1999:blog-123.post-002",
        "title":     "Workshop: Intro to AI - Computer Science Dept.",
        "link":      "http://annonces.isae.edu.lb/2025/01/ai-workshop.html",
        "published": "Tue, 07 Jan 2025 09:00:00 +0000",
        "summary":   "The CS department invites its students to an intro to AI workshop...",
    },
    {
        "id":        "tag:blogger.com,1999:blog-123.post-003",
        "title":     "Notice for Civil Engineering students only",
        "link":      "http://annonces.isae.edu.lb/2025/01/civil-eng.html",
        "published": "Wed, 08 Jan 2025 08:00:00 +0000",
        "summary":   "Civil Engineering students are asked to contact their supervisor...",
    },
]


class FakeEntry:
    """Mimics a feedparser entry object."""
    def __init__(self, data: dict):
        self._data = data
    def get(self, key, default=""):
        return self._data.get(key, default)


def simulate_check(entries, seen_file: str) -> list:
    """Run check_new_announcements logic on fake entries."""
    monitor.SEEN_FILE = seen_file
    seen_ids = monitor.load_seen()
    new_items = []

    for entry in entries:
        info = monitor.extract_entry_info(entry)
        if info["id"] not in seen_ids:
            new_items.append(info)
            seen_ids.append(info["id"])
            monitor.print_announcement(info)

    if new_items:
        monitor.save_seen(seen_ids)

    return new_items


def run_tests():
    TEST_SEEN = "seen_test.json"
    if os.path.exists(TEST_SEEN):
        os.remove(TEST_SEEN)

    fake = [FakeEntry(e) for e in FAKE_ENTRIES]

    print("=" * 55)
    print("  Local Test - Feed Simulation")
    print("=" * 55)

    # Run 1: everything is new
    print("\n>> Run 1 (no prior records - all entries are new):")
    found = simulate_check(fake, TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 3, "Expected 3 new announcements on first run"

    # Run 2: nothing new
    print("\n>> Run 2 (same entries - nothing new):")
    found = simulate_check(fake, TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 0, "Expected 0 new announcements on second run"

    # Run 3: one new entry added
    new_entry = FakeEntry({
        "id":        "tag:blogger.com,1999:blog-123.post-004",
        "title":     "Registration Deadline Extended",
        "link":      "http://annonces.isae.edu.lb/2025/01/registration.html",
        "published": "Thu, 09 Jan 2025 10:00:00 +0000",
        "summary":   "All students are informed that the registration deadline has been extended.",
    })
    print("\n>> Run 3 (one new entry added):")
    found = simulate_check(fake + [new_entry], TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 1, "Expected exactly 1 new announcement on third run"

    os.remove(TEST_SEEN)

    print("\n" + "=" * 55)
    print("  All tests passed!")
    print("=" * 55)


if __name__ == "__main__":
    run_tests()
