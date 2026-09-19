/**
 * @file config.h
 * @brief Configuration parsing and validation
 */

#ifndef ISAE_MONITOR_CONFIG_H
#define ISAE_MONITOR_CONFIG_H

#include "common.h"

/* Settings structure - holds all configuration */
typedef struct {
    char feed_url[MAX_URL];
    char state_file[MAX_FILE_PATH];
    
    /* AI provider settings */
    char gemini_keys[MAX_GEMINI_KEYS][MAX_API_KEY];
    size_t gemini_key_count;
    char gemini_model[MAX_MODEL_NAME];
    char openrouter_key[MAX_API_KEY];
    bool has_openrouter_key;
    char openrouter_model[MAX_MODEL_NAME];
    
    /* Telegram settings */
    char telegram_token[MAX_API_KEY];
    bool has_telegram_token;
    char general_channel[MAX_CHAT_ID];
    bool has_general_channel;
    char department_channels_keys[MAX_DEPARTMENT_CHANNELS][MAX_DEPARTMENT_KEY];
    char department_channels_ids[MAX_DEPARTMENT_CHANNELS][MAX_CHAT_ID];
    size_t department_channel_count;
    
    /* Tuning parameters */
    int32_t request_timeout;
    int32_t max_retries;
    double send_interval;
    int32_t state_history;
} settings_t;

/* Initialize settings with default values */
void settings_init(settings_t* settings);

/* Load settings from environment variables */
isae_error_t settings_from_env(settings_t* settings);

/* Load settings from a custom environment dictionary (for testing) */
isae_error_t settings_from_env_dict(settings_t* settings, 
                                     const char** env_keys,
                                     const char** env_values,
                                     size_t env_count);

/* Check if AI providers are configured */
bool settings_has_ai(const settings_t* settings);

/* Check if Telegram is configured */
bool settings_has_telegram(const settings_t* settings);

/* Get list of configuration problems/warnings */
typedef struct {
    char messages[10][256];
    size_t count;
} config_problems_t;

isae_error_t settings_get_problems(const settings_t* settings, 
                                    config_problems_t* problems);

/* Generate configuration summary string */
isae_error_t settings_summary(const settings_t* settings, 
                               char* buffer, size_t buffer_size);

/* Get department channel by key (returns NULL if not configured) */
const char* settings_get_department_channel(const settings_t* settings, 
                                             const char* dept_key);

/* Legacy channel variable mapping */
typedef struct {
    const char* legacy_var;
    const char* dept_key;
} legacy_channel_mapping_t;

extern const legacy_channel_mapping_t LEGACY_CHANNEL_VARS[];
extern const size_t LEGACY_CHANNEL_VARS_COUNT;

#endif /* ISAE_MONITOR_CONFIG_H */
