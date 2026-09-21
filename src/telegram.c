/**
 * ISAE Monitor - Telegram Notification Client
 *
 * Faithful C port of Python isae_monitor/notify/telegram.py.
 *
 * Behavior preserved:
 *   - parse_mode=HTML. Title, summary, and label are HTML-escaped
 *     (& < > -> &amp; &lt; &gt;). The link is NOT escaped (trusted).
 *   - The message layout is:
 *       <b>[Label]</b> <b>Title</b>   (omit [Label] when category=NULL)
 *       <i>published</i>
 *       summary                       (omitted if empty)
 *       link                          (omitted if empty)
 *     joined with "\n\n".
 *   - Title truncated to TELEGRAM_TITLE_LIMIT (250) CHARS before escape;
 *     summary to TELEGRAM_SUMMARY_LIMIT (400) chars.
 *   - Ellipsis is U+2026 (UTF-8: E2 80 A6), NOT three ASCII dots.
 *   - If total message exceeds TELEGRAM_MAX_MESSAGE (3900) chars, hard
 *     truncate and append U+2026.
 *   - disable_web_page_preview=true (links do not generate preview cards).
 *   - Throttle per-bot using CLOCK_MONOTONIC with send_interval (default
 *     1.2s). The throttle measures from the END of the previous send.
 *   - HTTP 200 with {"ok":false} is treated as an error (the API's
 *     description is logged).
 */

#include "isae_monitor/telegram.h"
#include "isae_monitor/departments.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TELEGRAM_API_FMT "https://api.telegram.org/bot%s/sendMessage"

/* ------------------------------------------------------------------ */
/* UTF-8 helpers (truncation by codepoint count)                       */
/* ------------------------------------------------------------------ */

static size_t utf8_codepoint_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  /* invalid lead byte; treat as 1 to make progress */
}

/* Truncate a NUL-terminated UTF-8 string to at most max_codepoints
 * codepoints, NUL-terminating the result. Operates in place. */
static void truncate_chars(char* s, size_t max_codepoints) {
    if (!s) return;
    size_t i = 0;
    size_t cp = 0;
    while (s[i] && cp < max_codepoints) {
        size_t adv = utf8_codepoint_len((unsigned char)s[i]);
        /* Don't walk past the NUL. */
        for (size_t k = 0; k < adv; k++) if (s[i + k] == '\0') { s[i] = '\0'; return; }
        i += adv;
        cp++;
    }
    s[i] = '\0';
}

static size_t codepoint_count(const char* s) {
    if (!s) return 0;
    size_t i = 0, cp = 0;
    while (s[i]) {
        i += utf8_codepoint_len((unsigned char)s[i]);
        cp++;
    }
    return cp;
}

/* ------------------------------------------------------------------ */
/* Client lifecycle                                                   */
/* ------------------------------------------------------------------ */

void telegram_client_init(telegram_client_t* tg, const char* bot_token,
                          const http_client_config_t* http_cfg,
                          double send_interval, bool dry_run) {
    if (!tg) return;
    memset(tg, 0, sizeof(*tg));
    if (bot_token) {
        strncpy(tg->bot_token, bot_token, sizeof(tg->bot_token) - 1);
        tg->bot_token[sizeof(tg->bot_token) - 1] = '\0';
    }
    if (http_cfg) tg->http_cfg = *http_cfg;
    else http_client_config_default(&tg->http_cfg);
    tg->send_interval = send_interval > 0 ? send_interval : 1.2;
    tg->last_send_end = 0.0;
    tg->dry_run = dry_run;
}

void telegram_client_cleanup(telegram_client_t* tg) {
    if (!tg) return;
    memset(tg, 0, sizeof(*tg));
}

/* ------------------------------------------------------------------ */
/* Message formatting                                                 */
/* ------------------------------------------------------------------ */

char* telegram_format_message(const announcement_t* ann, const char* category) {
    if (!ann) return NULL;

    /* Working buffer generously sized to fit all parts before the final
     * MAX_MESSAGE truncation. */
    static const size_t BUF_SIZE = 16384;
    char* out = (char*)malloc(BUF_SIZE);
    if (!out) return NULL;
    out[0] = '\0';

    /* Title: truncate to TITLE_LIMIT chars, then HTML-escape. */
    char title_buf[1024];
    title_buf[0] = '\0';
    if (ann->title) {
        strncpy(title_buf, ann->title, sizeof(title_buf) - 1);
        title_buf[sizeof(title_buf) - 1] = '\0';
        truncate_chars(title_buf, TELEGRAM_TITLE_LIMIT);
    }
    char title_escaped[2048];
    if (html_escape(title_buf, title_escaped, sizeof(title_escaped)) != ISAE_OK) {
        title_escaped[0] = '\0';
    }

    /* Summary: truncate to SUMMARY_LIMIT chars, then HTML-escape. */
    char summary_buf[1024];
    summary_buf[0] = '\0';
    if (ann->summary && ann->summary[0]) {
        strncpy(summary_buf, ann->summary, sizeof(summary_buf) - 1);
        summary_buf[sizeof(summary_buf) - 1] = '\0';
        truncate_chars(summary_buf, TELEGRAM_SUMMARY_LIMIT);
    }
    char summary_escaped[2048];
    if (summary_buf[0]) {
        if (html_escape(summary_buf, summary_escaped, sizeof(summary_escaped)) != ISAE_OK) {
            summary_escaped[0] = '\0';
        }
    } else {
        summary_escaped[0] = '\0';
    }

    /* Published: HTML-escape (mostly to neutralize any stray <>&). */
    char published_buf[256];
    published_buf[0] = '\0';
    if (ann->published) {
        strncpy(published_buf, ann->published, sizeof(published_buf) - 1);
        published_buf[sizeof(published_buf) - 1] = '\0';
    }
    char published_escaped[512];
    if (html_escape(published_buf, published_escaped, sizeof(published_escaped)) != ISAE_OK) {
        published_escaped[0] = '\0';
    }

    /* Build the message body. */
    size_t w = 0;
    int n;

    /* Line 1: <b>[Label]</b> <b>Title</b>   (or just <b>Title</b> when no category) */
    if (category && category[0]) {
        const char* label = departments_label_for(category);
        if (label) {
            char label_escaped[128];
            if (html_escape(label, label_escaped, sizeof(label_escaped)) == ISAE_OK) {
                n = snprintf(out + w, BUF_SIZE - w, "<b>[%s]</b> <b>%s</b>", label_escaped, title_escaped);
                if (n < 0) goto fail;
                w += (size_t)n;
            }
        }
    }
    if (w == 0) {
        /* No category, or label lookup failed. */
        n = snprintf(out + w, BUF_SIZE - w, "<b>%s</b>", title_escaped);
        if (n < 0) goto fail;
        w += (size_t)n;
    }

    /* Line 2: <i>published</i> */
    n = snprintf(out + w, BUF_SIZE - w, "\n\n<i>%s</i>", published_escaped);
    if (n < 0) goto fail;
    w += (size_t)n;

    /* Line 3 (optional): summary */
    if (summary_escaped[0]) {
        n = snprintf(out + w, BUF_SIZE - w, "\n\n%s", summary_escaped);
        if (n < 0) goto fail;
        w += (size_t)n;
    }

    /* Line 4 (optional): link (NOT escaped, mirrors Python) */
    if (ann->link && ann->link[0]) {
        n = snprintf(out + w, BUF_SIZE - w, "\n\n%s", ann->link);
        if (n < 0) goto fail;
        w += (size_t)n;
    }

    /* Final truncation by CODEPOINT count. If we exceed MAX_MESSAGE, hard
     * truncate and append U+2026 (E2 80 A6). */
    if (codepoint_count(out) > TELEGRAM_MAX_MESSAGE) {
        truncate_chars(out, TELEGRAM_MAX_MESSAGE);
        /* The truncation leaves a NUL at the cut; append the ellipsis. */
        size_t cur = strlen(out);
        if (cur + 3 < BUF_SIZE) {
            out[cur] = '\xE2';
            out[cur + 1] = '\x80';
            out[cur + 2] = '\xA6';
            out[cur + 3] = '\0';
        }
    }

    /* The Python code does NOT rtrim the final output, but truncating
     * mid-whitespace is rare and we leave the result as-is. */
    return out;

fail:
    free(out);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Send                                                               */
/* ------------------------------------------------------------------ */

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_seconds(double s) {
    if (s <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

isae_error_t telegram_send_message(telegram_client_t* tg,
                                   const char* chat_id,
                                   const char* text,
                                   long* response_status_out) {
    if (response_status_out) *response_status_out = 0;
    if (!tg || !chat_id || !text) return ISAE_ERR_INVALID_PARAM;
    if (!tg->bot_token[0]) return ISAE_ERR_CONFIG;
    if (!chat_id[0]) return ISAE_ERR_CONFIG;

    /* Throttle: wait until at least send_interval has elapsed since the
     * end of the previous send. */
    if (tg->last_send_end > 0.0) {
        double elapsed = monotonic_seconds() - tg->last_send_end;
        double wait = tg->send_interval - elapsed;
        if (wait > 0) sleep_seconds(wait);
    }

    /* Dry-run: print the message indented and return. */
    if (tg->dry_run) {
        printf("    [dry-run] -> %s\n", chat_id);
        const char* p = text;
        const char* nl;
        while ((nl = strchr(p, '\n'))) {
            printf("      %.*s\n", (int)(nl - p), p);
            p = nl + 1;
        }
        if (*p) printf("      %s\n", p);
        tg->last_send_end = monotonic_seconds();
        return ISAE_OK;
    }

    char url[512];
    int n = snprintf(url, sizeof(url), TELEGRAM_API_FMT, tg->bot_token);
    if (n < 0 || (size_t)n >= sizeof(url)) return ISAE_ERR_CONFIG;

    cJSON* root = cJSON_CreateObject();
    if (!root) return ISAE_ERR_MEMORY;
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "parse_mode", "HTML");
    cJSON_AddBoolToObject(root, "disable_web_page_preview", true);

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ISAE_ERR_MEMORY;

    http_response_t resp;
    http_response_init(&resp);
    isae_error_t err = http_post_json(url, body, &resp, &tg->http_cfg, NULL);
    free(body);
    if (response_status_out) *response_status_out = resp.status_code;

    if (err == ISAE_OK) {
        /* Check {"ok":true} -- an HTTP 200 with ok=false is still an error. */
        cJSON* parsed = cJSON_Parse(resp.body ? resp.body : "");
        if (parsed) {
            cJSON* ok = cJSON_GetObjectItem(parsed, "ok");
            if (ok && cJSON_IsBool(ok) && !cJSON_IsTrue(ok)) {
                cJSON* desc = cJSON_GetObjectItem(parsed, "description");
                fprintf(stderr, "Telegram API error: %s\n",
                        (desc && cJSON_IsString(desc)) ? desc->valuestring : "(no description)");
                err = ISAE_ERR_HTTP;
            }
            cJSON_Delete(parsed);
        }
    } else {
        fprintf(stderr, "Telegram send to %s failed (HTTP %ld): %s\n",
                chat_id, resp.status_code, resp.body ? resp.body : "(no body)");
    }

    http_response_cleanup(&resp);
    tg->last_send_end = monotonic_seconds();
    return err;
}
