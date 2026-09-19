/**
 * @file state.h
 * @brief Persistent state management (seen.json)
 * 
 * Format v2:
 *   {"version": 2, "seen": {"<id>": {"g": bool, "c": "<category>|null", "t": int}}}
 */

#ifndef ISAE_MONITOR_STATE_H
#define ISAE_MONITOR_STATE_H

#include "common.h"

/* Maximum number of seen entries to keep */
#define MAX_SEEN_ENTRIES 1000

/* State entry for a single announcement */
typedef struct {
    char id[MAX_ANNOUNCEMENT_ID];
    bool general_sent;      /* 'g' - sent to general channel */
    char category[MAX_DEPARTMENT_KEY];  /* 'c' - classification category */
    bool has_category;      /* whether category is set */
    int64_t timestamp;      /* 't' - Unix timestamp of last touch */
} state_entry_t;

/* State container */
typedef struct {
    char path[MAX_FILE_PATH];
    state_entry_t* entries;         /* Dynamic array of entries */
    size_t entry_count;
    size_t entry_capacity;
    int32_t history_limit;
    bool dirty;
    bool recovered_from_corruption;
} state_t;

/* Initialize state manager */
isae_error_t state_init(state_t* state, const char* path, int32_t history_limit);

/* Load state from file */
isae_error_t state_load(state_t* state);

/* Save state to file (atomic write with fsync) */
isae_error_t state_save(state_t* state);

/* Free state resources */
void state_cleanup(state_t* state);

/* Check if announcement needs general channel send */
bool state_needs_general(const state_t* state, const char* announcement_id);

/* Check if announcement needs classification */
bool state_needs_classification(const state_t* state, const char* announcement_id);

/* Get stored category for an announcement (NULL if not classified) */
const char* state_category_of(const state_t* state, const char* announcement_id);

/* Mark announcement as sent to general channel */
isae_error_t state_mark_general_sent(state_t* state, const char* announcement_id);

/* Mark announcement as classified */
isae_error_t state_mark_classified(state_t* state, 
                                    const char* announcement_id,
                                    const char* category);

/* Mark announcement as fully seen (for bootstrap) */
isae_error_t state_mark_all_seen(state_t* state, 
                                  const char* announcement_id,
                                  const char* category);

/* Get number of entries */
size_t state_count(const state_t* state);

/* Prune old entries to stay within history limit */
isae_error_t state_prune(state_t* state);

/* Quarantine corrupt state file */
isae_error_t state_quarantine(const char* path);

#endif /* ISAE_MONITOR_STATE_H */
