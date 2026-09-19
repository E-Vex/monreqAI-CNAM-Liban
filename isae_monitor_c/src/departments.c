/**
 * @file departments.c
 * @brief Department registry implementation
 */

#include "isae_monitor/departments.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Pseudo-categories */
const char* const DEPT_CATEGORY_GENERAL = "general";
const char* const DEPT_CATEGORY_OTHER = "other";

/* Static department data - ordered most-specific-first */
static const department_t g_departments[MAX_DEPARTMENTS] = {
    {
        .key = DEPT_INFORMATIQUE,
        .name_fr = "Génie Informatique",
        .name_en = "Computer Engineering / Computer Science",
        .label = "Informatique",
        .keywords = {
            "informatique", "genie informatique", "computer science",
            "computer engineering", "programmation", "programming",
            "algorithme", "algorithm", "reseaux", "network", "logiciel",
            "software", "base de donnees", "database", "cybersecurite",
            "intelligence artificielle"
        },
        .keyword_count = 15,
        .aliases = {"cs", "info", "computer", "it"},
        .alias_count = 4
    },
    {
        .key = DEPT_CIVIL,
        .name_fr = "Génie Civil",
        .name_en = "Civil Engineering",
        .label = "Génie Civil",
        .keywords = {
            "genie civil", "civil engineering", "batiment", "construction",
            "beton", "structures", "parasismique", "topographie",
            "ouvrages d'art", "routes", "hydraulique"
        },
        .keyword_count = 11,
        .aliases = {"gc", "civile"},
        .alias_count = 2
    },
    {
        .key = DEPT_ELECTRIQUE,
        .name_fr = "Génie Électrique",
        .name_en = "Electrical Engineering",
        .label = "Génie Électrique",
        .keywords = {
            "genie electrique", "electrical engineering", "electrotechnique",
            "electronique", "automatique", "signal", "ascensoriste",
            "energetique", "climatique", "froid", "hvac"
        },
        .keyword_count = 11,
        .aliases = {"ge", "electrical", "electricite"},
        .alias_count = 3
    },
    {
        .key = DEPT_MECANIQUE,
        .name_fr = "Génie Mécanique",
        .name_en = "Mechanical Engineering",
        .label = "Génie Mécanique",
        .keywords = {
            "genie mecanique", "mechanical engineering", "mecanique",
            "fabrication", "dessins industriels", "machines",
            "mecanique des structures", "thermodynamique"
        },
        .keyword_count = 8,
        .aliases = {"gm", "mechanical", "meca"},
        .alias_count = 3
    },
    {
        .key = DEPT_PROCEDES,
        .name_fr = "Génie des Procédés",
        .name_en = "Process Engineering (incl. petroleum & chemical)",
        .label = "Génie des Procédés",
        .keywords = {
            "genie des procedes", "process engineering", "procedes",
            "petrole", "petroleum", "chimie", "chemical", "raffinage",
            "petrochimie"
        },
        .keyword_count = 9,
        .aliases = {"gp", "process", "petrole"},
        .alias_count = 3
    },
    {
        .key = DEPT_ECONOMIE,
        .name_fr = "Économie et Gestion",
        .name_en = "Economics and Management",
        .label = "Économie & Gestion",
        .keywords = {
            "economie", "gestion", "economics", "management", "comptabilite",
            "accounting", "finance", "marketing", "mpa", "droit des societes",
            "audit"
        },
        .keyword_count = 11,
        .aliases = {"eco", "eco_g", "economie et gestion", "business"},
        .alias_count = 4
    },
    {
        .key = DEPT_STATISTIQUE,
        .name_fr = "Statistique et Mathématiques Appliquées",
        .name_en = "Statistics and Applied Mathematics",
        .label = "Statistique",
        .keywords = {
            "statistique", "statistics", "science des donnees", "data science",
            "mathematiques appliquees", "applied mathematics", "probabilite",
            "sondage"
        },
        .keyword_count = 8,
        .aliases = {"stat", "stats", "data"},
        .alias_count = 3
    },
    {
        .key = DEPT_PHYSIQUE,
        .name_fr = "Sciences Physiques et Mathématiques",
        .name_en = "Physical Sciences and Mathematics (foundation cell)",
        .label = "Sciences Physiques & Maths",
        .keywords = {
            "sciences physiques", "physique", "physics", "mathematiques",
            "mathematics", "analyse", "algebre", "cspm"
        },
        .keyword_count = 8,
        .aliases = {"cspm", "maths", "math", "maths_physique"},
        .alias_count = 4
    },
    {
        .key = DEPT_LANGUES,
        .name_fr = "Langues",
        .name_en = "Languages",
        .label = "Langues",
        .keywords = {
            "langues", "langue", "francais", "french", "anglais", "english",
            "delf", "delf b2", "tcf", "cours intensif", "language"
        },
        .keyword_count = 11,
        .aliases = {"langue", "language", "languages", "fle"},
        .alias_count = 4
    }
};

/* Valid categories array */
static valid_categories_t g_valid_categories;
static bool g_initialized = false;

/* Alias lookup table */
typedef struct {
    const char* alias;
    const char* canonical;
} alias_entry_t;

static const alias_entry_t g_aliases[] = {
    {"cs", DEPT_INFORMATIQUE},
    {"info", DEPT_INFORMATIQUE},
    {"computer", DEPT_INFORMATIQUE},
    {"it", DEPT_INFORMATIQUE},
    {"gc", DEPT_CIVIL},
    {"civile", DEPT_CIVIL},
    {"ge", DEPT_ELECTRIQUE},
    {"electrical", DEPT_ELECTRIQUE},
    {"electricite", DEPT_ELECTRIQUE},
    {"gm", DEPT_MECANIQUE},
    {"mechanical", DEPT_MECANIQUE},
    {"meca", DEPT_MECANIQUE},
    {"gp", DEPT_PROCEDES},
    {"process", DEPT_PROCEDES},
    {"petrole", DEPT_PROCEDES},
    {"eco", DEPT_ECONOMIE},
    {"eco_g", DEPT_ECONOMIE},
    {"economie et gestion", DEPT_ECONOMIE},
    {"business", DEPT_ECONOMIE},
    {"stat", DEPT_STATISTIQUE},
    {"stats", DEPT_STATISTIQUE},
    {"data", DEPT_STATISTIQUE},
    {"cspm", DEPT_PHYSIQUE},
    {"maths", DEPT_PHYSIQUE},
    {"math", DEPT_PHYSIQUE},
    {"maths_physique", DEPT_PHYSIQUE},
    {"langue", DEPT_LANGUES},
    {"language", DEPT_LANGUES},
    {"languages", DEPT_LANGUES},
    {"fle", DEPT_LANGUES},
    {"general", DEPT_CATEGORY_GENERAL},
    {"other", DEPT_CATEGORY_OTHER}
};

void departments_init(void) {
    if (g_initialized) return;
    
    /* Build valid categories list */
    g_valid_categories.categories[0] = DEPT_CATEGORY_GENERAL;
    size_t idx = 1;
    for (size_t i = 0; i < MAX_DEPARTMENTS; i++) {
        g_valid_categories.categories[idx++] = g_departments[i].key;
    }
    g_valid_categories.categories[idx++] = DEPT_CATEGORY_OTHER;
    g_valid_categories.count = idx;
    
    g_initialized = true;
}

size_t departments_get_count(void) {
    return MAX_DEPARTMENTS;
}

const department_t* departments_get_by_index(size_t index) {
    if (index >= MAX_DEPARTMENTS) return NULL;
    return &g_departments[index];
}

const department_t* departments_get_by_key(const char* key) {
    if (!key) return NULL;
    for (size_t i = 0; i < MAX_DEPARTMENTS; i++) {
        if (strcmp(g_departments[i].key, key) == 0) {
            return &g_departments[i];
        }
    }
    return NULL;
}

const char* department_get_env_var(const department_t* dept) {
    static char env_var[64];
    if (!dept) return NULL;
    
    /* Build TELEGRAM_CHANNEL_<KEY> */
    snprintf(env_var, sizeof(env_var), "TELEGRAM_CHANNEL_%s", dept->key);
    /* Convert to uppercase */
    for (char* p = env_var; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    return env_var;
}

const valid_categories_t* departments_get_valid_categories(void) {
    return &g_valid_categories;
}

/* Helper: lowercase string in place */
static void str_tolower(char* str) {
    for (char* p = str; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

const char* departments_resolve(const char* label) {
    if (!label || !*label) return NULL;
    
    /* Make a lowercase copy for comparison */
    char lower_label[MAX_DEPARTMENT_KEY];
    strncpy(lower_label, label, sizeof(lower_label) - 1);
    lower_label[sizeof(lower_label) - 1] = '\0';
    str_tolower(lower_label);
    
    /* Trim whitespace */
    char* start = lower_label;
    while (*start && isspace((unsigned char)*start)) start++;
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) *end-- = '\0';
    
    /* Replace underscores with spaces */
    for (char* p = start; *p; p++) {
        if (*p == '_') *p = ' ';
    }
    
    /* Look up in alias table */
    for (size_t i = 0; i < sizeof(g_aliases) / sizeof(g_aliases[0]); i++) {
        if (strcmp(start, g_aliases[i].alias) == 0) {
            return g_aliases[i].canonical;
        }
    }
    
    return NULL;
}

const char* departments_label_for(const char* category) {
    if (!category) return "Unknown";
    
    if (strcmp(category, DEPT_CATEGORY_GENERAL) == 0) {
        return "Général";
    }
    if (strcmp(category, DEPT_CATEGORY_OTHER) == 0) {
        return "Autre";
    }
    
    const department_t* dept = departments_get_by_key(category);
    if (dept) {
        return dept->label;
    }
    
    return "Inconnu";
}

isae_error_t departments_describe_for_prompt(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return ISAE_ERR_INVALID_ARG;
    
    size_t offset = 0;
    int written;
    
    /* General category description */
    written = snprintf(buffer + offset, buffer_size - offset,
        "- %s: concerns ALL students regardless of department "
        "(fees, registration, holidays, institute-wide exam calendars, "
        "campus closures, transport, general language/admin notices)\n",
        DEPT_CATEGORY_GENERAL);
    if (written < 0 || (size_t)written >= buffer_size - offset) return ISAE_ERR_MEMORY;
    offset += (size_t)written;
    
    /* Department descriptions */
    for (size_t i = 0; i < MAX_DEPARTMENTS; i++) {
        written = snprintf(buffer + offset, buffer_size - offset,
            "- %s: specifically about the %s (%s) department, "
            "its students or its courses\n",
            g_departments[i].key,
            g_departments[i].name_fr,
            g_departments[i].name_en);
        if (written < 0 || (size_t)written >= buffer_size - offset) return ISAE_ERR_MEMORY;
        offset += (size_t)written;
    }
    
    /* Other category description */
    written = snprintf(buffer + offset, buffer_size - offset,
        "- %s: none of the above (e.g. an unrelated job advert, "
        "a supplier notice, or something not aimed at students)",
        DEPT_CATEGORY_OTHER);
    if (written < 0 || (size_t)written >= buffer_size - offset) return ISAE_ERR_MEMORY;
    
    return ISAE_OK;
}
