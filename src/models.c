/**
 * ISAE Monitor - Models Implementation
 * 
 * Maps Python: models.py -> C: models.c
 * 
 * Implements announcement data structures and text normalization
 * for classification (French accent removal, Arabic diacritics).
 */

#include "isae_monitor/models.h"
#include <ctype.h>

void announcement_init(announcement_t* ann) {
    if (!ann) return;
    
    ann->title = NULL;
    ann->summary = NULL;
    ann->url = NULL;
    ann->published = NULL;
    ann->department_key = NULL;
    ann->normalized_text = NULL;
    ann->category = NULL;
    ann->is_new = false;
}

isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src) {
    if (!dest || !src) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    announcement_init(dest);
    
#define SAFE_STRDUP(dst, src_field) \
    do { \
        if ((src)->src_field) { \
            (dst) = strdup((src)->src_field); \
            if (!(dst)) { \
                announcement_cleanup(dest); \
                return ISAE_ERR_MEMORY; \
            } \
        } \
    } while(0)
    
    SAFE_STRDUP(dest->title, title);
    SAFE_STRDUP(dest->summary, summary);
    SAFE_STRDUP(dest->url, url);
    SAFE_STRDUP(dest->published, published);
    SAFE_STRDUP(dest->department_key, department_key);
    SAFE_STRDUP(dest->normalized_text, normalized_text);
    SAFE_STRDUP(dest->category, category);
    dest->is_new = src->is_new;
    
#undef SAFE_STRDUP
    
    return ISAE_OK;
}

void announcement_cleanup(announcement_t* ann) {
    if (!ann) return;
    
    free(ann->title);
    free(ann->summary);
    free(ann->url);
    free(ann->published);
    free(ann->department_key);
    free(ann->normalized_text);
    free(ann->category);
    
    announcement_init(ann);
}

/**
 * Remove French accents from UTF-8 encoded string.
 * This handles common accented characters by mapping them to ASCII equivalents.
 * 
 * Python equivalent: unicodedata.normalize('NFKD', text).encode('ascii', 'ignore')
 */
isae_error_t remove_french_accents(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;
    
    while (i < in_len && out_idx < output_size - 1) {
        unsigned char c = (unsigned char)input[i];
        
        /* ASCII character - pass through */
        if (c < 0x80) {
            output[out_idx++] = (char)c;
            i++;
            continue;
        }
        
        /* UTF-8 multi-byte sequence */
        if ((c & 0xE0) == 0xC0 && i + 1 < in_len) {
            /* 2-byte sequence */
            unsigned char b2 = (unsigned char)input[i + 1];
            
            /* Check for common accented characters */
            if (c == 0xC3) {
                /* Latin-1 Supplement block */
                switch (b2) {
                    case 0xA0: case 0xA1: /* À Á */
                        output[out_idx++] = 'A'; break;
                    case 0xC2: case 0xC3: case 0xC4: /* Â Ã Ä */
                        output[out_idx++] = 'A'; break;
                    case 0xC7: /* Ç */
                        output[out_idx++] = 'C'; break;
                    case 0xC8: case 0xC9: case 0xCA: case 0xCB: /* È É Ê Ë */
                        output[out_idx++] = 'E'; break;
                    case 0xCC: case 0xCD: case 0xCE: case 0xCF: /* Ì Í Î Ï */
                        output[out_idx++] = 'I'; break;
                    case 0xD1: /* Ñ */
                        output[out_idx++] = 'N'; break;
                    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: /* Ò Ó Ô Õ Ö */
                        output[out_idx++] = 'O'; break;
                    case 0xD9: case 0xDA: case 0xDB: case 0xDC: /* Ù Ú Û Ü */
                        output[out_idx++] = 'U'; break;
                    case 0xDD: /* Ý */
                        output[out_idx++] = 'Y'; break;
                    case 0xE0: case 0xE1: /* à á */
                        output[out_idx++] = 'a'; break;
                    case 0xE2: case 0xE3: case 0xE4: /* â ã ä */
                        output[out_idx++] = 'a'; break;
                    case 0xE7: /* ç */
                        output[out_idx++] = 'c'; break;
                    case 0xE8: case 0xE9: case 0xEA: case 0xEB: /* è é ê ë */
                        output[out_idx++] = 'e'; break;
                    case 0xEC: case 0xED: case 0xEE: case 0xEF: /* ì í î ï */
                        output[out_idx++] = 'i'; break;
                    case 0xF1: /* ñ */
                        output[out_idx++] = 'n'; break;
                    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: /* ò ó ô õ ö */
                        output[out_idx++] = 'o'; break;
                    case 0xF9: case 0xFA: case 0xFB: case 0xFC: /* ù ú û ü */
                        output[out_idx++] = 'u'; break;
                    case 0xFD: /* ý */
                        output[out_idx++] = 'y'; break;
                    case 0xFF: /* ÿ */
                        output[out_idx++] = 'y'; break;
                    default:
                        /* Keep original bytes for unknown chars */
                        if (out_idx + 2 < output_size) {
                            output[out_idx++] = (char)c;
                            output[out_idx++] = (char)b2;
                        }
                        break;
                }
                i += 2;
                continue;
            }
            
            /* Other 2-byte sequences - skip (remove accent) */
            i += 2;
            continue;
        }
        
        /* 3-byte or more - skip (remove) */
        if ((c & 0xF0) == 0xE0) {
            i += 3;
            continue;
        }
        if ((c & 0xF8) == 0xF0) {
            i += 4;
            continue;
        }
        
        /* Unknown encoding - skip */
        i++;
    }
    
    output[out_idx] = '\0';
    return ISAE_OK;
}

/**
 * Normalize Arabic text by removing diacritics and standardizing alef forms.
 */
isae_error_t normalize_arabic(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* For simplicity in this implementation, we'll copy the input as-is
     * A full implementation would handle Arabic Unicode ranges:
     * - Remove diacritics (U+064B to U+065F)
     * - Normalize alef forms (إ، آ، أ -> ا)
     */
    strncpy(output, input, output_size - 1);
    output[output_size - 1] = '\0';
    
    return ISAE_OK;
}

/**
 * Full text normalization pipeline:
 * 1. HTML unescape
 * 2. Remove French accents
 * 3. Normalize Arabic
 * 4. Convert to lowercase
 */
isae_error_t normalize_text(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Step 1: HTML unescape */
    char temp1[MAX_TEXT_NORMALIZED_LEN];
    isae_error_t err = html_unescape(input, temp1, sizeof(temp1));
    if (err != ISAE_OK) {
        return err;
    }
    
    /* Step 2: Remove French accents */
    char temp2[MAX_TEXT_NORMALIZED_LEN];
    err = remove_french_accents(temp1, temp2, sizeof(temp2));
    if (err != ISAE_OK) {
        return err;
    }
    
    /* Step 3: Normalize Arabic */
    err = normalize_arabic(temp2, output, output_size);
    if (err != ISAE_OK) {
        return err;
    }
    
    /* Step 4: Convert to lowercase */
    to_lowercase(output);
    
    return ISAE_OK;
}

void to_lowercase(char* str) {
    if (!str) return;
    
    for (char* p = str; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

/**
 * Unescape common HTML entities.
 */
isae_error_t html_unescape(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;
    
    while (i < in_len && out_idx < output_size - 1) {
        if (input[i] == '&') {
            /* Check for common entities */
            if (strncmp(&input[i], "&amp;", 5) == 0) {
                output[out_idx++] = '&';
                i += 5;
            } else if (strncmp(&input[i], "&lt;", 4) == 0) {
                output[out_idx++] = '<';
                i += 4;
            } else if (strncmp(&input[i], "&gt;", 4) == 0) {
                output[out_idx++] = '>';
                i += 4;
            } else if (strncmp(&input[i], "&quot;", 6) == 0) {
                output[out_idx++] = '"';
                i += 6;
            } else if (strncmp(&input[i], "&apos;", 6) == 0) {
                output[out_idx++] = '\'';
                i += 6;
            } else if (strncmp(&input[i], "&nbsp;", 6) == 0) {
                output[out_idx++] = ' ';
                i += 6;
            } else if (input[i + 1] == '#') {
                /* Numeric entity: &#NN; or &#xHH; */
                int code = 0;
                size_t j = i + 2;
                
                if (j < in_len && input[j] == 'x') {
                    /* Hexadecimal */
                    j++;
                    while (j < in_len && isxdigit((unsigned char)input[j])) {
                        code = code * 16 + (isdigit((unsigned char)input[j]) ? 
                                           input[j] - '0' : 
                                           tolower((unsigned char)input[j]) - 'a' + 10);
                        j++;
                    }
                } else {
                    /* Decimal */
                    while (j < in_len && isdigit((unsigned char)input[j])) {
                        code = code * 10 + (input[j] - '0');
                        j++;
                    }
                }
                
                if (j < in_len && input[j] == ';' && code > 0 && code < 128) {
                    output[out_idx++] = (char)code;
                    i = j + 1;
                } else {
                    /* Unknown entity - keep as-is */
                    output[out_idx++] = input[i++];
                }
            } else {
                /* Unknown entity - keep as-is */
                output[out_idx++] = input[i++];
            }
        } else {
            output[out_idx++] = input[i++];
        }
    }
    
    output[out_idx] = '\0';
    return ISAE_OK;
}

/**
 * Generate a simple hash for announcement deduplication.
 * Uses a basic FNV-1a hash of URL + title.
 */
isae_error_t announcement_hash(const announcement_t* ann, char* hash_out, size_t hash_size) {
    if (!ann || !hash_out || hash_size < 17) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* FNV-1a hash parameters */
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    
    uint64_t hash = FNV_OFFSET;
    
    /* Hash URL */
    if (ann->url) {
        const char* p = ann->url;
        while (*p) {
            hash ^= (uint64_t)(unsigned char)*p++;
            hash *= FNV_PRIME;
        }
    }
    
    /* Hash title */
    if (ann->title) {
        const char* p = ann->title;
        while (*p) {
            hash ^= (uint64_t)(unsigned char)*p++;
            hash *= FNV_PRIME;
        }
    }
    
    /* Convert to hex string */
    snprintf(hash_out, hash_size, "%016llx", (unsigned long long)hash);
    
    return ISAE_OK;
}
