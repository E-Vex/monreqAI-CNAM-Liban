import json
import os

import pytest

from isae_monitor.state import State


def test_new_file_starts_empty(tmp_path):
    s = State(str(tmp_path / "seen.json"))
    assert len(s) == 0
    assert s.needs_general("x")
    assert s.needs_classification("x")


def test_marking_and_roundtrip(tmp_path):
    path = str(tmp_path / "seen.json")
    s = State(path)
    s.mark_general_sent("a")
    s.mark_classified("a", "informatique")
    s.save()

    reloaded = State(path)
    assert not reloaded.needs_general("a")
    assert not reloaded.needs_classification("a")
    assert reloaded.category_of("a") == "informatique"


def test_partial_state_retries_only_missing_half(tmp_path):
    """General sent but classification pending: only the latter should retry."""
    path = str(tmp_path / "seen.json")
    s = State(path)
    s.mark_general_sent("a")
    s.save()

    reloaded = State(path)
    assert not reloaded.needs_general("a")
    assert reloaded.needs_classification("a")


def test_writes_are_atomic_no_partial_file(tmp_path, monkeypatch):
    """A failure mid-write must leave the previous good file intact."""
    path = str(tmp_path / "seen.json")
    s = State(path)
    s.mark_general_sent("good")
    s.save()
    original = open(path, encoding="utf-8").read()

    def explode(*a, **k):
        raise OSError("disk full")

    monkeypatch.setattr(json, "dump", explode)
    s.mark_general_sent("bad")
    with pytest.raises(OSError):
        s.save()

    # Old content survived, and no temp files were left behind.
    assert open(path, encoding="utf-8").read() == original
    assert [f for f in os.listdir(tmp_path) if f.startswith(".seen-")] == []


def test_corrupt_file_is_quarantined_not_deleted(tmp_path):
    path = tmp_path / "seen.json"
    path.write_text('{"version": 2, "seen": {"a"', encoding="utf-8")

    s = State(str(path))
    assert s.recovered_from_corruption
    assert (tmp_path / "seen.json.corrupt").exists(), "must keep the bad file for inspection"


def test_migrates_v0_plain_list(tmp_path):
    path = tmp_path / "seen.json"
    path.write_text(json.dumps(["id-1", "id-2"]), encoding="utf-8")

    s = State(str(path))
    assert not s.needs_general("id-1")
    assert not s.needs_general("id-2")
    assert s.needs_general("id-3")


def test_migrates_v1_two_list_format(tmp_path):
    path = tmp_path / "seen.json"
    path.write_text(json.dumps({
        "general_sent": ["a", "b"],
        "classified": ["a"],
    }), encoding="utf-8")

    s = State(str(path))
    assert not s.needs_general("a")
    assert not s.needs_classification("a")
    assert not s.needs_general("b")
    assert s.needs_classification("b"), "b was sent but never classified"


def test_pruning_keeps_history_bounded(tmp_path):
    path = str(tmp_path / "seen.json")
    s = State(path, history=50)
    for i in range(200):
        s.mark_general_sent(f"id-{i}")
    s.save()

    assert len(State(path, history=50)) == 50


def test_context_manager_saves_on_exit(tmp_path):
    path = str(tmp_path / "seen.json")
    with State(path) as s:
        s.mark_general_sent("a")
    assert not State(path).needs_general("a")
