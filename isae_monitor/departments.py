"""
The department registry: the single source of truth for every major.

Sourced from the institute's own department listing on newwww.isae.edu.lb.
Everything else in the codebase derives from this file -- the classifier's
allowed labels, the env var names, the Telegram routing table, the keyword
fallback. Adding a new major means adding one entry here and nothing else.

Note on `key`: it is a stable identifier used in seen.json and in env var
names. Renaming a key is a breaking change for anyone's existing state file,
so pick carefully and prefer adding an alias over renaming.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# Pseudo-categories. These are not departments but are valid classifications.
GENERAL = "general"
OTHER = "other"


@dataclass(frozen=True)
class Department:
    key: str                      # stable id, used in env vars + state
    name_fr: str                  # official French name
    name_en: str                  # English name, for the prompt
    label: str                    # short tag shown in Telegram messages
    keywords: Tuple[str, ...]     # fallback matching (fr / en / ar)
    aliases: Tuple[str, ...] = () # extra labels an LLM might emit

    @property
    def env_var(self) -> str:
        """Telegram channel env var for this department."""
        return f"TELEGRAM_CHANNEL_{self.key.upper()}"


# Ordered most-specific-first. keyword_classify() walks this in order, so a
# department whose keywords could be a substring of another's should come
# later. Keywords are matched case-insensitively against accent-folded text,
# so write them unaccented ("genie" not "génie").
DEPARTMENTS: Tuple[Department, ...] = (
    Department(
        key="informatique",
        name_fr="Génie Informatique",
        name_en="Computer Engineering / Computer Science",
        label="Informatique",
        keywords=(
            "informatique", "genie informatique", "computer science",
            "computer engineering", "programmation", "programming",
            "algorithme", "algorithm", "reseaux", "network", "logiciel",
            "software", "base de donnees", "database", "cybersecurite",
            "intelligence artificielle", "هندسة المعلوماتية", "معلوماتية",
            "برمجة",
        ),
        aliases=("cs", "info", "computer", "it"),
    ),
    Department(
        key="civil",
        name_fr="Génie Civil",
        name_en="Civil Engineering",
        label="Génie Civil",
        keywords=(
            "genie civil", "civil engineering", "batiment", "construction",
            "beton", "structures", "parasismique", "topographie",
            "ouvrages d'art", "routes", "hydraulique",
            "الهندسة المدنية", "هندسة مدنية", "بناء",
        ),
        aliases=("gc", "civile"),
    ),
    Department(
        key="electrique",
        name_fr="Génie Électrique",
        name_en="Electrical Engineering",
        label="Génie Électrique",
        keywords=(
            "genie electrique", "electrical engineering", "electrotechnique",
            "electronique", "automatique", "signal", "ascensoriste",
            "energetique", "climatique", "froid", "hvac",
            "الهندسة الكهربائية", "هندسة كهربائية", "كهرباء",
        ),
        aliases=("ge", "electrical", "electricite"),
    ),
    Department(
        key="mecanique",
        name_fr="Génie Mécanique",
        name_en="Mechanical Engineering",
        label="Génie Mécanique",
        keywords=(
            "genie mecanique", "mechanical engineering", "mecanique",
            "fabrication", "dessins industriels", "machines",
            "mecanique des structures", "thermodynamique",
            "الهندسة الميكانيكية", "هندسة ميكانيكية",
        ),
        aliases=("gm", "mechanical", "meca"),
    ),
    Department(
        key="procedes",
        name_fr="Génie des Procédés",
        name_en="Process Engineering (incl. petroleum & chemical)",
        label="Génie des Procédés",
        keywords=(
            "genie des procedes", "process engineering", "procedes",
            "petrole", "petroleum", "chimie", "chemical", "raffinage",
            "petrochimie", "هندسة العمليات", "بترول", "كيمياء",
        ),
        aliases=("gp", "process", "petrole"),
    ),
    Department(
        key="economie",
        name_fr="Économie et Gestion",
        name_en="Economics and Management",
        label="Économie & Gestion",
        keywords=(
            "economie", "gestion", "economics", "management", "comptabilite",
            "accounting", "finance", "marketing", "mpa", "droit des societes",
            "audit", "اقتصاد", "ادارة", "محاسبة",
        ),
        aliases=("eco", "eco_g", "economie et gestion", "business"),
    ),
    Department(
        key="statistique",
        name_fr="Statistique et Mathématiques Appliquées",
        name_en="Statistics and Applied Mathematics",
        label="Statistique",
        keywords=(
            "statistique", "statistics", "science des donnees", "data science",
            "mathematiques appliquees", "applied mathematics", "probabilite",
            "sondage", "احصاء", "علم البيانات",
        ),
        aliases=("stat", "stats", "data"),
    ),
    Department(
        key="physique",
        name_fr="Sciences Physiques et Mathématiques",
        name_en="Physical Sciences and Mathematics (foundation cell)",
        label="Sciences Physiques & Maths",
        keywords=(
            "sciences physiques", "physique", "physics", "mathematiques",
            "mathematics", "analyse", "algebre", "cspm",
            "فيزياء", "رياضيات",
        ),
        aliases=("cspm", "maths", "math", "maths_physique"),
    ),
    Department(
        key="langues",
        name_fr="Langues",
        name_en="Languages",
        label="Langues",
        keywords=(
            "langues", "langue", "francais", "french", "anglais", "english",
            "delf", "delf b2", "tcf", "cours intensif", "language",
            "لغة", "لغات", "فرنسية", "انكليزية",
        ),
        aliases=("langue", "language", "languages", "fle"),
    ),
)

# --- Derived lookups ---------------------------------------------------------

BY_KEY: Dict[str, Department] = {d.key: d for d in DEPARTMENTS}

#: Every label the classifier is allowed to return.
VALID_CATEGORIES: Tuple[str, ...] = (GENERAL,) + tuple(d.key for d in DEPARTMENTS) + (OTHER,)

# alias -> canonical key, including legacy names we still honour.
_ALIASES: Dict[str, str] = {}
for _d in DEPARTMENTS:
    _ALIASES[_d.key] = _d.key
    for _a in _d.aliases:
        _ALIASES[_a] = _d.key
_ALIASES[GENERAL] = GENERAL
_ALIASES[OTHER] = OTHER


def resolve(label: Optional[str]) -> Optional[str]:
    """
    Map a raw label to a canonical category key, or None if unrecognised.

    Accepts aliases, so a model answering "cs" still routes to informatique.
    """
    if not label:
        return None
    return _ALIASES.get(label.strip().lower().replace("_", " ").strip())


def label_for(category: str) -> str:
    """Human-readable tag for a category, used in notification messages."""
    dept = BY_KEY.get(category)
    if dept:
        return dept.label
    if category == GENERAL:
        return "Général"
    return "Autre"


def describe_for_prompt() -> str:
    """Render the category list as prompt text, so the prompt never drifts."""
    lines = [
        f"- {GENERAL}: concerns ALL students regardless of department "
        f"(fees, registration, holidays, institute-wide exam calendars, "
        f"campus closures, transport, general language/admin notices)",
    ]
    for d in DEPARTMENTS:
        lines.append(f"- {d.key}: specifically about the {d.name_fr} "
                     f"({d.name_en}) department, its students or its courses")
    lines.append(
        f"- {OTHER}: none of the above (e.g. an unrelated job advert, "
        f"a supplier notice, or something not aimed at students)"
    )
    return "\n".join(lines)
