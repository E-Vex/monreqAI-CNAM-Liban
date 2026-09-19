/**
 * ISAE Monitor - Keyword Classifier Implementation
 * 
 * Maps Python: keywords.py -> C: keywords.c
 * 
 * Fallback classification using keyword matching when AI is unavailable.
 */

#include "isae_monitor/keywords.h"
#include <ctype.h>

/* Keyword categories mapping */
typedef struct {
    const char* category;
    const char** keywords;
    size_t num_keywords;
} keyword_category_t;

/* Keywords for each category */
static const char* keywords_jobs[] = {
    "emploi", "job", "recrutement", "embauche", "poste", "carriere",
    "stage", "internship", "offre", "candidate", NULL
};

static const char* keywords_events[] = {
    "conference", "seminar", "colloque", "evenement", "event",
    "reunion", "meeting", "presentation", "soutenance", "defense",
    "journee", "atelier", "workshop", NULL
};

static const char* keywords_courses[] = {
    "cours", "course", "formation", "enseignement", "classe",
    "examen", "evaluation", "tp", "td", "projet", "student", NULL
};

static const char* keywords_research[] = {
    "recherche", "research", "publication", "article", "these",
    "doctorat", "phd", "laboratoire", "lab", "scientifique", NULL
};

static const char* keywords_admin[] = {
    "administration", "inscription", "registration", "secretariat",
    "bureau", "office", "procedure", "dossier", NULL
};

static keyword_category_t g_categories[] = {
    {"JOBS", keywords_jobs, sizeof(keywords_jobs) / sizeof(keywords_jobs[0]) - 1},
    {"EVENTS", keywords_events, sizeof(keywords_events) / sizeof(keywords_events[0]) - 1},
    {"COURSES", keywords_courses, sizeof(keywords_courses) / sizeof(keywords_courses[0]) - 1},
    {"RESEARCH", keywords_research, sizeof(keywords_research) / sizeof(keywords_research[0]) - 1},
    {"ADMIN", keywords_admin, sizeof(keywords_admin) / sizeof(keywords_admin[0]) - 1},
};

static size_t g_num_categories = sizeof(g_categories) / sizeof(g_categories[0]);

void keyword_result_init(keyword_result_t* result) {
    if (!result) return;
    
    result->category[0] = '\0';
    result->score = 0;
    result->matched = false;
}

void keyword_result_cleanup(keyword_result_t* result) {
    /* Nothing to free - fixed size arrays */
    (void)result;
}

static int count_keyword_matches(const char* text, const char** keywords, size_t num_keywords) {
    if (!text || !keywords) {
        return 0;
    }
    
    int count = 0;
    const char* p = text;
    
    while (*p) {
        for (size_t k = 0; k < num_keywords; k++) {
            const char* kw = keywords[k];
            size_t kw_len = strlen(kw);
            
            /* Check for word boundary match */
            if (strncasecmp(p, kw, kw_len) == 0) {
                /* Check word boundaries */
                int valid_start = (p == text || !isalnum((unsigned char)*(p-1)));
                int valid_end = (!p[kw_len] || !isalnum((unsigned char)p[kw_len]));
                
                if (valid_start && valid_end) {
                    count++;
                    p += kw_len;
                    break;
                }
            }
        }
        p++;
    }
    
    return count;
}

isae_error_t keywords_classify(const announcement_t* ann, 
                               keyword_result_t* result) {
    if (!ann || !result) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    keyword_result_init(result);
    
    /* Get normalized text or combine title + summary */
    const char* text = ann->normalized_text;
    char temp_text[MAX_TEXT_NORMALIZED_LEN];
    
    if (!text || !*text) {
        /* Build combined text */
        temp_text[0] = '\0';
        if (ann->title) {
            strncpy(temp_text, ann->title, sizeof(temp_text) - 1);
        }
        if (ann->summary) {
            size_t len = strlen(temp_text);
            if (len < sizeof(temp_text) - 2) {
                temp_text[len] = ' ';
                strncat(temp_text, ann->summary, sizeof(temp_text) - len - 2);
            }
        }
        
        /* Normalize it */
        isae_error_t err = normalize_text(temp_text, temp_text, sizeof(temp_text));
        if (err == ISAE_OK) {
            text = temp_text;
        } else {
            text = ann->title ? ann->title : "";
        }
    }
    
    /* Score each category */
    int best_score = 0;
    const char* best_category = "UNKNOWN";
    
    for (size_t i = 0; i < g_num_categories; i++) {
        int score = count_keyword_matches(text, g_categories[i].keywords, 
                                          g_categories[i].num_keywords);
        
        if (score > best_score) {
            best_score = score;
            best_category = g_categories[i].category;
        }
    }
    
    /* Set result */
    if (best_score > 0) {
        strncpy(result->category, best_category, MAX_CATEGORY_NAME - 1);
        result->category[MAX_CATEGORY_NAME - 1] = '\0';
        result->score = best_score;
        result->matched = true;
    } else {
        /* Default fallback */
        strncpy(result->category, "GENERAL", MAX_CATEGORY_NAME - 1);
        result->category[MAX_CATEGORY_NAME - 1] = '\0';
        result->score = 0;
        result->matched = false;
    }
    
    return ISAE_OK;
}
