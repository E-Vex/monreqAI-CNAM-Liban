/**
 * ISAE Monitor - State Manager Implementation
 *
 * Faithful C port of Python isae_monitor/state.py.
 *
 * Key behaviors preserved:
 *   - Two-phase g/c semantics: 'g' (general-sent) and 'c' (classified)
 *     are mutated independently. 'c' is set ONLY after a successful
 *     department-channel send. The pipeline may classify an item twice
 *     if the first classification's delivery failed.
 *   - Atomic write: same-dir temp file -> fflush -> fsync(file) ->
 *     fsync(parent dir) -> rename. Cleanup on any failure.
 *   - Corruption quarantine: corrupt JSON is moved to <path>.corrupt,
 *     not deleted. recovered_from_corruption is set so the pipeline
 *     can surface a warning in the run report.
 *   - v0/v1 -> v2 migration: a plain JSON list of IDs is treated as
 *     already-sent (g=true, c=null). A dict with general_sent/classified
 *     lists is migrated per Python rules.
 *   - Pruning on save(): keep the newest `history` entries by 't'.
 */

#include "isae_monitor/state.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#define STATE_FILE_VERSION_C 2

/* ------------------------------------------------------------------ */
/* Init / cleanup                                                     */
/* ------------------------------------------------------------------ */

isae_error_t state_init(state_manager_t* state, const char* file_path, size_t history) {
    if (!state || !file_path) return ISAE_ERR_INVALID_PARAM;
    memset(state, 0, sizeof(*state));
    state->history = history ? history : STATE_HISTORY_DEFAULT;
    strncpy(state->file_path, file_path, sizeof(state->file_path) - 1);
    state->file_path[sizeof(state->file_path) - 1] = '\0';
    return ISAE_OK;
}

void state_cleanup(state_manager_t* state) {
    if (!state) return;
    free(state->entries);
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
}

/* ------------------------------------------------------------------ */
/* Internal: entry lookup / creation                                  */
/* ------------------------------------------------------------------ */

static state_entry_t* state_find_mutable(state_manager_t* state, const char* key) {
    if (!state || !key) return NULL;
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].key, key) == 0) return &state->entries[i];
    }
    return NULL;
}

const state_entry_t* state_get(const state_manager_t* state, const char* key) {
    if (!state || !key) return NULL;
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].key, key) == 0) return &state->entries[i];
    }
    return NULL;
}

static state_entry_t* state_entry(state_manager_t* state, const char* key) {
    state_entry_t* e = state_find_mutable(state, key);
    if (e) return e;

    /* Grow capacity geometrically. */
    if (state->count == state->capacity) {
        size_t new_cap = state->capacity ? state->capacity * 2 : 64;
        if (new_cap <= state->capacity) return NULL; /* overflow */
        state_entry_t* new_entries = (state_entry_t*)realloc(state->entries, new_cap * sizeof(*new_entries));
        if (!new_entries) return NULL;
        state->entries = new_entries;
        state->capacity = new_cap;
    }

    state_entry_t* slot = &state->entries[state->count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->key, key, sizeof(slot->key) - 1);
    slot->key[sizeof(slot->key) - 1] = '\0';
    slot->g = false;
    slot->c[0] = '\0';
    slot->t = time(NULL);
    return slot;
}

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

bool state_needs_general(const state_manager_t* state, const char* key) {
    const state_entry_t* e = state_get(state, key);
    return !e ? true : !e->g;
}

bool state_needs_classification(const state_manager_t* state, const char* key) {
    const state_entry_t* e = state_get(state, key);
    /* An entry is "needs classification" iff its 'c' field is empty.
     * The 'bootstrap' value is truthy in Python (so bootstrapped items
     * are NOT re-classified); we mirror that by treating any non-empty
     * 'c' as classified. */
    return !e ? true : e->c[0] == '\0';
}

/* ------------------------------------------------------------------ */
/* Mutators                                                            */
/* ------------------------------------------------------------------ */

isae_error_t state_mark_general_sent(state_manager_t* state, const char* key) {
    state_entry_t* e = state_entry(state, key);
    if (!e) return ISAE_ERR_MEMORY;
    e->g = true;
    state->dirty = true;
    return ISAE_OK;
}

isae_error_t state_mark_classified(state_manager_t* state, const char* key, const char* category) {
    if (!category) return ISAE_ERR_INVALID_PARAM;
    state_entry_t* e = state_entry(state, key);
    if (!e) return ISAE_ERR_MEMORY;
    strncpy(e->c, category, sizeof(e->c) - 1);
    e->c[sizeof(e->c) - 1] = '\0';
    state->dirty = true;
    return ISAE_OK;
}

isae_error_t state_mark_all_seen(state_manager_t* state, const char* key, const char* category) {
    state_entry_t* e = state_entry(state, key);
    if (!e) return ISAE_ERR_MEMORY;
    e->g = true;
    if (category) {
        strncpy(e->c, category, sizeof(e->c) - 1);
        e->c[sizeof(e->c) - 1] = '\0';
    } else {
        strcpy(e->c, "bootstrap");
    }
    state->dirty = true;
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* Load: parse + migrate                                               */
/* ------------------------------------------------------------------ */

static void state_quarantine(state_manager_t* state) {
    char corrupt[MAX_URL_LEN + 16];
    int n = snprintf(corrupt, sizeof(corrupt), "%s.corrupt", state->file_path);
    if (n < 0 || (size_t)n >= sizeof(corrupt)) return;
    /* rename() overwrites the destination if it exists. */
    (void)rename(state->file_path, corrupt);
    state->recovered_from_corruption = true;
}

/* Read the file contents into a heap buffer. Returns NULL if the file
 * does not exist or cannot be read. *out_size receives the byte count. */
static char* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || sz > 10 * 1024 * 1024) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_size) *out_size = rd;
    return buf;
}

/* Migrate a v0 plain-list-of-ids array: each id becomes {g:true, c:null, t:now}. */
static void migrate_v0(state_manager_t* state, cJSON* arr) {
    cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsString(item)) continue;
        state_entry_t* e = state_entry(state, item->valuestring);
        if (e) { e->g = true; e->c[0] = '\0'; }
    }
}

/* Migrate a v1 dict {"general_sent":[...], "classified":[...]}:
 * - each id in general_sent gets g=true, c=""
 * - each id in classified gets c="unknown" (v1 didn't store the category)
 *   and g=true if it was also in general_sent. */
static void migrate_v1(state_manager_t* state, cJSON* obj) {
    cJSON* gs = cJSON_GetObjectItem(obj, "general_sent");
    cJSON* cl = cJSON_GetObjectItem(obj, "classified");
    if (gs && cJSON_IsArray(gs)) {
        cJSON* item;
        cJSON_ArrayForEach(item, gs) {
            if (!cJSON_IsString(item)) continue;
            state_entry_t* e = state_entry(state, item->valuestring);
            if (e) e->g = true;
        }
    }
    if (cl && cJSON_IsArray(cl)) {
        cJSON* item;
        cJSON_ArrayForEach(item, cl) {
            if (!cJSON_IsString(item)) continue;
            state_entry_t* e = state_entry(state, item->valuestring);
            if (e) { e->g = true; strcpy(e->c, "unknown"); }
        }
    }
}

/* Parse a v2 "seen" object: keys are Atom ids, values are {g,c,t} dicts.
 * Stringify the JSON key (it might come in as a number in malformed files). */
static void load_v2(state_manager_t* state, cJSON* seen) {
    cJSON* item;
    cJSON_ArrayForEach(item, seen) {
        if (!cJSON_IsObject(item)) continue;
        const char* key = item->string;
        if (!key || !*key) continue;

        state_entry_t* e = state_entry(state, key);
        if (!e) continue;

        cJSON* g = cJSON_GetObjectItem(item, "g");
        cJSON* c = cJSON_GetObjectItem(item, "c");
        cJSON* t = cJSON_GetObjectItem(item, "t");

        e->g = (g && cJSON_IsBool(g)) ? cJSON_IsTrue(g) : false;
        if (c && cJSON_IsString(c)) {
            strncpy(e->c, c->valuestring, sizeof(e->c) - 1);
            e->c[sizeof(e->c) - 1] = '\0';
        } else {
            e->c[0] = '\0';
        }
        e->t = (t && cJSON_IsNumber(t)) ? (time_t)t->valuedouble : time(NULL);
    }
}

isae_error_t state_load(state_manager_t* state) {
    if (!state) return ISAE_ERR_INVALID_PARAM;

    size_t file_size = 0;
    char* buf = read_file(state->file_path, &file_size);
    if (!buf) {
        /* File missing is normal -- fresh state. */
        return ISAE_OK;
    }

    cJSON* root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        state_quarantine(state);
        return ISAE_OK;
    }

    if (cJSON_IsArray(root)) {
        /* v0: plain list of ids. */
        migrate_v0(state, root);
        cJSON_Delete(root);
        state->dirty = true; /* needs rewrite in v2 format */
        return ISAE_OK;
    }
    if (cJSON_IsObject(root)) {
        cJSON* ver = cJSON_GetObjectItem(root, "version");
        cJSON* seen = cJSON_GetObjectItem(root, "seen");

        if (ver && cJSON_IsNumber(ver) && ver->valuedouble == 2.0 && seen && cJSON_IsObject(seen)) {
            load_v2(state, seen);
            cJSON_Delete(root);
            return ISAE_OK;
        }
        /* v1 detection: dict with general_sent or classified. */
        if (cJSON_GetObjectItem(root, "general_sent") ||
            cJSON_GetObjectItem(root, "classified")) {
            migrate_v1(state, root);
            cJSON_Delete(root);
            state->dirty = true; /* needs rewrite in v2 format */
            return ISAE_OK;
        }
    }

    /* Unknown shape: quarantine to preserve the original for inspection. */
    cJSON_Delete(root);
    state_quarantine(state);
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* Save: atomic write                                                  */
/* ------------------------------------------------------------------ */

static int cmp_entries_by_t_desc(const void* a, const void* b) {
    const state_entry_t* ea = (const state_entry_t*)a;
    const state_entry_t* eb = (const state_entry_t*)b;
    if (ea->t < eb->t) return 1;
    if (ea->t > eb->t) return -1;
    return 0;  /* stable on ties via qsort_r would be better; qsort is
               * not stable, but the order among equal-t entries has no
               * observable effect (we keep the newest N). */
}

static void state_prune(state_manager_t* state) {
    if (state->count <= state->history) return;
    qsort(state->entries, state->count, sizeof(*state->entries), cmp_entries_by_t_desc);
    state->count = state->history;
}

/* fsync the parent directory of `path`. POSIX requires this for the
 * rename() durability guarantee. Best-effort: silently ignored on failure. */
static void fsync_parent_dir(const char* path) {
    char dir[MAX_URL_LEN];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char* slash = strrchr(dir, '/');
    if (!slash) {
        /* No slash: file is in CWD. */
        strcpy(dir, ".");
    } else if (slash == dir) {
        /* File is at root. */
        dir[1] = '\0';
    } else {
        *slash = '\0';
    }

    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return;
    (void)fsync(fd);
    close(fd);
}

isae_error_t state_save(state_manager_t* state) {
    if (!state) return ISAE_ERR_INVALID_PARAM;
    if (!state->dirty) return ISAE_OK;

    state_prune(state);

    cJSON* seen = cJSON_CreateObject();
    if (!seen) return ISAE_ERR_MEMORY;

    for (size_t i = 0; i < state->count; i++) {
        const state_entry_t* e = &state->entries[i];
        cJSON* entry = cJSON_CreateObject();
        if (!entry) { cJSON_Delete(seen); return ISAE_ERR_MEMORY; }
        cJSON_AddBoolToObject(entry, "g", e->g);
        if (e->c[0]) {
            cJSON_AddStringToObject(entry, "c", e->c);
        } else {
            cJSON_AddNullToObject(entry, "c");
        }
        cJSON_AddNumberToObject(entry, "t", (double)e->t);
        cJSON_AddItemToObject(seen, e->key, entry);
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) { cJSON_Delete(seen); return ISAE_ERR_MEMORY; }
    cJSON_AddNumberToObject(root, "version", STATE_FILE_VERSION_C);
    cJSON_AddItemToObject(root, "seen", seen);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ISAE_ERR_MEMORY;

    /* Same-dir temp file so the rename() is atomic on POSIX. */
    char temp_path[MAX_URL_LEN + 32];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", state->file_path, (int)getpid());
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        free(json);
        return ISAE_ERR_IO;
    }

    FILE* f = fopen(temp_path, "wb");
    if (!f) {
        free(json);
        return ISAE_ERR_IO;
    }

    isae_error_t err = ISAE_OK;
    size_t need = strlen(json);
    size_t wrote = fwrite(json, 1, need, f);
    int flush_ok = (fflush(f) == 0);
    int fsync_ok = (fsync(fileno(f)) == 0);
    int close_ok = (fclose(f) == 0);
    free(json);

    if (wrote != need || !flush_ok || !fsync_ok || !close_ok) {
        (void)unlink(temp_path);
        return ISAE_ERR_IO;
    }

    if (rename(temp_path, state->file_path) != 0) {
        (void)unlink(temp_path);
        return ISAE_ERR_IO;
    }

    fsync_parent_dir(state->file_path);

    state->dirty = false;
    return err;
}
