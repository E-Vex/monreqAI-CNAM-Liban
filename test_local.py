"""
Local test - simulates the feed without an internet connection.
Verifies that the "detect new" and "save seen" logic works correctly,
including:
  - the two-state (general_sent / classified) tracking
  - the partial-failure recovery (general sent, classification pending)
  - the backward-compat upgrade from the old list-format seen.json
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


def _patch_feed(entries):
    """Replace monitor.fetch_feed so it returns the given entries (no network)."""
    monitor.fetch_feed = lambda: entries


def simulate_check(entries, seen_file: str) -> list:
    """
    Run the real check_new_announcements() against fake entries, then mark
    whatever it finds as fully processed - same as monitor.py would after a
    successful general-channel send + classification.
    """
    monitor.SEEN_FILE = seen_file
    _patch_feed(entries)

    new_items = monitor.check_new_announcements()

    for info in new_items:
        monitor.print_announcement(info)
        monitor.mark_general_sent(info["id"])
        monitor.mark_classified(info["id"])

    return new_items


def simulate_partial_general_only(entries, seen_file: str) -> list:
    """
    Simulate a run where only the general-channel send succeeds but
    classification is skipped (as would happen if the AI API was down).

    The next check_new_announcements() call should still return these items
    with pending_classification=True, so classification gets retried.
    """
    monitor.SEEN_FILE = seen_file
    _patch_feed(entries)

    new_items = monitor.check_new_announcements()

    for info in new_items:
        monitor.mark_general_sent(info["id"])
        # intentionally NOT calling mark_classified() - simulating failure

    return new_items


def run_tests():
    TEST_SEEN = "seen_test.json"
    if os.path.exists(TEST_SEEN):
        os.remove(TEST_SEEN)

    fake = [FakeEntry(e) for e in FAKE_ENTRIES]

    print("=" * 55)
    print("  Local Test - Feed Simulation")
    print("=" * 55)

    # --- Test 1: full happy path (same as before) ---
    print("\n>> Test 1: first run, everything new, all marked:")
    found = simulate_check(fake, TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 3, f"Expected 3, got {len(found)}"
    # pending flags should both be True for new items
    for item in found:
        assert item["pending_general"] is True, "new items should be pending general"
        assert item["pending_classification"] is True, "new items should be pending classification"

    # --- Test 2: nothing new on second run ---
    print("\n>> Test 2: second run, nothing new:")
    found = simulate_check(fake, TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 0, f"Expected 0, got {len(found)}"

    # --- Test 3: one new entry added ---
    print("\n>> Test 3: one new entry added:")
    new_entry = FakeEntry({
        "id":        "tag:blogger.com,1999:blog-123.post-004",
        "title":     "Registration Deadline Extended",
        "link":      "http://annonces.isae.edu.lb/2025/01/registration.html",
        "published": "Thu, 09 Jan 2025 10:00:00 +0000",
        "summary":   "All students are informed that the registration deadline has been extended.",
    })
    found = simulate_check(fake + [new_entry], TEST_SEEN)
    print(f"  -> New announcements found: {len(found)}")
    assert len(found) == 1, f"Expected 1, got {len(found)}"

    # --- Test 4: partial run - general sent, classification skipped ---
    print("\n>> Test 4: partial run (general sent, classification pending):")
    os.remove(TEST_SEEN)
    found = simulate_partial_general_only(fake, TEST_SEEN)
    print(f"  -> Items processed (general only): {len(found)}")
    assert len(found) == 3, f"Expected 3, got {len(found)}"

    # On the next run, all 3 should come back with pending_classification=True
    # but pending_general=False (general already sent).
    print("\n>> Test 4b: re-run after partial - items still pending classification:")
    monitor.SEEN_FILE = TEST_SEEN
    _patch_feed(fake)
    found_again = monitor.check_new_announcements()
    print(f"  -> Items returned again: {len(found_again)}")
    assert len(found_again) == 3, f"Expected 3 items still pending, got {len(found_again)}"
    for item in found_again:
        assert item["pending_general"] is False, "general should already be sent"
        assert item["pending_classification"] is True, "classification should be pending"

    # --- Test 5: backward-compat upgrade from old list format ---
    print("\n>> Test 5: upgrade old list-format seen.json:")
    os.remove(TEST_SEEN)
    with open(TEST_SEEN, "w", encoding="utf-8") as f:
        # write the old format: a plain list of IDs
        json.dump([
            "tag:blogger.com,1999:blog-123.post-001",
            "tag:blogger.com,1999:blog-123.post-002",
        ], f)

    seen = monitor.load_seen()
    assert isinstance(seen, dict), f"Expected dict after upgrade, got {type(seen)}"
    assert "general_sent" in seen and "classified" in seen, "missing keys after upgrade"
    assert len(seen["general_sent"]) == 2, "general_sent should have 2 entries"
    assert len(seen["classified"]) == 2, "classified should have 2 entries"
    # only post-003 should be new
    _patch_feed(fake)
    found = monitor.check_new_announcements()
    print(f"  -> New items after upgrade: {len(found)}")
    assert len(found) == 1, f"Expected 1 new after upgrade, got {len(found)}"
    assert found[0]["id"] == "tag:blogger.com,1999:blog-123.post-003"

    # --- Cleanup ---
    if os.path.exists(TEST_SEEN):
        os.remove(TEST_SEEN)

    print("\n" + "=" * 55)
    print("  All tests passed!")
    print("=" * 55)


if __name__ == "__main__":
    run_tests()
