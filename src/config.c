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

static void strip_quotes(char* str) {
    if (!str || !*str) return;
    
    size_t len = strlen(str);
    
    /* Check if wrapped in matching quotes */
    if ((str[0] == '"' && str[len-1] == '"') ||
        (str[0] == '\'' && str[len-1] == '\'')) {
        /* Remove quotes by shifting content left */
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
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
        strip_quotes(settings->gemini_api_key);
    }
    
    val = get_env("OPENROUTER_API_KEY");
    if (val && *val) {
        strncpy(settings->openrouter_api_key, val, MAX_API_KEY_LEN - 1);
        settings->openrouter_api_key[MAX_API_KEY_LEN - 1] = '\0';
        trim_whitespace(settings->openrouter_api_key);
        strip_quotes(settings->openrouter_api_key);
    }
    
    /* Telegram Bot Token */
    val = get_env("TELEGRAM_BOT_TOKEN");
    if (val && *val) {
        strncpy(settings->telegram_bot_token, val, MAX_API_KEY_LEN - 1);
        settings->telegram_bot_token[MAX_API_KEY_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_bot_token);
        strip_quotes(settings->telegram_bot_token);
    }
    
    /* Telegram Channel IDs */
    val = get_env("TELEGRAM_CHANNEL_GENERAL");
    if (val && *val) {
        strncpy(settings->telegram_channel_general, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_general[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_general);
        strip_quotes(settings->telegram_channel_general);
    }
    
    val = get_env("TELEGRAM_CHANNEL_INFORMATIQUE");
    if (val && *val) {
        strncpy(settings->telegram_channel_informatique, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_informatique[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_informatique);
        strip_quotes(settings->telegram_channel_informatique);
    }
    
    val = get_env("TELEGRAM_CHANNEL_CIVIL");
    if (val && *val) {
        strncpy(settings->telegram_channel_civil, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_civil[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_civil);
        strip_quotes(settings->telegram_channel_civil);
    }
    
    val = get_env("TELEGRAM_CHANNEL_ELECTRIQUE");
    if (val && *val) {
        strncpy(settings->telegram_channel_electrique, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_electrique[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_electrique);
        strip_quotes(settings->telegram_channel_electrique);
    }
    
    val = get_env("TELEGRAM_CHANNEL_MECANIQUE");
    if (val && *val) {
        strncpy(settings->telegram_channel_mecanique, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_mecanique[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_mecanique);
        strip_quotes(settings->telegram_channel_mecanique);
    }
    
    val = get_env("TELEGRAM_CHANNEL_PROCEDES");
    if (val && *val) {
        strncpy(settings->telegram_channel_procedes, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_procedes[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_procedes);
        strip_quotes(settings->telegram_channel_procedes);
    }
    
    val = get_env("TELEGRAM_CHANNEL_ECONOMIE");
    if (val && *val) {
        strncpy(settings->telegram_channel_economie, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_economie[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_economie);
        strip_quotes(settings->telegram_channel_economie);
    }
    
    val = get_env("TELEGRAM_CHANNEL_STATISTIQUE");
    if (val && *val) {
        strncpy(settings->telegram_channel_statistique, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_statistique[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_statistique);
        strip_quotes(settings->telegram_channel_statistique);
    }
    
    val = get_env("TELEGRAM_CHANNEL_PHYSIQUE");
    if (val && *val) {
        strncpy(settings->telegram_channel_physique, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_physique[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_physique);
        strip_quotes(settings->telegram_channel_physique);
    }
    
    val = get_env("TELEGRAM_CHANNEL_LANGUES");
    if (val && *val) {
        strncpy(settings->telegram_channel_langues, val, MAX_CHAT_ID_LEN - 1);
        settings->telegram_channel_langues[MAX_CHAT_ID_LEN - 1] = '\0';
        trim_whitespace(settings->telegram_channel_langues);
        strip_quotes(settings->telegram_channel_langues);
    }
    
    /* Determine provider */
    settings->provider = config_get_provider(settings);
    
    return ISAE_OK;
}

isae_error_t config_validate(const settings_t* settings) {
    if (!settings) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* At least one AI provider should be configured for classification */
    if (settings->provider == PROVIDER_NONE) {
        fprintf(stderr, "Warning: No AI provider configured. Will use keyword-only classification.\n");
    }
    
    /* If Telegram is partially configured, warn */
    if ((settings->telegram_bot_token[0] && !settings->telegram_channel_general[0])) {
        fprintf(stderr, "Warning: Telegram bot token set but no general channel configured.\n");
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

const char* config_get_dept_channel(const settings_t* settings, const char* dept_key) {
    if (!settings || !dept_key) {
        return NULL;
    }
    
    /* Map department key to channel */
    if (strcmp(dept_key, "INFO") == 0 || strcmp(dept_key, "INFORMATIQUE") == 0) {
        return settings->telegram_channel_informatique[0] ? settings->telegram_channel_informatique : NULL;
    }
    if (strcmp(dept_key, "CECM") == 0 || strcmp(dept_key, "CIVIL") == 0) {
        return settings->telegram_channel_civil[0] ? settings->telegram_channel_civil : NULL;
    }
    if (strcmp(dept_key, "ELM") == 0 || strcmp(dept_key, "ELECTRIQUE") == 0) {
        return settings->telegram_channel_electrique[0] ? settings->telegram_channel_electrique : NULL;
    }
    if (strcmp(dept_key, "MECM") == 0 || strcmp(dept_key, "MECANIQUE") == 0) {
        return settings->telegram_channel_mecanique[0] ? settings->telegram_channel_mecanique : NULL;
    }
    if (strcmp(dept_key, "GPIM") == 0 || strcmp(dept_key, "PROCEDES") == 0) {
        return settings->telegram_channel_procedes[0] ? settings->telegram_channel_procedes : NULL;
    }
    if (strcmp(dept_key, "EAC") == 0 || strcmp(dept_key, "ECONOMIE") == 0) {
        return settings->telegram_channel_economie[0] ? settings->telegram_channel_economie : NULL;
    }
    if (strcmp(dept_key, "MATHS") == 0 || strcmp(dept_key, "STATISTIQUE") == 0) {
        return settings->telegram_channel_statistique[0] ? settings->telegram_channel_statistique : NULL;
    }
    if (strcmp(dept_key, "PHYSIQUE") == 0) {
        return settings->telegram_channel_physique[0] ? settings->telegram_channel_physique : NULL;
    }
    if (strcmp(dept_key, "LANGUES") == 0) {
        return settings->telegram_channel_langues[0] ? settings->telegram_channel_langues : NULL;
    }
    
    return NULL;
}
