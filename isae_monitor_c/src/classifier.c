/**
 * @file classifier.c
 * @brief Classification engine - orchestrates AI and keyword-based classification
 * 
 * Python equivalent: classifier.py with AI provider fallback logic
 */

#include "isae_monitor/classifier.h"
#include "isae_monitor/providers.h"
#include "isae_monitor/keywords.h"
#include "isae_monitor/departments.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Initialize classifier */
isae_error_t classifier_init(classifier_t* classifier, const settings_t* settings) {
    if (!classifier || !settings) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    memset(classifier, 0, sizeof(classifier_t));
    classifier->settings = settings;
    classifier->dead_key_count = 0;
    classifier->note_count = 0;
    
    return ISAE_OK;
}

/* Free classifier resources */
void classifier_cleanup(classifier_t* classifier) {
    if (!classifier) {
        return;
    }
    /* Nothing dynamic to free - all fixed-size arrays */
    memset(classifier, 0, sizeof(classifier_t));
}

/* Check if a key is marked as dead */
bool classifier_is_key_dead(const classifier_t* classifier, const char* key) {
    if (!classifier || !key) {
        return false;
    }
    
    for (size_t i = 0; i < classifier->dead_key_count; i++) {
        if (strncmp(classifier->dead_keys[i], key, MAX_API_KEY - 1) == 0) {
            return true;
        }
    }
    return false;
}

/* Mark a key as dead for this run */
isae_error_t classifier_mark_key_dead(classifier_t* classifier, const char* key) {
    if (!classifier || !key) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    if (classifier->dead_key_count >= MAX_GEMINI_KEYS) {
        return ISAE_ERR_BUFFER_FULL;
    }
    
    /* Check if already marked */
    if (classifier_is_key_dead(classifier, key)) {
        return ISAE_OK;
    }
    
    strncpy(classifier->dead_keys[classifier->dead_key_count], key, MAX_API_KEY - 1);
    classifier->dead_keys[classifier->dead_key_count][MAX_API_KEY - 1] = '\0';
    classifier->dead_key_count++;
    
    return ISAE_OK;
}

/* Add a note/warning */
isae_error_t classifier_add_note(classifier_t* classifier, const char* note) {
    if (!classifier || !note) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    if (classifier->note_count >= 10) {
        return ISAE_ERR_BUFFER_FULL;
    }
    
    strncpy(classifier->notes[classifier->note_count], note, 255);
    classifier->notes[classifier->note_count][255] = '\0';
    classifier->note_count++;
    
    return ISAE_OK;
}

/* Get all notes */
const char* const* classifier_get_notes(const classifier_t* classifier, size_t* count) {
    if (!classifier || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    *count = classifier->note_count;
    return (const char* const*)classifier->notes;
}

/* Build classification prompt for an announcement */
isae_error_t classifier_build_prompt(const classifier_t* classifier,
                                      const announcement_t* ann,
                                      char* prompt, size_t prompt_size) {
    if (!classifier || !ann || !prompt || prompt_size < 512) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    int written = snprintf(prompt, prompt_size,
        "You are an assistant that classifies academic announcements into departments.\n"
        "Choose ONE of these exact department names:\n"
        "- Informatique\n"
        "- Mathematiques\n"
        "- Physique\n"
        "- Chimie\n"
        "- Biologie\n"
        "- Sciences de l'Ingenieur\n"
        "- Sciences Economiques et de Gestion\n"
        "- Langues Etrangeres\n"
        "- Sport\n"
        "- Autre\n"
        "\n"
        "Announcement:\n"
        "Title: %s\n"
        "Summary: %s\n"
        "\n"
        "Department:",
        ann->title,
        ann->summary);
    
    if (written < 0 || (size_t)written >= prompt_size) {
        return ISAE_ERR_BUFFER_FULL;
    }
    
    return ISAE_OK;
}

/* Classify an announcement */
isae_error_t classifier_classify(classifier_t* classifier,
                                  const announcement_t* ann,
                                  classification_result_t* result) {
    if (!classifier || !ann || !result) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    isae_error_t ret = ISAE_ERR_CLASSIFICATION;
    char ai_result[MAX_DEPARTMENT_NAME] = {0};
    
    /* Initialize result */
    memset(result, 0, sizeof(classification_result_t));
    result->is_ai = false;
    strncpy(result->source, "none", sizeof(result->source) - 1);
    
    /* Validate input */
    if (strlen(ann->title) < 3) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    const settings_t* settings = classifier->settings;
    
    /* Try Gemini API first if configured */
    for (size_t i = 0; i < settings->gemini_key_count; i++) {
        const char* key = settings->gemini_keys[i];
        
        /* Skip dead keys */
        if (classifier_is_key_dead(classifier, key)) {
            continue;
        }
        
        fprintf(stderr, "[DEBUG] Trying Gemini API key %zu...\n", i);
        
        ret = ai_classify_gemini(key, ann->title, ann->summary, 
                                 ai_result, sizeof(ai_result));
        
        if (ret == ISAE_OK && strlen(ai_result) > 0) {
            fprintf(stderr, "[DEBUG] Gemini returned: %s\n", ai_result);
            
            /* Resolve label to canonical key */
            const char* resolved = departments_resolve(ai_result);
            if (resolved != NULL) {
                strncpy(result->category, resolved, MAX_DEPARTMENT_KEY - 1);
                strncpy(result->source, "gemini", sizeof(result->source) - 1);
                result->is_ai = true;
                return ISAE_OK;
            }
            
            /* AI returned something but not recognized - try next key */
            fprintf(stderr, "[DEBUG] Gemini result not recognized, trying next key...\n");
        } else {
            fprintf(stderr, "[DEBUG] Gemini API failed: %d\n", ret);
            /* Mark key as dead for this run */
            classifier_mark_key_dead(classifier, key);
        }
    }
    
    /* Try OpenRouter if Gemini failed */
    if (settings->has_openrouter_key && strlen(settings->openrouter_key) > 0) {
        fprintf(stderr, "[DEBUG] Trying OpenRouter API...\n");
        
        ret = ai_classify_openrouter(settings->openrouter_key, ann->title, ann->summary,
                                     ai_result, sizeof(ai_result));
        
        if (ret == ISAE_OK && strlen(ai_result) > 0) {
            fprintf(stderr, "[DEBUG] OpenRouter returned: %s\n", ai_result);
            
            const char* resolved = departments_resolve(ai_result);
            if (resolved != NULL) {
                strncpy(result->category, resolved, MAX_DEPARTMENT_KEY - 1);
                strncpy(result->source, "openrouter", sizeof(result->source) - 1);
                result->is_ai = true;
                return ISAE_OK;
            }
            fprintf(stderr, "[DEBUG] OpenRouter result not recognized\n");
        } else {
            fprintf(stderr, "[DEBUG] OpenRouter API failed: %d\n", ret);
        }
    }
    
    /* Fallback to keyword-based classification */
    fprintf(stderr, "[DEBUG] Falling back to keyword-based classification...\n");
    
    ret = keywords_classify(ann, result->category, sizeof(result->category));
    
    if (ret == ISAE_OK && strlen(result->category) > 0) {
        strncpy(result->source, "keywords", sizeof(result->source) - 1);
        result->is_ai = false;
        return ISAE_OK;
    }
    
    /* All methods failed */
    fprintf(stderr, "[DEBUG] All classification methods failed\n");
    result->category[0] = '\0';
    strncpy(result->source, "failed", sizeof(result->source) - 1);
    return ISAE_ERR_CLASSIFICATION;
}
