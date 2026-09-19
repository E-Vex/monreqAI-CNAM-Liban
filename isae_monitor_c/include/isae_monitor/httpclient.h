/**
 * @file httpclient.h
 * @brief HTTP client using libcurl with retry logic
 */

#ifndef ISAE_MONITOR_HTTPCLIENT_H
#define ISAE_MONITOR_HTTPCLIENT_H

#include "common.h"

/* HTTP response structure */
typedef struct {
    char* body;           /* Response body (null-terminated) */
    size_t body_size;     /* Size of body in bytes */
    long status_code;     /* HTTP status code */
    char* content_type;   /* Content-Type header value */
} http_response_t;

/* Initialize response structure */
void http_response_init(http_response_t* response);

/* Free response resources */
void http_response_cleanup(http_response_t* response);

/* HTTP error structure */
typedef struct {
    isae_error_t code;
    long status_code;     /* 0 if network error */
    char message[256];
    char body[800];       /* Error response body snippet */
} http_error_t;

/* Check if error is retryable */
bool http_error_is_retryable(const http_error_t* error);

/* Perform HTTP request with retries and exponential backoff */
isae_error_t http_request(
    const char* url,
    const char* method,           /* "GET" or "POST" */
    const char* headers[],        /* NULL-terminated array of "Header: Value" */
    const char* json_body,        /* NULL for GET requests */
    int32_t timeout_seconds,
    int32_t max_retries,
    http_response_t* response
);

/* Perform HTTP request and parse JSON response */
isae_error_t http_request_json(
    const char* url,
    const char* method,
    const char* headers[],
    const char* json_body,
    int32_t timeout_seconds,
    int32_t max_retries,
    char** json_output          /* Caller must free */
);

/* User agent string */
extern const char* HTTP_USER_AGENT;

#endif /* ISAE_MONITOR_HTTPCLIENT_H */
