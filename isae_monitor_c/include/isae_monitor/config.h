#ifndef ISAE_MONITOR_CONFIG_H
#define ISAE_MONITOR_CONFIG_H

#include "common.h"

/* Configuration structure */
typedef struct {
    char state_file[MAX_URL_LEN];
    int poll_interval;
    int http_timeout;
    char gemini_api_key[MAX_API_KEY_LEN];
    char openrouter_api_key[MAX_API_KEY_LEN];
    char telegram_bot_token[MAX_API_KEY_LEN];
    char telegram_chat_ids[MAX_API_KEY_LEN * 4];  /* Multiple chat IDs */
    provider_type_t provider;
} settings_t;

/* Initialize settings with defaults */
void config_init(settings_t* settings);

/* Load configuration from environment variables */
isae_error_t config_load(settings_t* settings);

/* Validate configuration */
isae_error_t config_validate(const settings_t* settings);

/* Get the preferred API provider based on available keys */
provider_type_t config_get_provider(const settings_t* settings);

#endif /* ISAE_MONITOR_CONFIG_H */
