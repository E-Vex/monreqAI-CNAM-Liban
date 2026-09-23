/**
 * ISAE Monitor - Classification Engine
 *
 * Faithful C port of Python isae_monitor/classify/__init__.py.
 *
 * Tries Gemini keys (in order, skipping dead ones), then OpenRouter
 * (if alive), then the keyword fallback. Key health is tracked for the
 * duration of the run only; it is deliberately NOT persisted across
 * processes (the process is short-lived, and the HTTP layer's backoff
 * covers the cross-run case).
 *
 * Dead-key status sets (mirror Python exactly):
 *   Gemini:     {400, 401, 403, 404, 429}
 *   OpenRouter: {401, 402, 403}
 *
 * These sets are intentionally asymmetric -- 400/404/429 from OpenRouter
 * is treated as a transient retryable failure rather than a dead key.
 */

#include "isae_monitor/classifier.h"
#include "isae_monitor/departments.h"

#include <stdio.h>
#include <string.h>

void classification_result_init(classification_result_t* result) {
    if (!result) return;
    result->category[0] = '\0';
    result->source[0] = '\0';
    result->detail[0] = '\0';
    result->is_ai = false;
}

void classifier_init(classifier_t* classifier, const settings_t* settings) {
    if (!classifier) return;
    memset(classifier, 0, sizeof(*classifier));
    classifier->settings = settings;
    /* Derive the http_client_config_t from settings. */
    http_client_config_default(&classifier->http_cfg);
    if (settings) {
        classifier->http_cfg.timeout_seconds = settings->request_timeout;
        classifier->http_cfg.max_retries = settings->max_retries;
    }
}

void classifier_cleanup(classifier_t* classifier) {
    if (!classifier) return;
    /* Nothing to free; everything is inline. */
    memset(classifier, 0, sizeof(*classifier));
}

static void classifier_note(classifier_t* classifier, const char* message) {
    if (!classifier || !message) return;
    for (int i = 0; i < classifier->notes_count; i++) {
        if (strcmp(classifier->notes[i], message) == 0) return;  /* dedup */
    }
    if (classifier->notes_count >= (int)(sizeof(classifier->notes) / sizeof(classifier->notes[0]))) return;
    strncpy(classifier->notes[classifier->notes_count], message, sizeof(classifier->notes[0]) - 1);
    classifier->notes[classifier->notes_count][sizeof(classifier->notes[0]) - 1] = '\0';
    classifier->notes_count++;
}

int classifier_notes_count(const classifier_t* classifier) {
    return classifier ? classifier->notes_count : 0;
}

const char* classifier_note_at(const classifier_t* classifier, int index) {
    if (!classifier || index < 0 || index >= classifier->notes_count) return NULL;
    /* notes_count is bounded by the array size in classifier_note(), so a
     * non-negative index below notes_count is always in range here. */
    return classifier->notes[index];
}

static bool is_gemini_dead_status(long status) {
    switch (status) {
        case 400: case 401: case 403: case 404: case 429:
            return true;
        default:
            return false;
    }
}

static bool is_openrouter_dead_status(long status) {
    switch (status) {
        case 401: case 402: case 403:
            return true;
        default:
            return false;
    }
}

isae_error_t classifier_classify(classifier_t* classifier,
                                 announcement_t* ann,
                                 classification_result_t* result) {
    if (!classifier || !ann || !result) return ISAE_ERR_INVALID_PARAM;
    classification_result_init(result);

    const settings_t* s = classifier->settings;
    if (!s) return ISAE_ERR_CONFIG;

    char* prompt = providers_build_prompt(ann);
    if (!prompt) return ISAE_ERR_MEMORY;

    char last_error[256] = "";

    /* --- Phase 1: Gemini keys (in order, skip dead ones) --- */
    for (int i = 0; i < s->gemini_keys_count; i++) {
        if (classifier->gemini_dead[i]) continue;

        ai_response_t ai;
        ai_response_init(&ai);
        isae_error_t err = providers_call_gemini(prompt, s->gemini_keys[i],
                                                  s->gemini_model,
                                                  &classifier->http_cfg, &ai);
        if (err != ISAE_OK) {
            snprintf(last_error, sizeof(last_error) - 1, "gemini: %s", ai.error);
            if (is_gemini_dead_status(ai.status)) {
                classifier->gemini_dead[i] = true;
                char note[300];
                snprintf(note, sizeof(note), "gemini key #%d disabled for this run (%s)", i + 1, ai.error);
                classifier_note(classifier, note);
            }
            ai_response_cleanup(&ai);
            continue;
        }

        const char* cat = providers_extract_category(ai.raw_text);
        if (cat) {
            strncpy(result->category, cat, sizeof(result->category) - 1);
            result->category[sizeof(result->category) - 1] = '\0';
            strcpy(result->source, "gemini");
            result->is_ai = true;
            ai_response_cleanup(&ai);
            free(prompt);
            return ISAE_OK;
        }
        snprintf(last_error, sizeof(last_error) - 1, "gemini: unparseable answer (%.80s)", ai.raw_text ? ai.raw_text : "");
        ai_response_cleanup(&ai);
    }

    /* --- Phase 2: OpenRouter (if configured and not dead) --- */
    if (s->openrouter_api_key[0] && !classifier->openrouter_dead) {
        ai_response_t ai;
        ai_response_init(&ai);
        isae_error_t err = providers_call_openrouter(prompt, s->openrouter_api_key,
                                                     s->openrouter_model,
                                                     &classifier->http_cfg, &ai);
        if (err != ISAE_OK) {
            snprintf(last_error, sizeof(last_error) - 1, "openrouter: %s", ai.error);
            if (is_openrouter_dead_status(ai.status)) {
                classifier->openrouter_dead = true;
                char note[300];
                snprintf(note, sizeof(note), "openrouter key disabled for this run (%s)", ai.error);
                classifier_note(classifier, note);
            }
            ai_response_cleanup(&ai);
        } else {
            const char* cat = providers_extract_category(ai.raw_text);
            if (cat) {
                strncpy(result->category, cat, sizeof(result->category) - 1);
                result->category[sizeof(result->category) - 1] = '\0';
                strcpy(result->source, "openrouter");
                result->is_ai = true;
                ai_response_cleanup(&ai);
                free(prompt);
                return ISAE_OK;
            }
            snprintf(last_error, sizeof(last_error) - 1, "openrouter: unparseable answer (%.80s)", ai.raw_text ? ai.raw_text : "");
            ai_response_cleanup(&ai);
        }
    }

    /* --- Phase 3: keyword fallback (always succeeds) --- */
    keyword_result_t kw;
    keyword_result_init(&kw);
    isae_error_t err = keywords_classify(ann, &kw);
    free(prompt);
    if (err != ISAE_OK) return err;

    strncpy(result->category, kw.category, sizeof(result->category) - 1);
    result->category[sizeof(result->category) - 1] = '\0';
    strcpy(result->source, "keywords");
    strncpy(result->detail, last_error, sizeof(result->detail) - 1);
    result->detail[sizeof(result->detail) - 1] = '\0';
    return ISAE_OK;
}
