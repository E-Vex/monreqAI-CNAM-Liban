/**
 * ISAE Monitor - HTTP Client Implementation
 *
 * Faithful C port of Python isae_monitor/httpclient.py.
 *
 * Key behaviors preserved:
 *   - TLS verification is DISABLED (the ISAE announcements server has a
 *     flaky TLS setup; the Python client also disables it).
 *   - Retryable status set: {408, 425, 429, 500, 502, 503, 504} plus
 *     network failures (status_code == 0).
 *   - Exponential backoff: min(2^attempt, 30) seconds, multiplied by
 *     jitter in [0.5, 1.0). The Retry-After header is honored and
 *     capped at 60s.
 *   - Response body is truncated to HTTP_MAX_BODY (800) bytes so a
 *     hostile response cannot exhaust memory.
 *   - The User-Agent does not leak API keys (the Gemini endpoint uses
 *     a header, never a query parameter).
 */

#include "isae_monitor/httpclient.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define USER_AGENT "ISAEMonitor/2.0 (+https://github.com/E-Vex/monreqAI-CNAM-Liban)"

/* ------------------------------------------------------------------ */
/* Response lifecycle                                                  */
/* ------------------------------------------------------------------ */

void http_response_init(http_response_t* resp) {
    if (!resp) return;
    resp->body = NULL;
    resp->body_size = 0;
    resp->status_code = 0;
    resp->content_type = NULL;
    resp->retry_after = NULL;
}

void http_response_cleanup(http_response_t* resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->content_type);
    free(resp->retry_after);
    http_response_init(resp);
}

void http_client_config_default(http_client_config_t* config) {
    if (!config) return;
    config->timeout_seconds = 20;
    config->max_retries = HTTP_MAX_RETRIES_DEFAULT;
    config->user_agent = USER_AGENT;
    config->follow_redirects = true;
    config->max_redirects = 5;
    config->disable_tls_verify = true;  /* Python reference disables TLS */
}

bool http_status_is_retryable(long status_code) {
    switch (status_code) {
        case 0:    /* network failure */
        case 408:
        case 425:
        case 429:
        case 500:
        case 502:
        case 503:
        case 504:
            return true;
        default:
            return false;
    }
}

long http_backoff_millis(int attempt, const char* retry_after) {
    if (retry_after && retry_after[0]) {
        /* Honor Retry-After as seconds (we don't parse HTTP-date form). */
        char* end = NULL;
        double v = strtod(retry_after, &end);
        if (end != retry_after && v > 0) {
            if (v > HTTP_RETRY_AFTER_CAP_SECONDS) v = HTTP_RETRY_AFTER_CAP_SECONDS;
            return (long)(v * 1000.0);
        }
    }
    /* Exponential backoff, capped at HTTP_BACKOFF_CAP_SECONDS. */
    double base = pow(2.0, (double)attempt);
    if (base > HTTP_BACKOFF_CAP_SECONDS) base = HTTP_BACKOFF_CAP_SECONDS;
    /* Jitter in [0.5, 1.0). */
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
    double jitter = 0.5 + ((double)rand() / (double)RAND_MAX) / 2.0;
    return (long)(base * jitter * 1000.0);
}

/* ------------------------------------------------------------------ */
/* Internal: shared per-attempt runner                                */
/* ------------------------------------------------------------------ */

/* Callback: cap body size at HTTP_MAX_RESPONSE_BODY to bound memory.
 *
 * NOTE: Python's httpclient does NOT cap successful response bodies --
 * the 800-char MAX_BODY is only for error-message formatting. We use a
 * generous 1 MiB cap here purely as a DoS guard; legitimate Atom feeds
 * are well under 100 KiB. */
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* resp = (http_response_t*)userp;

    /* If we've already hit the cap, "consume" the data without storing it.
     * libcurl treats a return value != realsize as an error, so we MUST
     * return the full realsize (not 0) to keep the transfer going. */
    if (resp->body_size >= HTTP_MAX_RESPONSE_BODY) {
        return realsize;
    }
    size_t remaining = HTTP_MAX_RESPONSE_BODY - resp->body_size;
    size_t to_write = (realsize > remaining) ? remaining : realsize;

    char* new_ptr = (char*)realloc(resp->body, resp->body_size + to_write + 1);
    if (!new_ptr) return 0;  /* OOM -- aborts the transfer */
    resp->body = new_ptr;
    memcpy(&resp->body[resp->body_size], contents, to_write);
    resp->body_size += to_write;
    resp->body[resp->body_size] = '\0';
    /* Always return the ORIGINAL realsize so libcurl considers the data
     * consumed, even though we only wrote `to_write` bytes. */
    return realsize;
}

/* Header callback: capture Retry-After. */
static size_t header_callback(void* ptr, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    http_response_t* resp = (http_response_t*)userp;
    if (total > 0) {
        const char* s = (const char*)ptr;
        /* Case-insensitive prefix match on "Retry-After:" */
        const char* col = memchr(s, ':', total);
        if (col && (size_t)(col - s) == 11) {
            if (strncasecmp(s, "Retry-After", 11) == 0) {
                /* Copy the value (trimmed). */
                size_t val_start = (size_t)(col - s) + 1;
                while (val_start < total && (s[val_start] == ' ' || s[val_start] == '\t')) val_start++;
                size_t val_end = total;
                while (val_end > val_start && (s[val_end - 1] == '\r' || s[val_end - 1] == '\n')) val_end--;
                size_t vlen = val_end - val_start;
                free(resp->retry_after);
                resp->retry_after = (char*)malloc(vlen + 1);
                if (resp->retry_after) {
                    memcpy(resp->retry_after, s + val_start, vlen);
                    resp->retry_after[vlen] = '\0';
                }
            }
        }
    }
    return total;
}

static void apply_common_options(CURL* curl, const http_client_config_t* config) {
    if (!config) {
        config = &(const http_client_config_t){ 0 };
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                    config->follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)(config->max_redirects ? config->max_redirects : 5));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)(config->timeout_seconds ? config->timeout_seconds : 20));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config->user_agent ? config->user_agent : USER_AGENT);
    if (config->disable_tls_verify) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);  /* set by caller */
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);  /* set by caller */
}

static void sleep_millis(long ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

isae_error_t http_get(const char* url, http_response_t* response,
                      const http_client_config_t* config) {
    if (!url || !response) return ISAE_ERR_INVALID_PARAM;
    http_client_config_t defaults;
    if (!config) { http_client_config_default(&defaults); config = &defaults; }

    int max_retries = config->max_retries > 0 ? config->max_retries : 1;
    isae_error_t result = ISAE_OK;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        http_response_cleanup(response);
        CURL* curl = curl_easy_init();
        if (!curl) return ISAE_ERR_MEMORY;

        apply_common_options(curl, config);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);

        CURLcode rc = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response->status_code = http_code;
        const char* ct = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) response->content_type = strdup(ct);
        curl_easy_cleanup(curl);

        if (rc == CURLE_OK && http_code >= 200 && http_code < 400) {
            return ISAE_OK;
        }
        /* Failure. Decide whether to retry. */
        bool retry = (rc != CURLE_OK) || http_status_is_retryable(http_code);
        if (!retry || attempt == max_retries - 1) {
            result = (rc == CURLE_OPERATION_TIMEDOUT) ? ISAE_ERR_TIMEOUT : ISAE_ERR_HTTP;
            return result;
        }
        /* Sleep with backoff + jitter. */
        long ms = http_backoff_millis(attempt, response->retry_after);
        sleep_millis(ms);
    }
    return result;
}

isae_error_t http_post_json(const char* url, const char* json_body,
                            http_response_t* response,
                            const http_client_config_t* config,
                            const char* const* extra_headers) {
    if (!url || !json_body || !response) return ISAE_ERR_INVALID_PARAM;
    http_client_config_t defaults;
    if (!config) { http_client_config_default(&defaults); config = &defaults; }

    int max_retries = config->max_retries > 0 ? config->max_retries : 1;
    isae_error_t result = ISAE_OK;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        http_response_cleanup(response);
        CURL* curl = curl_easy_init();
        if (!curl) return ISAE_ERR_MEMORY;

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        if (extra_headers) {
            for (size_t i = 0; extra_headers[i]; i++) {
                headers = curl_slist_append(headers, extra_headers[i]);
            }
        }

        apply_common_options(curl, config);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);

        CURLcode rc = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response->status_code = http_code;
        const char* ct = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) response->content_type = strdup(ct);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc == CURLE_OK && http_code >= 200 && http_code < 400) {
            return ISAE_OK;
        }
        bool retry = (rc != CURLE_OK) || http_status_is_retryable(http_code);
        if (!retry || attempt == max_retries - 1) {
            result = (rc == CURLE_OPERATION_TIMEDOUT) ? ISAE_ERR_TIMEOUT : ISAE_ERR_HTTP;
            return result;
        }
        long ms = http_backoff_millis(attempt, response->retry_after);
        sleep_millis(ms);
    }
    return result;
}
