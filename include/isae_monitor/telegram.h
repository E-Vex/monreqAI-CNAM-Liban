#ifndef ISAE_MONITOR_TELEGRAM_H
#define ISAE_MONITOR_TELEGRAM_H

#include "common.h"
#include "models.h"
#include "httpclient.h"

/* Telegram client. One instance per run; throttles itself per-bot using
 * a CLOCK_MONOTONIC timestamp so two consecutive sends are at least
 * settings->send_interval seconds apart (default 1.2s, matches Python). */
typedef struct {
    char bot_token[MAX_API_KEY_LEN];
    http_client_config_t http_cfg;
    double send_interval;     /* seconds */
    /* CLOCK_MONOTONIC timestamp of the end of the previous send, or 0.0
     * if no send has happened yet. */
    double last_send_end;
    bool dry_run;
} telegram_client_t;

/* Initialize with defaults. The bot_token is NOT read from environment
 * here -- the caller passes the resolved settings_t in. */
void telegram_client_init(telegram_client_t* tg, const char* bot_token,
                          const http_client_config_t* http_cfg,
                          double send_interval, bool dry_run);
void telegram_client_cleanup(telegram_client_t* tg);

/* Format an announcement as the HTML message Telegram expects.
 *
 * Layout (joined with "\n\n"):
 *   1. <b>[Label]</b> <b>Title</b>      (omit [Label] when category is NULL)
 *   2. <i>published</i>
 *   3. summary                          (omitted if empty)
 *   4. link                             (omitted if empty; not escaped)
 *
 * Title is HTML-escaped and truncated to TELEGRAM_TITLE_LIMIT chars
 * (codepoint count, not bytes) BEFORE escaping, mirroring Python's
 * html.escape(_truncate(title, TITLE_LIMIT)).
 * Summary is HTML-escaped and truncated to TELEGRAM_SUMMARY_LIMIT chars.
 *
 * If the total message exceeds TELEGRAM_MAX_MESSAGE chars, it is hard-
 * truncated and a single U+2026 ellipsis is appended (Python's behavior).
 *
 * Caller owns the returned string; free() it. Returns NULL on OOM. */
char* telegram_format_message(const announcement_t* ann, const char* category);

/* Send a message to a chat. Returns ISAE_OK on success.
 * On HTTP failure, returns ISAE_ERR_HTTP with the status code in
 * response_status_out (if non-NULL). On API-level failure (HTTP 200 but
 * {"ok":false}), returns ISAE_ERR_HTTP. The bot_token must be non-empty
 * and chat_id must be non-empty; otherwise returns ISAE_ERR_CONFIG.
 *
 * Honors the throttle: if less than send_interval seconds have passed
 * since the end of the previous send, sleeps until the interval has
 * elapsed. The CLOCK_MONOTONIC clock is used so wall-clock jumps do not
 * affect the throttle. */
isae_error_t telegram_send_message(telegram_client_t* tg,
                                   const char* chat_id,
                                   const char* text,
                                   long* response_status_out);

#endif /* ISAE_MONITOR_TELEGRAM_H */
