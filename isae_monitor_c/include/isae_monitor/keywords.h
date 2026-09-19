/**
 * @file keywords.h
 * @brief Keyword-based fallback classifier
 */

#ifndef ISAE_MONITOR_KEYWORDS_H
#define ISAE_MONITOR_KEYWORDS_H

#include "common.h"
#include "models.h"

/* General markers - signal institute-wide notices */
extern const char* GENERAL_MARKERS[];
extern const size_t GENERAL_MARKERS_COUNT;

/* Other markers - signal non-student notices */
extern const char* OTHER_MARKERS[];
extern const size_t OTHER_MARKERS_COUNT;

/* Weight constants for scoring */
#define TITLE_WEIGHT 3
#define SUMMARY_WEIGHT 1

/* Score result for a single department */
typedef struct {
    char key[MAX_DEPARTMENT_KEY];
    int score;
} dept_score_t;

/* Score all departments for an announcement */
isae_error_t score_departments(const announcement_t* ann,
                                dept_score_t* scores,
                                size_t* score_count,
                                size_t max_scores);

/* Classify announcement using keyword matching */
isae_error_t keywords_classify(const announcement_t* ann,
                                char* category, size_t category_size);

#endif /* ISAE_MONITOR_KEYWORDS_H */
