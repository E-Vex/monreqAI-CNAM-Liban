/**
 * @file keywords.c
 * @brief Keyword-based fallback classifier implementation
 */

#include "isae_monitor/keywords.h"
#include "isae_monitor/departments.h"
#include <string.h>

/* General markers - signal institute-wide notices */
const char* GENERAL_MARKERS[] = {
    "tous les auditeurs", "tous les etudiants", "a tous", "all students",
    "inscription", "registration", "frais", "fees", "rentree", "calendrier",
    "vacance", "conge", "holiday", "ferme", "closure", "transport",
    "horaire", "schedule", "deadline", "delai", "attestation", "diplome",
    "الى جميع الطلاب", "جميع الطلاب", "كافة المراكز", "التسجيل", "الرسوم",
    "عطلة", "المواعيد", "اعلان عام"
};
const size_t GENERAL_MARKERS_COUNT = sizeof(GENERAL_MARKERS) / sizeof(GENERAL_MARKERS[0]);

/* Other markers - signal non-student notices */
const char* OTHER_MARKERS[] = {
    "offre d'emploi", "job offer", "recrute", "recruitment", "vacancy",
    "appel d'offres", "tender", "فرص عمل", "وظيفة", "مناقصة"
};
const size_t OTHER_MARKERS_COUNT = sizeof(OTHER_MARKERS) / sizeof(OTHER_MARKERS[0]);

static bool contains_keyword(const char* text, const char* keyword) {
    if (!text || !keyword) return false;
    return strstr(text, keyword) != NULL;
}

isae_error_t score_departments(const announcement_t* ann,
                                dept_score_t* scores,
                                size_t* score_count,
                                size_t max_scores) {
    if (!ann || !scores || !score_count) return ISAE_ERR_INVALID_ARG;
    
    departments_init();
    *score_count = 0;
    
    /* Get normalized search text */
    char search_text[MAX_ANNOUNCEMENT_TITLE + MAX_ANNOUNCEMENT_SUMMARY + 2];
    isae_error_t err = announcement_get_search_text(ann, search_text, sizeof(search_text));
    if (err != ISAE_OK) return err;
    
    /* Score each department */
    for (size_t i = 0; i < departments_get_count() && *score_count < max_scores; i++) {
        const department_t* dept = departments_get_by_index(i);
        if (!dept) continue;
        
        int score = 0;
        
        /* Check each keyword */
        for (size_t k = 0; k < dept->keyword_count; k++) {
            const char* keyword = dept->keywords[k];
            
            /* Normalize keyword */
            char norm_keyword[MAX_KEYWORD];
            err = normalize_text(keyword, norm_keyword, sizeof(norm_keyword));
            if (err != ISAE_OK) continue;
            
            if (!norm_keyword[0]) continue;
            
            /* Title matches are weighted higher */
            if (strstr(search_text, norm_keyword)) {
                /* Multi-word keywords are more specific */
                int weight = (strchr(norm_keyword, ' ')) ? TITLE_WEIGHT * 2 : TITLE_WEIGHT;
                score += weight;
            }
        }
        
        if (score > 0) {
            strncpy(scores[*score_count].key, dept->key, MAX_DEPARTMENT_KEY - 1);
            scores[*score_count].score = score;
            (*score_count)++;
        }
    }
    
    return ISAE_OK;
}

isae_error_t keywords_classify(const announcement_t* ann,
                                char* category, size_t category_size) {
    if (!ann || !category || category_size == 0) return ISAE_ERR_INVALID_ARG;
    
    /* Get search text */
    char search_text[MAX_ANNOUNCEMENT_TITLE + MAX_ANNOUNCEMENT_SUMMARY + 2];
    isae_error_t err = announcement_get_search_text(ann, search_text, sizeof(search_text));
    if (err != ISAE_OK) return err;
    
    /* Check for OTHER markers first */
    for (size_t i = 0; i < OTHER_MARKERS_COUNT; i++) {
        char norm_marker[MAX_KEYWORD];
        err = normalize_text(OTHER_MARKERS[i], norm_marker, sizeof(norm_marker));
        if (err != ISAE_OK) continue;
        
        if (strstr(search_text, norm_marker)) {
            strncpy(category, DEPT_CATEGORY_OTHER, category_size - 1);
            category[category_size - 1] = '\0';
            return ISAE_OK;
        }
    }
    
    /* Score departments */
    dept_score_t scores[MAX_DEPARTMENTS];
    size_t score_count = 0;
    err = score_departments(ann, scores, &score_count, MAX_DEPARTMENTS);
    if (err != ISAE_OK) return err;
    
    if (score_count > 0) {
        /* Find best score */
        int best_score = scores[0].score;
        for (size_t i = 1; i < score_count; i++) {
            if (scores[i].score > best_score) {
                best_score = scores[i].score;
            }
        }
        
        /* Count winners with best score */
        size_t winner_count = 0;
        for (size_t i = 0; i < score_count; i++) {
            if (scores[i].score == best_score) {
                winner_count++;
            }
        }
        
        /* Only route if there's a single clear winner */
        if (winner_count == 1) {
            for (size_t i = 0; i < score_count; i++) {
                if (scores[i].score == best_score) {
                    strncpy(category, scores[i].key, category_size - 1);
                    category[category_size - 1] = '\0';
                    return ISAE_OK;
                }
            }
        }
    }
    
    /* Check for GENERAL markers */
    for (size_t i = 0; i < GENERAL_MARKERS_COUNT; i++) {
        char norm_marker[MAX_KEYWORD];
        err = normalize_text(GENERAL_MARKERS[i], norm_marker, sizeof(norm_marker));
        if (err != ISAE_OK) continue;
        
        if (strstr(search_text, norm_marker)) {
            strncpy(category, DEPT_CATEGORY_GENERAL, category_size - 1);
            category[category_size - 1] = '\0';
            return ISAE_OK;
        }
    }
    
    /* Conservative default: general rather than other */
    strncpy(category, DEPT_CATEGORY_GENERAL, category_size - 1);
    category[category_size - 1] = '\0';
    
    return ISAE_OK;
}
