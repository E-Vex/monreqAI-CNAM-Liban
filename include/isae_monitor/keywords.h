#ifndef ISAE_MONITOR_KEYWORDS_H
#define ISAE_MONITOR_KEYWORDS_H

#include "common.h"
#include "models.h"

/* Keyword classification result.
 *
 * category is always one of the 9 department keys, "general" or "other".
 * score is the weighted score (0 if no match, used for diagnostics only).
 * matched is true iff a non-default category was chosen. */
typedef struct {
    char category[MAX_CATEGORY_NAME];
    int score;
    bool matched;
} keyword_result_t;

void keyword_result_init(keyword_result_t* result);

/* Classify an announcement using the keyword algorithm.
 *
 * Mirrors Python isae_monitor.classify.keywords.classify():
 *   1. If any OTHER_MARKER (normalized) is a substring of the
 *      normalized title+summary -> return "other".
 *   2. For each department, score its keywords:
 *        TITLE_WEIGHT   = 3  (multi-word keyword: x2)
 *        SUMMARY_WEIGHT = 1  (multi-word keyword: x2)
 *      The Python code uses 'elif' between title and summary, so a
 *      keyword that appears in BOTH is counted only once (in the title).
 *   3. If exactly one department has the max score -> that's the winner.
 *      Ties -> no decision -> fall through to step 4.
 *   4. If any GENERAL_MARKER is a substring -> return "general".
 *   5. Else -> return "general" (conservative default).
 */
isae_error_t keywords_classify(announcement_t* ann, keyword_result_t* result);

#endif /* ISAE_MONITOR_KEYWORDS_H */
