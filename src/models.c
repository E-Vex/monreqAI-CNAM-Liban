/**
 * ISAE Monitor - Models Implementation
 *
 * Faithful C port of Python's isae_monitor/models.py.
 *
 * The text-normalization pipeline is the load-bearing part:
 *   html_unescape -> remove_french_accents -> normalize_arabic
 *   -> to_lowercase -> collapse_whitespace
 *
 * The previous C implementation silently stripped every multibyte byte
 * that was not in the Latin-1 Supplement block (i.e. ALL Arabic content
 * was dropped before the keyword classifier ever ran). This version
 * decodes UTF-8 codepoints properly so Arabic keyword matching works.
 */

#include "isae_monitor/models.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void announcement_init(announcement_t* ann) {
    if (!ann) return;
    ann->id              = NULL;
    ann->title           = NULL;
    ann->link            = NULL;
    ann->published       = NULL;
    ann->summary         = NULL;
    ann->normalized_text = NULL;
    ann->category        = NULL;
    ann->is_new          = false;
}

isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src) {
    if (!dest || !src) return ISAE_ERR_INVALID_PARAM;
    announcement_init(dest);

#define SAFE_STRDUP(field) do { \
    if (src->field) { \
        dest->field = strdup(src->field); \
        if (!dest->field) { announcement_cleanup(dest); return ISAE_ERR_MEMORY; } \
    } \
} while (0)
    SAFE_STRDUP(id);
    SAFE_STRDUP(title);
    SAFE_STRDUP(link);
    SAFE_STRDUP(published);
    SAFE_STRDUP(summary);
    SAFE_STRDUP(normalized_text);
    SAFE_STRDUP(category);
#undef SAFE_STRDUP
    dest->is_new = src->is_new;
    return ISAE_OK;
}

void announcement_cleanup(announcement_t* ann) {
    if (!ann) return;
    free(ann->id);
    free(ann->title);
    free(ann->link);
    free(ann->published);
    free(ann->summary);
    free(ann->normalized_text);
    free(ann->category);
    announcement_init(ann);
}

const char* announcement_search_text(announcement_t* ann) {
    if (!ann) return NULL;
    if (ann->normalized_text) return ann->normalized_text;

    /* Build "title summary" and normalize. */
    size_t title_len = ann->title ? strlen(ann->title) : 0;
    size_t summary_len = ann->summary ? strlen(ann->summary) : 0;
    size_t total = title_len + 1 + summary_len + 1;
    char* buf = (char*)malloc(total);
    if (!buf) return NULL;
    buf[0] = '\0';
    if (ann->title) memcpy(buf, ann->title, title_len);
    buf[title_len] = ' ';
    if (ann->summary) memcpy(buf + title_len + 1, ann->summary, summary_len);
    buf[title_len + 1 + summary_len] = '\0';

    char norm[MAX_TEXT_NORMALIZED_LEN];
    if (normalize_text(buf, norm, sizeof(norm)) != ISAE_OK) {
        free(buf);
        return NULL;
    }
    free(buf);
    ann->normalized_text = strdup(norm);
    return ann->normalized_text;
}

/* ------------------------------------------------------------------ */
/* UTF-8 codec                                                        */
/* ------------------------------------------------------------------ */

size_t utf8_decode(const char* input, size_t max_read, uint32_t* cp_out) {
    if (!input || max_read == 0 || !cp_out) return 0;
    unsigned char b0 = (unsigned char)input[0];
    *cp_out = 0;

    if (b0 < 0x80) {
        *cp_out = b0;
        return 1;
    }
    if (b0 < 0xC0) {
        return 0;  /* continuation byte where a lead byte was expected */
    }
    if (b0 < 0xE0) {
        if (max_read < 2) return 0;
        unsigned char b1 = (unsigned char)input[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        *cp_out = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        return (*cp_out >= 0x80) ? 2 : 0;  /* reject overlong */
    }
    if (b0 < 0xF0) {
        if (max_read < 3) return 0;
        unsigned char b1 = (unsigned char)input[1];
        unsigned char b2 = (unsigned char)input[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12)
                    | ((uint32_t)(b1 & 0x3F) << 6)
                    | (uint32_t)(b2 & 0x3F);
        if (cp < 0x800) return 0; /* overlong */
        /* surrogates are invalid in UTF-8 */
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        *cp_out = cp;
        return 3;
    }
    if (b0 < 0xF8) {
        if (max_read < 4) return 0;
        unsigned char b1 = (unsigned char)input[1];
        unsigned char b2 = (unsigned char)input[2];
        unsigned char b3 = (unsigned char)input[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18)
                    | ((uint32_t)(b1 & 0x3F) << 12)
                    | ((uint32_t)(b2 & 0x3F) << 6)
                    | (uint32_t)(b3 & 0x3F);
        if (cp < 0x10000) return 0;  /* overlong */
        if (cp > 0x10FFFF) return 0; /* out of range */
        *cp_out = cp;
        return 4;
    }
    return 0;  /* 5/6-byte sequences are not legal UTF-8 */
}

size_t utf8_encode(uint32_t cp, char* output, size_t output_size) {
    if (!output || output_size == 0) return 0;
    if (cp < 0x80) {
        if (output_size < 1) return 0;
        output[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (output_size < 2) return 0;
        output[0] = (char)(0xC0 | (cp >> 6));
        output[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (output_size < 3) return 0;
        output[0] = (char)(0xE0 | (cp >> 12));
        output[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        output[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        if (output_size < 4) return 0;
        output[0] = (char)(0xF0 | (cp >> 18));
        output[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        output[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        output[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Accent folding                                                     */
/* ------------------------------------------------------------------ */

/* Map Latin-1 Supplement accented letters to their ASCII base. We accept
 * both the precomposed form and decompose-on-the-fly for anything in the
 * U+00C0..U+017F range. The result is the ASCII letter (lowercase, since
 * the caller will lowercase later anyway). */
static char fold_latin1(uint32_t cp) {
    switch (cp) {
        /* A-family */
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            return 'a';
        /* AE ligatures - fold to "ae"; we approximate by 'a' here, the
         * caller's keyword table uses "ae" or "æ" explicitly. */
        case 0xC6: case 0xE6:
            return 'a';
        /* C-cedilla */
        case 0xC7: case 0xE7:
            return 'c';
        /* E-family */
        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            return 'e';
        /* I-family */
        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xEC: case 0xED: case 0xEE: case 0xEF:
            return 'i';
        /* N-tilde */
        case 0xD1: case 0xF1:
            return 'n';
        /* O-family */
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8:
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8:
            return 'o';
        /* U-family */
        case 0xD9: case 0xDA: case 0xDB: case 0xDC:
        case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            return 'u';
        /* Y / y */
        case 0xDD: case 0xFD: case 0xFF:
            return 'y';
        /* German sharp s -> "ss"; approximate as 's'. */
        case 0xDF:
            return 's';
        /* NBSP -> regular space (Python's NFKD does this). */
        case 0xA0:
            return ' ';
        default:
            return 0;  /* not a foldable Latin-1 letter */
    }
}

isae_error_t remove_french_accents(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;

    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;

    while (i < in_len && out_idx + 4 < output_size) {
        uint32_t cp = 0;
        size_t adv = utf8_decode(input + i, in_len - i, &cp);
        if (adv == 0) {
            /* Invalid UTF-8: emit '?' and skip one byte to resync. */
            output[out_idx++] = '?';
            i++;
            continue;
        }
        if (cp < 0x80) {
            output[out_idx++] = (char)cp;
            i += adv;
            continue;
        }
        char folded = fold_latin1(cp);
        if (folded) {
            output[out_idx++] = folded;
            i += adv;
            continue;
        }
        /* Not Latin-1: preserve verbatim so Arabic / CJK / etc. survive. */
        size_t w = utf8_encode(cp, output + out_idx, output_size - out_idx - 1);
        if (w == 0) break;  /* output buffer exhausted */
        out_idx += w;
        i += adv;
    }
    output[out_idx] = '\0';
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* Arabic normalization                                                */
/* ------------------------------------------------------------------ */

isae_error_t normalize_arabic(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;

    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;

    while (i < in_len && out_idx + 4 < output_size) {
        uint32_t cp = 0;
        size_t adv = utf8_decode(input + i, in_len - i, &cp);
        if (adv == 0) {
            output[out_idx++] = '?';
            i++;
            continue;
        }

        /* Strip Arabic diacritics (harakat U+064B..U+0652) and tatweel U+0640. */
        if ((cp >= 0x064B && cp <= 0x0652) || cp == 0x0640) {
            i += adv;
            continue;
        }
        /* Fold alef variants. */
        if (cp == 0x0623 || cp == 0x0625 || cp == 0x0622) cp = 0x0627; /* أ إ آ -> ا */
        if (cp == 0x0629) cp = 0x0647;  /* ة -> ه */
        if (cp == 0x0649) cp = 0x064A;  /* ى -> ي */

        size_t w = utf8_encode(cp, output + out_idx, output_size - out_idx - 1);
        if (w == 0) break;
        out_idx += w;
        i += adv;
    }
    output[out_idx] = '\0';
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* Lowercase                                                          */
/* ------------------------------------------------------------------ */

void to_lowercase(char* str) {
    if (!str) return;
    for (unsigned char* p = (unsigned char*)str; *p; p++) {
        /* Only fold ASCII [A-Z]; multibyte UTF-8 lead/continuation bytes
         * are >= 0x80 so they're never matched here, so UTF-8 sequences
         * pass through untouched. */
        if (*p >= 'A' && *p <= 'Z') *p = (unsigned char)(*p - 'A' + 'a');
    }
}

/* ------------------------------------------------------------------ */
/* Full normalization pipeline                                        */
/* ------------------------------------------------------------------ */

isae_error_t normalize_text(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;

    /* Use heap buffers because the stack cost of two 8KB buffers per call
     * chain adds up quickly (the keyword classifier calls us per keyword). */
    char* temp1 = (char*)malloc(MAX_TEXT_NORMALIZED_LEN);
    char* temp2 = (char*)malloc(MAX_TEXT_NORMALIZED_LEN);
    if (!temp1 || !temp2) {
        free(temp1); free(temp2);
        return ISAE_ERR_MEMORY;
    }

    isae_error_t err = html_unescape(input, temp1, MAX_TEXT_NORMALIZED_LEN);
    if (err != ISAE_OK) goto done;
    err = remove_french_accents(temp1, temp2, MAX_TEXT_NORMALIZED_LEN);
    if (err != ISAE_OK) goto done;
    err = normalize_arabic(temp2, output, output_size);
    if (err != ISAE_OK) goto done;
    to_lowercase(output);

    /* Collapse runs of whitespace into a single space, with no leading or
     * trailing space (mirrors Python's " ".join(text.split())). */
    {
        char collapsed[MAX_TEXT_NORMALIZED_LEN];
        size_t in_len = strlen(output);
        size_t w = 0;
        bool pending_space = false;
        for (size_t r = 0; r < in_len && w + 1 < sizeof(collapsed); r++) {
            char c = output[r];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
                if (w > 0) pending_space = true;
            } else {
                if (pending_space) { collapsed[w++] = ' '; pending_space = false; }
                collapsed[w++] = c;
            }
        }
        collapsed[w] = '\0';
        memcpy(output, collapsed, w + 1);
    }

done:
    free(temp1); free(temp2);
    return err;
}

/* ------------------------------------------------------------------ */
/* HTML unescape                                                      */
/* ------------------------------------------------------------------ */

/* Named entities we recognize beyond the XML core five. Extend as needed. */
typedef struct { const char* name; uint32_t cp; } named_entity_t;
static const named_entity_t NAMED[] = {
    { "amp",   '&'  },
    { "lt",    '<'  },
    { "gt",    '>'  },
    { "quot",  '"'  },
    { "apos",  '\'' },
    { "nbsp",  0xA0 },   /* keep as NBSP, then collapse-whitespace handles it */
    { "eacute",  0xE9 },
    { "egrave",  0xE8 },
    { "ecirc",   0xEA },
    { "euml",    0xEB },
    { "agrave",  0xE0 },
    { "aacute",  0xE1 },
    { "acirc",   0xE2 },
    { "atilde",  0xE3 },
    { "auml",    0xE4 },
    { "ccedil",  0xE7 },
    { "icirc",   0xEE },
    { "igrave",  0xEC },
    { "iacute",  0xED },
    { "iuml",    0xEF },
    { "ocirc",   0xF4 },
    { "ograve",  0xF2 },
    { "oacute",  0xF3 },
    { "otilde",  0xF5 },
    { "ouml",    0xF6 },
    { "ucirc",   0xFB },
    { "ugrave",  0xF9 },
    { "uacute",  0xFA },
    { "uuml",    0xFC },
    { "laquo",   0xAB },
    { "raquo",   0xBB },
    { "rsquo",   0x2019 },
    { "lsquo",   0x2018 },
    { "rdquo",   0x201D },
    { "ldquo",   0x201C },
    { "ndash",   0x2013 },
    { "mdash",   0x2014 },
    { "hellip",  0x2026 },
    { "bull",    0x2022 },
    { "deg",     0xB0  },
    { "micro",   0xB5  },
    { "times",   0xD7  },
    { "divide",  0xF7  },
    { "copy",    0xA9  },
    { "reg",     0xAE  },
    { "trade",   0x2122 },
    { NULL, 0 }
};

static size_t try_named_entity(const char* p, size_t in_len, uint32_t* cp_out) {
    /* p points at '&'. Look for the matching ';' and compare the inner
     * substring against NAMED[]. Returns the number of bytes consumed
     * (including & and ;) or 0 if no match. */
    if (in_len < 3 || p[0] != '&') return 0;
    size_t j = 1;
    while (j < in_len && p[j] != ';' && p[j] != '&' && p[j] != '\n' && j <= 16) j++;
    if (j >= in_len || p[j] != ';') return 0;
    size_t name_len = j - 1;
    for (const named_entity_t* e = NAMED; e->name; e++) {
        if (strlen(e->name) == name_len && strncmp(p + 1, e->name, name_len) == 0) {
            *cp_out = e->cp;
            return j + 1;
        }
    }
    return 0;
}

isae_error_t html_unescape(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;

    size_t in_len = strlen(input);
    size_t out_idx = 0;
    size_t i = 0;

    while (i < in_len && out_idx + 4 < output_size) {
        if (input[i] != '&') {
            output[out_idx++] = input[i++];
            continue;
        }

        /* Try named entity first. */
        uint32_t cp = 0;
        size_t consumed = try_named_entity(input + i, in_len - i, &cp);
        if (consumed > 0) {
            size_t w = utf8_encode(cp, output + out_idx, output_size - out_idx - 1);
            if (w == 0) break;
            out_idx += w;
            i += consumed;
            continue;
        }

        /* Try numeric entity: &#NN; or &#xHH; */
        if (i + 2 < in_len && input[i + 1] == '#') {
            size_t j = i + 2;
            uint32_t code = 0;
            bool ok = false;
            if (j < in_len && (input[j] == 'x' || input[j] == 'X')) {
                j++;
                while (j < in_len && isxdigit((unsigned char)input[j])) {
                    unsigned char c = (unsigned char)input[j];
                    unsigned val = isdigit(c) ? (c - '0') : (tolower(c) - 'a' + 10);
                    if (code > 0x10FFFFu / 16) { ok = false; break; } /* overflow guard */
                    code = code * 16 + val;
                    j++;
                    ok = true;
                }
            } else {
                while (j < in_len && isdigit((unsigned char)input[j])) {
                    if (code > 0x10FFFFu / 10) { ok = false; break; }
                    code = code * 10 + (unsigned)(input[j] - '0');
                    j++;
                    ok = true;
                }
            }
            if (ok && j < in_len && input[j] == ';' && code > 0 && code <= 0x10FFFF) {
                size_t w = utf8_encode(code, output + out_idx, output_size - out_idx - 1);
                if (w == 0) break;
                out_idx += w;
                i = j + 1;
                continue;
            }
        }

        /* Unknown entity: emit '&' verbatim and advance by one. */
        output[out_idx++] = '&';
        i++;
    }
    output[out_idx] = '\0';
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* HTML escape (for Telegram parse_mode=HTML)                         */
/* ------------------------------------------------------------------ */

isae_error_t html_escape(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;
    size_t in_len = strlen(input);
    size_t out_idx = 0;
    for (size_t i = 0; i < in_len; i++) {
        const char* seq = NULL;
        size_t seq_len = 0;
        switch (input[i]) {
            case '&': seq = "&amp;";  seq_len = 5; break;
            case '<': seq = "&lt;";   seq_len = 4; break;
            case '>': seq = "&gt;";   seq_len = 4; break;
            default: break;
        }
        if (seq) {
            if (out_idx + seq_len + 1 >= output_size) {
                output[out_idx] = '\0';
                return ISAE_ERR_INVALID_PARAM; /* output too small */
            }
            memcpy(output + out_idx, seq, seq_len);
            out_idx += seq_len;
        } else {
            if (out_idx + 2 >= output_size) {
                output[out_idx] = '\0';
                return ISAE_ERR_INVALID_PARAM;
            }
            output[out_idx++] = input[i];
        }
    }
    output[out_idx] = '\0';
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* strip_html                                                         */
/* ------------------------------------------------------------------ */

isae_error_t strip_html(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return ISAE_ERR_INVALID_PARAM;

    /* Step 1: HTML-unescape into a working buffer. */
    char* unescaped = (char*)malloc(MAX_TEXT_NORMALIZED_LEN);
    if (!unescaped) return ISAE_ERR_MEMORY;
    isae_error_t err = html_unescape(input, unescaped, MAX_TEXT_NORMALIZED_LEN);
    if (err != ISAE_OK) { free(unescaped); return err; }

    /* Step 2: replace '<...>' with a single space. */
    char* stripped = (char*)malloc(MAX_TEXT_NORMALIZED_LEN);
    if (!stripped) { free(unescaped); return ISAE_ERR_MEMORY; }

    size_t in_len = strlen(unescaped);
    size_t out_idx = 0;
    bool in_tag = false;
    for (size_t i = 0; i < in_len && out_idx + 1 < MAX_TEXT_NORMALIZED_LEN; i++) {
        char c = unescaped[i];
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; if (out_idx + 1 < MAX_TEXT_NORMALIZED_LEN) stripped[out_idx++] = ' '; continue; }
        if (in_tag) continue;
        stripped[out_idx++] = c;
    }
    stripped[out_idx] = '\0';

    /* Step 3: collapse whitespace into single spaces. Whitespace is
     * detected at the CODEPOINT level, not the byte level, because
     * NBSP (U+00A0) is encoded in UTF-8 as the two bytes 0xC2 0xA0.
     * The previous byte-level check `c == (char)0xA0` matched only the
     * second byte of NBSP, leaving the lead byte 0xC2 dangling in the
     * output and producing an invalid UTF-8 sequence (0xC2 0x20) that
     * Telegram rejects with "text must be encoded in UTF-8". */
    size_t w = 0;
    bool pending_space = false;
    size_t r = 0;
    while (r < out_idx && w + 4 < output_size) {
        uint32_t cp = 0;
        size_t adv = utf8_decode(stripped + r, out_idx - r, &cp);
        if (adv == 0) {
            /* Invalid UTF-8 lead byte: emit '?' and resync by one byte. */
            if (pending_space) { output[w++] = ' '; pending_space = false; }
            output[w++] = '?';
            r++;
            continue;
        }
        bool is_ws = (cp == ' '  || cp == '\t' || cp == '\n' || cp == '\r'
                   || cp == '\v' || cp == '\f' || cp == 0xA0);
        if (is_ws) {
            if (w > 0) pending_space = true;
            r += adv;
        } else {
            if (pending_space) { output[w++] = ' '; pending_space = false; }
            if (w + adv >= output_size) break;
            memcpy(output + w, stripped + r, adv);
            w += adv;
            r += adv;
        }
    }
    output[w] = '\0';

    free(unescaped);
    free(stripped);
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* Backwards-compat: announcement_hash() kept for the old pipeline.   */
/* The new pipeline uses the Atom <id> directly as the dedup key.    */
/* ------------------------------------------------------------------ */

isae_error_t announcement_hash(const announcement_t* ann, char* hash_out, size_t hash_size) {
    if (!ann || !hash_out || hash_size < 17) return ISAE_ERR_INVALID_PARAM;
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t hash = FNV_OFFSET;

    if (ann->link) {
        for (const char* p = ann->link; *p; p++) {
            hash ^= (uint64_t)(unsigned char)*p;
            hash *= FNV_PRIME;
        }
    }
    if (ann->title) {
        for (const char* p = ann->title; *p; p++) {
            hash ^= (uint64_t)(unsigned char)*p;
            hash *= FNV_PRIME;
        }
    }
    snprintf(hash_out, hash_size, "%016llx", (unsigned long long)hash);
    return ISAE_OK;
}
