#ifndef ISAE_MONITOR_WHATSAPP_H
#define ISAE_MONITOR_WHATSAPP_H

#include "common.h"
#include "models.h"
#include "httpclient.h"

/* Maximum characters (codepoints) of a WhatsApp Channel message. */
#define WHATSAPP_MAX_MESSAGE 3900

/* Client for the local whatsapp-service/ Node sidecar (Baileys).
 * The service is OPTIONAL: when service_url or shared_secret is empty the
 * client is "disabled" and every send is a silent no-op. */
typedef struct {
    char service_url[MAX_URL_LEN];      /* e.g. http://127.0.0.1:3100 */
    char shared_secret[MAX_API_KEY_LEN];
    http_client_config_t http_cfg;
    bool dry_run;
} whatsapp_client_t;

void whatsapp_client_init(whatsapp_client_t* wa, const char* service_url,
                          const char* shared_secret,
                          const http_client_config_t* base_http_cfg,
                          bool dry_run);
void whatsapp_client_cleanup(whatsapp_client_t* wa);

/* True when service URL, shared secret are both set. */
bool whatsapp_client_enabled(const whatsapp_client_t* wa);

/* Format an announcement as plain text (WhatsApp *bold* for the title):
 *   *Title*
 *
 *   published
 *
 *   summary   (omitted if empty)
 *
 *   link      (omitted if empty)
 * Truncated to WHATSAPP_MAX_MESSAGE bytes on a UTF-8 boundary with U+2026.
 * Caller free()s. Returns NULL on OOM. */
char* whatsapp_format_message(const announcement_t* ann);

/* POST {"text": ...} to <service_url>/send-channel-message with the
 * X-Internal-Secret header. Returns true on success. NEVER fatal: on any
 * failure (service down, timeout, non-2xx, success:false) the error is
 * logged to stderr and false is returned. Sends are NOT retried (a retry
 * after a timeout could post the same message twice). */
bool send_to_whatsapp_channel(whatsapp_client_t* wa, const char* text);

#endif /* ISAE_MONITOR_WHATSAPP_H */
