#ifndef ISAE_MONITOR_CONFIG_H
#define ISAE_MONITOR_CONFIG_H

#include "common.h"

/* Number of department channels */
#define NUM_DEPT_CHANNELS 10

/* Department channel names (for reference) */
#define DEPT_CHANNEL_NAMES \
    X(INFORMATIQUE) \
    X(CIVIL) \
    X(ELECTRIQUE) \
    X(MECANIQUE) \
    X(PROCEDES) \
    X(ECONOMIE) \
    X(STATISTIQUE) \
    X(PHYSIQUE) \
    X(LANGUES) \
    X(GENERAL)

/* Configuration structure */
typedef struct {
    char state_file[MAX_URL_LEN];
    int poll_interval;
    int http_timeout;
    char gemini_api_key[MAX_API_KEY_LEN];
    char openrouter_api_key[MAX_API_KEY_LEN];
    char telegram_bot_token[MAX_API_KEY_LEN];
    char telegram_channel_general[MAX_CHAT_ID_LEN];
    char telegram_channel_informatique[MAX_CHAT_ID_LEN];
    char telegram_channel_civil[MAX_CHAT_ID_LEN];
    char telegram_channel_electrique[MAX_CHAT_ID_LEN];
    char telegram_channel_mecanique[MAX_CHAT_ID_LEN];
    char telegram_channel_procedes[MAX_CHAT_ID_LEN];
    char telegram_channel_economie[MAX_CHAT_ID_LEN];
    char telegram_channel_statistique[MAX_CHAT_ID_LEN];
    char telegram_channel_physique[MAX_CHAT_ID_LEN];
    char telegram_channel_langues[MAX_CHAT_ID_LEN];
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

/* Get department channel ID by key, returns NULL if not configured */
const char* config_get_dept_channel(const settings_t* settings, const char* dept_key);

#endif /* ISAE_MONITOR_CONFIG_H */
