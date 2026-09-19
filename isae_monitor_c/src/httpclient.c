/**
 * @file httpclient.c
 * @brief HTTP client using libcurl with retry logic
 */

#include "isae_monitor/httpclient.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

const char* HTTP_USER_AGENT = "ISAEMonitor/2.0 (+https://github.com/E-Vex/monreqAI-CNAM-Liban)";

/* Retryable status codes */
static const long RETRYABLE_STATUS[] = {408, 425, 429, 500, 502, 503, 504};
static const size_t RETRYABLE_COUNT = sizeof(RETRYABLE_STATUS) / sizeof(RETRYABLE_STATUS[0]);

void http_response_init(http_response_t* response) {
    if (!response) return;
    memset(response, 0, sizeof(http_response_t));
}

void http_response_cleanup(http_response_t* response) {
    if (!response) return;
    free(response->body);
    free(response->content_type);
    response->body = NULL;
    response->content_type = NULL;
}

bool http_error_is_retryable(const http_error_t* error) {
    if (!error) return false;
    if (error->status_code == 0) return true;  /* Network error */
    
    for (size_t i = 0; i < RETRYABLE_COUNT; i++) {
        if (error->status_code == RETRYABLE_STATUS[i]) return true;
    }
    return false;
}

/* Write callback for curl */
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* response = (http_response_t*)userp;
    
    char* ptr = realloc(response->body, response->body_size + realsize + 1);
    if (!ptr) return 0;
    
    response->body = ptr;
    memcpy(&(response->body[response->body_size]), contents, realsize);
    response->body_size += realsize;
    response->body[response->body_size] = '\0';
    
    return realsize;
}

static double calculate_sleep(int32_t attempt, const char* retry_after) {
    if (retry_after && *retry_after) {
        double seconds = atof(retry_after);
        if (seconds > 0 && seconds <= 60.0) {
            return seconds;
        }
    }
    
    /* Exponential backoff with jitter: 2^attempt * (0.5 to 1.0), capped at 30s */
    double base = pow(2.0, (double)attempt);
    double jitter = 0.5 + ((double)rand() / RAND_MAX) * 0.5;
    double result = base * jitter;
    return result < 30.0 ? result : 30.0;
}

isae_error_t http_request(
    const char* url,
    const char* method,
    const char* headers[],
    const char* json_body,
    int32_t timeout_seconds,
    int32_t max_retries,
    http_response_t* response
) {
    if (!url || !method || !response) return ISAE_ERR_INVALID_ARG;
    
    CURL* curl = NULL;
    struct curl_slist* header_list = NULL;
    isae_error_t result = ISAE_OK;
    
    http_response_init(response);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    for (int32_t attempt = 0; attempt < max_retries; attempt++) {
        curl = curl_easy_init();
        if (!curl) {
            result = ISAE_ERR_HTTP;
            goto cleanup;
        }
        
        /* Set URL */
        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        /* Set method */
        if (strcmp(method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (strcmp(method, "GET") != 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        }
        
        /* Set headers */
        header_list = curl_slist_append(header_list, 
            "User-Agent: ISAEMonitor/2.0 (+https://github.com/E-Vex/monreqAI-CNAM-Liban)");
        
        if (headers) {
            for (size_t i = 0; headers[i]; i++) {
                header_list = curl_slist_append(header_list, headers[i]);
            }
        }
        
        if (json_body) {
            header_list = curl_slist_append(header_list, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
        }
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        
        /* Set response writer */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response);
        
        /* Timeout */
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_seconds);
        
        /* SSL options - disable verification for problematic servers */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        /* Follow redirects */
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        /* Perform request */
        CURLcode curl_result = curl_easy_perform(curl);
        
        /* Get status code */
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        response->status_code = status_code;
        
        if (curl_result == CURLE_OK) {
            /* Success */
            result = ISAE_OK;
            goto cleanup;
        }
        
        /* Error handling */
        if (curl_result != CURLE_HTTP_RETURNED_ERROR || 
            !http_error_is_retryable(&(http_error_t){.status_code = status_code})) {
            /* Non-retryable error */
            if (attempt == max_retries - 1) {
                result = ISAE_ERR_HTTP;
                goto cleanup;
            }
        }
        
        /* Clean up for retry */
        curl_easy_cleanup(curl);
        curl = NULL;
        curl_slist_free_all(header_list);
        header_list = NULL;
        http_response_cleanup(response);
        http_response_init(response);
        
        /* Sleep before retry */
        if (attempt < max_retries - 1) {
            double sleep_time = calculate_sleep(attempt, NULL);
            struct timespec ts;
            ts.tv_sec = (time_t)sleep_time;
            ts.tv_nsec = (long)((sleep_time - ts.tv_sec) * 1e9);
            nanosleep(&ts, NULL);
        }
    }
    
cleanup:
    if (header_list) curl_slist_free_all(header_list);
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return result;
}

isae_error_t http_request_json(
    const char* url,
    const char* method,
    const char* headers[],
    const char* json_body,
    int32_t timeout_seconds,
    int32_t max_retries,
    char** json_output
) {
    if (!url || !json_output) return ISAE_ERR_INVALID_ARG;
    
    http_response_t response;
    isae_error_t result = http_request(url, method, headers, json_body,
                                        timeout_seconds, max_retries, &response);
    
    if (result != ISAE_OK) {
        http_response_cleanup(&response);
        return result;
    }
    
    if (!response.body) {
        http_response_cleanup(&response);
        return ISAE_ERR_HTTP;
    }
    
    *json_output = response.body;
    response.body = NULL;  /* Transfer ownership */
    http_response_cleanup(&response);
    
    return ISAE_OK;
}
