/**
 * @file config.c
 * @brief Configuration parsing and validation implementation
 */

#include "isae_monitor/config.h"
#include "isae_monitor/departments.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Legacy channel variable mapping */
const legacy_channel_mapping_t LEGACY_CHANNEL_VARS[] = {
    {"TELEGRAM_CHANNEL_CS", DEPT_INFORMATIQUE}
};
const size_t LEGACY_CHANNEL_VARS_COUNT = sizeof(LEGACY_CHANNEL_VARS) / sizeof(LEGACY_CHANNEL_VARS[0]);

void settings_init(settings_t* settings) {
    if (!settings) return;
    memset(settings, 0, sizeof(settings_t));
    
    /* Set defaults */
    strncpy(settings->feed_url, DEFAULT_FEED_URL, MAX_URL - 1);
    strncpy(settings->state_file, "seen.json", MAX_FILE_PATH - 1);
    strncpy(settings->gemini_model, DEFAULT_GEMINI_MODEL, MAX_MODEL_NAME - 1);
    strncpy(settings->openrouter_model, DEFAULT_OPENROUTER_MODEL, MAX_MODEL_NAME - 1);
    settings->request_timeout = DEFAULT_REQUEST_TIMEOUT;
    settings->max_retries = DEFAULT_MAX_RETRIES;
    settings->send_interval = DEFAULT_SEND_INTERVAL;
    settings->state_history = DEFAULT_STATE_HISTORY;
}

static const char* get_env(const char** env_keys, const char** env_values, 
                           size_t env_count, const char* key, const char* default_val) {
    if (env_keys && env_values) {
        for (size_t i = 0; i < env_count; i++) {
            if (strcmp(env_keys[i], key) == 0) {
                return env_values[i];
            }
        }
    } else {
        const char* val = getenv(key);
        if (val) return val;
    }
    return default_val ? default_val : "";
}

static void trim_string(char* dest, const char* src, size_t max_len) {
    if (!src) {
        dest[0] = '\0';
        return;
    }
    
    /* Skip leading whitespace */
    while (*src && (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r')) src++;
    
    /* Copy */
    size_t len = strlen(src);
    while (len > 0 && (src[len-1] == ' ' || src[len-1] == '\t' || 
                       src[len-1] == '\n' || src[len-1] == '\r')) {
        len--;
    }
    
    if (len >= max_len) len = max_len - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

isae_error_t settings_from_env_dict(settings_t* settings,
                                     const char** env_keys,
                                     const char** env_values,
                                     size_t env_count) {
    if (!settings) return ISAE_ERR_INVALID_ARG;
    
    settings_init(settings);
    
    /* Get feed URL */
    const char* feed_url = get_env(env_keys, env_values, env_count, 
                                    "FEED_URL", DEFAULT_FEED_URL);
    trim_string(settings->feed_url, feed_url, MAX_URL);
    
    /* Get state file */
    const char* state_file = get_env(env_keys, env_values, env_count,
                                      "STATE_FILE", "seen.json");
    trim_string(settings->state_file, state_file, MAX_FILE_PATH);
    
    /* Get Gemini keys (comma-separated) */
    const char* gemini_keys = get_env(env_keys, env_values, env_count,
                                       "GEMINI_API_KEYS", "");
    if (!gemini_keys || !*gemini_keys) {
        /* Try singular form */
        gemini_keys = get_env(env_keys, env_values, env_count,
                              "GEMINI_API_KEY", "");
    }
    
    if (gemini_keys && *gemini_keys) {
        const char* p = gemini_keys;
        while (*p && settings->gemini_key_count < MAX_GEMINI_KEYS) {
            /* Skip leading whitespace/commas */
            while (*p && (*p == ' ' || *p == ',')) p++;
            if (!*p) break;
            
            /* Find end of key */
            const char* start = p;
            while (*p && *p != ',') p++;
            
            /* Copy key */
            size_t len = (size_t)(p - start);
            if (len > 0 && len < MAX_API_KEY) {
                /* Trim whitespace */
                while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) len--;
                if (len > 0) {
                    memcpy(settings->gemini_keys[settings->gemini_key_count], start, len);
                    settings->gemini_keys[settings->gemini_key_count][len] = '\0';
                    settings->gemini_key_count++;
                }
            }
        }
    }
    
    /* Get Gemini model */
    const char* gemini_model = get_env(env_keys, env_values, env_count,
                                        "GEMINI_MODEL", DEFAULT_GEMINI_MODEL);
    trim_string(settings->gemini_model, gemini_model, MAX_MODEL_NAME);
    
    /* Get OpenRouter key */
    const char* openrouter_key = get_env(env_keys, env_values, env_count,
                                          "OPENROUTER_API_KEY", "");
    trim_string(settings->openrouter_key, openrouter_key, MAX_API_KEY);
    settings->has_openrouter_key = (settings->openrouter_key[0] != '\0');
    
    /* Get OpenRouter model */
    const char* openrouter_model = get_env(env_keys, env_values, env_count,
                                            "OPENROUTER_MODEL", DEFAULT_OPENROUTER_MODEL);
    trim_string(settings->openrouter_model, openrouter_model, MAX_MODEL_NAME);
    
    /* Get Telegram token */
    const char* telegram_token = get_env(env_keys, env_values, env_count,
                                          "TELEGRAM_BOT_TOKEN", "");
    trim_string(settings->telegram_token, telegram_token, MAX_API_KEY);
    settings->has_telegram_token = (settings->telegram_token[0] != '\0');
    
    /* Get general channel */
    const char* general_channel = get_env(env_keys, env_values, env_count,
                                           "TELEGRAM_CHANNEL_GENERAL", "");
    trim_string(settings->general_channel, general_channel, MAX_CHAT_ID);
    settings->has_general_channel = (settings->general_channel[0] != '\0');
    
    /* Get department channels */
    departments_init();
    for (size_t i = 0; i < departments_get_count(); i++) {
        const department_t* dept = departments_get_by_index(i);
        if (!dept) continue;
        
        char env_var[64];
        snprintf(env_var, sizeof(env_var), "TELEGRAM_CHANNEL_%s", dept->key);
        /* Convert to uppercase */
        for (char* c = env_var; *c; c++) {
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - ('a' - 'A'));
        }
        
        const char* chat_id = get_env(env_keys, env_values, env_count, env_var, "");
        if (chat_id && *chat_id) {
            if (settings->department_channel_count < MAX_DEPARTMENT_CHANNELS) {
                size_t idx = settings->department_channel_count;
                strncpy(settings->department_channels_keys[idx],
                        dept->key, MAX_DEPARTMENT_KEY - 1);
                settings->department_channels_keys[idx][MAX_DEPARTMENT_KEY - 1] = '\0';
                trim_string(settings->department_channels_ids[idx],
                            chat_id, MAX_CHAT_ID);
                settings->department_channel_count++;
            }
        }
    }
    
    /* Handle legacy channel vars */
    for (size_t i = 0; i < LEGACY_CHANNEL_VARS_COUNT; i++) {
        const char* legacy_val = get_env(env_keys, env_values, env_count,
                                          LEGACY_CHANNEL_VARS[i].legacy_var, "");
        if (legacy_val && *legacy_val) {
            /* Check if already configured with new var */
            bool found = false;
            for (size_t j = 0; j < settings->department_channel_count; j++) {
                if (strcmp(settings->department_channels_keys[j], 
                          LEGACY_CHANNEL_VARS[i].dept_key) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && settings->department_channel_count < MAX_DEPARTMENT_CHANNELS) {
                size_t idx = settings->department_channel_count;
                strncpy(settings->department_channels_keys[idx],
                        LEGACY_CHANNEL_VARS[i].dept_key, MAX_DEPARTMENT_KEY - 1);
                settings->department_channels_keys[idx][MAX_DEPARTMENT_KEY - 1] = '\0';
                trim_string(settings->department_channels_ids[idx],
                            legacy_val, MAX_CHAT_ID);
                settings->department_channel_count++;
            }
        }
    }
    
    /* Get tuning parameters */
    const char* timeout_str = get_env(env_keys, env_values, env_count,
                                       "REQUEST_TIMEOUT", "20");
    settings->request_timeout = atoi(timeout_str);
    if (settings->request_timeout <= 0) settings->request_timeout = 20;
    
    const char* retries_str = get_env(env_keys, env_values, env_count,
                                       "MAX_RETRIES", "3");
    settings->max_retries = atoi(retries_str);
    if (settings->max_retries <= 0) settings->max_retries = 3;
    
    const char* interval_str = get_env(env_keys, env_values, env_count,
                                        "SEND_INTERVAL", "1.2");
    settings->send_interval = atof(interval_str);
    if (settings->send_interval <= 0) settings->send_interval = 1.2;
    
    const char* history_str = get_env(env_keys, env_values, env_count,
                                       "STATE_HISTORY", "1000");
    settings->state_history = atoi(history_str);
    if (settings->state_history <= 0) settings->state_history = 1000;
    
    return ISAE_OK;
}

isae_error_t settings_from_env(settings_t* settings) {
    return settings_from_env_dict(settings, NULL, NULL, 0);
}

bool settings_has_ai(const settings_t* settings) {
    if (!settings) return false;
    return settings->gemini_key_count > 0 || settings->has_openrouter_key;
}

bool settings_has_telegram(const settings_t* settings) {
    if (!settings) return false;
    return settings->has_telegram_token && 
           (settings->has_general_channel || settings->department_channel_count > 0);
}

isae_error_t settings_get_problems(const settings_t* settings,
                                    config_problems_t* problems) {
    if (!settings || !problems) return ISAE_ERR_INVALID_ARG;
    
    problems->count = 0;
    
    if (!settings->has_telegram_token) {
        strncpy(problems->messages[problems->count++],
                "TELEGRAM_BOT_TOKEN is not set: nothing will be delivered.",
                255);
    } else if (!settings->has_general_channel && settings->department_channel_count == 0) {
        strncpy(problems->messages[problems->count++],
                "No channels configured: set TELEGRAM_CHANNEL_GENERAL "
                "and/or per-department channels.",
                255);
    }
    
    if (!settings_has_ai(settings)) {
        strncpy(problems->messages[problems->count++],
                "No AI keys (GEMINI_API_KEYS / OPENROUTER_API_KEY): "
                "falling back to keyword classification, which is rough.",
                255);
    }
    
    if (settings->department_channel_count == 0) {
        strncpy(problems->messages[problems->count++],
                "No department channels configured: every announcement "
                "will only reach the general channel.",
                255);
    }
    
    return ISAE_OK;
}

isae_error_t settings_summary(const settings_t* settings,
                               char* buffer, size_t buffer_size) {
    if (!settings || !buffer || buffer_size == 0) return ISAE_ERR_INVALID_ARG;
    
    /* Build department list */
    char depts[512] = "none";
    if (settings->department_channel_count > 0) {
        depts[0] = '\0';
        for (size_t i = 0; i < settings->department_channel_count; i++) {
            if (i > 0) strncat(depts, ", ", sizeof(depts) - strlen(depts) - 1);
            strncat(depts, settings->department_channels_keys[i], 
                    sizeof(depts) - strlen(depts) - 1);
        }
    }
    
    /* Build providers list */
    char providers[256] = "keyword fallback only";
    if (settings->gemini_key_count > 0 || settings->has_openrouter_key) {
        providers[0] = '\0';
        if (settings->gemini_key_count > 0) {
            snprintf(providers, sizeof(providers), "gemini x%zu (%s)",
                     settings->gemini_key_count, settings->gemini_model);
        }
        if (settings->has_openrouter_key) {
            if (settings->gemini_key_count > 0) {
                strncat(providers, ", ", sizeof(providers) - strlen(providers) - 1);
            }
            strncat(providers, "openrouter (", sizeof(providers) - strlen(providers) - 1);
            strncat(providers, settings->openrouter_model, 
                    sizeof(providers) - strlen(providers) - 1);
            strncat(providers, ")", sizeof(providers) - strlen(providers) - 1);
        }
    }
    
    snprintf(buffer, buffer_size,
             "feed      : %s\n"
             "state     : %s\n"
             "providers : %s\n"
             "general   : %s\n"
             "departments: %s",
             settings->feed_url,
             settings->state_file,
             providers,
             settings->has_general_channel ? "configured" : "not set",
             depts);
    
    return ISAE_OK;
}

const char* settings_get_department_channel(const settings_t* settings,
                                             const char* dept_key) {
    if (!settings || !dept_key) return NULL;
    
    for (size_t i = 0; i < settings->department_channel_count; i++) {
        if (strcmp(settings->department_channels_keys[i], dept_key) == 0) {
            return settings->department_channels_ids[i];
        }
    }
    return NULL;
}
