#ifndef ISAE_MONITOR_DEPARTMENTS_H
#define ISAE_MONITOR_DEPARTMENTS_H

#include "common.h"

/* monreqAI-CNAM-Liban department registry.
 *
 * Mirrors the Python reference: nine departments at ISSAE / Cnam-Liban,
 * each routable to its own Telegram channel, plus the two pseudo-categories
 * "general" and "other" used by the classifier. There is no per-department
 * feed URL; all announcements come from a single Atom feed and are routed
 * AFTER classification. */
typedef struct {
    const char* key;          /* routing key (lowercase, ASCII)          */
    const char* name_fr;      /* display name in French                  */
    const char* label;        /* short label shown in Telegram prefix    */
    const char* const* aliases; /* NULL-terminated alias list            */
} department_t;

/* Pseudo-categories. These are not in the registry but are valid classifier
 * outputs. */
#define CATEGORY_GENERAL  "general"
#define CATEGORY_OTHER    "other"

/* Initialize the registry (currently a no-op; kept for API stability). */
void departments_init(void);

/* Number of departments (always 9). */
size_t departments_count(void);

/* Registry access. */
const department_t* departments_get_at(size_t index);
const department_t* departments_get_by_key(const char* key);

/* Resolve an alias or canonical key to a routing key.
 * Returns NULL if the input is not a known department or alias.
 * Mirrors Python's departments.resolve(). */
const char* departments_resolve(const char* label);

/* Convenience: returns the routing key for a department, given either
 * the canonical key or any alias. Equivalent to departments_resolve()
 * but returns "" (never NULL) so callers can blindly use the result. */
const char* departments_resolve_or_empty(const char* label);

/* Look up the canonical env var name (e.g. "TELEGRAM_CHANNEL_INFORMATIQUE")
 * for a department key. Returns NULL for unknown keys. */
const char* departments_env_var(const char* key);

/* Look up the human-readable label for a routing key (department key,
 * "general" or "other"). Returns "general"/"other" for those, the
 * department's label for departments, or NULL for unknown. */
const char* departments_label_for(const char* key);

/* True if the routing key is one of the 9 departments, "general" or "other". */
bool departments_is_valid_category(const char* key);

/* True if the routing key is one of the 9 departments (excludes
 * pseudo-categories). */
bool departments_is_real_department(const char* key);

/* Build the multi-line "{key}: {description}" string shown inside the
 * Gemini / OpenRouter prompt. The caller owns the returned buffer and
 * must free() it. Returns NULL on allocation failure. */
char* departments_describe_for_prompt(void);

#endif /* ISAE_MONITOR_DEPARTMENTS_H */
