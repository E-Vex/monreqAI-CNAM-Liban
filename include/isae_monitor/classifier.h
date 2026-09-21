#ifndef ISAE_MONITOR_CLASSIFIER_H
#define ISAE_MONITOR_CLASSIFIER_H

#include "common.h"
#include "models.h"
#include "config.h"
#include "keywords.h"
#include "providers.h"
#include "httpclient.h"

/* Classification result. Mirrors Python's classify.Result.
 *
 * category is always set to a valid routing key (one of the 9
 * departments, "general", or "other"). The keyword fallback always
 * succeeds, so success is always true.
 *
 * source is "gemini", "openrouter", or "keywords".
 *
 * detail is the last error message from the AI phase, if any. It is
 * surfaced only for diagnostics, never used for routing.
 */
typedef struct {
    char category[MAX_CATEGORY_NAME];
    char source[16];
    char detail[256];
    bool is_ai;
} classification_result_t;

void classification_result_init(classification_result_t* result);

/* Classifier context. Per-run only; not persisted across processes. */
typedef struct {
    const settings_t* settings;
    http_client_config_t http_cfg;
    /* Dead keys (Gemini): up to MAX_GEMINI_KEYS entries. NULL means alive. */
    bool gemini_dead[MAX_GEMINI_KEYS];
    bool openrouter_dead;
    /* Notes (deduplicated by string). */
    char notes[MAX_GEMINI_KEYS + 1][256];
    int notes_count;
} classifier_t;

void classifier_init(classifier_t* classifier, const settings_t* settings);
void classifier_cleanup(classifier_t* classifier);

/* Classify an announcement. Always succeeds (keyword fallback never throws). */
isae_error_t classifier_classify(classifier_t* classifier,
                                 announcement_t* ann,
                                 classification_result_t* result);

/* Per-run notes (e.g. "gemini key disabled for this run (HTTP 401)").
 * Deduplicated by exact string match. Caller does not own the strings. */
const char* const* classifier_notes(const classifier_t* classifier, int* count_out);

#endif /* ISAE_MONITOR_CLASSIFIER_H */
