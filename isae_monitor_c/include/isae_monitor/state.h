#ifndef ISAE_MONITOR_STATE_H
#define ISAE_MONITOR_STATE_H

#include "common.h"

/* Maximum number of tracked announcements */
#define MAX_TRACKED_ANNOUNCEMENTS 1000

/* Tracked announcement entry */
typedef struct {
    char hash[MAX_HASH_LEN];
    char department_key[MAX_DEPARTMENT_KEY];
    char category[MAX_CATEGORY_NAME];
    time_t first_seen;
    bool notified;
} tracked_entry_t;

/* State management structure */
typedef struct {
    tracked_entry_t* entries;
    size_t count;
    size_t capacity;
    time_t last_save;
    char file_path[MAX_URL_LEN];
} state_manager_t;

/* Initialize state manager */
isae_error_t state_init(state_manager_t* state, const char* file_path);

/* Free state manager resources */
void state_cleanup(state_manager_t* state);

/* Load state from file */
isae_error_t state_load(state_manager_t* state);

/* Save state to file (atomic write) */
isae_error_t state_save(const state_manager_t* state);

/* Check if announcement hash exists */
bool state_contains(const state_manager_t* state, const char* hash);

/* Add new announcement to state */
isae_error_t state_add(state_manager_t* state, const char* hash, 
                       const char* department_key, const char* category);

/* Mark announcement as notified */
isae_error_t state_mark_notified(state_manager_t* state, const char* hash);

/* Get entry by hash */
const tracked_entry_t* state_get_entry(const state_manager_t* state, const char* hash);

/* Remove old entries (older than max_age_seconds) */
isae_error_t state_prune_old(state_manager_t* state, time_t max_age_seconds);

#endif /* ISAE_MONITOR_STATE_H */
