#ifndef ISAE_MONITOR_COMMON_H
#define ISAE_MONITOR_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Version information.
 * The build system (Makefile / CMakeLists.txt) is allowed to override
 * ISAE_VERSION_STRING via -D so the version stays in lockstep with the
 * build definition; the fallback below is only used when that override
 * is absent (e.g. when an IDE compiles a single TU without the project
 * flags). Keep all four in sync. */
#ifndef ISAE_VERSION_STRING
#define ISAE_VERSION_STRING "2.0.0"
#endif
#define ISAE_VERSION_MAJOR 2
#define ISAE_VERSION_MINOR 0
#define ISAE_VERSION_PATCH 0

/* Maximum sizes for various fields */
#define MAX_TITLE_LEN 512
#define MAX_SUMMARY_LEN 4096
#define MAX_URL_LEN 1024
#define MAX_DEPARTMENT_KEY 32
#define MAX_CATEGORY_NAME 64
#define MAX_PUBLISHED_DATE 64
#define MAX_HASH_LEN 64
#define MAX_TEXT_NORMALIZED_LEN 8192
#define MAX_API_KEY_LEN 256
#define MAX_CHAT_ID_LEN 64
#define MAX_ERROR_MSG_LEN 256
#define MAX_API_RESPONSE_LEN 8192

/* Number of departments at ISSAE / Cnam-Liban (matches Python reference). */
#define NUM_DEPARTMENTS 9
/* 9 departments + 2 pseudo-categories (general + other). */
#define NUM_CATEGORIES (NUM_DEPARTMENTS + 2)
/* Maximum number of Gemini keys accepted via GEMINI_API_KEYS. */
#define MAX_GEMINI_KEYS 16

/* State file version (matches the Python seen.json v2 schema). */
#define STATE_FILE_VERSION 2
#define STATE_HISTORY_DEFAULT 1000

/* Telegram message size caps (character-count semantics, see telegram.c). */
#define TELEGRAM_MAX_MESSAGE 3900
#define TELEGRAM_TITLE_LIMIT 250
#define TELEGRAM_SUMMARY_LIMIT 400

/* HTTP client behavior. */
#define HTTP_MAX_BODY 800            /* truncated body kept for error messages */
#define HTTP_MAX_RESPONSE_BODY (1ul << 20)  /* 1 MiB cap on successful responses */
#define HTTP_MAX_RETRIES_DEFAULT 3
#define HTTP_BACKOFF_CAP_SECONDS 30
#define HTTP_RETRY_AFTER_CAP_SECONDS 60

/* Error codes */
typedef enum {
    ISAE_OK = 0,
    ISAE_ERR_MEMORY = -1,
    ISAE_ERR_INVALID_PARAM = -2,
    ISAE_ERR_IO = -3,
    ISAE_ERR_PARSE = -4,
    ISAE_ERR_HTTP = -5,
    ISAE_ERR_CONFIG = -6,
    ISAE_ERR_CLASSIFICATION = -7,
    ISAE_ERR_NETWORK = -8,
    ISAE_ERR_TIMEOUT = -9,
    ISAE_ERR_AUTH = -10,
    ISAE_ERR_FEED = -11,             /* feed fetch / parse failure */
    ISAE_ERR_INTERRUPTED = -12      /* caught SIGINT mid-run */
} isae_error_t;

/* API Provider types */
typedef enum {
    PROVIDER_NONE = 0,
    PROVIDER_GEMINI,
    PROVIDER_OPENROUTER,
    PROVIDER_KEYWORDS     /* local fallback, never configured via env */
} provider_type_t;

/* Forward declaration for announcement_t (fully defined in models.h) */
typedef struct announcement announcement_t;

/* Utility function declarations */
static inline const char* isae_strerror(isae_error_t err) {
    switch (err) {
        case ISAE_OK:                  return "Success";
        case ISAE_ERR_MEMORY:          return "Memory allocation failed";
        case ISAE_ERR_INVALID_PARAM:   return "Invalid parameter";
        case ISAE_ERR_IO:              return "I/O error";
        case ISAE_ERR_PARSE:           return "Parse error";
        case ISAE_ERR_HTTP:            return "HTTP error";
        case ISAE_ERR_CONFIG:          return "Configuration error";
        case ISAE_ERR_CLASSIFICATION:  return "Classification failed";
        case ISAE_ERR_NETWORK:         return "Network error";
        case ISAE_ERR_TIMEOUT:         return "Operation timed out";
        case ISAE_ERR_AUTH:            return "Authentication failed";
        case ISAE_ERR_FEED:            return "Feed fetch/parse error";
        case ISAE_ERR_INTERRUPTED:     return "Interrupted by signal";
        default:                       return "Unknown error";
    }
}

#endif /* ISAE_MONITOR_COMMON_H */
