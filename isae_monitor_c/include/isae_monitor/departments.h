/**
 * @file departments.h
 * @brief Department registry - single source of truth for all departments
 * 
 * This file defines all nine departments at ISSAE / Cnam Liban.
 * Everything else in the codebase derives from this file: classifier labels,
 * env var names, Telegram routing, keyword fallback.
 */

#ifndef ISAE_MONITOR_DEPARTMENTS_H
#define ISAE_MONITOR_DEPARTMENTS_H

#include "common.h"

/* Pseudo-categories (not departments but valid classifications) */
extern const char* const DEPT_CATEGORY_GENERAL;
extern const char* const DEPT_CATEGORY_OTHER;

/* Department structure */
typedef struct {
    const char* key;                      /* Stable ID for env vars + state */
    const char* name_fr;                  /* Official French name */
    const char* name_en;                  /* English name for prompt */
    const char* label;                    /* Short tag shown in messages */
    const char* keywords[MAX_KEYWORDS_PER_DEPT];  /* Fallback matching */
    size_t keyword_count;
    const char* aliases[MAX_ALIASES_PER_DEPT];    /* Extra labels LLM might emit */
    size_t alias_count;
} department_t;

/* Get the number of departments */
size_t departments_get_count(void);

/* Get a department by index */
const department_t* departments_get_by_index(size_t index);

/* Get a department by key (returns NULL if not found) */
const department_t* departments_get_by_key(const char* key);

/* Get environment variable name for a department */
const char* department_get_env_var(const department_t* dept);

/* Valid categories array (for classifier) */
typedef struct {
    const char* categories[MAX_DEPARTMENTS + 3];  /* general + 9 depts + other */
    size_t count;
} valid_categories_t;

/* Get all valid categories */
const valid_categories_t* departments_get_valid_categories(void);

/* Resolve a label to canonical category key (NULL if unrecognized) */
const char* departments_resolve(const char* label);

/* Get human-readable label for a category */
const char* departments_label_for(const char* category);

/* Build category description for AI prompt */
isae_error_t departments_describe_for_prompt(char* buffer, size_t buffer_size);

/* Initialize department data (called once at startup) */
void departments_init(void);

#endif /* ISAE_MONITOR_DEPARTMENTS_H */
