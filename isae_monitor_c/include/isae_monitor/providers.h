/**
 * @file providers.h
 * @brief LLM provider API calls (Gemini, OpenRouter)
 */

#ifndef ISAE_MONITOR_PROVIDERS_H
#define ISAE_MONITOR_PROVIDERS_H

#include "common.h"

/* Provider error structure */
typedef struct {
    isae_error_t code;
    long status_code;     /* HTTP status for API errors */
    char message[256];
} provider_error_t;

/* Maximum tokens for response */
#define MAX_OUTPUT_TOKENS 512

/* Classify text using Gemini API */
isae_error_t ai_classify_gemini(
    const char* api_key,
    const char* title,
    const char* summary,
    char* department_out,
    size_t out_size
);

/* Classify text using OpenRouter API */
isae_error_t ai_classify_openrouter(
    const char* api_key,
    const char* title,
    const char* summary,
    char* department_out,
    size_t out_size
);

#endif /* ISAE_MONITOR_PROVIDERS_H */
