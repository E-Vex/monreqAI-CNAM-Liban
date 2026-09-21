#ifndef ISAE_MONITOR_DOTENV_H
#define ISAE_MONITOR_DOTENV_H

#include "common.h"

/* Maximum number of environment variables we can load */
#define MAX_ENV_VARS 64
#define MAX_ENV_LINE_LEN 1024

/* Environment variable entry */
typedef struct {
    char key[MAX_ENV_LINE_LEN];
    char value[MAX_ENV_LINE_LEN];
} env_var_t;

/* Environment loader context */
typedef struct {
    env_var_t vars[MAX_ENV_VARS];
    size_t count;
} dotenv_t;

/* Initialize dotenv loader */
void dotenv_init(dotenv_t* env);

/* Load .env file from path (or current directory if NULL) */
isae_error_t dotenv_load(dotenv_t* env, const char* path);

/* Get value by key, returns NULL if not found */
const char* dotenv_get(const dotenv_t* env, const char* key);

/* Check if key exists and has non-empty value */
bool dotenv_has_key(const dotenv_t* env, const char* key);

/* Cleanup dotenv resources */
void dotenv_cleanup(dotenv_t* env);

#endif /* ISAE_MONITOR_DOTENV_H */
