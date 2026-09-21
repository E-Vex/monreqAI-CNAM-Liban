#ifndef ISAE_MONITOR_PROVIDERS_H
#define ISAE_MONITOR_PROVIDERS_H

#include "common.h"
#include "models.h"
#include "httpclient.h"

/* AI provider result.
 *
 * raw_text is the text extracted from the provider's response (already
 * unwrapped from candidates[0].content.parts[].text for Gemini, or
 * choices[0].message.content for OpenRouter). The classifier parses
 * this further into a category.
 *
 * status is the HTTP status code that caused the failure, or 0 if the
 * failure was not HTTP-related (e.g. malformed response shape).
 *
 * ok is true iff raw_text contains a non-empty answer.
 */
typedef struct {
    char* raw_text;
    long status;        /* HTTP status of the failing response, or 0 */
    bool ok;
    char error[256];    /* human-readable error message when !ok */
} ai_response_t;

void ai_response_init(ai_response_t* resp);
void ai_response_cleanup(ai_response_t* resp);

/* Build the prompt that both providers receive. Caller owns the returned
 * heap string; free() it. Mirrors Python's Classifier.build_prompt(). */
char* providers_build_prompt(const announcement_t* ann);

/* Call Gemini. Returns ISAE_OK with response->ok=true on success.
 * On failure, returns ISAE_ERR_HTTP (with response->status set) or
 * ISAE_ERR_PARSE (malformed response). The 400 special-case retries
 * once without thinkingConfig/responseSchema. */
isae_error_t providers_call_gemini(const char* prompt,
                                   const char* api_key,
                                   const char* model,
                                   const http_client_config_t* http_cfg,
                                   ai_response_t* response);

/* Call OpenRouter. Same success/failure semantics. */
isae_error_t providers_call_openrouter(const char* prompt,
                                       const char* api_key,
                                       const char* model,
                                       const http_client_config_t* http_cfg,
                                       ai_response_t* response);

/* Parse the raw_text from a provider response into a routing category.
 * Returns NULL if the response is unparseable (caller falls through to
 * the next provider). The returned pointer is a static buffer; copy it
 * immediately if you need to keep it. */
const char* providers_extract_category(const char* raw_text);

#endif /* ISAE_MONITOR_PROVIDERS_H */
