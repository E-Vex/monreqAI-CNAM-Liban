/**
 * ISAE Monitor - Configuration Implementation
 * 
 * Maps Python: config.py -> C: config.c
 * 
 * Loads configuration from environment variables with legacy support.
 */

#include "isae_monitor/config.h"
#include <ctype.h>

void config_init(settings_t* settings) {
    if (!settings) return;
    
    memset(settings, 0, sizeof(settings_t));
    
    /* Set defaults */
    strncpy(settings->state_file, "~/.isae_monitor_state.json", MAX_URL_LEN - 1);
    settings->poll_interval = 300;  /* 5 minutes */
    settings->http_timeout = 30;
    settings->provider = PROVIDER_NONE;
}

static const char* get_env(const char* name) {
    return getenv(name);
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

isae_error_t config_load(settings_t* settings) {
    if (!settings) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    const char* val;
    
    /* State file - check new then legacy */
    val = get_env("ISAE_STATE_FILE");
    if (!val || !*val) {
        val = get_env("STATE_FILE");  /* Legacy */
    }
    if (val && *val) {
        strncpy(settings->state_file, val, MAX_URL_LEN - 1);
        settings->state_file[MAX_URL_LEN - 1] = '\0';
    }
    
    /* Poll interval */
    val = get_env("ISAE_POLL_INTERVAL");
    if (!val || !*val) {
        val = get_env("POLL_INTERVAL");  /* Legacy */
    }
    if (val && *val) {
        int interval = atoi(val);
        if (interval > 0) {
            settings->poll_interval = interval;
        }
    }
    
    /* HTTP timeout */
    val = get_env("ISAE_HTTP_TIMEOUT");
    if (!val || !*val) {
        val = get_env("HTTP_TIMEOUT");  /* Legacy */
    }
    if (val && *val) {
        int timeout = atoi(val);
        if (timeout > 0) {
            settings->http_timeout = timeout;
        }
    }
    
    /* API Keys */
    val = get_env("GEMINI_API_KEY");
    if (val && *val) {
        strncpy(settings->gemini_api_key, val, MAX_API_KEY_LEN - 1);
        settings->gemini_api_key[MAX_API_KEY_LEN - 1] = '\0';
        trim_whitespace(settings->gemini_api_key);
    }
    
    val = get_env("OPENROUTER_API_KEY");
    if (val && *val) {
        strncpy(settings->openrouter_api_key, val, MAX_API_KEY_LEN - 1);
        settings->openrouter_api_key[MAX_API_KEY_LEN - 1] = '\0';
        trim_whitespace(settings->openrouter_api_key);
    }
    
    /* Telegram */
    val = get_env("TELEGRAM_BOT_TOKEN");
    if (val && *val) {
        strncpy(settings->telegram_bot_token, val, MAX_API_KEY_LEN - 1);
        settings->telegram_bot_token[MAX_API_KEY_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_bot_token);
    }
    
    val = get_env("TELEGRAM_CHAT_IDS");
    if (val && *val) {
        strncpy(settings->telegram_chat_ids, val, sizeof(settings->telegram_chat_ids) - 1);
        settings->telegram_chat_ids[sizeof(settings->telegram_chat_ids) - 1] = '\0';
        trim_whitespace(settings->telegram_chat_ids);
    }
    
    /* Determine provider */
    settings->provider = config_get_provider(settings);
    
    return ISAE_OK;
}

isae_error_t config_validate(const settings_t* settings) {
    if (!settings) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* At least one API provider should be configured for classification */
    if (settings->provider == PROVIDER_NONE) {
        fprintf(stderr, "Warning: No AI provider configured. Will use keyword-only classification.\n");
    }
    
    /* If Telegram is partially configured, warn */
    if ((settings->telegram_bot_token[0] && !settings->telegram_chat_ids[0]) ||
        (!settings->telegram_bot_token[0] && settings->telegram_chat_ids[0])) {
        fprintf(stderr, "Warning: Telegram partially configured. Need both BOT_TOKEN and CHAT_IDS.\n");
    }
    
    return ISAE_OK;
}

provider_type_t config_get_provider(const settings_t* settings) {
    if (!settings) {
        return PROVIDER_NONE;
    }
    
    /* Prefer Gemini if available */
    if (settings->gemini_api_key[0] != '\0') {
        return PROVIDER_GEMINI;
    }
    
    /* Fall back to OpenRouter */
    if (settings->openrouter_api_key[0] != '\0') {
        return PROVIDER_OPENROUTER;
    }
    
    return PROVIDER_NONE;
}
