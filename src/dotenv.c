/**
 * ISAE Monitor - .env File Loader Implementation
 * 
 * Parses .env files in shell-compatible format.
 * Supports: KEY=value, KEY="value", KEY='value'
 * Ignores: comments (#), empty lines, export prefix
 */

#include "isae_monitor/dotenv.h"
#include <ctype.h>

void dotenv_init(dotenv_t* env) {
    if (!env) return;
    memset(env, 0, sizeof(dotenv_t));
    env->count = 0;
}

static void trim_whitespace(char* str) {
    if (!str || !*str) return;
    
    /* Trim leading */
    char* start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    
    /* Trim trailing */
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    
    /* Shift if needed */
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

static void strip_quotes(char* str) {
    if (!str || !*str) return;

    size_t len = strlen(str);
    /* Need at least 2 characters for a matching pair; otherwise len-2
     * underflows to SIZE_MAX and the subsequent memmove clobbers memory. */
    if (len < 2) return;

    /* Check if wrapped in matching quotes */
    if ((str[0] == '"' && str[len - 1] == '"') ||
        (str[0] == '\'' && str[len - 1] == '\'')) {
        /* Remove quotes by shifting content left */
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

static isae_error_t parse_line(const char* line, char* key, size_t key_size,
                                char* value, size_t value_size) {
    if (!line || !key || !value) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Skip empty lines and comments */
    const char* p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    
    if (*p == '\0' || *p == '#') {
        return ISAE_ERR_INVALID_PARAM;  /* Not a valid env line */
    }
    
    /* Skip 'export ' prefix if present */
    if (strncmp(p, "export ", 7) == 0) {
        p += 7;
    }
    
    /* Find the '=' sign */
    const char* eq = strchr(p, '=');
    if (!eq) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Extract key. memcpy + explicit NUL: strncpy would zero-pad and
     * trigger -Wstringop-truncation when key_len == key_size - 1. */
    size_t key_len = (size_t)(eq - p);
    if (key_len >= key_size) {
        key_len = key_size - 1;
    }
    memcpy(key, p, key_len);
    key[key_len] = '\0';
    trim_whitespace(key);
    
    /* Extract value */
    const char* val_start = eq + 1;
    
    /* Skip leading whitespace on value */
    while (*val_start && isspace((unsigned char)*val_start)) {
        val_start++;
    }
    
    /* Copy value */
    strncpy(value, val_start, value_size - 1);
    value[value_size - 1] = '\0';
    trim_whitespace(value);
    
    /* Strip surrounding quotes */
    strip_quotes(value);
    
    return ISAE_OK;
}

isae_error_t dotenv_load(dotenv_t* env, const char* path) {
    if (!env) {
        return ISAE_ERR_INVALID_PARAM;
    }

    dotenv_init(env);

    /* Default path */
    char default_path[] = ".env";
    const char* filepath = path ? path : default_path;

    FILE* file = fopen(filepath, "r");
    if (!file) {
        /* File not found is not an error - just means no .env file */
        return ISAE_OK;
    }

    char line[MAX_ENV_LINE_LEN];

    while (fgets(line, sizeof(line), file) && env->count < MAX_ENV_VARS) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        char key[MAX_ENV_LINE_LEN];
        char value[MAX_ENV_LINE_LEN];

        isae_error_t err = parse_line(line, key, sizeof(key), value, sizeof(value));
        if (err != ISAE_OK) {
            continue;  /* Skip invalid lines */
        }

        /* Store the variable (memcpy + explicit NUL; strncpy trips
         * -Wstringop-truncation on full-length values). */
        size_t klen = strnlen(key, MAX_ENV_LINE_LEN - 1);
        memcpy(env->vars[env->count].key, key, klen);
        env->vars[env->count].key[klen] = '\0';
        size_t vlen = strnlen(value, MAX_ENV_LINE_LEN - 1);
        memcpy(env->vars[env->count].value, value, vlen);
        env->vars[env->count].value[vlen] = '\0';
        env->count++;
    }

    fclose(file);
    return ISAE_OK;
}

size_t dotenv_export_to_environ(const dotenv_t* env) {
    if (!env) return 0;
    size_t exported = 0;
    for (size_t i = 0; i < env->count; i++) {
        /* overwrite=0 -> existing env vars are not clobbered. This matches
         * the conservative behavior of 'set -a; source .env; set +a' run
         * in a shell where some vars were already exported. */
        if (setenv(env->vars[i].key, env->vars[i].value, 0) == 0) {
            exported++;
        }
    }
    return exported;
}

const char* dotenv_get(const dotenv_t* env, const char* key) {
    if (!env || !key) {
        return NULL;
    }
    
    for (size_t i = 0; i < env->count; i++) {
        if (strcmp(env->vars[i].key, key) == 0) {
            return env->vars[i].value;
        }
    }
    
    return NULL;
}

bool dotenv_has_key(const dotenv_t* env, const char* key) {
    const char* val = dotenv_get(env, key);
    return val != NULL && val[0] != '\0';
}

void dotenv_cleanup(dotenv_t* env) {
    if (!env) return;
    /* Nothing to free - static allocation */
    env->count = 0;
}
