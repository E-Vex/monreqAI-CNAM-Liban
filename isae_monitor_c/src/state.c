/**
 * @file state.c
#define _POSIX_C_SOURCE 200809L
 * @brief Persistent state management - tracks processed announcements
 * 
 * Python equivalent: json-based state persistence
 * C Implementation: manual JSON parsing/writing with atomic file updates
 */

#include "isae_monitor/state.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

/* Maximum state file size (1MB) */
#define MAX_STATE_FILE_SIZE (1024 * 1024)

/* Initial capacity for entries array */
#define INITIAL_ENTRY_CAPACITY 64

isae_error_t state_init(state_t* state, const char* path, int32_t history_limit) {
    if (!state || !path) return ISAE_ERR_INVALID_ARG;
    
    memset(state, 0, sizeof(state_t));
    
    strncpy(state->path, path, MAX_FILE_PATH - 1);
    state->path[MAX_FILE_PATH - 1] = '\0';
    
    state->history_limit = history_limit > 0 ? history_limit : 1000;
    state->entries = calloc(INITIAL_ENTRY_CAPACITY, sizeof(state_entry_t));
    if (!state->entries) {
        return ISAE_ERR_MEMORY;
    }
    state->entry_capacity = INITIAL_ENTRY_CAPACITY;
    state->entry_count = 0;
    state->dirty = false;
    state->recovered_from_corruption = false;
    
    return ISAE_OK;
}

void state_cleanup(state_t* state) {
    if (!state) return;
    
    if (state->entries) {
        free(state->entries);
        state->entries = NULL;
    }
    state->entry_count = 0;
    state->entry_capacity = 0;
}

/* Find entry by ID */
static state_entry_t* find_entry(const state_t* state, const char* id) {
    if (!state || !id) return NULL;
    
    for (size_t i = 0; i < state->entry_count; i++) {
        if (strcmp(state->entries[i].id, id) == 0) {
            return &state->entries[i];
        }
    }
    return NULL;
}

/* Add or update entry */
static state_entry_t* get_or_create_entry(state_t* state, const char* id) {
    if (!state || !id) return NULL;
    
    /* Check if exists */
    state_entry_t* existing = find_entry(state, id);
    if (existing) return existing;
    
    /* Expand if needed */
    if (state->entry_count >= state->entry_capacity) {
        size_t new_cap = state->entry_capacity * 2;
        state_entry_t* new_entries = realloc(state->entries, new_cap * sizeof(state_entry_t));
        if (!new_entries) return NULL;
        
        state->entries = new_entries;
        state->entry_capacity = new_cap;
    }
    
    /* Create new entry */
    state_entry_t* entry = &state->entries[state->entry_count++];
    memset(entry, 0, sizeof(state_entry_t));
    strncpy(entry->id, id, MAX_ANNOUNCEMENT_ID - 1);
    entry->id[MAX_ANNOUNCEMENT_ID - 1] = '\0';
    entry->general_sent = false;
    entry->has_category = false;
    entry->category[0] = '\0';
    entry->timestamp = (int64_t)time(NULL);
    state->dirty = true;
    
    return entry;
}

/* Simple JSON string extraction */
static char* extract_json_string(const char* json, const char* key) {
    if (!json || !key) return NULL;
    
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char* key_pos = strstr(json, search_pattern);
    if (!key_pos) return NULL;
    
    const char* colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    
    const char* value_start = colon + 1;
    while (*value_start && (*value_start == ' ' || *value_start == '\t' || *value_start == '\n')) {
        value_start++;
    }
    
    if (*value_start != '"') return NULL;
    value_start++;
    
    const char* value_end = value_start;
    while (*value_end && *value_end != '"') {
        if (*value_end == '\\' && *(value_end + 1)) {
            value_end += 2;
        } else {
            value_end++;
        }
    }
    
    size_t len = value_end - value_start;
    char* result = malloc(len + 1);
    if (!result) return NULL;
    
    strncpy(result, value_start, len);
    result[len] = '\0';
    return result;
}

/* Extract boolean from JSON */
static int extract_json_bool(const char* json, const char* key, int default_val) {
    if (!json || !key) return default_val;
    
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char* key_pos = strstr(json, search_pattern);
    if (!key_pos) return default_val;
    
    const char* colon = strchr(key_pos, ':');
    if (!colon) return default_val;
    
    const char* value_start = colon + 1;
    while (*value_start && (*value_start == ' ' || *value_start == '\t' || *value_start == '\n')) {
        value_start++;
    }
    
    if (strncmp(value_start, "true", 4) == 0) return 1;
    if (strncmp(value_start, "false", 5) == 0) return 0;
    
    return default_val;
}

/* Extract integer from JSON */
static long extract_json_int(const char* json, const char* key, long default_val) {
    if (!json || !key) return default_val;
    
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char* key_pos = strstr(json, search_pattern);
    if (!key_pos) return default_val;
    
    const char* colon = strchr(key_pos, ':');
    if (!colon) return default_val;
    
    const char* value_start = colon + 1;
    while (*value_start && (*value_start == ' ' || *value_start == '\t' || *value_start == '\n')) {
        value_start++;
    }
    
    char* endptr;
    long val = strtol(value_start, &endptr, 10);
    if (endptr == value_start) return default_val;
    
    return val;
}

/* Parse seen object from JSON - simplified parser */
static isae_error_t parse_seen_object(const char* json, state_t* state) {
    if (!json || !state) return ISAE_ERR_INVALID_ARG;
    
    const char* seen_start = strstr(json, "\"seen\"");
    if (!seen_start) return ISAE_OK;
    
    const char* brace = strchr(seen_start, '{');
    if (!brace) return ISAE_OK;
    
    /* Find matching closing brace (simplified - assumes well-formed JSON) */
    int depth = 1;
    const char* pos = brace + 1;
    while (*pos && depth > 0) {
        if (*pos == '{') depth++;
        else if (*pos == '}') depth--;
        pos++;
    }
    
    /* Parse each entry in seen object */
    const char* entry_start = brace + 1;
    while (entry_start < pos - 1) {
        /* Find next key (announcement ID) */
        while (entry_start < pos && (*entry_start == ' ' || *entry_start == '\t' || 
               *entry_start == '\n' || *entry_start == ',')) {
            entry_start++;
        }
        
        if (entry_start >= pos - 1 || *entry_start != '"') break;
        entry_start++;
        
        /* Find end of key */
        const char* key_end = entry_start;
        while (key_end < pos && *key_end != '"') {
            if (*key_end == '\\' && *(key_end + 1)) key_end += 2;
            else key_end++;
        }
        
        if (key_end >= pos) break;
        
        size_t id_len = key_end - entry_start;
        if (id_len == 0 || id_len >= MAX_ANNOUNCEMENT_ID) {
            entry_start = key_end + 1;
            continue;
        }
        
        /* Get entry data object */
        const char* data_start = strchr(key_end, '{');
        if (!data_start || data_start >= pos) break;
        
        const char* data_end = strchr(data_start, '}');
        if (!data_end || data_end >= pos) break;
        
        /* Extract data length for substring operations */
        size_t data_len = data_end - data_start + 1;
        char* data_str = malloc(data_len + 1);
        if (!data_str) return ISAE_ERR_MEMORY;
        
        strncpy(data_str, data_start, data_len);
        data_str[data_len] = '\0';
        
        /* Create entry */
        state_entry_t* entry = get_or_create_entry(state, (const char*)entry_start);
        if (entry) {
            char* category = extract_json_string(data_str, "c");
            if (category) {
                strncpy(entry->category, category, MAX_DEPARTMENT_KEY - 1);
                entry->has_category = (strlen(category) > 0 && strcmp(category, "null") != 0);
                free(category);
            }
            
            entry->general_sent = extract_json_bool(data_str, "g", 0);
            entry->timestamp = extract_json_int(data_str, "t", (long)time(NULL));
        }
        
        free(data_str);
        entry_start = data_end + 1;
    }
    
    return ISAE_OK;
}

isae_error_t state_load(state_t* state) {
    if (!state) return ISAE_ERR_INVALID_ARG;
    
    FILE* f = fopen(state->path, "r");
    if (!f) {
        if (errno == ENOENT) {
            return ISAE_OK;
        }
        return ISAE_ERR_IO;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > MAX_STATE_FILE_SIZE) {
        fclose(f);
        return ISAE_ERR_IO;
    }
    
    char* json = malloc(fsize + 1);
    if (!json) {
        fclose(f);
        return ISAE_ERR_MEMORY;
    }
    
    size_t read_size = fread(json, 1, fsize, f);
    fclose(f);
    json[read_size] = '\0';
    
    /* Parse version */
    int version = (int)extract_json_int(json, "version", 1);
    
    /* Parse seen object */
    isae_error_t ret = parse_seen_object(json, state);
    
    free(json);
    state->dirty = false;
    
    return ret;
}

isae_error_t state_save(state_t* state) {
    if (!state) return ISAE_ERR_INVALID_ARG;
    
    isae_error_t ret = ISAE_OK;
    char* temp_path = NULL;
    FILE* f = NULL;
    
    /* Calculate required buffer size */
    size_t estimated_size = 256 + state->entry_count * 128;
    char* json = malloc(estimated_size);
    if (!json) return ISAE_ERR_MEMORY;
    
    /* Build JSON */
    int offset = 0;
    offset += snprintf(json + offset, estimated_size - offset, 
                       "{\n  \"version\": %d,\n", 2);
    offset += snprintf(json + offset, estimated_size - offset,
                       "  \"seen\": {\n");
    
    for (size_t i = 0; i < state->entry_count; i++) {
        state_entry_t* entry = &state->entries[i];
        if (i > 0) {
            offset += snprintf(json + offset, estimated_size - offset, ",\n");
        }
        offset += snprintf(json + offset, estimated_size - offset,
                           "    \"%s\": {\"g\": %s, \"c\": \"%s\", \"t\": %ld}",
                           entry->id,
                           entry->general_sent ? "true" : "false",
                           entry->has_category ? entry->category : "",
                           (long)entry->timestamp);
    }
    
    offset += snprintf(json + offset, estimated_size - offset, "\n  }\n}\n");
    
    /* Create temporary file */
    temp_path = malloc(strlen(state->path) + 32);
    if (!temp_path) {
        free(json);
        return ISAE_ERR_MEMORY;
    }
    snprintf(temp_path, strlen(state->path) + 32, "%s.tmp.%d", state->path, getpid());
    
    f = fopen(temp_path, "w");
    if (!f) {
        fprintf(stderr, "[STATE] Failed to create temp file %s: %s\n", temp_path, strerror(errno));
        ret = ISAE_ERR_IO;
        goto cleanup;
    }
    
    if (fwrite(json, 1, strlen(json), f) != strlen(json)) {
        fprintf(stderr, "[STATE] Failed to write state: %s\n", strerror(errno));
        ret = ISAE_ERR_IO;
        goto cleanup;
    }
    
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fprintf(stderr, "[STATE] Failed to sync state: %s\n", strerror(errno));
        ret = ISAE_ERR_IO;
        goto cleanup;
    }
    
    fclose(f);
    f = NULL;
    
    if (rename(temp_path, state->path) != 0) {
        fprintf(stderr, "[STATE] Failed to rename state file: %s\n", strerror(errno));
        ret = ISAE_ERR_IO;
        goto cleanup;
    }
    
    state->dirty = false;
    
cleanup:
    if (f) fclose(f);
    if (temp_path) {
        unlink(temp_path);
        free(temp_path);
    }
    free(json);
    return ret;
}

bool state_needs_general(const state_t* state, const char* announcement_id) {
    state_entry_t* entry = find_entry(state, announcement_id);
    if (!entry) return true; /* New entry needs general send */
    return !entry->general_sent;
}

bool state_needs_classification(const state_t* state, const char* announcement_id) {
    state_entry_t* entry = find_entry(state, announcement_id);
    if (!entry) return true; /* New entry needs classification */
    return !entry->has_category;
}

const char* state_category_of(const state_t* state, const char* announcement_id) {
    state_entry_t* entry = find_entry(state, announcement_id);
    if (!entry || !entry->has_category) return NULL;
    return entry->category;
}

isae_error_t state_mark_general_sent(state_t* state, const char* announcement_id) {
    state_entry_t* entry = get_or_create_entry(state, announcement_id);
    if (!entry) return ISAE_ERR_MEMORY;
    
    entry->general_sent = true;
    entry->timestamp = (int64_t)time(NULL);
    return ISAE_OK;
}

isae_error_t state_mark_classified(state_t* state, const char* announcement_id, const char* category) {
    state_entry_t* entry = get_or_create_entry(state, announcement_id);
    if (!entry) return ISAE_ERR_MEMORY;
    
    if (category && strlen(category) > 0) {
        strncpy(entry->category, category, MAX_DEPARTMENT_KEY - 1);
        entry->has_category = true;
    } else {
        entry->has_category = false;
        entry->category[0] = '\0';
    }
    entry->timestamp = (int64_t)time(NULL);
    return ISAE_OK;
}

isae_error_t state_mark_all_seen(state_t* state, const char* announcement_id, const char* category) {
    isae_error_t ret = state_mark_classified(state, announcement_id, category);
    if (ret != ISAE_OK) return ret;
    
    return state_mark_general_sent(state, announcement_id);
}

size_t state_count(const state_t* state) {
    return state ? state->entry_count : 0;
}

isae_error_t state_prune(state_t* state) {
    if (!state || state->entry_count <= (size_t)state->history_limit) {
        return ISAE_OK;
    }
    
    /* Sort by timestamp and remove oldest - simplified: just remove first N entries */
    size_t to_remove = state->entry_count - (size_t)state->history_limit;
    
    /* Move remaining entries to front */
    memmove(state->entries, state->entries + to_remove, 
            (state->entry_count - to_remove) * sizeof(state_entry_t));
    state->entry_count -= to_remove;
    state->dirty = true;
    
    return ISAE_OK;
}

isae_error_t state_quarantine(const char* path) {
    if (!path) return ISAE_ERR_INVALID_ARG;
    
    char quarantine_path[MAX_FILE_PATH];
    snprintf(quarantine_path, sizeof(quarantine_path), "%s.corrupt.%ld", 
             path, (long)time(NULL));
    
    if (rename(path, quarantine_path) != 0) {
        return ISAE_ERR_IO;
    }
    
    return ISAE_OK;
}
