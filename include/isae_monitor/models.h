#ifndef ISAE_MONITOR_MODELS_H
#define ISAE_MONITOR_MODELS_H

#include "common.h"

/* Forward declaration */
struct announcement;

/* Announcement structure (defined in common.h, extended here) */
typedef struct announcement {
    char* title;
    char* summary;
    char* url;
    char* published;
    char* department_key;
    char* normalized_text;  /* For classification */
    char* category;         /* Classified category */
    bool is_new;
} announcement_t;

/* Initialize an announcement structure */
void announcement_init(announcement_t* ann);

/* Deep copy an announcement */
isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src);

/* Free announcement resources */
void announcement_cleanup(announcement_t* ann);

/* Text normalization functions */

/* Normalize text for classification: remove accents, diacritics, lowercase */
isae_error_t normalize_text(const char* input, char* output, size_t output_size);

/* Remove French accents from UTF-8 string */
isae_error_t remove_french_accents(const char* input, char* output, size_t output_size);

/* Remove Arabic diacritics and normalize alef forms */
isae_error_t normalize_arabic(const char* input, char* output, size_t output_size);

/* Convert to lowercase (ASCII only for simplicity) */
void to_lowercase(char* str);

/* HTML entity unescaping */
isae_error_t html_unescape(const char* input, char* output, size_t output_size);

/* Generate a simple hash for announcement deduplication */
isae_error_t announcement_hash(const announcement_t* ann, char* hash_out, size_t hash_size);

#endif /* ISAE_MONITOR_MODELS_H */
