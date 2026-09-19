/**
 * @file models.h
 * @brief Core data types and text normalization utilities
 */

#ifndef ISAE_MONITOR_MODELS_H
#define ISAE_MONITOR_MODELS_H

#include "common.h"

/* Announcement structure - represents a single feed entry */
typedef struct {
    char id[MAX_ANNOUNCEMENT_ID];
    char title[MAX_ANNOUNCEMENT_TITLE];
    char link[MAX_ANNOUNCEMENT_LINK];
    char published[MAX_PUBLISHED_DATE];
    char summary[MAX_ANNOUNCEMENT_SUMMARY];
    
    /* Pipeline state (not from feed) */
    bool needs_general;
    bool needs_classification;
    char category[MAX_DEPARTMENT_KEY];
} announcement_t;

/* Initialize an announcement with default values */
void announcement_init(announcement_t* ann);

/* Copy an announcement (deep copy of all strings) */
isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src);

/* Free any dynamically allocated resources in announcement */
void announcement_cleanup(announcement_t* ann);

/* Build announcement from feedparser-like entry (key-value pairs) */
isae_error_t announcement_from_entry(announcement_t* ann, 
                                      const char* id,
                                      const char* title,
                                      const char* link,
                                      const char* published,
                                      const char* summary);

/* Get normalized search text (title + summary, lowercased, accents removed) */
isae_error_t announcement_get_search_text(const announcement_t* ann, 
                                           char* buffer, size_t buffer_size);

/* Text normalization functions */

/* Strip HTML tags and unescape entities */
isae_error_t strip_html(const char* input, char* output, size_t output_size);

/* Normalize text for keyword matching:
 * - Lowercase
 * - Remove French accents (é -> e)
 * - Remove Arabic diacritics
 * - Normalize alef variants (أ/إ/آ -> ا)
 * - Collapse whitespace
 */
isae_error_t normalize_text(const char* input, char* output, size_t output_size);

/* Escape HTML special characters (& < > " ') */
isae_error_t html_escape(const char* input, char* output, size_t output_size);

/* Truncate string to limit, adding ellipsis if truncated */
isae_error_t truncate_string(const char* input, char* output, 
                              size_t limit, size_t output_size);

#endif /* ISAE_MONITOR_MODELS_H */
