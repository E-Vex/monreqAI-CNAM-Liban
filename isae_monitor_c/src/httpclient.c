/**
 * ISAE Monitor - HTTP Client Implementation
 * 
 * Maps Python: httpclient.py -> C: httpclient.c
 * 
 * Wraps libcurl for HTTP requests with retry logic.
 */

#include "isae_monitor/httpclient.h"
#include <curl/curl.h>

/* Write callback for collecting response body */
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    http_response_t* resp = (http_response_t*)userp;
    
    /* Reallocate buffer */
    char* new_ptr = realloc(resp->body, resp->body_size + realsize + 1);
    if (!new_ptr) {
        return 0;  /* Out of memory */
    }
    
    resp->body = new_ptr;
    memcpy(&(resp->body[resp->body_size]), contents, realsize);
    resp->body_size += realsize;
    resp->body[resp->body_size] = '\0';
    
    return realsize;
}

void http_response_init(http_response_t* resp) {
    if (!resp) return;
    
    resp->body = NULL;
    resp->body_size = 0;
    resp->status_code = 0;
    resp->content_type = NULL;
}

void http_response_cleanup(http_response_t* resp) {
    if (!resp) return;
    
    free(resp->body);
    free(resp->content_type);
    http_response_init(resp);
}

void http_client_config_default(http_client_config_t* config) {
    if (!config) return;
    
    config->timeout_seconds = 30;
    config->user_agent = "ISAE-Monitor-C/1.0";
    config->follow_redirects = true;
    config->max_redirects = 5;
}

isae_error_t http_get(const char* url, http_response_t* response,
                      const http_client_config_t* config) {
    if (!url || !response) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Initialize libcurl */
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ISAE_ERR_MEMORY;
    }
    
    isae_error_t result = ISAE_OK;
    
    /* Set options */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config ? config->follow_redirects : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config ? config->max_redirects : 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config ? config->timeout_seconds : 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, 
                     config && config->user_agent ? config->user_agent : "ISAE-Monitor-C/1.0");
    
    /* Write callback */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response);
    
    /* Perform request */
    CURLcode curl_res = curl_easy_perform(curl);
    
    if (curl_res != CURLE_OK) {
        fprintf(stderr, "HTTP GET failed for %s: %s\n", url, curl_easy_strerror(curl_res));
        
        if (curl_res == CURLE_OPERATION_TIMEDOUT) {
            result = ISAE_ERR_TIMEOUT;
        } else {
            result = ISAE_ERR_HTTP;
        }
        goto cleanup;
    }
    
    /* Get status code */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response->status_code = http_code;
    
    /* Check for HTTP errors */
    if (http_code < 200 || http_code >= 400) {
        fprintf(stderr, "HTTP error %ld for %s\n", http_code, url);
        result = ISAE_ERR_HTTP;
        goto cleanup;
    }
    
cleanup:
    curl_easy_cleanup(curl);
    return result;
}

isae_error_t http_post_json(const char* url, const char* json_body,
                            http_response_t* response,
                            const http_client_config_t* config,
                            const char* api_key_header) {
    if (!url || !json_body || !response) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Initialize libcurl */
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ISAE_ERR_MEMORY;
    }
    
    isae_error_t result = ISAE_OK;
    
    /* Set headers */
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    
    if (api_key_header) {
        headers = curl_slist_append(headers, api_key_header);
    }
    
    /* Set options */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config ? config->follow_redirects : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config ? config->max_redirects : 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config ? config->timeout_seconds : 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     config && config->user_agent ? config->user_agent : "ISAE-Monitor-C/1.0");
    
    /* Write callback */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response);
    
    /* Perform request */
    CURLcode curl_res = curl_easy_perform(curl);
    
    if (curl_res != CURLE_OK) {
        fprintf(stderr, "HTTP POST failed for %s: %s\n", url, curl_easy_strerror(curl_res));
        
        if (curl_res == CURLE_OPERATION_TIMEDOUT) {
            result = ISAE_ERR_TIMEOUT;
        } else {
            result = ISAE_ERR_HTTP;
        }
        goto cleanup;
    }
    
    /* Get status code */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    response->status_code = http_code;
    
    /* Check for HTTP errors */
    if (http_code < 200 || http_code >= 400) {
        fprintf(stderr, "HTTP error %ld for POST %s\n", http_code, url);
        result = ISAE_ERR_HTTP;
        goto cleanup;
    }
    
cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
