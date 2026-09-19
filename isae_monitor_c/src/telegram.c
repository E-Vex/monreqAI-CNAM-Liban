/**
 * ISAE Monitor - Telegram Notification Implementation
 * 
 * Maps Python: telegram.py -> C: telegram.c
 * 
 * Formats and sends notifications to Telegram channels.
 */

#include "isae_monitor/telegram.h"
#include "isae_monitor/httpclient.h"
#include <cjson/cJSON.h>
#include <ctype.h>

void telegram_message_init(telegram_message_t* msg) {
    if (!msg) return;
    
    msg->text[0] = '\0';
    msg->has_markdown = false;
}

isae_error_t telegram_format_message(const announcement_t* ann,
                                     const char* category,
                                     telegram_message_t* msg) {
    if (!ann || !category || !msg) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    telegram_message_init(msg);
    
    /* Build formatted message with Markdown */
    char* title = ann->title ? ann->title : "No title";
    char* summary = ann->summary ? ann->summary : "";
    char* dept = ann->department_key ? ann->department_key : "Unknown";
    
    /* Truncate summary if too long (Telegram limit is 4096) */
    char short_summary[512];
    if (strlen(summary) > sizeof(short_summary) - 3) {
        strncpy(short_summary, summary, sizeof(short_summary) - 3);
        strcat(short_summary, "...");
    } else {
        strncpy(short_summary, summary, sizeof(short_summary) - 1);
    }
    
    /* Format with Markdown */
    snprintf(msg->text, sizeof(msg->text),
             "*📢 %s*\n\n"
             "*Department:* %s\n"
             "*Category:* %s\n\n"
             "%s\n\n"
             "[Read more](%s)",
             title,
             dept,
             category,
             short_summary,
             ann->url ? ann->url : "");
    
    msg->has_markdown = true;
    
    return ISAE_OK;
}

isae_error_t telegram_send_message(const char* bot_token,
                                   const char* chat_id,
                                   const telegram_message_t* msg,
                                   int timeout_seconds) {
    if (!bot_token || !chat_id || !msg) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Build JSON request */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chat_id", chat_id);
    cJSON_AddStringToObject(root, "text", msg->text);
    
    if (msg->has_markdown) {
        cJSON_AddStringToObject(root, "parse_mode", "Markdown");
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ISAE_ERR_MEMORY;
    }
    
    /* Build URL */
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot%s/sendMessage",
             bot_token);
    
    /* Make request */
    http_response_t response;
    http_response_init(&response);
    
    http_client_config_t config;
    http_client_config_default(&config);
    config.timeout_seconds = timeout_seconds;
    
    isae_error_t err = http_post_json(url, json_str, &response, &config, NULL);
    free(json_str);
    
    if (err != ISAE_OK) {
        http_response_cleanup(&response);
        return err;
    }
    
    /* Check response */
    bool success = false;
    if (response.body) {
        cJSON* resp_json = cJSON_Parse(response.body);
        if (resp_json) {
            cJSON* ok = cJSON_GetObjectItem(resp_json, "ok");
            if (ok && cJSON_IsBool(ok) && ok->valueint) {
                success = true;
            }
            cJSON_Delete(resp_json);
        }
    }
    
    http_response_cleanup(&response);
    
    return success ? ISAE_OK : ISAE_ERR_HTTP;
}

isae_error_t telegram_notify(const char* bot_token,
                             const char* chat_ids_csv,
                             const announcement_t* ann,
                             const char* category,
                             int timeout_seconds) {
    if (!bot_token || !chat_ids_csv || !ann || !category) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Format message */
    telegram_message_t msg;
    isae_error_t err = telegram_format_message(ann, category, &msg);
    if (err != ISAE_OK) {
        return err;
    }
    
    /* Parse comma-separated chat IDs and send to each */
    char chat_ids_copy[MAX_API_KEY_LEN * 4];
    strncpy(chat_ids_copy, chat_ids_csv, sizeof(chat_ids_copy) - 1);
    chat_ids_copy[sizeof(chat_ids_copy) - 1] = '\0';
    
    char* saveptr = NULL;
    char* token = strtok_r(chat_ids_copy, ",", &saveptr);
    
    isae_error_t last_err = ISAE_OK;
    int sent_count = 0;
    
    while (token) {
        /* Trim whitespace */
        while (*token && isspace((unsigned char)*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) *end-- = '\0';
        
        if (*token) {
            err = telegram_send_message(bot_token, token, &msg, timeout_seconds);
            if (err == ISAE_OK) {
                sent_count++;
            }
            last_err = err;
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return sent_count > 0 ? ISAE_OK : (last_err != ISAE_OK ? last_err : ISAE_ERR_INVALID_PARAM);
}
