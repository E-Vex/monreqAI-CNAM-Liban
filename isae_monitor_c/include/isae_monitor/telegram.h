/**
 * @file telegram.h
 * @brief Telegram API client for message delivery
 */

#ifndef ISAE_MONITOR_TELEGRAM_H
#define ISAE_MONITOR_TELEGRAM_H

#include "common.h"
#include "config.h"
#include "models.h"

/* Telegram error codes */
typedef enum {
    TELEGRAM_OK = 0,
    TELEGRAM_ERR_CONFIG = -1,
    TELEGRAM_ERR_HTTP = -2,
    TELEGRAM_ERR_API = -3,
    TELEGRAM_ERR_LIMIT = -4
} telegram_error_t;

/* Telegram client state */
typedef struct {
    const settings_t* settings;
    bool dry_run;
    double last_send_time;    /* Monotonic time of last send */
} telegram_t;

/* Initialize Telegram client */
isae_error_t telegram_init(telegram_t* client, 
                            const settings_t* settings,
                            bool dry_run);

/* Free Telegram client resources */
void telegram_cleanup(telegram_t* client);

/* Format announcement as Telegram message */
isae_error_t telegram_format_message(const announcement_t* ann,
                                      const char* category,
                                      char* message, size_t message_size);

/* Send message to a chat */
telegram_error_t telegram_send(telegram_t* client,
                                const char* chat_id,
                                const char* text);

/* Get error string */
const char* telegram_error_string(telegram_error_t error);

#endif /* ISAE_MONITOR_TELEGRAM_H */
