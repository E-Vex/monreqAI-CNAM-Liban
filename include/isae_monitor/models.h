#ifndef ISAE_MONITOR_MODELS_H
#define ISAE_MONITOR_MODELS_H

#include "common.h"

/* Announcement: mirrors the Python isae_monitor.models.Announcement dataclass.
 * The 'id' field is the Atom <id> value and is used as the dedup key in
 * seen.json. It is intentionally a heap string (so it can be any length,
 * not capped at MAX_URL_LEN). */
typedef struct announcement {
    char* id;               /* Atom <id>; falls back to link if missing */
    char* title;            /* defaults to "(sans titre)" if missing */
    char* link;             /* Atom <link rel=alternate href> */
    char* published;        /* defaults to "date inconnue" if missing */
    char* summary;          /* HTML-stripped, truncated to 500 chars */
    char* normalized_text;  /* cached normalize(title + " " + summary) */
    char* category;         /* last classified category, or NULL */
    bool is_new;
} announcement_t;

/* Lifecycle */
void announcement_init(announcement_t* ann);
isae_error_t announcement_copy(announcement_t* dest, const announcement_t* src);
void announcement_cleanup(announcement_t* ann);

/* search_text = normalize(title + " " + summary). Computed once and cached
 * in ann->normalized_text; subsequent calls return the cached value. */
const char* announcement_search_text(announcement_t* ann);

/* ---- Text normalization (UTF-8 aware, mirrors Python models.normalize) ----
 *
 * Pipeline: html_unescape -> fold_accents (preserve Arabic) -> normalize_arabic
 *           -> to_lowercase -> collapse_whitespace.
 *
 * The output is suitable for keyword matching: French accents are folded to
 * ASCII, Arabic diacritics are stripped, alef variants are folded, and the
 * whole string is lowercased. Multi-byte sequences that are neither Latin
 * accents nor Arabic letters are preserved (so CJK / emoji do not get
 * silently dropped, unlike the previous implementation).
 */
isae_error_t normalize_text(const char* input, char* output, size_t output_size);

/* Fold French (and other Latin-1 Supplement) accents to their ASCII base
 * letter. Arabic and other multibyte scripts pass through unchanged.
 * Mirrors the Python behavior of unicodedata.normalize('NFKD', ...) +
 * strip-combining-marks, restricted to the characters that actually appear
 * in the ISAE feed. */
isae_error_t remove_french_accents(const char* input, char* output, size_t output_size);

/* Strip Arabic diacritics (harakat U+064B-U+0652, tatweel U+0640) and fold
 * alef variants (أ إ آ -> ا), ta-marbuta (ة -> ه), alef-maqsura (ى -> ي).
 * Other codepoints pass through unchanged. */
isae_error_t normalize_arabic(const char* input, char* output, size_t output_size);

/* In-place ASCII lowercase. Safe to call on UTF-8 input: only ASCII bytes
 * [A-Z] are rewritten, multibyte sequences are untouched. */
void to_lowercase(char* str);

/* HTML entity unescape: handles &amp; &lt; &gt; &quot; &apos; &nbsp;,
 * a handful of named Latin-1 entities (&eacute; &egrave; &ccedil; &laquo;
 * &raquo; etc.), and numeric entities &#NN; / &#xHH; for any code point
 * (encoded as UTF-8). Unknown entities are passed through verbatim.
 * Mirrors Python's html.unescape for the entities the ISAE feed actually
 * emits. */
isae_error_t html_unescape(const char* input, char* output, size_t output_size);

/* HTML escape for Telegram (parse_mode=HTML): & < > become &amp; &lt; &gt;.
 * Mirrors Python's html.escape(..., quote=False) used by telegram.format_message. */
isae_error_t html_escape(const char* input, char* output, size_t output_size);

/* Strip HTML tags and collapse whitespace. Mirrors Python's strip_html():
 * tags are replaced with a single space, entities are unescaped, the result
 * is split on whitespace and joined with single spaces. */
isae_error_t strip_html(const char* input, char* output, size_t output_size);

/* ---- UTF-8 helpers ---- */

/* Decode one UTF-8 codepoint at *p, write it to *cp, return the number of
 * bytes consumed (1..4). Returns 0 on invalid UTF-8 (caller decides whether
 * to skip the byte or stop). Never reads past input + max_read. */
size_t utf8_decode(const char* input, size_t max_read, uint32_t* cp);

/* Encode a single codepoint as UTF-8, writing 1..4 bytes at output[0..].
 * Returns the number of bytes written, or 0 if output_size is too small. */
size_t utf8_encode(uint32_t cp, char* output, size_t output_size);

/* ---- Dedup hash (kept for backwards compat with pipeline.c) ---- */

/* FNV-1a hash of URL + title, written as a 16-char hex string. The new
 * pipeline uses the Atom <id> directly as the dedup key, but this is kept
 * so the existing pipeline code still compiles. */
isae_error_t announcement_hash(const announcement_t* ann, char* hash_out, size_t hash_size);

#endif /* ISAE_MONITOR_MODELS_H */
