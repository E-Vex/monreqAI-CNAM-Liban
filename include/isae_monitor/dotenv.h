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

/* Load .env file from path (or current directory if NULL).
 * Returns ISAE_OK whether or not the file exists -- a missing .env is
 * normal and never an error. Returns ISAE_ERR_INVALID_PARAM if env is
 * NULL. Returns ISAE_ERR_IO only if the file exists but cannot be read. */
isae_error_t dotenv_load(dotenv_t* env, const char* path);

/* Push every loaded variable into the process environment via setenv()
 * with overwrite=0, so existing values win (mirrors the shell convention
 * 'set -a; source .env; set +a' run in a shell that already had the
 * variable set). Returns the number of variables actually exported. */
size_t dotenv_export_to_environ(const dotenv_t* env);

/* Get value by key, returns NULL if not found */
const char* dotenv_get(const dotenv_t* env, const char* key);

/* Check if key exists and has non-empty value */
bool dotenv_has_key(const dotenv_t* env, const char* key);

/* Cleanup dotenv resources */
void dotenv_cleanup(dotenv_t* env);

#endif /* ISAE_MONITOR_DOTENV_H */
