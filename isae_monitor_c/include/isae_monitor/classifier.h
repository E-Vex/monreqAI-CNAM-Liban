/**
 * @file classifier.h
 * @brief Classification engine - orchestrates AI providers and keyword fallback
 */

#ifndef ISAE_MONITOR_CLASSIFIER_H
#define ISAE_MONITOR_CLASSIFIER_H

#include "common.h"
#include "config.h"
#include "models.h"

/* Classification result */
typedef struct {
    char category[MAX_DEPARTMENT_KEY];
    char source[16];          /* "gemini", "openrouter", or "keywords" */
    char detail[256];         /* Error details if fallback was used */
    bool is_ai;               /* True if classified by AI provider */
} classification_result_t;

/* Classifier state */
typedef struct {
    const settings_t* settings;
    char dead_keys[MAX_GEMINI_KEYS][MAX_API_KEY];  /* Keys disabled for this run */
    size_t dead_key_count;
    char notes[10][256];      /* Notes/warnings to report */
    size_t note_count;
} classifier_t;

/* Initialize classifier */
isae_error_t classifier_init(classifier_t* classifier, const settings_t* settings);

/* Free classifier resources */
void classifier_cleanup(classifier_t* classifier);

/* Build classification prompt for an announcement */
isae_error_t classifier_build_prompt(const classifier_t* classifier,
                                      const announcement_t* ann,
                                      char* prompt, size_t prompt_size);

/* Classify an announcement */
isae_error_t classifier_classify(classifier_t* classifier,
                                  const announcement_t* ann,
                                  classification_result_t* result);

/* Check if a key is marked as dead */
bool classifier_is_key_dead(const classifier_t* classifier, const char* key);

/* Mark a key as dead for this run */
isae_error_t classifier_mark_key_dead(classifier_t* classifier, const char* key);

/* Add a note/warning */
isae_error_t classifier_add_note(classifier_t* classifier, const char* note);

/* Get all notes */
const char* const* classifier_get_notes(const classifier_t* classifier, 
                                         size_t* count);

#endif /* ISAE_MONITOR_CLASSIFIER_H */
