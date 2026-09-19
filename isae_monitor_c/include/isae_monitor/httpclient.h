#ifndef ISAE_MONITOR_HTTPCLIENT_H
#define ISAE_MONITOR_HTTPCLIENT_H

#include "common.h"

/* HTTP response structure */
typedef struct {
    char* body;
    size_t body_size;
    long status_code;
    char* content_type;
} http_response_t;

/* Initialize HTTP response */
void http_response_init(http_response_t* resp);

/* Free HTTP response resources */
void http_response_cleanup(http_response_t* resp);

/* HTTP client configuration */
typedef struct {
    int timeout_seconds;
    const char* user_agent;
    bool follow_redirects;
    int max_redirects;
} http_client_config_t;

/* Default HTTP client config */
void http_client_config_default(http_client_config_t* config);

/* Perform HTTP GET request */
isae_error_t http_get(const char* url, http_response_t* response, 
                      const http_client_config_t* config);

/* Perform HTTP POST request with JSON body */
isae_error_t http_post_json(const char* url, const char* json_body,
                            http_response_t* response,
                            const http_client_config_t* config,
                            const char* api_key_header);

#endif /* ISAE_MONITOR_HTTPCLIENT_H */
