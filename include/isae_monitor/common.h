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

/* Version information */
#define ISAE_VERSION_MAJOR 1
#define ISAE_VERSION_MINOR 0
#define ISAE_VERSION_PATCH 0
#define ISAE_VERSION_STRING "1.0.0"

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
    ISAE_ERR_AUTH = -10
} isae_error_t;

/* Department types */
typedef enum {
    DEPT_UNKNOWN = 0,
    DEPT_INFO,      /* Computer Science */
    DEPT_ELECM,     /* Electronics */
    DEPT_ELM,       /* Electrical Engineering */
    DEPT_MECM,      /* Mechanical Engineering */
    DEPT_CECM,      /* Civil Engineering */
    DEPT_GPIM,      /* Industrial Engineering */
    DEPT_EAC,       /* Applied Economics */
    DEPT_MATHS,     /* Mathematics */
    DEPT_PHYSIQUE   /* Physics */
} department_type_t;

/* API Provider types */
typedef enum {
    PROVIDER_NONE = 0,
    PROVIDER_GEMINI,
    PROVIDER_OPENROUTER
} provider_type_t;

/* Forward declaration for announcement_t (fully defined in models.h) */
typedef struct announcement announcement_t;

/* Utility function declarations */
static inline const char* isae_strerror(isae_error_t err) {
    switch (err) {
        case ISAE_OK: return "Success";
        case ISAE_ERR_MEMORY: return "Memory allocation failed";
        case ISAE_ERR_INVALID_PARAM: return "Invalid parameter";
        case ISAE_ERR_IO: return "I/O error";
        case ISAE_ERR_PARSE: return "Parse error";
        case ISAE_ERR_HTTP: return "HTTP error";
        case ISAE_ERR_CONFIG: return "Configuration error";
        case ISAE_ERR_CLASSIFICATION: return "Classification failed";
        case ISAE_ERR_NETWORK: return "Network error";
        case ISAE_ERR_TIMEOUT: return "Operation timed out";
        case ISAE_ERR_AUTH: return "Authentication failed";
        default: return "Unknown error";
    }
}

#endif /* ISAE_MONITOR_COMMON_H */
