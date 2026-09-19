/**
 * ISAE Monitor - Classifier Implementation
 * 
 * Maps Python: classifier.py -> C: classifier.c
 * 
 * Orchestrates classification: tries AI first, falls back to keywords.
 */

#include "isae_monitor/classifier.h"
#include "isae_monitor/providers.h"

void classification_result_init(classification_result_t* result) {
    if (!result) return;
    
    result->category[0] = '\0';
    result->method[0] = '\0';
    result->confidence = 0;
    result->success = false;
}

void classification_result_cleanup(classification_result_t* result) {
    /* Nothing to free - fixed size arrays */
    (void)result;
}

void classifier_init(classifier_t* classifier, settings_t* settings) {
    if (!classifier) return;
    
    classifier->settings = settings;
}

void classifier_cleanup(classifier_t* classifier) {
    (void)classifier;
}

isae_error_t classifier_classify(classifier_t* classifier,
                                 const announcement_t* ann,
                                 classification_result_t* result) {
    if (!classifier || !ann || !result) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    classification_result_init(result);
    
    if (!classifier->settings) {
        /* No settings available, use keyword fallback */
        goto keyword_fallback;
    }
    
    /* Try AI classification based on configured provider */
    provider_type_t provider = classifier->settings->provider;
    isae_error_t ai_err = ISAE_ERR_CLASSIFICATION;
    
    if (provider == PROVIDER_GEMINI && classifier->settings->gemini_api_key[0]) {
        ai_response_t ai_resp;
        ai_response_init(&ai_resp);
        
        ai_err = gemini_classify(ann, 
                                  classifier->settings->gemini_api_key,
                                  &ai_resp,
                                  classifier->settings->http_timeout);
        
        if (ai_err == ISAE_OK && ai_resp.success) {
            strncpy(result->category, ai_resp.category, MAX_CATEGORY_NAME - 1);
            result->category[MAX_CATEGORY_NAME - 1] = '\0';
            result->confidence = ai_resp.confidence;
            strncpy(result->method, "gemini", sizeof(result->method) - 1);
            result->success = true;
            
            ai_response_cleanup(&ai_resp);
            return ISAE_OK;
        }
        
        ai_response_cleanup(&ai_resp);
    } else if (provider == PROVIDER_OPENROUTER && classifier->settings->openrouter_api_key[0]) {
        ai_response_t ai_resp;
        ai_response_init(&ai_resp);
        
        ai_err = openrouter_classify(ann,
                                      classifier->settings->openrouter_api_key,
                                      &ai_resp,
                                      classifier->settings->http_timeout);
        
        if (ai_err == ISAE_OK && ai_resp.success) {
            strncpy(result->category, ai_resp.category, MAX_CATEGORY_NAME - 1);
            result->category[MAX_CATEGORY_NAME - 1] = '\0';
            result->confidence = ai_resp.confidence;
            strncpy(result->method, "openrouter", sizeof(result->method) - 1);
            result->success = true;
            
            ai_response_cleanup(&ai_resp);
            return ISAE_OK;
        }
        
        ai_response_cleanup(&ai_resp);
    }
    
    /* AI failed or not configured, fall back to keywords */
keyword_fallback:
    keyword_result_t kw_result;
    keyword_result_init(&kw_result);
    
    isae_error_t kw_err = keywords_classify(ann, &kw_result);
    
    if (kw_err == ISAE_OK && kw_result.matched) {
        strncpy(result->category, kw_result.category, MAX_CATEGORY_NAME - 1);
        result->category[MAX_CATEGORY_NAME - 1] = '\0';
        result->confidence = kw_result.score > 0 ? kw_result.score * 20 : 30;
        strncpy(result->method, "keyword", sizeof(result->method) - 1);
        result->success = true;
    } else {
        /* Even without match, return GENERAL category */
        strncpy(result->category, kw_result.category, MAX_CATEGORY_NAME - 1);
        result->category[MAX_CATEGORY_NAME - 1] = '\0';
        result->confidence = 10;
        strncpy(result->method, "keyword", sizeof(result->method) - 1);
        result->success = true;  /* Still consider it a success with default */
    }
    
    keyword_result_cleanup(&kw_result);
    
    return ISAE_OK;
}
