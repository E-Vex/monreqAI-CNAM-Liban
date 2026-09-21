/**
 * ISAE Monitor - Keyword Classification
 *
 * Faithful C port of Python isae_monitor/classify/keywords.py.
 *
 * Algorithm:
 *   1. If any OTHER_MARKER (normalized) is a substring of the normalized
 *      title+summary, return "other".
 *   2. For each department, score its keywords against title and summary
 *      independently:
 *        TITLE_WEIGHT   = 3, multiplied by 2 if the keyword contains a space
 *        SUMMARY_WEIGHT = 1, multiplied by 2 if the keyword contains a space
 *        'elif' between title and summary: a keyword that appears in BOTH
 *        is counted only once (in the title).
 *   3. If exactly one department has the maximum score -> that's the winner.
 *      Ties -> no decision -> fall through.
 *   4. If any GENERAL_MARKER matches -> return "general".
 *   5. Else -> return "general" (conservative default).
 */

#include "isae_monitor/keywords.h"
#include "isae_monitor/departments.h"

#include <string.h>

#define TITLE_WEIGHT   3
#define SUMMARY_WEIGHT 1

static const char* const GENERAL_MARKERS[] = {
    "tous les auditeurs", "tous les etudiants", "a tous", "all students",
    "inscription", "registration", "frais", "fees", "rentree", "calendrier",
    "vacance", "conge", "holiday", "ferme", "closure", "transport",
    "horaire", "schedule", "deadline", "delai", "attestation", "diplome",
    "\xd8\xa7\xd9\x84\xd9\x89 \xd8\xac\xd9\x85\xd9\x8a\xd8\xb9 \xd8\xa7\xd9\x84\xd8\xb7\xd9\x84\xd8\xa7\xd8\xa8",  /* الى جميع الطلاب */
    "\xd8\xac\xd9\x85\xd9\x8a\xd8\xb9 \xd8\xa7\xd9\x84\xd8\xb7\xd9\x84\xd8\xa7\xd8\xa8",  /* جميع الطلاب */
    "\xd9\x83\xd8\xa7\xd9\x81\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x85\xd8\xb1\xd8\xa7\xd9\x83\xd8\xb2",  /* كافة المراكز */
    "\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb3\xd8\xac\xd9\x8a\xd9\x84",  /* التسجيل */
    "\xd8\xa7\xd9\x84\xd8\xb1\xd8\xb3\xd9\x88\xd9\x85",  /* الرسوم */
    "\xd8\xb9\xd8\xb7\xd9\x84\xd8\xa9",  /* عطلة */
    "\xd8\xa7\xd9\x84\xd9\x85\xd9\x88\xd8\xa7\xd8\xb9\xd9\x8a\xd8\xaf",  /* المواعيد */
    "\xd8\xa7\xd8\xb9\xd9\x84\xd8\xa7\xd9\x86 \xd8\xb9\xd8\xa7\xd9\x85",  /* اعلان عام */
    NULL,
};

static const char* const OTHER_MARKERS[] = {
    "offre d'emploi", "job offer", "recrute", "recruitment", "vacancy",
    "appel d'offres", "tender",
    "\xd9\x81\xd8\xb1\xd8\xb5 \xd8\xb9\xd9\x85\xd9\x84",  /* فرص عمل */
    "\xd9\x88\xd8\xb8\xd9\x8a\xd9\x81\xd8\xa9",  /* وظيفة */
    "\xd9\x85\xd9\x86\xd8\xa7\xd9\x82\xd8\xb5\xd8\xa9",  /* مناقصة */
    NULL,
};

void keyword_result_init(keyword_result_t* result) {
    if (!result) return;
    result->category[0] = '\0';
    result->score = 0;
    result->matched = false;
}

static bool any_marker_in(const char* const* markers, const char* text) {
    for (size_t i = 0; markers[i]; i++) {
        char folded[MAX_TEXT_NORMALIZED_LEN];
        if (normalize_text(markers[i], folded, sizeof(folded)) != ISAE_OK) continue;
        if (!folded[0]) continue;
        if (strstr(text, folded) != NULL) return true;
    }
    return false;
}

/* Returns the score for a single department against the given title/summary
 * (both already normalized). */
static int score_one_department(const department_t* dept,
                                const char* title, const char* summary) {
    int score = 0;
    for (const char* const* kp = dept->keywords; *kp; kp++) {
        char folded[MAX_TEXT_NORMALIZED_LEN];
        if (normalize_text(*kp, folded, sizeof(folded)) != ISAE_OK) continue;
        if (!folded[0]) continue;

        bool multi_word = (strchr(folded, ' ') != NULL);
        int weight = multi_word ? 2 : 1;

        if (strstr(title, folded) != NULL) {
            score += TITLE_WEIGHT * weight;
        } else if (strstr(summary, folded) != NULL) {
            score += SUMMARY_WEIGHT * weight;
        }
        /* else: not present, contributes nothing */
    }
    return score;
}

isae_error_t keywords_classify(announcement_t* ann, keyword_result_t* result) {
    if (!ann || !result) return ISAE_ERR_INVALID_PARAM;
    keyword_result_init(result);

    /* Build the combined search text. announcement_search_text() handles
     * caching (compute once, reuse on subsequent calls). */
    const char* text = announcement_search_text(ann);
    if (!text) {
        /* Fall back to GENERAL on OOM. */
        strncpy(result->category, CATEGORY_GENERAL, sizeof(result->category) - 1);
        result->category[sizeof(result->category) - 1] = '\0';
        return ISAE_OK;
    }

    /* Step 1: OTHER_MARKERS first. */
    if (any_marker_in(OTHER_MARKERS, text)) {
        strncpy(result->category, CATEGORY_OTHER, sizeof(result->category) - 1);
        result->category[sizeof(result->category) - 1] = '\0';
        result->matched = true;
        return ISAE_OK;
    }

    /* Step 2-3: score every department, find unique max. */
    char norm_title[MAX_TEXT_NORMALIZED_LEN];
    char norm_summary[MAX_TEXT_NORMALIZED_LEN];
    norm_title[0] = '\0';
    norm_summary[0] = '\0';
    if (ann->title)   normalize_text(ann->title,   norm_title,   sizeof(norm_title));
    if (ann->summary) normalize_text(ann->summary, norm_summary, sizeof(norm_summary));

    int best_score = 0;
    const char* best_key = NULL;
    int num_at_best = 0;

    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        int s = score_one_department(d, norm_title, norm_summary);
        if (s == 0) continue;
        if (s > best_score) {
            best_score = s;
            best_key = d->key;
            num_at_best = 1;
        } else if (s == best_score) {
            num_at_best++;
        }
    }

    if (best_key && num_at_best == 1) {
        strncpy(result->category, best_key, sizeof(result->category) - 1);
        result->category[sizeof(result->category) - 1] = '\0';
        result->score = best_score;
        result->matched = true;
        return ISAE_OK;
    }

    /* Step 4-5: GENERAL (the marker check is functionally a no-op since
     * both branches return GENERAL, but it's preserved for parity). */
    if (any_marker_in(GENERAL_MARKERS, text)) {
        strncpy(result->category, CATEGORY_GENERAL, sizeof(result->category) - 1);
        result->category[sizeof(result->category) - 1] = '\0';
        result->matched = true;
        return ISAE_OK;
    }

    strncpy(result->category, CATEGORY_GENERAL, sizeof(result->category) - 1);
    result->category[sizeof(result->category) - 1] = '\0';
    return ISAE_OK;
}
