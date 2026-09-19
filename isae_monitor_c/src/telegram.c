/**
 * @file telegram.c
 * @brief Telegram Bot API client for sending notifications
 * 
 * Python equivalent: requests calls to Telegram Bot API
 * C Implementation: libcurl HTTP client with JSON construction
 */

#include "isae_monitor/telegram.h"
#include "isae_monitor/httpclient.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Escape special characters for MarkdownV2 */
static char* escape_markdown_v2(const char* text) {
    if (!text) return NULL;
    
    size_t len = strlen(text);
    /* Worst case: every char needs escaping */
    char* escaped = malloc(len * 2 + 1);
    if (!escaped) return NULL;
    
    size_t j = 0;
    const char* special_chars = "_*[]()~`>#+-=|{}.!";
    
    for (size_t i = 0; i < len && j < len * 2 - 1; i++) {
        char c = text[i];
        /* Check if character needs escaping */
        if (strchr(special_chars, c) != NULL) {
            escaped[j++] = '\\';
        }
        escaped[j++] = c;
    }
    escaped[j] = '\0';
    
    return escaped;
}

int telegram_send_message(const char* bot_token, int64_t chat_id, 
                          const char* title, const char* summary,
                          const char* link, const char* department) {
    if (!bot_token || !title || !summary) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    int ret = ISAE_OK;
    char* url = NULL;
    char* payload = NULL;
    HttpResponse response = {0};
    
    /* Build URL */
    url = malloc(512);
    if (!url) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    snprintf(url, 512, "https://api.telegram.org/bot%s/sendMessage", bot_token);
    
    /* Format message in MarkdownV2 style */
    char* escaped_title = escape_markdown_v2(title);
    char* escaped_summary = escape_markdown_v2(summary);
    char* escaped_dept = department ? escape_markdown_v2(department) : strdup("Unknown");
    
    if (!escaped_title || !escaped_summary || !escaped_dept) {
        free(escaped_title);
        free(escaped_summary);
        free(escaped_dept);
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    /* Build message text */
    char message[4096];
    int msg_len = snprintf(message, sizeof(message),
        "*📢 New Announcement*\n\n"
        "*Department:* %s\n"
        "*Title:* %s\n\n"
        "%s\n",
        escaped_dept, escaped_title, escaped_summary);
    
    if (link && strlen(link) > 0) {
        msg_len += snprintf(message + msg_len, sizeof(message) - msg_len,
                           "\n[Read More](%s)", link);
    }
    
    free(escaped_title);
    free(escaped_summary);
    free(escaped_dept);
    
    /* Escape message for JSON */
    size_t msg_len_str = strlen(message);
    size_t json_size = msg_len_str * 2 + 256;
    payload = malloc(json_size);
    if (!payload) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    /* Simple JSON escaping */
    char* json_escaped = malloc(msg_len_str * 2 + 1);
    if (!json_escaped) {
        ret = ISAE_ERR_MEMORY;
        goto cleanup;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < msg_len_str && j < msg_len_str * 2 - 1; i++) {
        char c = message[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
            json_escaped[j++] = '\\';
            switch (c) {
                case '\n': json_escaped[j++] = 'n'; break;
                case '\r': json_escaped[j++] = 'r'; break;
                case '\t': json_escaped[j++] = 't'; break;
                default: json_escaped[j++] = c; break;
            }
        } else {
            json_escaped[j++] = c;
        }
    }
    json_escaped[j] = '\0';
    
    snprintf(payload, json_size,
             "{\"chat_id\":%" PRId64 ",\"text\":\"%s\",\"parse_mode\":\"MarkdownV2\"}",
             chat_id, json_escaped);
    
    free(json_escaped);
    
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
    
    /* Check response for success */
    if (response.body && response.body_size > 0) {
        /* Ensure null termination */
        if (response.body[response.body_size - 1] != '\0') {
            char* new_body = realloc(response.body, response.body_size + 1);
            if (new_body) {
                response.body = new_body;
                response.body[response.body_size] = '\0';
            }
        }
        
        /* Simple check for \"ok\":true */
        if (strstr(response.body, "\"ok\":true") == NULL &&
            strstr(response.body, "\"ok\": true") == NULL) {
            fprintf(stderr, "[TELEGRAM] API returned error: %s\n", response.body);
            ret = ISAE_ERR_HTTP;
        }
    }
    
cleanup:
    free(url);
    free(payload);
    http_response_free(&response);
    return ret;
}

int telegram_notify_announcement(const Config* config, const Announcement* ann,
                                 const char* department) {
    if (!config || !ann) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Check if Telegram is configured */
    if (!config->telegram_bot_token || !config->telegram_chat_ids ||
        strlen(config->telegram_bot_token) == 0) {
        return ISAE_OK; /* Not configured, skip silently */
    }
    
    int ret = ISAE_OK;
    int overall_ret = ISAE_OK;
    
    /* Parse chat IDs and send to each */
    char* chat_ids_str = strdup(config->telegram_chat_ids);
    if (!chat_ids_str) {
        return ISAE_ERR_MEMORY;
    }
    
    char* saveptr = NULL;
    char* token = strtok_r(chat_ids_str, ",", &saveptr);
    
    while (token != NULL) {
        /* Trim whitespace */
        while (*token && isspace((unsigned char)*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) *end-- = '\0';
        
        if (strlen(token) > 0) {
            int64_t chat_id = strtoll(token, NULL, 10);
            
            ret = telegram_send_message(
                config->telegram_bot_token,
                chat_id,
                ann->title ? ann->title : "No Title",
                ann->summary ? ann->summary : "No Summary",
                ann->link ? ann->link : "",
                department
            );
            
            if (ret != ISAE_OK) {
                fprintf(stderr, "[TELEGRAM] Failed to send to chat %s: %d\n", token, ret);
                overall_ret = ret;
            } else {
                fprintf(stderr, "[TELEGRAM] Sent notification to chat %s\n", token);
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(chat_ids_str);
    return overall_ret;
}
