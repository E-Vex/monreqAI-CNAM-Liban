/**
 * ISAE Monitor - State Management Implementation
 * 
 * Maps Python: state.py -> C: state.c
 * 
 * Handles persistent state storage for tracking processed announcements.
 * Uses JSON format with atomic writes for crash safety.
 */

#include "isae_monitor/state.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>

/* cJSON is included via pkg-config in build system */
#include <cjson/cJSON.h>

isae_error_t state_init(state_manager_t* state, const char* file_path) {
    if (!state || !file_path) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    memset(state, 0, sizeof(state_manager_t));
    
    /* Expand ~ to home directory */
    if (file_path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(state->file_path, MAX_URL_LEN - 1, "%s%s", home, file_path + 1);
        } else {
            strncpy(state->file_path, file_path, MAX_URL_LEN - 1);
        }
    } else {
        strncpy(state->file_path, file_path, MAX_URL_LEN - 1);
    }
    state->file_path[MAX_URL_LEN - 1] = '\0';
    
    /* Initial capacity */
    state->capacity = 100;
    state->entries = calloc(state->capacity, sizeof(tracked_entry_t));
    if (!state->entries) {
        return ISAE_ERR_MEMORY;
    }
    
    return ISAE_OK;
}

void state_cleanup(state_manager_t* state) {
    if (!state) return;
    
    free(state->entries);
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
}

isae_error_t state_load(state_manager_t* state) {
    if (!state) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    FILE* f = fopen(state->file_path, "r");
    if (!f) {
        /* File doesn't exist yet - not an error */
        if (errno == ENOENT) {
            return ISAE_OK;
        }
        return ISAE_ERR_IO;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {  /* Max 10MB */
        fclose(f);
        return ISAE_ERR_PARSE;
    }
    
    /* Read file content */
    char* json_str = malloc(fsize + 1);
    if (!json_str) {
        fclose(f);
        return ISAE_ERR_MEMORY;
    }
    
    size_t read_size = fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[read_size] = '\0';
    
    /* Parse JSON */
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        fprintf(stderr, "Warning: Failed to parse state file, starting fresh\n");
        return ISAE_OK;  /* Start fresh instead of failing */
    }
    
    /* Check version and migrate if needed */
    cJSON* version = cJSON_GetObjectItem(root, "version");
    (void)version;  /* Version info available if needed for future migrations */
    
    /* Get entries array */
    cJSON* entries_json = cJSON_GetObjectItem(root, "entries");
    if (!entries_json || !cJSON_IsArray(entries_json)) {
        cJSON_Delete(root);
        return ISAE_OK;
    }
    
    int num_entries = cJSON_GetArraySize(entries_json);
    
    /* Ensure capacity */
    if ((size_t)num_entries > state->capacity) {
        size_t new_capacity = (size_t)num_entries + 10;
        tracked_entry_t* new_entries = realloc(state->entries, 
                                                new_capacity * sizeof(tracked_entry_t));
        if (!new_entries) {
            cJSON_Delete(root);
            return ISAE_ERR_MEMORY;
        }
        state->entries = new_entries;
        state->capacity = new_capacity;
    }
    
    /* Parse entries */
    state->count = 0;
    for (int i = 0; i < num_entries && state->count < state->capacity; i++) {
        cJSON* entry = cJSON_GetArrayItem(entries_json, i);
        if (!entry) continue;
        
        cJSON* hash_item = cJSON_GetObjectItem(entry, "hash");
        cJSON* dept_item = cJSON_GetObjectItem(entry, "department");
        cJSON* cat_item = cJSON_GetObjectItem(entry, "category");
        cJSON* seen_item = cJSON_GetObjectItem(entry, "first_seen");
        cJSON* notified_item = cJSON_GetObjectItem(entry, "notified");
        
        if (!hash_item || !cJSON_IsString(hash_item)) continue;
        
        tracked_entry_t* e = &state->entries[state->count];
        strncpy(e->hash, hash_item->valuestring, MAX_HASH_LEN - 1);
        e->hash[MAX_HASH_LEN - 1] = '\0';
        
        if (dept_item && cJSON_IsString(dept_item)) {
            strncpy(e->department_key, dept_item->valuestring, MAX_DEPARTMENT_KEY - 1);
            e->department_key[MAX_DEPARTMENT_KEY - 1] = '\0';
        }
        
        if (cat_item && cJSON_IsString(cat_item)) {
            strncpy(e->category, cat_item->valuestring, MAX_CATEGORY_NAME - 1);
            e->category[MAX_CATEGORY_NAME - 1] = '\0';
        }
        
        e->first_seen = seen_item ? (time_t)seen_item->valueint : time(NULL);
        e->notified = notified_item ? notified_item->valueint : 0;
        
        state->count++;
    }
    
    cJSON_Delete(root);
    return ISAE_OK;
}

isae_error_t state_save(const state_manager_t* state) {
    if (!state) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Create JSON structure */
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return ISAE_ERR_MEMORY;
    }
    
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "last_save", (double)time(NULL));
    cJSON_AddNumberToObject(root, "count", (double)state->count);
    
    cJSON* entries_array = cJSON_CreateArray();
    if (!entries_array) {
        cJSON_Delete(root);
        return ISAE_ERR_MEMORY;
    }
    
    for (size_t i = 0; i < state->count; i++) {
        cJSON* entry = cJSON_CreateObject();
        if (!entry) continue;
        
        const tracked_entry_t* e = &state->entries[i];
        cJSON_AddStringToObject(entry, "hash", e->hash);
        cJSON_AddStringToObject(entry, "department", e->department_key);
        cJSON_AddStringToObject(entry, "category", e->category);
        cJSON_AddNumberToObject(entry, "first_seen", (double)e->first_seen);
        cJSON_AddBoolToObject(entry, "notified", e->notified);
        
        cJSON_AddItemToArray(entries_array, entry);
    }
    
    cJSON_AddItemToObject(root, "entries", entries_array);
    
    /* Convert to string */
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ISAE_ERR_MEMORY;
    }
    
    /* Atomic write: write to temp file, then rename */
    char temp_path[MAX_URL_LEN];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", state->file_path, getpid());
    
    FILE* f = fopen(temp_path, "w");
    if (!f) {
        free(json_str);
        return ISAE_ERR_IO;
    }
    
    size_t written = fwrite(json_str, 1, strlen(json_str), f);
    fflush(f);
    fsync(fileno(f));  /* Ensure data on disk */
    fclose(f);
    free(json_str);
    
    if (written == 0) {
        unlink(temp_path);
        return ISAE_ERR_IO;
    }
    
    /* Rename temp to actual */
    if (rename(temp_path, state->file_path) != 0) {
        unlink(temp_path);
        return ISAE_ERR_IO;
    }
    
    return ISAE_OK;
}

bool state_contains(const state_manager_t* state, const char* hash) {
    if (!state || !hash) {
        return false;
    }
    
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].hash, hash) == 0) {
            return true;
        }
    }
    
    return false;
}

isae_error_t state_add(state_manager_t* state, const char* hash,
                       const char* department_key, const char* category) {
    if (!state || !hash) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Check if already exists */
    if (state_contains(state, hash)) {
        return ISAE_OK;  /* Already tracked */
    }
    
    /* Ensure capacity */
    if (state->count >= state->capacity) {
        size_t new_capacity = state->capacity * 2;
        tracked_entry_t* new_entries = realloc(state->entries,
                                                new_capacity * sizeof(tracked_entry_t));
        if (!new_entries) {
            return ISAE_ERR_MEMORY;
        }
        state->entries = new_entries;
        state->capacity = new_capacity;
    }
    
    /* Add entry */
    tracked_entry_t* e = &state->entries[state->count];
    memset(e, 0, sizeof(tracked_entry_t));
    
    strncpy(e->hash, hash, MAX_HASH_LEN - 1);
    e->hash[MAX_HASH_LEN - 1] = '\0';
    
    if (department_key) {
        strncpy(e->department_key, department_key, MAX_DEPARTMENT_KEY - 1);
        e->department_key[MAX_DEPARTMENT_KEY - 1] = '\0';
    }
    
    if (category) {
        strncpy(e->category, category, MAX_CATEGORY_NAME - 1);
        e->category[MAX_CATEGORY_NAME - 1] = '\0';
    }
    
    e->first_seen = time(NULL);
    e->notified = false;
    
    state->count++;
    
    return ISAE_OK;
}

isae_error_t state_mark_notified(state_manager_t* state, const char* hash) {
    if (!state || !hash) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].hash, hash) == 0) {
            state->entries[i].notified = true;
            return ISAE_OK;
        }
    }
    
    return ISAE_ERR_INVALID_PARAM;  /* Not found */
}

const tracked_entry_t* state_get_entry(const state_manager_t* state, const char* hash) {
    if (!state || !hash) {
        return NULL;
    }
    
    for (size_t i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].hash, hash) == 0) {
            return &state->entries[i];
        }
    }
    
    return NULL;
}

isae_error_t state_prune_old(state_manager_t* state, time_t max_age_seconds) {
    if (!state) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    time_t now = time(NULL);
    time_t cutoff = now - max_age_seconds;
    
    /* Compact in-place */
    size_t write_idx = 0;
    for (size_t i = 0; i < state->count; i++) {
        if (state->entries[i].first_seen >= cutoff) {
            if (write_idx != i) {
                state->entries[write_idx] = state->entries[i];
            }
            write_idx++;
        }
    }
    
    state->count = write_idx;
    
    return ISAE_OK;
}
