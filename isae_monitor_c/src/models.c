/**
 * @file models.c
 * @brief Core data types and text normalization implementation
 */

#include "isae_monitor/models.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

void announcement_init(announcement_t* ann) {
    if (!ann) return;
    memset(ann, 0, sizeof(announcement_t));
    ann->needs_general = true;
    ann->needs_classification = true;
}

isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src) {
    if (!dest || !src) return ISAE_ERR_INVALID_ARG;
    
    strncpy(dest->id, src->id, MAX_ANNOUNCEMENT_ID - 1);
    dest->id[MAX_ANNOUNCEMENT_ID - 1] = '\0';
    
    strncpy(dest->title, src->title, MAX_ANNOUNCEMENT_TITLE - 1);
    dest->title[MAX_ANNOUNCEMENT_TITLE - 1] = '\0';
    
    strncpy(dest->link, src->link, MAX_ANNOUNCEMENT_LINK - 1);
    dest->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
    
    strncpy(dest->published, src->published, MAX_PUBLISHED_DATE - 1);
    dest->published[MAX_PUBLISHED_DATE - 1] = '\0';
    
    strncpy(dest->summary, src->summary, MAX_ANNOUNCEMENT_SUMMARY - 1);
    dest->summary[MAX_ANNOUNCEMENT_SUMMARY - 1] = '\0';
    
    dest->needs_general = src->needs_general;
    dest->needs_classification = src->needs_classification;
    
    strncpy(dest->category, src->category, MAX_DEPARTMENT_KEY - 1);
    dest->category[MAX_DEPARTMENT_KEY - 1] = '\0';
    
    return ISAE_OK;
}

void announcement_cleanup(announcement_t* ann) {
    UNUSED(ann);  /* No dynamic allocation in this struct */
}

isae_error_t announcement_from_entry(announcement_t* ann,
                                      const char* id,
                                      const char* title,
                                      const char* link,
                                      const char* published,
                                      const char* summary) {
    if (!ann) return ISAE_ERR_INVALID_ARG;
    
    announcement_init(ann);
    
    /* ID falls back to link if missing */
    if (id && *id) {
        strncpy(ann->id, id, MAX_ANNOUNCEMENT_ID - 1);
        ann->id[MAX_ANNOUNCEMENT_ID - 1] = '\0';
    } else if (link && *link) {
        strncpy(ann->id, link, MAX_ANNOUNCEMENT_ID - 1);
        ann->id[MAX_ANNOUNCEMENT_ID - 1] = '\0';
    }
    
    /* Title defaults to "(sans titre)" if missing */
    if (title && *title) {
        strncpy(ann->title, title, MAX_ANNOUNCEMENT_TITLE - 1);
        ann->title[MAX_ANNOUNCEMENT_TITLE - 1] = '\0';
    } else {
        strncpy(ann->title, "(sans titre)", MAX_ANNOUNCEMENT_TITLE - 1);
    }
    
    if (link && *link) {
        strncpy(ann->link, link, MAX_ANNOUNCEMENT_LINK - 1);
        ann->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
    }
    
    if (published && *published) {
        strncpy(ann->published, published, MAX_PUBLISHED_DATE - 1);
        ann->published[MAX_PUBLISHED_DATE - 1] = '\0';
    } else {
        strncpy(ann->published, "date inconnue", MAX_PUBLISHED_DATE - 1);
    }
    
    /* Strip HTML from summary and limit to 500 chars */
    if (summary && *summary) {
        char stripped[MAX_ANNOUNCEMENT_SUMMARY];
        strip_html(summary, stripped, sizeof(stripped));
        strncpy(ann->summary, stripped, MAX_ANNOUNCEMENT_SUMMARY - 1);
        ann->summary[MAX_ANNOUNCEMENT_SUMMARY - 1] = '\0';
    }
    
    return ISAE_OK;
}

isae_error_t announcement_get_search_text(const announcement_t* ann,
                                           char* buffer, size_t buffer_size) {
    if (!ann || !buffer || buffer_size == 0) return ISAE_ERR_INVALID_ARG;
    
    /* Concatenate title and summary, then normalize */
    char combined[MAX_ANNOUNCEMENT_TITLE + MAX_ANNOUNCEMENT_SUMMARY + 2];
    snprintf(combined, sizeof(combined), "%s %s", ann->title, ann->summary);
    
    return normalize_text(combined, buffer, buffer_size);
}

/* HTML entity lookup table */
typedef struct {
    const char* entity;
    char character;
} html_entity_t;

static const html_entity_t g_html_entities[] = {
    {"&nbsp;", ' '}, {"&#160;", ' '},
    {"&amp;", '&'}, {"&#38;", '&'},
    {"&lt;", '<'}, {"&#60;", '<'},
    {"&gt;", '>'}, {"&#62;", '>'},
    {"&quot;", '"'}, {"&#34;", '"'},
    {"&apos;", '\''}, {"&#39;", '\''},
    {"&copy;", 'c'}, {"&#169;", 'c'},
    {"&reg;", 'r'}, {"&#174;", 'r'},
    {NULL, 0}
};

isae_error_t strip_html(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_ARG;
    
    const char* src = input;
    char* dst = output;
    char* dst_end = output + output_size - 1;
    
    while (*src && dst < dst_end) {
        if (*src == '<') {
            /* Skip HTML tag */
            src++;
            while (*src && *src != '>') src++;
            if (*src) src++;  /* Skip closing > */
            if (dst < dst_end) *dst++ = ' ';
        } else if (*src == '&') {
            /* Decode HTML entity */
            bool found = false;
            for (size_t i = 0; g_html_entities[i].entity; i++) {
                size_t len = strlen(g_html_entities[i].entity);
                if (strncmp(src, g_html_entities[i].entity, len) == 0) {
                    *dst++ = g_html_entities[i].character;
                    src += len;
                    found = true;
                    break;
                }
            }
            if (!found) {
                /* Unknown entity, copy as-is */
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    
    *dst = '\0';
    
    /* Collapse whitespace */
    char* write = output;
    bool last_was_space = false;
    for (char* read = output; *read; read++) {
        if (isspace((unsigned char)*read)) {
            if (!last_was_space) {
                *write++ = ' ';
                last_was_space = true;
            }
        } else {
            *write++ = *read;
            last_was_space = false;
        }
    }
    *write = '\0';
    
    /* Trim leading/trailing whitespace */
    char* start = output;
    while (*start && isspace((unsigned char)*start)) start++;
    
    char* end = output + strlen(output) - 1;
    while (end >= start && isspace((unsigned char)*end)) *end-- = '\0';
    
    if (start != output) {
        memmove(output, start, strlen(start) + 1);
    }
    
    return ISAE_OK;
}

/* Unicode combining mark check (simplified for common French accents) */
static bool is_combining_mark(uint8_t byte) {
    /* Combining diacritical marks range: U+0300 to U+036F */
    /* In UTF-8, these are encoded as 0xCC 0x80 to 0xCC 0xAF */
    return byte == 0xCC;
}

/* Arabic diacritics check (U+064B to U+0652) */
static bool is_arabic_diacritic(const uint8_t* p) {
    if (p[0] == 0xD9 && p[1] >= 0x8B && p[1] <= 0x92) return true;  /* Fatha, Dammatan, etc. */
    if (p[0] == 0xD9 && p[1] == 0x93) return true;  /* Kasratan */
    if (p[0] == 0xD9 && p[1] >= 0x96 && p[1] <= 0x9F) return true;  /* Shadda, etc. */
    return false;
}

isae_error_t normalize_text(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_ARG;
    
    const uint8_t* src = (const uint8_t*)input;
    uint8_t* dst = (uint8_t*)output;
    uint8_t* dst_end = (uint8_t*)output + output_size - 1;
    
    while (*src && dst < dst_end) {
        /* Check for UTF-8 multi-byte sequences */
        if (src[0] == 0xC3 && src[1]) {
            /* Latin Extended-A/B characters with accents */
            /* C3 0x80-0xBF maps to U+00C0-U+00FF */
            /* We convert accented chars to their base form */
            switch (src[1]) {
                case 0x80: case 0x81:  /* À Á */
                    *dst++ = 'A'; src += 2; break;
                case 0x82:  /* Â */
                    *dst++ = 'A'; src += 2; break;
                case 0x87:  /* Ç */
                    *dst++ = 'C'; src += 2; break;
                case 0x88: case 0x89: case 0x8A: case 0x8B:  /* È É Ê Ë */
                    *dst++ = 'E'; src += 2; break;
                case 0x8D: case 0x8E: case 0x8F:  /* Ì Í Î */
                    *dst++ = 'I'; src += 2; break;
                case 0x91: case 0x92: case 0x93: case 0x94:  /* Ñ Ò Ó Ô */
                    *dst++ = (src[1] == 0x91) ? 'N' : 'O'; src += 2; break;
                case 0x95: case 0x96: case 0x97: case 0x98:  /* Õ Ö Ø Ù */
                    *dst++ = (src[1] == 0x98) ? 'U' : 'O'; src += 2; break;
                case 0x99: case 0x9A: case 0x9B: case 0x9C:  /* Ú Û Ü Ý */
                    *dst++ = (src[1] == 0x9C) ? 'Y' : 'U'; src += 2; break;
                case 0xA0: case 0xA1: case 0xA2:  /* à á â */
                    *dst++ = 'a'; src += 2; break;
                case 0xA7:  /* ç */
                    *dst++ = 'c'; src += 2; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:  /* è é ê ë */
                    *dst++ = 'e'; src += 2; break;
                case 0xAD: case 0xAE: case 0xAF:  /* ì í î */
                    *dst++ = 'i'; src += 2; break;
                case 0xB1: case 0xB2: case 0xB3: case 0xB4:  /* ñ ò ó ô */
                    *dst++ = (src[1] == 0xB1) ? 'n' : 'o'; src += 2; break;
                case 0xB5: case 0xB6: case 0xB7: case 0xB8:  /* õ ö ø ù */
                    *dst++ = (src[1] == 0xB8) ? 'u' : 'o'; src += 2; break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:  /* ú û ü ý */
                    *dst++ = (src[1] == 0xBC) ? 'y' : 'u'; src += 2; break;
                default:
                    *dst++ = *src++; break;
            }
        } else if (src[0] == 0xC2 && src[1] >= 0xA0 && src[1] <= 0xBF) {
            /* U+00A0-U+00BF range */
            if (src[1] == 0xA0) {
                /* Non-breaking space -> regular space */
                *dst++ = ' '; src += 2;
            } else {
                *dst++ = *src++;
            }
        } else if (src[0] == 0xD9 && src[1]) {
            /* Arabic characters */
            if (is_arabic_diacritic(src)) {
                /* Skip Arabic diacritics */
                src += 2;
            } else {
                /* Normalize alef variants */
                if (src[1] == 0x83 || src[1] == 0x84 || src[1] == 0x81) {
                    /* أ (U+0623), إ (U+0625), آ (U+0622) -> ا (U+0627) */
                    dst[0] = 0xD9; dst[1] = 0x87; dst += 2; src += 2;
                } else if (src[1] == 0x89) {
                    /* ة (U+0629) -> ه (U+0647) */
                    dst[0] = 0xD9; dst[1] = 0x87; dst += 2; src += 2;
                } else if (src[1] == 0x8A) {
                    /* ك (U+0643) stays */
                    *dst++ = *src++; *dst++ = *src++;
                } else {
                    *dst++ = *src++; *dst++ = *src++;
                }
            }
        } else if (src[0] == 0xDB && src[1] == 0x8A) {
            /* ى (U+0649) -> ي (U+064A) */
            dst[0] = 0xDB; dst[1] = 0x8B; dst += 2; src += 2;
        } else if (is_combining_mark(src[0])) {
            /* Skip combining marks */
            src++;
            if (*src) src++;  /* Skip second byte of combining char */
        } else {
            *dst++ = *src++;
        }
    }
    
    *dst = '\0';
    
    /* Lowercase ASCII */
    for (uint8_t* p = (uint8_t*)output; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (uint8_t)(*p + ('a' - 'A'));
        }
    }
    
    /* Collapse whitespace */
    uint8_t* write = (uint8_t*)output;
    bool last_was_space = false;
    for (uint8_t* read = (uint8_t*)output; *read; read++) {
        if (isspace(*read)) {
            if (!last_was_space) {
                *write++ = ' ';
                last_was_space = true;
            }
        } else {
            *write++ = *read;
            last_was_space = false;
        }
    }
    *write = '\0';
    
    return ISAE_OK;
}

isae_error_t html_escape(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_ARG;
    
    const char* src = input;
    char* dst = output;
    char* dst_end = output + output_size - 1;
    
    while (*src && dst < dst_end) {
        switch (*src) {
            case '&':
                if (dst + 5 <= dst_end) {
                    memcpy(dst, "&amp;", 5);
                    dst += 5;
                }
                break;
            case '<':
                if (dst + 4 <= dst_end) {
                    memcpy(dst, "&lt;", 4);
                    dst += 4;
                }
                break;
            case '>':
                if (dst + 4 <= dst_end) {
                    memcpy(dst, "&gt;", 4);
                    dst += 4;
                }
                break;
            case '"':
                if (dst + 6 <= dst_end) {
                    memcpy(dst, "&quot;", 6);
                    dst += 6;
                }
                break;
            case '\'':
                if (dst + 6 <= dst_end) {
                    memcpy(dst, "&apos;", 6);
                    dst += 6;
                }
                break;
            default:
                *dst++ = *src;
                break;
        }
        src++;
    }
    
    *dst = '\0';
    return ISAE_OK;
}

isae_error_t truncate_string(const char* input, char* output,
                              size_t limit, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_ARG;
    
    size_t input_len = strlen(input);
    
    if (input_len <= limit) {
        strncpy(output, input, output_size - 1);
        output[output_size - 1] = '\0';
        return ISAE_OK;
    }
    
    if (limit + 2 >= output_size) {
        /* Not enough room for ellipsis */
        strncpy(output, input, output_size - 1);
        output[output_size - 1] = '\0';
        return ISAE_OK;
    }
    
    /* Copy up to limit-1 and add ellipsis */
    memcpy(output, input, limit - 1);
    output[limit - 1] = '\xE2';  /* UTF-8 ellipsis: … */
    output[limit] = '\x80';
    output[limit + 1] = '\xA6';
    output[limit + 2] = '\0';
    
    return ISAE_OK;
}
