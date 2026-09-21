/**
 * ISAE Monitor - Department Registry
 *
 * Faithful C port of the Python isae_monitor/departments.py module.
 *
 * The institute (ISSAE / Cnam-Liban) has nine departments; every
 * announcement is fetched from a single Atom feed and routed AFTER
 * classification. There is no per-department feed URL.
 */

#include "isae_monitor/departments.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Each alias list is NULL-terminated. */
static const char* const ALIAS_INFO[]      = { "cs", "info", "computer", "it", NULL };
static const char* const ALIAS_CIVIL[]     = { "gc", "civile", NULL };
static const char* const ALIAS_ELECTRIQUE[]= { "ge", "electrical", "electricite", NULL };
static const char* const ALIAS_MECANIQUE[] = { "gm", "mechanical", "meca", NULL };
static const char* const ALIAS_PROCEDES[]  = { "gp", "process", "petrole", NULL };
static const char* const ALIAS_ECONOMIE[]  = { "eco", "eco_g", "economie et gestion", "business", NULL };
static const char* const ALIAS_STATISTIQUE[] = { "stat", "stats", "data", NULL };
static const char* const ALIAS_PHYSIQUE[]  = { "cspm", "maths", "math", "maths_physique", NULL };
static const char* const ALIAS_LANGUES[]   = { "langue", "language", "languages", "fle", NULL };

/* Keyword tables -- faithfully ported from isae_monitor/departments.py. */
static const char* const KW_INFO[] = {
    "informatique", "genie informatique", "computer science",
    "computer engineering", "programmation", "programming",
    "algorithme", "algorithm", "reseaux", "network", "logiciel",
    "software", "base de donnees", "database", "cybersecurite",
    "intelligence artificielle",
    "\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa\xd9\x8a\xd8\xa9",  /* هندسة المعلوماتية */
    "\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa\xd9\x8a\xd8\xa9",  /* معلوماتية */
    "\xd8\xa8\xd8\xb1\xd9\x85\xd8\xac\xd8\xa9",  /* برمجة */
    NULL,
};
static const char* const KW_CIVIL[] = {
    "genie civil", "civil engineering", "batiment", "construction",
    "beton", "structures", "parasismique", "topographie",
    "ouvrages d'art", "routes", "hydraulique",
    "\xd8\xa7\xd9\x84\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x85\xd8\xaf\xd9\x86\xd9\x8a\xd8\xa9",  /* الهندسة المدنية */
    "\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd9\x85\xd8\xaf\xd9\x86\xd9\x8a\xd8\xa9",  /* هندسة مدنية */
    "\xd8\xa8\xd9\x86\xd8\xa7\xd8\xa1",  /* بناء */
    NULL,
};
static const char* const KW_ELECTRIQUE[] = {
    "genie electrique", "electrical engineering", "electrotechnique",
    "electronique", "automatique", "signal", "ascensoriste",
    "energetique", "climatique", "froid", "hvac",
    "\xd8\xa7\xd9\x84\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x83\xd9\x87\xd8\xb1\xd8\xa8\xd8\xa7\xd8\xa6\xd9\x8a\xd8\xa9",  /* الهندسة الكهربائية */
    "\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd9\x83\xd9\x87\xd8\xb1\xd8\xa8\xd8\xa7\xd8\xa6\xd9\x8a\xd8\xa9",  /* هندسة كهربائية */
    "\xd9\x83\xd9\x87\xd8\xb1\xd8\xa8\xd8\xa7\xd8\xa1",  /* كهرباء */
    NULL,
};
static const char* const KW_MECANIQUE[] = {
    "genie mecanique", "mechanical engineering", "mecanique",
    "fabrication", "dessins industriels", "machines",
    "mecanique des structures", "thermodynamique",
    "\xd8\xa7\xd9\x84\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x85\xd9\x8a\xd9\x83\xd8\xa7\xd9\x86\xd9\x8a\xd9\x83\xd9\x8a\xd8\xa9",  /* الهندسة الميكانيكية */
    "\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd9\x85\xd9\x8a\xd9\x83\xd8\xa7\xd9\x86\xd9\x8a\xd9\x83\xd9\x8a\xd8\xa9",  /* هندسة ميكانيكية */
    NULL,
};
static const char* const KW_PROCEDES[] = {
    "genie des procedes", "process engineering", "procedes",
    "petrole", "petroleum", "chimie", "chemical", "raffinage",
    "petrochimie",
    "\xd9\x87\xd9\x86\xd8\xaf\xd8\xb3\xd8\xa9 \xd8\xa7\xd9\x84\xd8\xb9\xd9\x85\xd9\x84\xd9\x8a\xd8\xa7\xd8\xaa",  /* هندسة العمليات */
    "\xd8\xa8\xd8\xaa\xd8\xb1\xd9\x88\xd9\x84",  /* بترول */
    "\xd9\x83\xd9\x8a\xd9\x85\xd9\x8a\xd8\xa7\xd8\xa1",  /* كيمياء */
    NULL,
};
static const char* const KW_ECONOMIE[] = {
    "economie", "gestion", "economics", "management", "comptabilite",
    "accounting", "finance", "marketing", "mpa", "droit des societes",
    "audit",
    "\xd8\xa7\xd9\x82\xd8\xaa\xd8\xb5\xd8\xa7\xd8\xaf",  /* اقتصاد */
    "\xd8\xa7\xd8\xaf\xd8\xa7\xd8\xb1\xd8\xa9",  /* ادارة */
    "\xd9\x85\xd8\xad\xd8\xa7\xd8\xb3\xd8\xa8\xd8\xa9",  /* محاسبة */
    NULL,
};
static const char* const KW_STATISTIQUE[] = {
    "statistique", "statistics", "science des donnees", "data science",
    "mathematiques appliquees", "applied mathematics", "probabilite",
    "sondage",
    "\xd8\xa7\xd8\xad\xd8\xb5\xd8\xa7\xd8\xa1",  /* احصاء */
    "\xd8\xb9\xd9\x84\xd9\x85 \xd8\xa7\xd9\x84\xd8\xa8\xd9\x8a\xd8\xa7\xd9\x86\xd8\xa7\xd8\xaa",  /* علم البيانات */
    NULL,
};
static const char* const KW_PHYSIQUE[] = {
    "sciences physiques", "physique", "physics", "mathematiques",
    "mathematics", "analyse", "algebre", "cspm",
    "\xd9\x81\xd9\x8a\xd8\xb2\xd9\x8a\xd8\xa7\xd8\xa1",  /* فيزياء */
    "\xd8\xb1\xd9\x8a\xd8\xa7\xd8\xb6\xd9\x8a\xd8\xa7\xd8\xaa",  /* رياضيات */
    NULL,
};
static const char* const KW_LANGUES[] = {
    "langues", "langue", "francais", "french", "anglais", "english",
    "delf", "delf b2", "tcf", "cours intensif", "language",
    "\xd9\x84\xd8\xba\xd8\xa9",  /* لغة */
    "\xd9\x84\xd8\xba\xd8\xa7\xd8\xaa",  /* لغات */
    "\xd9\x81\xd8\xb1\xd9\x86\xd8\xb3\xd9\x8a\xd8\xa9",  /* فرنسية */
    "\xd8\xa7\xd9\x86\xd9\x83\xd9\x84\xd9\x8a\xd8\xb2\xd9\x8a\xd8\xa9",  /* انكليزية */
    NULL,
};

static const department_t g_departments[NUM_DEPARTMENTS] = {
    { "informatique", "Génie Informatique",                  "Informatique",              ALIAS_INFO,        KW_INFO },
    { "civil",        "Génie Civil",                        "Génie Civil",               ALIAS_CIVIL,       KW_CIVIL },
    { "electrique",   "Génie Électrique",                   "Génie Électrique",          ALIAS_ELECTRIQUE,  KW_ELECTRIQUE },
    { "mecanique",    "Génie Mécanique",                    "Génie Mécanique",           ALIAS_MECANIQUE,   KW_MECANIQUE },
    { "procedes",     "Génie des Procédés",                  "Génie des Procédés",        ALIAS_PROCEDES,    KW_PROCEDES },
    { "economie",     "Économie et Gestion",                 "Économie & Gestion",        ALIAS_ECONOMIE,    KW_ECONOMIE },
    { "statistique",  "Statistique et Mathématiques Appliquées", "Statistique",            ALIAS_STATISTIQUE, KW_STATISTIQUE },
    { "physique",     "Sciences Physiques et Mathématiques", "Sciences Physiques & Maths", ALIAS_PHYSIQUE,    KW_PHYSIQUE },
    { "langues",      "Langues",                             "Langues",                   ALIAS_LANGUES,     KW_LANGUES },
};

/* Short descriptions used in the AI prompt. Keep them in registry order so
 * the prompt is deterministic. */
static const char* const g_descriptions[NUM_DEPARTMENTS] = {
    "specifically about the Génie Informatique (Computer Engineering / Computer Science) department, its students or its courses",
    "specifically about the Génie Civil (Civil Engineering) department, its students or its courses",
    "specifically about the Génie Électrique (Electrical Engineering) department, its students or its courses",
    "specifically about the Génie Mécanique (Mechanical Engineering) department, its students or its courses",
    "specifically about the Génie des Procédés (Process / Petroleum Engineering) department, its students or its courses",
    "specifically about the Économie et Gestion department, its students or its courses",
    "specifically about the Statistique et Mathématiques Appliquées department, its students or its courses",
    "specifically about the Sciences Physiques et Mathématiques department, its students or its courses",
    "specifically about the Langues department, its students or its courses",
};

void departments_init(void) {
    /* static data, nothing to do */
}

size_t departments_count(void) {
    return NUM_DEPARTMENTS;
}

const department_t* departments_get_at(size_t index) {
    if (index >= NUM_DEPARTMENTS) return NULL;
    return &g_departments[index];
}

const department_t* departments_get_by_key(const char* key) {
    if (!key) return NULL;
    for (size_t i = 0; i < NUM_DEPARTMENTS; i++) {
        if (strcmp(g_departments[i].key, key) == 0) {
            return &g_departments[i];
        }
    }
    return NULL;
}

/* Normalize an alias for matching: lowercase, underscores->spaces, strip.
 * Writes into the caller-provided buffer (capacity MAX_DEPARTMENT_KEY). */
static void normalize_alias(const char* in, char* out, size_t cap) {
    if (!in || !out || cap == 0) {
        if (out && cap) out[0] = '\0';
        return;
    }
    size_t w = 0;
    bool in_space_pending = false;
    for (size_t r = 0; in[r] && w + 1 < cap; r++) {
        unsigned char c = (unsigned char)in[r];
        if (c == '_' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            /* collapse runs of whitespace + underscores into a single space,
             * leading/trailing stripped */
            if (w > 0) in_space_pending = true;
            continue;
        }
        if (in_space_pending) {
            if (w + 1 < cap) out[w++] = ' ';
            in_space_pending = false;
        }
        out[w++] = (char)tolower(c);
    }
    out[w] = '\0';
}

const char* departments_resolve(const char* label) {
    if (!label || !*label) return NULL;

    char norm[MAX_DEPARTMENT_KEY];
    normalize_alias(label, norm, sizeof(norm));
    if (!*norm) return NULL;

    for (size_t i = 0; i < NUM_DEPARTMENTS; i++) {
        /* canonical key match (normalized) */
        char key_norm[MAX_DEPARTMENT_KEY];
        normalize_alias(g_departments[i].key, key_norm, sizeof(key_norm));
        if (strcmp(key_norm, norm) == 0) return g_departments[i].key;

        /* alias match */
        for (const char* const* a = g_departments[i].aliases; *a; a++) {
            char alias_norm[MAX_DEPARTMENT_KEY];
            normalize_alias(*a, alias_norm, sizeof(alias_norm));
            if (strcmp(alias_norm, norm) == 0) return g_departments[i].key;
        }
    }
    return NULL;
}

const char* departments_resolve_or_empty(const char* label) {
    const char* r = departments_resolve(label);
    return r ? r : "";
}

const char* departments_env_var(const char* key) {
    static char env_name[64]; /* enough for "TELEGRAM_CHANNEL_INFORMATIQUE\0" */
    const department_t* d = departments_get_by_key(key);
    if (!d) return NULL;
    int n = snprintf(env_name, sizeof(env_name), "TELEGRAM_CHANNEL_%s", d->key);
    if (n < 0 || (size_t)n >= sizeof(env_name)) return NULL;
    /* The key is always lowercase ASCII, so we uppercase in place. */
    for (char* p = env_name + strlen("TELEGRAM_CHANNEL_"); *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    return env_name;
}

const char* departments_label_for(const char* key) {
    if (!key) return NULL;
    if (strcmp(key, CATEGORY_GENERAL) == 0) return "General";
    if (strcmp(key, CATEGORY_OTHER) == 0)   return "Other";
    const department_t* d = departments_get_by_key(key);
    return d ? d->label : NULL;
}

bool departments_is_valid_category(const char* key) {
    if (!key) return false;
    if (strcmp(key, CATEGORY_GENERAL) == 0) return true;
    if (strcmp(key, CATEGORY_OTHER) == 0)   return true;
    return departments_get_by_key(key) != NULL;
}

bool departments_is_real_department(const char* key) {
    if (!key) return false;
    return departments_get_by_key(key) != NULL;
}

char* departments_describe_for_prompt(void) {
    /* Pre-compute the total length we will need:
     *   "- " + key + ": " + description + "\n"
     * for each of the 9 departments, plus general/other lines, plus trailing nul. */
    size_t total = 1;
    static const char* const extras[2] = {
        "concerns ALL students regardless of department (fees, registration, holidays, institute-wide exam calendars, campus closures, transport, general language/admin notices)",
        "none of the above (e.g. an unrelated job advert, a supplier notice, or something not aimed at students)",
    };
    static const char* const extra_keys[2] = { CATEGORY_GENERAL, CATEGORY_OTHER };

    for (size_t i = 0; i < NUM_DEPARTMENTS; i++) {
        total += 2 + strlen(g_departments[i].key) + 2 + strlen(g_descriptions[i]) + 1;
    }
    for (size_t i = 0; i < 2; i++) {
        total += 2 + strlen(extra_keys[i]) + 2 + strlen(extras[i]) + 1;
    }

    char* out = (char*)malloc(total);
    if (!out) return NULL;
    size_t w = 0;
#define APPEND(s) do { size_t n = strlen(s); if (w + n + 1 >= total) { free(out); return NULL; } memcpy(out + w, s, n); w += n; } while (0)
    for (size_t i = 0; i < NUM_DEPARTMENTS; i++) {
        APPEND("- ");
        APPEND(g_departments[i].key);
        APPEND(": ");
        APPEND(g_descriptions[i]);
        APPEND("\n");
    }
    for (size_t i = 0; i < 2; i++) {
        APPEND("- ");
        APPEND(extra_keys[i]);
        APPEND(": ");
        APPEND(extras[i]);
        APPEND("\n");
    }
#undef APPEND
    out[w] = '\0';
    return out;
}
