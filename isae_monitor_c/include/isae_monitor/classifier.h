#ifndef ISAE_MONITOR_CLASSIFIER_H
#define ISAE_MONITOR_CLASSIFIER_H

#include "common.h"
#include "models.h"
#include "config.h"
#include "keywords.h"

/* Classification result */
typedef struct {
    char category[MAX_CATEGORY_NAME];
    char method[32];  /* "gemini", "openrouter", "keyword" */
    int confidence;   /* 0-100 */
    bool success;
} classification_result_t;

/* Initialize classification result */
void classification_result_init(classification_result_t* result);

/* Free classification result resources */
void classification_result_cleanup(classification_result_t* result);

/* Classifier context */
typedef struct {
    settings_t* settings;
} classifier_t;

/* Initialize classifier */
void classifier_init(classifier_t* classifier, settings_t* settings);

/* Cleanup classifier */
void classifier_cleanup(classifier_t* classifier);

/* Classify an announcement */
isae_error_t classifier_classify(classifier_t* classifier,
                                 const announcement_t* ann,
                                 classification_result_t* result);

#endif /* ISAE_MONITOR_CLASSIFIER_H */
