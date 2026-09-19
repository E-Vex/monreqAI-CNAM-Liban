/**
 * @file providers.c
 * @brief AI provider implementations (Gemini, OpenRouter)
 * 
 * Python equivalent: requests library calls to AI APIs
 * C Implementation: libcurl HTTP client with JSON parsing
 */

#include "isae_monitor/providers.h"
#include "isae_monitor/httpclient.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Maximum response size for AI API calls */
#define MAX_AI_RESPONSE_SIZE (64 * 1024)

/* Trim whitespace from string */
static char* trim_whitespace(char* str) {
    if (!str) return NULL;
    
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

/* Extract classification from JSON response - simple parser */
static isae_error_t extract_classification_json(const char* json, char* department_out, size_t out_size) {
    if (!json || !department_out || out_size == 0) return ISAE_ERR_INVALID_PARAM;
    
    /* Look for "department" or "classification" key */
    const char* keys[] = {"department", "classification", "result", NULL};
    const char* found_value = NULL;
    
    for (int i = 0; keys[i] != NULL; i++) {
        char search_pattern[64];
        snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", keys[i]);
        
        const char* key_pos = strstr(json, search_pattern);
        if (key_pos) {
            /* Find colon after key */
            const char* colon = strchr(key_pos, ':');
            if (colon) {
                /* Skip whitespace */
                const char* value_start = colon + 1;
                while (*value_start && isspace((unsigned char)*value_start)) value_start++;
                
                if (*value_start == '"') {
                    /* String value */
                    value_start++;
                    const char* value_end = value_start;
                    while (*value_end && *value_end != '"') value_end++;
                    
                    size_t value_len = value_end - value_start;
                    if (value_len < out_size && value_len > 0) {
                        strncpy(department_out, value_start, value_len);
                        department_out[value_len] = '\0';
                        found_value = department_out;
                        break;
                    }
                } else {
                    /* Non-string value - skip */
                    continue;
                }
            }
        }
    }
    
    if (!found_value) {
        /* Try to find any quoted string that looks like a department name */
        const char* quote = strchr(json, '"');
        while (quote) {
            quote++;
            const char* end_quote = strchr(quote, '"');
            if (end_quote) {
                size_t len = end_quote - quote;
                if (len > 2 && len < out_size) {
                    /* Check if it looks like a department name */
                    strncpy(department_out, quote, len);
                    department_out[len] = '\0';
                    
                    /* Basic validation: contains letters, reasonable length */
                    if (strlen(department_out) >= 3 && strlen(department_out) <= 50) {
                        return ISAE_OK;
                    }
                }
                quote = end_quote + 1;
            } else {
                break;
            }
        }
        return ISAE_ERR_PARSE;
    }
    
    return ISAE_OK;
}

isae_error_t ai_classify_gemini(const char* api_key, const char* title, 
                                const char* summary, char* department_out, size_t out_size) {
    if (!api_key || !title || !department_out || out_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    isae_error_t ret = ISAE_OK;
    char* url = NULL;
    char* payload = NULL;
    HttpResponse response = {0};
    
    /* Build combined text */
    char combined_text[2048];
    snprintf(combined_text, sizeof(combined_text), 
             "Title: %s\nSummary: %s",
             title ? title : "",
             summary ? summary : "");
    
    /* Build URL */
    url = malloc(512);
    if (!url) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    snprintf(url, 512, 
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-lite:generateContent?key=%s",
             api_key);
    
    /* Build JSON payload */
    /* Escape text for JSON */
    size_t text_len = strlen(combined_text);
    size_t escaped_size = text_len * 2 + 256;
    payload = malloc(escaped_size);
    if (!payload) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    /* Simple JSON escaping */
    char* escaped_text = malloc(text_len * 2 + 1);
    if (!escaped_text) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < text_len && j < text_len * 2 - 1; i++) {
        char c = combined_text[i];
        if (c == '"' || c == '\\') {
            escaped_text[j++] = '\\';
        }
        escaped_text[j++] = c;
    }
    escaped_text[j] = '\0';
    
    snprintf(payload, escaped_size,
             "{\"contents\":[{\"parts\":[{\"text\":\"Classify this academic announcement into exactly ONE of these departments: Informatique, Mathematiques, Physique, Chimie, Biologie, Sciences de l'Ingenieur, Sciences Economiques et de Gestion, Langues Etrangeres, Sport, Autre.\\n\\n%s\\n\\nRespond with ONLY the exact department name from the list above.\"}]}],\"generationConfig\":{\"temperature\":0.1,\"maxOutputTokens\":50}}",
             escaped_text);
    
    free(escaped_text);
    
    /* Set headers */
    const char* headers[] = {
        "Content-Type: application/json",
        NULL
    };
    
    /* Make HTTP POST request */
    ret = http_post(url, payload, strlen(payload), headers, &response, 30);
    if (ret != ISAE_OK) {
        goto cleanup;
    }
    
    /* Ensure null termination */
    if (response.body && response.body_size > 0) {
        if (response.body[response.body_size - 1] != '\0') {
            char* new_body = realloc(response.body, response.body_size + 1);
            if (new_body) {
                response.body = new_body;
                response.body[response.body_size] = '\0';
            }
        }
    }
    
    /* Parse response */
    if (!response.body || response.body_size == 0) {
        ret = ISAE_ERR_PARSE;
        goto cleanup;
    }
    
    ret = extract_classification_json(response.body, department_out, out_size);
    
cleanup:
    free(url);
    free(payload);
    http_response_free(&response);
    return ret;
}

isae_error_t ai_classify_openrouter(const char* api_key, const char* title,
                                    const char* summary, char* department_out, size_t out_size) {
    if (!api_key || !title || !department_out || out_size == 0) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    isae_error_t ret = ISAE_OK;
    char* url = NULL;
    char* payload = NULL;
    HttpResponse response = {0};
    
    /* Build combined text */
    char combined_text[2048];
    snprintf(combined_text, sizeof(combined_text), 
             "Title: %s\nSummary: %s",
             title ? title : "",
             summary ? summary : "");
    
    /* Build URL */
    url = strdup("https://openrouter.ai/api/v1/chat/completions");
    if (!url) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    /* Build JSON payload */
    size_t text_len = strlen(combined_text);
    size_t escaped_size = text_len * 2 + 512;
    payload = malloc(escaped_size);
    if (!payload) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    /* Simple JSON escaping */
    char* escaped_text = malloc(text_len * 2 + 1);
    if (!escaped_text) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < text_len && j < text_len * 2 - 1; i++) {
        char c = combined_text[i];
        if (c == '"' || c == '\\' || c == '\n') {
            escaped_text[j++] = '\\';
            if (c == '\n') {
                escaped_text[j++] = 'n';
            } else {
                escaped_text[j++] = c;
                continue;
            }
        }
        escaped_text[j++] = c;
    }
    escaped_text[j] = '\0';
    
    snprintf(payload, escaped_size,
             "{\"model\":\"meta-llama/llama-3-8b-instruct\",\"messages\":[{\"role\":\"system\",\"content\":\"You are a classification assistant for academic announcements. Respond with ONLY the exact department name from this list: Informatique, Mathematiques, Physique, Chimie, Biologie, Sciences de l'Ingenieur, Sciences Economiques et de Gestion, Langues Etrangeres, Sport, Autre.\"},{\"role\":\"user\",\"content\":\"%s\\n\\nDepartment:\"}],\"max_tokens\":50,\"temperature\":0.1}",
             escaped_text);
    
    free(escaped_text);
    
    /* Set headers */
    const char* headers_arr[4];
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    headers_arr[0] = "Content-Type: application/json";
    headers_arr[1] = auth_header;
    headers_arr[2] = "HTTP-Referer: https://isae-monitor.local";
    headers_arr[3] = NULL;
    
    /* Make HTTP POST request */
    ret = http_post(url, payload, strlen(payload), headers_arr, &response, 30);
    if (ret != ISAE_OK) {
        goto cleanup;
    }
    
    /* Ensure null termination */
    if (response.body && response.body_size > 0) {
        if (response.body[response.body_size - 1] != '\0') {
            char* new_body = realloc(response.body, response.body_size + 1);
            if (new_body) {
                response.body = new_body;
                response.body[response.body_size] = '\0';
            }
        }
    }
    
    /* Parse response - look for content in choices array */
    if (!response.body || response.body_size == 0) {
        ret = ISAE_ERR_PARSE;
        goto cleanup;
    }
    
    /* Look for "content" field in response */
    const char* content_marker = strstr(response.body, "\"content\"");
    if (content_marker) {
        const char* colon = strchr(content_marker, ':');
        if (colon) {
            const char* value_start = colon + 1;
            while (*value_start && isspace((unsigned char)*value_start)) value_start++;
            
            if (*value_start == '"') {
                value_start++;
                const char* value_end = value_start;
                while (*value_end && *value_end != '"') {
                    if (*value_end == '\\' && *(value_end + 1)) {
                        value_end += 2;
                    } else {
                        value_end++;
                    }
                }
                
                size_t value_len = value_end - value_start;
                if (value_len > 0 && value_len < out_size) {
                    strncpy(department_out, value_start, value_len);
                    department_out[value_len] = '\0';
                    
                    /* Trim whitespace */
                    char* trimmed = trim_whitespace(department_out);
                    if (trimmed != department_out) {
                        memmove(department_out, trimmed, strlen(trimmed) + 1);
                    }
                    
                    ret = ISAE_OK;
                    goto cleanup;
                }
            }
        }
    }
    
    /* Fallback to generic extraction */
    ret = extract_classification_json(response.body, department_out, out_size);
    
cleanup:
    free(url);
    free(payload);
    http_response_free(&response);
    return ret;
}
