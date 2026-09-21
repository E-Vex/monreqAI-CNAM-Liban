#ifndef ISAE_MONITOR_TELEGRAM_H
#define ISAE_MONITOR_TELEGRAM_H

#include "common.h"
#include "models.h"

/* Telegram message formatting */
typedef struct {
    char text[4096];
    bool has_markdown;
} telegram_message_t;

/* Initialize telegram message */
void telegram_message_init(telegram_message_t* msg);

/* Format announcement for Telegram */
isae_error_t telegram_format_message(const announcement_t* ann,
                                     const char* category,
                                     telegram_message_t* msg);

/* Send message to Telegram chat */
isae_error_t telegram_send_message(const char* bot_token,
                                   const char* chat_id,
                                   const telegram_message_t* msg,
                                   int timeout_seconds);

/* Send notification to general channel and optionally to department channel */
isae_error_t telegram_notify_dept(const char* bot_token,
                                  const char* general_channel,
                                  const char* dept_channel,  /* Can be NULL */
                                  const announcement_t* ann,
                                  const char* category,
                                  int timeout_seconds);

#endif /* ISAE_MONITOR_TELEGRAM_H */
