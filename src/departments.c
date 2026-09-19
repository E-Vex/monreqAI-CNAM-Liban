/**
 * ISAE Monitor - Department Registry Implementation
 * 
 * Maps Python: departments.py -> C: departments.c
 * 
 * This module maintains a registry of all engineering departments
 * with their feed URLs and classification keys.
 */

#include "isae_monitor/departments.h"
#include <ctype.h>

/* Internal department database */
static department_t g_departments[] = {
    {
        .key = "INFO",
        .name = "Computer Science",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-informatique/actualites/feed/",
        .type = DEPT_INFO
    },
    {
        .key = "ELECM",
        .name = "Electronics",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-electronique/actualites/feed/",
        .type = DEPT_ELECM
    },
    {
        .key = "ELM",
        .name = "Electrical Engineering",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-energie-electrique/actualites/feed/",
        .type = DEPT_ELM
    },
    {
        .key = "MECM",
        .name = "Mechanical Engineering",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-mecanique/actualites/feed/",
        .type = DEPT_MECM
    },
    {
        .key = "CECM",
        .name = "Civil Engineering",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-genie-civil/actualites/feed/",
        .type = DEPT_CECM
    },
    {
        .key = "GPIM",
        .name = "Industrial Engineering",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-genie-industriel/actualites/feed/",
        .type = DEPT_GPIM
    },
    {
        .key = "EAC",
        .name = "Applied Economics",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-economie-appliquee/actualites/feed/",
        .type = DEPT_EAC
    },
    {
        .key = "MATHS",
        .name = "Mathematics",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-mathematiques/actualites/feed/",
        .type = DEPT_MATHS
    },
    {
        .key = "PHYSIQUE",
        .name = "Physics",
        .feed_url = "https://www.isae-supaero.fr/fr/departement-physique/actualites/feed/",
        .type = DEPT_PHYSIQUE
    }
};

/* Alias mapping for flexible key matching */
typedef struct {
    const char* alias;
    const char* canonical_key;
} alias_mapping_t;

static alias_mapping_t g_aliases[] = {
    {"informatique", "INFO"},
    {"info", "INFO"},
    {"computer", "INFO"},
    {"electronique", "ELECM"},
    {"elec", "ELECM"},
    {"electronics", "ELECM"},
    {"electrique", "ELM"},
    {"electrical", "ELM"},
    {"energy", "ELM"},
    {"mecanique", "MECM"},
    {"mechanical", "MECM"},
    {"civil", "CECM"},
    {"genie-civil", "CECM"},
    {"industriel", "GPIM"},
    {"industrie", "GPIM"},
    {"economie", "EAC"},
    {"economics", "EAC"},
    {"maths", "MATHS"},
    {"mathematics", "MATHS"},
    {"math", "MATHS"},
    {"physique", "PHYSIQUE"},
    {"physics", "PHYSIQUE"}
};

static size_t g_num_departments = sizeof(g_departments) / sizeof(g_departments[0]);
static size_t g_num_aliases = sizeof(g_aliases) / sizeof(g_aliases[0]);

void departments_init(void) {
    /* Nothing to initialize - static data */
    /* This function exists for API consistency */
}

const department_t* departments_get_by_key(const char* key) {
    if (!key) {
        return NULL;
    }
    
    /* Convert to uppercase for comparison */
    char upper_key[MAX_DEPARTMENT_KEY];
    size_t len = strlen(key);
    if (len >= MAX_DEPARTMENT_KEY) {
        len = MAX_DEPARTMENT_KEY - 1;
    }
    
    for (size_t i = 0; i < len; i++) {
        upper_key[i] = (char)toupper((unsigned char)key[i]);
    }
    upper_key[len] = '\0';
    
    /* Search in main departments */
    for (size_t i = 0; i < g_num_departments; i++) {
        if (strcmp(g_departments[i].key, upper_key) == 0) {
            return &g_departments[i];
        }
    }
    
    return NULL;
}

const department_t* departments_get_by_type(department_type_t type) {
    for (size_t i = 0; i < g_num_departments; i++) {
        if (g_departments[i].type == type) {
            return &g_departments[i];
        }
    }
    return NULL;
}

size_t departments_count(void) {
    return g_num_departments;
}

const department_t* departments_get_at(size_t index) {
    if (index >= g_num_departments) {
        return NULL;
    }
    return &g_departments[index];
}

bool departments_is_valid_alias(const char* alias, const char** out_key) {
    if (!alias) {
        return false;
    }
    
    /* Convert to lowercase for comparison */
    char lower_alias[MAX_DEPARTMENT_KEY];
    size_t len = strlen(alias);
    if (len >= MAX_DEPARTMENT_KEY) {
        len = MAX_DEPARTMENT_KEY - 1;
    }
    
    for (size_t i = 0; i < len; i++) {
        lower_alias[i] = (char)tolower((unsigned char)alias[i]);
    }
    lower_alias[len] = '\0';
    
    /* Search in aliases */
    for (size_t i = 0; i < g_num_aliases; i++) {
        if (strcmp(g_aliases[i].alias, lower_alias) == 0) {
            if (out_key) {
                *out_key = g_aliases[i].canonical_key;
            }
            return true;
        }
    }
    
    return false;
}
