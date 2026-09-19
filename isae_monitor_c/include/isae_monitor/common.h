/**
 * @file common.h
 * @brief Common types, macros, and error codes for ISAE Monitor
 */

#ifndef ISAE_MONITOR_COMMON_H
#define ISAE_MONITOR_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Version information */
#define ISAE_MONITOR_VERSION "2.0.0"
#define ISAE_MONITOR_VERSION_MAJOR 2
#define ISAE_MONITOR_VERSION_MINOR 0
#define ISAE_MONITOR_VERSION_PATCH 0

/* Buffer sizes - chosen to handle all expected inputs safely */
#define MAX_ANNOUNCEMENT_ID     512
#define MAX_ANNOUNCEMENT_TITLE  256
#define MAX_ANNOUNCEMENT_SUMMARY 512
#define MAX_ANNOUNCEMENT_LINK   512
#define MAX_PUBLISHED_DATE      64
#define MAX_DEPARTMENT_KEY      32
#define MAX_DEPARTMENT_NAME     128
#define MAX_KEYWORD             128
#define MAX_CHAT_ID             64
#define MAX_API_KEY             256
#define MAX_MODEL_NAME          128
#define MAX_URL                 512
#define MAX_FILE_PATH           512
#define MAX_ENV_VALUE           1024

/* Array limits */
#define MAX_GEMINI_KEYS         10
#define MAX_DEPARTMENTS         9
#define MAX_KEYWORDS_PER_DEPT   30
#define MAX_ALIASES_PER_DEPT    10
#define MAX_DEPARTMENT_CHANNELS 10

/* Message limits (Telegram) */
#define MAX_TELEGRAM_MESSAGE    3900
#define MAX_TITLE_DISPLAY       250
#define MAX_SUMMARY_DISPLAY     400

/* HTTP defaults */
#define DEFAULT_REQUEST_TIMEOUT 20
#define DEFAULT_MAX_RETRIES     3
#define DEFAULT_SEND_INTERVAL   1.2
#define DEFAULT_STATE_HISTORY   1000

/* Default URLs and models */
#define DEFAULT_FEED_URL "http://annonces.isae.edu.lb/feeds/posts/default?max-results=25"
#define DEFAULT_GEMINI_MODEL "gemini-2.5-flash"
#define DEFAULT_OPENROUTER_MODEL "meta-llama/llama-3.3-70b-instruct"
#define GEMINI_ENDPOINT_TEMPLATE "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"
#define OPENROUTER_ENDPOINT "https://openrouter.ai/api/v1/chat/completions"
#define TELEGRAM_API_TEMPLATE "https://api.telegram.org/bot%s/sendMessage"

/* State file version */
#define STATE_VERSION 2

/* Error codes - negative values indicate errors */
typedef enum {
    ISAE_OK = 0,
    ISAE_ERR_MEMORY = -1,
    ISAE_ERR_INVALID_ARG = -2,
    ISAE_ERR_NOT_FOUND = -3,
    ISAE_ERR_HTTP = -4,
    ISAE_ERR_JSON_PARSE = -5,
    ISAE_ERR_XML_PARSE = -6,
    ISAE_ERR_IO = -7,
    ISAE_ERR_CONFIG = -8,
    ISAE_ERR_TELEGRAM = -9,
    ISAE_ERR_CLASSIFIER = -10,
    ISAE_ERR_FEED = -11,
    ISAE_ERR_INTERRUPTED = -12,
    ISAE_ERR_INVALID_PARAM = -13,  /* Alias for ISAE_ERR_INVALID_ARG */
    ISAE_ERR_BUFFER_FULL = -14,
    ISAE_ERR_PARSE = -15,
    ISAE_ERR_CLASSIFICATION = -16
} isae_error_t;

/* Category strings */
#define CAT_GENERAL "general"
#define CAT_OTHER "other"

/* Department category keys */
#define DEPT_INFORMATIQUE "informatique"
#define DEPT_CIVIL "civil"
#define DEPT_ELECTRIQUE "electrique"
#define DEPT_MECANIQUE "mecanique"
#define DEPT_PROCEDES "procedes"
#define DEPT_ECONOMIE "economie"
#define DEPT_STATISTIQUE "statistique"
#define DEPT_PHYSIQUE "physique"
#define DEPT_LANGUES "langues"

/* Macro for unused parameters */
#define UNUSED(x) (void)(x)

/* Macro for array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Helper macro for min/max */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#endif /* ISAE_MONITOR_COMMON_H */
