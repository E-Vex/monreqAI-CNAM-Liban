#ifndef ISAE_MONITOR_CONFIG_H
#define ISAE_MONITOR_CONFIG_H

#include "common.h"

/* Full configuration for one run of the monitor.
 *
 * Mirrors Python isae_monitor.config.Settings. Every field has an env
 * var (or two for legacy) and a sensible default. The dry_run / bootstrap
 * / check / list_departments / quiet fields are CLI flags rather than
 * env vars (the Python tool parses them via argparse). */
typedef struct {
    /* Feed */
    char feed_url[MAX_URL_LEN];                       /* FEED_URL */

    /* State */
    char state_file[MAX_URL_LEN];                     /* STATE_FILE */

    /* Gemini */
    char gemini_keys[MAX_GEMINI_KEYS][MAX_API_KEY_LEN]; /* GEMINI_API_KEYS (comma-separated) */
    int  gemini_keys_count;
    char gemini_model[64];                            /* GEMINI_MODEL */

    /* OpenRouter */
    char openrouter_api_key[MAX_API_KEY_LEN];         /* OPENROUTER_API_KEY */
    char openrouter_model[128];                      /* OPENROUTER_MODEL */

    /* Telegram */
    char telegram_bot_token[MAX_API_KEY_LEN];         /* TELEGRAM_BOT_TOKEN */
    char telegram_channel_general[MAX_CHAT_ID_LEN];   /* TELEGRAM_CHANNEL_GENERAL */
    char department_channels[NUM_DEPARTMENTS][MAX_CHAT_ID_LEN]; /* per-department TELEGRAM_CHANNEL_<KEY> */

    /* WhatsApp Channel sidecar (optional; general category only) */
    char whatsapp_service_url[MAX_URL_LEN];           /* WHATSAPP_SERVICE_URL */
    char whatsapp_shared_secret[MAX_API_KEY_LEN];     /* WHATSAPP_SHARED_SECRET */
    char whatsapp_channel_jid[MAX_CHAT_ID_LEN];       /* WHATSAPP_CHANNEL_JID (read by the sidecar; shown by --check) */

    /* HTTP client */
    int  request_timeout;                             /* REQUEST_TIMEOUT (seconds) */
    int  max_retries;                                 /* MAX_RETRIES */

    /* Telegram client */
    double send_interval;                             /* SEND_INTERVAL (seconds) */

    /* State retention */
    size_t state_history;                             /* STATE_HISTORY */

    /* CLI flags (not env-driven). Defaults to false; main.c flips them. */
    bool dry_run;
    bool bootstrap;
    bool check;
    bool list_departments;
    bool quiet;
} settings_t;

/* Initialize with defaults. Also auto-loads .env from CWD (idempotent). */
void config_init(settings_t* settings);

/* Populate settings from environment variables. */
isae_error_t config_load(settings_t* settings);

/* Derived properties (mirror Python). */
bool config_has_ai(const settings_t* settings);
bool config_has_telegram(const settings_t* settings);
bool config_has_whatsapp(const settings_t* settings);

/* Return the department channel for a category, or NULL if not configured.
 * Accepts canonical keys, "general", "other" (the latter two always return NULL). */
const char* config_dept_channel(const settings_t* settings, const char* category);

/* Render a one-line summary of the resolved config for --check output.
 * Caller owns the returned string; free() it. */
char* config_summary(const settings_t* settings);

/* Render the list of config problems (warnings, not errors) for --check output.
 * Each problem is written to the caller's callback. */
typedef void (*config_problem_cb)(const char* message, void* user_data);
void config_problems(const settings_t* settings, config_problem_cb cb, void* user_data);

#endif /* ISAE_MONITOR_CONFIG_H */
