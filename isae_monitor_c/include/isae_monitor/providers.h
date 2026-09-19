#ifndef ISAE_MONITOR_PROVIDERS_H
#define ISAE_MONITOR_PROVIDERS_H

#include "common.h"
#include "models.h"

/* AI classification response */
typedef struct {
    char category[MAX_CATEGORY_NAME];
    int confidence;
    char raw_response[1024];
    bool success;
} ai_response_t;

/* Initialize AI response */
void ai_response_init(ai_response_t* resp);

/* Free AI response resources */
void ai_response_cleanup(ai_response_t* resp);

/* Classify using Google Gemini API */
isae_error_t gemini_classify(const announcement_t* ann, 
                             const char* api_key,
                             ai_response_t* response,
                             int timeout_seconds);

/* Classify using OpenRouter API */
isae_error_t openrouter_classify(const announcement_t* ann,
                                 const char* api_key,
                                 ai_response_t* response,
                                 int timeout_seconds);

#endif /* ISAE_MONITOR_PROVIDERS_H */
