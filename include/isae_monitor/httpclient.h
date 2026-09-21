#ifndef ISAE_MONITOR_HTTPCLIENT_H
#define ISAE_MONITOR_HTTPCLIENT_H

#include "common.h"

/* HTTP response.
 *
 * body is always a NUL-terminated heap string (NULL only when body_size==0).
 * body is truncated to HTTP_MAX_BODY bytes to bound memory usage on hostile
 * responses (mirrors Python's truncation of HTTP error bodies to 800 chars).
 *
 * status_code is the HTTP status code (0 if the request never reached a
 * server, e.g. DNS failure / connection refused / timeout). Callers that
 * need to distinguish retryable from non-retryable failures should use
 * http_status_is_retryable().
 */
typedef struct {
    char* body;
    size_t body_size;
    long status_code;
    char* content_type;
    /* If non-NULL, a copy of the Retry-After header value (raw string).
     * NULL when the server did not send one. */
    char* retry_after;
} http_response_t;

void http_response_init(http_response_t* resp);
void http_response_cleanup(http_response_t* resp);

/* HTTP client configuration. */
typedef struct {
    int timeout_seconds;        /* per-attempt timeout */
    int max_retries;            /* total attempts including the first */
    const char* user_agent;
    bool follow_redirects;
    int max_redirects;
    bool disable_tls_verify;     /* mirrors Python's verify=False */
} http_client_config_t;

/* Default config: 20s timeout, 3 retries, TLS verification disabled
 * (matches the Python reference for the ISAE feed server's flaky TLS). */
void http_client_config_default(http_client_config_t* config);

/* Status codes that warrant a retry. Matches Python's RETRYABLE set:
 *   408 Request Timeout
 *   425 Too Early
 *   429 Too Many Requests
 *   500 Internal Server Error
 *   502 Bad Gateway
 *   503 Service Unavailable
 *   504 Gateway Timeout
 * A status_code of 0 (network failure) is also retryable. */
bool http_status_is_retryable(long status_code);

/* Compute the sleep duration (in milliseconds) for a given attempt index
 * (0-based) and an optional Retry-After header value. Mirrors Python's
 * _sleep_for():
 *   - Retry-After header honored, capped at HTTP_RETRY_AFTER_CAP_SECONDS (60)
 *   - exponential backoff: min(2^attempt, 30) seconds
 *   - multiplied by jitter in [0.5, 1.0)
 * The jitter is derived from a deterministic seed for reproducibility. */
long http_backoff_millis(int attempt, const char* retry_after);

/* Perform an HTTP GET. Retries on retryable failures (network errors
 * and {408,425,429,500,502,503,504}) up to config->max_retries times
 * with exponential backoff + jitter. The Retry-After header is honored. */
isae_error_t http_get(const char* url, http_response_t* response,
                      const http_client_config_t* config);

/* Perform an HTTP POST with a JSON body. Same retry semantics as http_get.
 * extra_headers is a NULL-terminated array of pre-formatted header strings
 * (e.g. "Authorization: Bearer xxx"); each is appended to the request. */
isae_error_t http_post_json(const char* url, const char* json_body,
                            http_response_t* response,
                            const http_client_config_t* config,
                            const char* const* extra_headers);

#endif /* ISAE_MONITOR_HTTPCLIENT_H */
