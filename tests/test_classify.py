import pytest

from isae_monitor.classify import keywords
from isae_monitor.classify.providers import _extract_category, _read_gemini_text, ProviderError
from isae_monitor.departments import DEPARTMENTS, VALID_CATEGORIES, resolve
from isae_monitor.models import Announcement, normalize


def ann(title, summary=""):
    return Announcement(id="x", title=title, link="", published="", summary=summary)


# --- Response parsing (regression tests for the old last-match heuristic) -----

def test_parses_structured_json():
    assert _extract_category('{"category": "informatique"}') == "informatique"


def test_parses_fenced_json():
    assert _extract_category('```json\n{"category": "civil"}\n```') == "civil"


def test_parses_bare_label():
    assert _extract_category("general") == "general"


def test_legacy_cs_alias_routes_to_informatique():
    assert _extract_category('{"category": "cs"}') == "informatique"


def test_reasoning_prose_is_rejected_not_guessed():
    """
    The old parser took the LAST keyword match in the text, so this sentence
    resolved to "other" -- the exact opposite of what it says.
    """
    prose = "This is about the Computer Science dept, so not general or other"
    assert _extract_category(prose) is None


def test_empty_response_is_rejected():
    """Old behaviour silently returned 'other'; that must now fail loudly."""
    assert _extract_category("") is None


def test_negation_is_not_mined_for_keywords():
    assert _extract_category("It is not cs, it is general") is None


# --- Gemini response reading --------------------------------------------------

def test_safety_block_raises_provider_error_not_indexerror():
    payload = {"candidates": [{"finishReason": "SAFETY", "content": {"parts": []}}]}
    with pytest.raises(ProviderError):
        _read_gemini_text(payload)


def test_max_tokens_raises_provider_error_not_keyerror():
    payload = {"candidates": [{"finishReason": "MAX_TOKENS", "content": {"role": "model"}}]}
    with pytest.raises(ProviderError):
        _read_gemini_text(payload)


def test_prompt_feedback_block_is_reported():
    with pytest.raises(ProviderError):
        _read_gemini_text({"promptFeedback": {"blockReason": "OTHER"}})


def test_reads_normal_response():
    payload = {"candidates": [{"content": {"parts": [{"text": '{"category":"civil"}'}]}}]}
    assert _read_gemini_text(payload) == '{"category":"civil"}'


# --- Department registry ------------------------------------------------------

def test_nine_departments_registered():
    assert len(DEPARTMENTS) == 9


def test_every_department_has_unique_key_and_env_var():
    keys = [d.key for d in DEPARTMENTS]
    assert len(keys) == len(set(keys))
    assert len({d.env_var for d in DEPARTMENTS}) == len(keys)


def test_valid_categories_cover_departments_plus_pseudo():
    assert set(d.key for d in DEPARTMENTS) < set(VALID_CATEGORIES)
    assert "general" in VALID_CATEGORIES and "other" in VALID_CATEGORIES


@pytest.mark.parametrize("alias,expected", [
    ("cs", "informatique"),
    ("CS", "informatique"),
    ("gc", "civil"),
    ("stat", "statistique"),
    ("unknown-thing", None),
    ("", None),
    (None, None),
])
def test_alias_resolution(alias, expected):
    assert resolve(alias) == expected


# --- Normalisation ------------------------------------------------------------

def test_accents_are_folded():
    assert normalize("Génie Électrique") == "genie electrique"


def test_arabic_alef_variants_normalised():
    assert normalize("إعلان") == normalize("اعلان")


# --- Keyword fallback ---------------------------------------------------------

@pytest.mark.parametrize("title,expected", [
    ("No 9 Genie Electrique - Oraux probatoires", "electrique"),
    ("Informatique - Resultats des examens", "informatique"),
    ("Génie civil - Examens d'admission", "civil"),
    ("Master Economie et gestion", "economie"),
    ("Programmation Java : bibliothèques et patterns - NFA035", "informatique"),
    ("cours intensif niveau DELF B2", "langues"),
    ("Offre d'emploi: Comptable / Auditeur (Dekwaneh)", "other"),
])
def test_keyword_routing_real_titles(title, expected):
    assert keywords.classify(ann(title)) == expected


def test_arabic_announcement_is_not_dropped():
    """
    The original keyword list was French/English only, so every Arabic notice
    fell through to 'other' and reached nobody.
    """
    assert keywords.classify(ann("إعلان إلى طلاب بيروت وكافة المراكز")) == "general"


def test_arabic_department_notice_routes():
    assert keywords.classify(ann("تعميم لطلاب الهندسة المدنية")) == "civil"


def test_unmatched_defaults_to_general_not_other():
    """Conservative default: better to over-share than to route wrongly."""
    assert keywords.classify(ann("Note de service 42")) == "general"


def test_title_outweighs_summary():
    item = ann("Génie Civil - reunion", summary="rappel: cours d'informatique annulé")
    assert keywords.classify(item) == "civil"
