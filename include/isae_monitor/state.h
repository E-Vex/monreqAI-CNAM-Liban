#ifndef ISAE_MONITOR_STATE_H
#define ISAE_MONITOR_STATE_H

#include "common.h"

/* State file schema (matches Python v2 seen.json):
 *
 *   {
 *     "version": 2,
 *     "seen": {
 *       "tag:blogger.com,2025:post-12345": {"g": true, "c": "informatique", "t": 1736291400}
 *     }
 *   }
 *
 * The keys are Atom <id> values from the feed. Each value is an object
 * with three fields:
 *   g (bool)  - announcement has been delivered to the general channel.
 *   c (string|null) - classified category, or null if classification
 *                     is still pending. The literal string "bootstrap"
 *                     marks items touched by --bootstrap.
 *   t (int)   - Unix timestamp of last touch (set on entry creation only,
 *               used as the sort key for pruning).
 *
 * The state is two-phase: 'g' and 'c' are mutated independently, so an
 * announcement can be in any combination of (sent / not-sent) x
 * (classified / not-classified).
 *
 * On corrupt JSON, the file is moved aside to <path>.corrupt and the
 * state starts empty. The pipeline can read state.recovered_from_corruption
 * to surface a warning in the run report.
 */

#define STATE_KEY_MAX 512

typedef struct {
    char key[STATE_KEY_MAX];    /* Atom <id> */
    bool g;                      /* general-channel sent */
    char c[MAX_CATEGORY_NAME];  /* classified category, "" if pending */
    time_t t;                    /* creation timestamp */
} state_entry_t;

typedef struct {
    state_entry_t* entries;
    size_t count;
    size_t capacity;
    char file_path[MAX_URL_LEN];
    size_t history;              /* STATE_HISTORY_DEFAULT = 1000 */
    bool dirty;
    bool recovered_from_corruption;
} state_manager_t;

/* Initialize a state manager with the given file path and history size.
 * history=0 falls back to STATE_HISTORY_DEFAULT. */
isae_error_t state_init(state_manager_t* state, const char* file_path, size_t history);

/* Free all resources. */
void state_cleanup(state_manager_t* state);

/* Load state from disk. On parse failure, the corrupt file is moved to
 * <path>.corrupt and state starts empty with recovered_from_corruption=true. */
isae_error_t state_load(state_manager_t* state);

/* Save state to disk atomically:
 *   - write to <path>.tmp.<pid>
 *   - fflush + fsync(file) + fsync(parent dir)
 *   - rename over the target
 *   - cleanup temp file on any failure
 * No-op (returns ISAE_OK) when nothing has changed since the last save. */
isae_error_t state_save(state_manager_t* state);

/* Two-phase mutators. All set state->dirty = true. */
isae_error_t state_mark_general_sent(state_manager_t* state, const char* key);
isae_error_t state_mark_classified(state_manager_t* state, const char* key, const char* category);
isae_error_t state_mark_all_seen(state_manager_t* state, const char* key, const char* category);

/* Predicates used by select_pending. */
bool state_needs_general(const state_manager_t* state, const char* key);
bool state_needs_classification(const state_manager_t* state, const char* key);

/* Look up an entry by key. Returns NULL if not present. */
const state_entry_t* state_get(const state_manager_t* state, const char* key);

#endif /* ISAE_MONITOR_STATE_H */
