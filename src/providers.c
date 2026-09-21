/**
 * ISAE Monitor - AI Providers Implementation
 * 
 * Maps Python: providers.py -> C: providers.c
 * 
 * Implements Google Gemini and OpenRouter API calls for classification.
 */

#include "isae_monitor/providers.h"
#include "isae_monitor/httpclient.h"
#include <cJSON.h>
#include <ctype.h>

void ai_response_init(ai_response_t* resp) {
    if (!resp) return;
    
    resp->category[0] = '\0';
    resp->confidence = 0;
    resp->raw_response[0] = '\0';
    resp->success = false;
}

void ai_response_cleanup(ai_response_t* resp) {
    /* Nothing to free - fixed size arrays */
    (void)resp;
}

/**
 * Parse category from AI response text.
 * Expected format: "CATEGORY: <name>" or just the category name.
 */
static isae_error_t parse_ai_category(const char* response_text, 
                                      char* category_out, size_t category_size,
                                      int* confidence_out) {
    if (!response_text || !category_out) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Default values */
    strncpy(category_out, "GENERAL", category_size - 1);
    category_out[category_size - 1] = '\0';
    *confidence_out = 50;
    
    /* Look for common category names in response */
    const char* categories[] = {
        "JOBS", "EVENTS", "COURSES", "RESEARCH", "ADMIN",
        "EMPLOI", "STAGE", "CONFERENCE", "SEMINAR",
        NULL
    };
    
    /* Convert response to uppercase for matching */
    char upper_resp[1024];
    size_t len = strlen(response_text);
    if (len >= sizeof(upper_resp)) {
        len = sizeof(upper_resp) - 1;
    }
    
    for (size_t i = 0; i < len; i++) {
        upper_resp[i] = (char)toupper((unsigned char)response_text[i]);
    }
    upper_resp[len] = '\0';
    
    /* Search for category keywords */
    for (int i = 0; categories[i] != NULL; i++) {
        if (strstr(upper_resp, categories[i]) != NULL) {
            /* Map French categories to English equivalents */
            if (strcmp(categories[i], "EMPLOI") == 0 || 
                strcmp(categories[i], "STAGE") == 0) {
                strncpy(category_out, "JOBS", category_size - 1);
            } else if (strcmp(categories[i], "CONFERENCE") == 0 ||
                       strcmp(categories[i], "SEMINAR") == 0) {
                strncpy(category_out, "EVENTS", category_size - 1);
            } else {
                strncpy(category_out, categories[i], category_size - 1);
            }
            category_out[category_size - 1] = '\0';
            *confidence_out = 80;
            return ISAE_OK;
        }
    }
    
    /* Try to extract first word that looks like a category */
    const char* p = response_text;
    while (*p && *p != ':' && *p != '\n') {
        p++;
    }
    
    if (*p == ':') {
        p++;
        while (*p && (*p == ' ' || *p == '\t')) {
            p++;
        }
        
        /* Extract word */
        char word[64];
        size_t wi = 0;
        while (*p && *p != ' ' && *p != '\n' && wi < sizeof(word) - 1) {
            word[wi++] = (char)toupper((unsigned char)*p++);
        }
        word[wi] = '\0';
        
        if (wi > 0) {
            strncpy(category_out, word, category_size - 1);
            category_out[category_size - 1] = '\0';
            *confidence_out = 70;
        }
    }
    
    return ISAE_OK;
}

isae_error_t gemini_classify(const announcement_t* ann,
                             const char* api_key,
                             ai_response_t* response,
                             int timeout_seconds) {
    if (!ann || !api_key || !response) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    ai_response_init(response);
    
    /* Build prompt */
    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
             "Classify this school announcement into one of these categories: "
             "JOBS, EVENTS, COURSES, RESEARCH, ADMIN, GENERAL. "
             "Respond with ONLY the category name.\n\n"
             "Title: %s\n"
             "Summary: %s\n"
             "Department: %s\n\n"
             "Category:",
             ann->title ? ann->title : "",
             ann->summary ? ann->summary : "",
             ann->department_key ? ann->department_key : "");
    
    /* Build JSON request */
    cJSON* root = cJSON_CreateObject();
    cJSON* contents = cJSON_CreateArray();
    cJSON* part = cJSON_CreateObject();
    cJSON_AddItemToObject(part, "text", cJSON_CreateString(prompt));
    cJSON_AddItemToArray(contents, part);
    cJSON* content_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(content_obj, "parts", contents);
    cJSON* contents_arr = cJSON_CreateArray();
    cJSON_AddItemToArray(contents_arr, content_obj);
    cJSON_AddItemToObject(root, "contents", contents_arr);
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ISAE_ERR_MEMORY;
    }
    
    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=%s",
             api_key);
    
    /* Make request */
    http_response_t http_resp;
    http_response_init(&http_resp);
    
    http_client_config_t config;
    http_client_config_default(&config);
    config.timeout_seconds = timeout_seconds;
    
    isae_error_t err = http_post_json(url, json_str, &http_resp, &config, NULL);
    free(json_str);
    
    if (err != ISAE_OK) {
        http_response_cleanup(&http_resp);
        return err;
    }
    
    /* Parse response */
    if (http_resp.body) {
        strncpy(response->raw_response, http_resp.body, sizeof(response->raw_response) - 1);
        
        cJSON* resp_json = cJSON_Parse(http_resp.body);
        if (resp_json) {
            cJSON* candidates = cJSON_GetObjectItem(resp_json, "candidates");
            if (candidates && cJSON_IsArray(candidates) && 
                cJSON_GetArraySize(candidates) > 0) {
                cJSON* first = cJSON_GetArrayItem(candidates, 0);
                cJSON* content = cJSON_GetObjectItem(first, "content");
                if (content) {
                    cJSON* parts = cJSON_GetObjectItem(content, "parts");
                    if (parts && cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
                        cJSON* text_item = cJSON_GetArrayItem(parts, 0);
                        cJSON* text = cJSON_GetObjectItem(text_item, "text");
                        if (text && cJSON_IsString(text)) {
                            parse_ai_category(text->valuestring, 
                                             response->category, sizeof(response->category),
                                             &response->confidence);
                            response->success = true;
                        }
                    }
                }
            }
            cJSON_Delete(resp_json);
        }
    }
    
    http_response_cleanup(&http_resp);
    
    return response->success ? ISAE_OK : ISAE_ERR_CLASSIFICATION;
}

isae_error_t openrouter_classify(const announcement_t* ann,
                                 const char* api_key,
                                 ai_response_t* response,
                                 int timeout_seconds) {
    if (!ann || !api_key || !response) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    ai_response_init(response);
    
    /* Build prompt */
    char system_prompt[] = "You are a classification assistant. Classify school announcements into: JOBS, EVENTS, COURSES, RESEARCH, ADMIN, or GENERAL. Respond with ONLY the category name.";
    char user_prompt[1024];
    snprintf(user_prompt, sizeof(user_prompt),
             "Title: %s\nSummary: %s\nDepartment: %s",
             ann->title ? ann->title : "",
             ann->summary ? ann->summary : "",
             ann->department_key ? ann->department_key : "");
    
    /* Build JSON request */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "meta-llama/llama-3-8b-instruct");
    
    cJSON* messages = cJSON_CreateArray();
    
    cJSON* sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system_prompt);
    cJSON_AddItemToArray(messages, sys_msg);
    
    cJSON* user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_prompt);
    cJSON_AddItemToArray(messages, user_msg);
    
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddNumberToObject(root, "max_tokens", 20);
    cJSON_AddNumberToObject(root, "temperature", 0.1);
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ISAE_ERR_MEMORY;
    }
    
    /* Make request */
    http_response_t http_resp;
    http_response_init(&http_resp);
    
    http_client_config_t config;
    http_client_config_default(&config);
    config.timeout_seconds = timeout_seconds;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    isae_error_t err = http_post_json("https://openrouter.ai/api/v1/chat/completions",
                                       json_str, &http_resp, &config, auth_header);
    free(json_str);
    
    if (err != ISAE_OK) {
        http_response_cleanup(&http_resp);
        return err;
    }
    
    /* Parse response */
    if (http_resp.body) {
        strncpy(response->raw_response, http_resp.body, sizeof(response->raw_response) - 1);
        
        cJSON* resp_json = cJSON_Parse(http_resp.body);
        if (resp_json) {
            cJSON* choices = cJSON_GetObjectItem(resp_json, "choices");
            if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON* first = cJSON_GetArrayItem(choices, 0);
                cJSON* message = cJSON_GetObjectItem(first, "message");
                if (message) {
                    cJSON* content = cJSON_GetObjectItem(message, "content");
                    if (content && cJSON_IsString(content)) {
                        parse_ai_category(content->valuestring,
                                         response->category, sizeof(response->category),
                                         &response->confidence);
                        response->success = true;
                    }
                }
            }
            cJSON_Delete(resp_json);
        }
    }
    
    http_response_cleanup(&http_resp);
    
    return response->success ? ISAE_OK : ISAE_ERR_CLASSIFICATION;
}
