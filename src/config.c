/**
 * ISAE Monitor - Configuration
 *
 * Faithful C port of Python isae_monitor/config.py.
 *
 * Reads every env var the Python reference reads, with the same defaults
 * and the same legacy fallbacks (STATE_FILE / ISAE_STATE_FILE,
 * GEMINI_API_KEY singular when GEMINI_API_KEYS plural is empty,
 * TELEGRAM_CHANNEL_CS legacy alias for informatique).
 */

#include "isae_monitor/config.h"
#include "isae_monitor/dotenv.h"
#include "isae_monitor/departments.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FEED_URL          "http://annonces.isae.edu.lb/feeds/posts/default?max-results=25"
#define DEFAULT_STATE_FILE         "seen.json"
#define DEFAULT_GEMINI_MODEL       "gemini-2.5-flash"
#define DEFAULT_OPENROUTER_MODEL   "meta-llama/llama-3.3-70b-instruct"
#define DEFAULT_REQUEST_TIMEOUT    20
#define DEFAULT_MAX_RETRIES        3
#define DEFAULT_SEND_INTERVAL      1.2
#define DEFAULT_STATE_HISTORY      STATE_HISTORY_DEFAULT

static dotenv_t g_dotenv;
static bool g_dotenv_loaded = false;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Python: (e.get(name) or default).strip() -- empty string is treated as missing. */
static const char* env_get(const char* name, const char* fallback) {
    const char* v = getenv(name);
    if (v) {
        while (*v && isspace((unsigned char)*v)) v++;
        if (*v) return v;
    }
    return fallback;
}

static void copy_value(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    /* Python applies .strip() to every value. */
    while (*src && isspace((unsigned char)*src)) src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void strip_quotes(char* str) {
    if (!str || !*str) return;
    size_t len = strlen(str);
    if (len < 2) return;
    if ((str[0] == '"' && str[len - 1] == '"') ||
        (str[0] == '\'' && str[len - 1] == '\'')) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

static int env_get_int(const char* name, int fallback) {
    const char* v = getenv(name);
    if (!v || !*v) return fallback;
    char* end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return fallback;
    return (int)n;
}

static double env_get_double(const char* name, double fallback) {
    const char* v = getenv(name);
    if (!v || !*v) return fallback;
    char* end = NULL;
    double d = strtod(v, &end);
    if (end == v) return fallback;
    return d;
}

static size_t env_get_size(const char* name, size_t fallback) {
    const char* v = getenv(name);
    if (!v || !*v) return fallback;
    char* end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v || n < 0) return fallback;
    return (size_t)n;
}

/* Split a comma-separated string into at most dst_cap entries of dst_size each.
 * Empty entries are dropped (Python: [k.strip() for k in s.split(",") if k.strip()]). */
static int split_csv(const char* src, char dst[][MAX_API_KEY_LEN], int dst_cap, size_t entry_size) {
    if (!src || !*src) return 0;
    int count = 0;
    const char* p = src;
    while (*p && count < dst_cap) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        /* trim */
        while (len > 0 && isspace((unsigned char)*p)) { p++; len--; }
        while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
        if (len > 0) {
            if (len >= entry_size) len = entry_size - 1;
            memcpy(dst[count], p, len);
            dst[count][len] = '\0';
            strip_quotes(dst[count]);
            count++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void config_init(settings_t* settings) {
    if (!settings) return;
    memset(settings, 0, sizeof(*settings));

    /* One-shot .env auto-load. */
    if (!g_dotenv_loaded) {
        dotenv_init(&g_dotenv);
        if (dotenv_load(&g_dotenv, ".env") == ISAE_OK) {
            (void)dotenv_export_to_environ(&g_dotenv);
        }
        g_dotenv_loaded = true;
    }

    /* Defaults */
    copy_value(settings->feed_url, sizeof(settings->feed_url), DEFAULT_FEED_URL);
    copy_value(settings->state_file, sizeof(settings->state_file), DEFAULT_STATE_FILE);
    copy_value(settings->gemini_model, sizeof(settings->gemini_model), DEFAULT_GEMINI_MODEL);
    copy_value(settings->openrouter_model, sizeof(settings->openrouter_model), DEFAULT_OPENROUTER_MODEL);
    settings->request_timeout = DEFAULT_REQUEST_TIMEOUT;
    settings->max_retries = DEFAULT_MAX_RETRIES;
    settings->send_interval = DEFAULT_SEND_INTERVAL;
    settings->state_history = DEFAULT_STATE_HISTORY;
}

isae_error_t config_load(settings_t* settings) {
    if (!settings) return ISAE_ERR_INVALID_PARAM;

    const char* v;

    /* FEED_URL */
    v = env_get("FEED_URL", settings->feed_url);
    copy_value(settings->feed_url, sizeof(settings->feed_url), v);

    /* STATE_FILE -- accept STATE_FILE or ISAE_STATE_FILE (legacy). */
    v = getenv("STATE_FILE");
    if (!v || !*v) v = getenv("ISAE_STATE_FILE");
    if (v && *v) copy_value(settings->state_file, sizeof(settings->state_file), v);

    /* GEMINI_API_KEYS (plural, comma-separated). Fall back to GEMINI_API_KEY
     * (singular) when plural is unset or empty. */
    v = getenv("GEMINI_API_KEYS");
    if (!v || !*v) v = getenv("GEMINI_API_KEY");
    if (v && *v) {
        settings->gemini_keys_count = split_csv(v, settings->gemini_keys, MAX_GEMINI_KEYS, MAX_API_KEY_LEN);
    }

    /* GEMINI_MODEL, OPENROUTER_MODEL, OPENROUTER_API_KEY */
    v = env_get("GEMINI_MODEL", settings->gemini_model);
    copy_value(settings->gemini_model, sizeof(settings->gemini_model), v);
    v = getenv("OPENROUTER_API_KEY");
    if (v && *v) {
        copy_value(settings->openrouter_api_key, sizeof(settings->openrouter_api_key), v);
        strip_quotes(settings->openrouter_api_key);
    }
    v = env_get("OPENROUTER_MODEL", settings->openrouter_model);
    copy_value(settings->openrouter_model, sizeof(settings->openrouter_model), v);

    /* Telegram */
    v = getenv("TELEGRAM_BOT_TOKEN");
    if (v && *v) {
        copy_value(settings->telegram_bot_token, sizeof(settings->telegram_bot_token), v);
        strip_quotes(settings->telegram_bot_token);
    }
    v = getenv("TELEGRAM_CHANNEL_GENERAL");
    if (v && *v) {
        copy_value(settings->telegram_channel_general, sizeof(settings->telegram_channel_general), v);
        strip_quotes(settings->telegram_channel_general);
    }

    /* Per-department channels via the departments_env_var() lookup.
     * This keeps the canonical-key -> env-var mapping in one place. */
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        const char* env_name = departments_env_var(d->key);
        if (!env_name) continue;
        v = getenv(env_name);
        if (v && *v) {
            copy_value(settings->department_channels[i], MAX_CHAT_ID_LEN, v);
            strip_quotes(settings->department_channels[i]);
        }
    }

    /* Legacy TELEGRAM_CHANNEL_CS -> informatique (only if the canonical
     * var was not set, mirroring the Python LEGACY_CHANNEL_VARS handling). */
    v = getenv("TELEGRAM_CHANNEL_CS");
    if (v && *v) {
        /* Find informatique's slot. */
        for (size_t i = 0; i < departments_count(); i++) {
            const department_t* d = departments_get_at(i);
            if (strcmp(d->key, "informatique") == 0 && settings->department_channels[i][0] == '\0') {
                copy_value(settings->department_channels[i], MAX_CHAT_ID_LEN, v);
                strip_quotes(settings->department_channels[i]);
                break;
            }
        }
    }

    /* Numeric env vars */
    settings->request_timeout = env_get_int("REQUEST_TIMEOUT", settings->request_timeout);
    settings->max_retries     = env_get_int("MAX_RETRIES", settings->max_retries);
    settings->send_interval   = env_get_double("SEND_INTERVAL", settings->send_interval);
    settings->state_history   = env_get_size("STATE_HISTORY", settings->state_history);

    return ISAE_OK;
}

bool config_has_ai(const settings_t* settings) {
    if (!settings) return false;
    if (settings->gemini_keys_count > 0) return true;
    return settings->openrouter_api_key[0] != '\0';
}

bool config_has_telegram(const settings_t* settings) {
    if (!settings) return false;
    if (!settings->telegram_bot_token[0]) return false;
    if (settings->telegram_channel_general[0]) return true;
    for (size_t i = 0; i < departments_count(); i++) {
        if (settings->department_channels[i][0]) return true;
    }
    return false;
}

const char* config_dept_channel(const settings_t* settings, const char* category) {
    if (!settings || !category) return NULL;
    if (strcmp(category, CATEGORY_GENERAL) == 0) return settings->telegram_channel_general[0] ? settings->telegram_channel_general : NULL;
    if (strcmp(category, CATEGORY_OTHER) == 0) return NULL;
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        if (strcmp(d->key, category) == 0) {
            return settings->department_channels[i][0] ? settings->department_channels[i] : NULL;
        }
    }
    return NULL;
}

char* config_summary(const settings_t* s) {
    if (!s) return NULL;
    char* buf = (char*)malloc(4096);
    if (!buf) return NULL;
    int w = 0;
    w += snprintf(buf + w, 4096 - w, "Configuration summary:\n");
    w += snprintf(buf + w, 4096 - w, "  FEED_URL            : %s\n", s->feed_url);
    w += snprintf(buf + w, 4096 - w, "  STATE_FILE          : %s\n", s->state_file);
    w += snprintf(buf + w, 4096 - w, "  STATE_HISTORY       : %zu\n", s->state_history);
    w += snprintf(buf + w, 4096 - w, "  GEMINI_MODEL        : %s\n", s->gemini_model);
    w += snprintf(buf + w, 4096 - w, "  GEMINI_API_KEYS     : %d configured\n", s->gemini_keys_count);
    w += snprintf(buf + w, 4096 - w, "  OPENROUTER_MODEL    : %s\n", s->openrouter_model);
    w += snprintf(buf + w, 4096 - w, "  OPENROUTER_API_KEY  : %s\n", s->openrouter_api_key[0] ? "set" : "(not set)");
    w += snprintf(buf + w, 4096 - w, "  TELEGRAM_BOT_TOKEN  : %s\n", s->telegram_bot_token[0] ? "set" : "(not set)");
    w += snprintf(buf + w, 4096 - w, "  GENERAL_CHANNEL     : %s\n", s->telegram_channel_general[0] ? s->telegram_channel_general : "(not set)");
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        w += snprintf(buf + w, 4096 - w, "  %-20s: %s\n", departments_env_var(d->key),
                      s->department_channels[i][0] ? s->department_channels[i] : "(not set)");
    }
    w += snprintf(buf + w, 4096 - w, "  REQUEST_TIMEOUT     : %d s\n", s->request_timeout);
    w += snprintf(buf + w, 4096 - w, "  MAX_RETRIES         : %d\n", s->max_retries);
    w += snprintf(buf + w, 4096 - w, "  SEND_INTERVAL       : %.2f s\n", s->send_interval);
    return buf;
}

void config_problems(const settings_t* s, config_problem_cb cb, void* user_data) {
    if (!s || !cb) return;
    if (!config_has_ai(s)) {
        cb("No AI provider configured -- keyword fallback will be used for all announcements.", user_data);
    }
    if (s->telegram_bot_token[0] && !s->telegram_channel_general[0] && !config_has_telegram(s)) {
        cb("TELEGRAM_BOT_TOKEN is set but no channel is configured -- notifications will be skipped.", user_data);
    }
    if (!s->telegram_bot_token[0] && s->telegram_channel_general[0]) {
        cb("TELEGRAM_CHANNEL_GENERAL is set but TELEGRAM_BOT_TOKEN is missing.", user_data);
    }
}
