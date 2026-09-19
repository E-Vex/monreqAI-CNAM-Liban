#ifndef ISAE_MONITOR_KEYWORDS_H
#define ISAE_MONITOR_KEYWORDS_H

#include "common.h"
#include "models.h"

/* Keyword matching result */
typedef struct {
    char category[MAX_CATEGORY_NAME];
    int score;
    bool matched;
} keyword_result_t;

/* Initialize keyword result */
void keyword_result_init(keyword_result_t* result);

/* Free keyword result resources */
void keyword_result_cleanup(keyword_result_t* result);

/* Classify announcement using keyword matching */
isae_error_t keywords_classify(const announcement_t* ann, 
                               keyword_result_t* result);

#endif /* ISAE_MONITOR_KEYWORDS_H */
