import pytest

from isae_monitor.config import Settings
from isae_monitor.models import Announcement, strip_html
from isae_monitor.notify.telegram import MAX_MESSAGE, format_message
from isae_monitor.pipeline import select_pending
from isae_monitor.state import State


def ann(aid, title="Titre", summary="", link="http://x.lb/a"):
    return Announcement(id=aid, title=title, link=link,
                        published="Mon, 06 Jan 2025", summary=summary)


# --- Config -------------------------------------------------------------------

def test_department_channels_parsed_from_env():
    s = Settings.from_env({
        "TELEGRAM_CHANNEL_INFORMATIQUE": "-1001",
        "TELEGRAM_CHANNEL_CIVIL": "-1002",
    })
    assert s.department_channels == {"informatique": "-1001", "civil": "-1002"}


def test_legacy_cs_channel_var_still_works():
    s = Settings.from_env({"TELEGRAM_CHANNEL_CS": "-100old"})
    assert s.department_channels["informatique"] == "-100old"


def test_new_var_wins_over_legacy():
    s = Settings.from_env({
        "TELEGRAM_CHANNEL_CS": "-100old",
        "TELEGRAM_CHANNEL_INFORMATIQUE": "-100new",
    })
    assert s.department_channels["informatique"] == "-100new"


def test_singular_gemini_key_tolerated():
    assert Settings.from_env({"GEMINI_API_KEY": "abc"}).gemini_keys == ["abc"]


def test_gemini_keys_split_and_stripped():
    s = Settings.from_env({"GEMINI_API_KEYS": " a , b ,, c "})
    assert s.gemini_keys == ["a", "b", "c"]


def test_missing_config_is_reported_not_crashed():
    problems = Settings.from_env({}).problems()
    assert any("TELEGRAM_BOT_TOKEN" in p for p in problems)


def test_feed_url_defaults_to_https():
    assert Settings.from_env({}).feed_url.startswith("https://")


# --- Pending selection --------------------------------------------------------

def test_select_pending_skips_fully_processed(tmp_path):
    state = State(str(tmp_path / "s.json"))
    state.mark_general_sent("a")
    state.mark_classified("a", "general")

    pending = select_pending([ann("a"), ann("b")], state)
    assert [p.id for p in pending] == ["b"]


def test_select_pending_flags_partial_work(tmp_path):
    state = State(str(tmp_path / "s.json"))
    state.mark_general_sent("a")

    pending = select_pending([ann("a")], state)
    assert len(pending) == 1
    assert pending[0].needs_general is False
    assert pending[0].needs_classification is True


# --- Message formatting -------------------------------------------------------

def test_long_title_cannot_exceed_telegram_limit():
    """A 3000-char title used to produce an HTTP 400 and stall forever."""
    item = ann("x", title="T" * 3000, summary="S" * 3000)
    assert len(format_message(item)) <= MAX_MESSAGE + 1


def test_html_is_escaped():
    item = ann("x", title="Maths & <b>Physique</b>")
    message = format_message(item)
    assert "&amp;" in message and "&lt;b&gt;" in message


def test_department_label_is_prefixed():
    assert "Génie Civil" in format_message(ann("x"), "civil")


def test_link_is_included():
    assert "http://x.lb/a" in format_message(ann("x"))


# --- HTML stripping -----------------------------------------------------------

def test_strip_html_unescapes_and_flattens():
    assert strip_html("<p>Bonjour&nbsp;&amp; bonsoir</p>").replace("\xa0", " ").strip() \
        == "Bonjour & bonsoir"


def test_strip_html_handles_empty():
    assert strip_html("") == ""


# --- Announcement parsing -----------------------------------------------------

def test_announcement_falls_back_to_link_when_id_missing():
    class E(dict):
        def get(self, k, d=""):
            return dict.get(self, k, d)

    item = Announcement.from_entry(E(link="http://x.lb/post"))
    assert item.id == "http://x.lb/post"


def test_announcement_handles_missing_title():
    class E(dict):
        def get(self, k, d=""):
            return dict.get(self, k, d)

    assert Announcement.from_entry(E(id="1")).title == "(sans titre)"
