/**
 * ISAE Monitor - WhatsApp Channel client
 *
 * Thin client for the local whatsapp-service/ sidecar (Node + Baileys).
 * Every failure is non-fatal: it is logged and reported as `false` so the
 * Telegram delivery and the rest of the run are never affected.
 */

#include "isae_monitor/whatsapp.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WHATSAPP_SEND_PATH "/send-channel-message"
#define WHATSAPP_TIMEOUT_MIN 20

static void copy_str(char* dst, size_t dst_size, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void whatsapp_client_init(whatsapp_client_t* wa, const char* service_url,
                          const char* shared_secret,
                          const http_client_config_t* base_http_cfg,
                          bool dry_run) {
    if (!wa) return;
    memset(wa, 0, sizeof(*wa));
    copy_str(wa->service_url, sizeof(wa->service_url), service_url);
    copy_str(wa->shared_secret, sizeof(wa->shared_secret), shared_secret);
    /* Drop trailing slashes so we can append the path safely. */
    size_t n = strlen(wa->service_url);
    while (n > 0 && wa->service_url[n - 1] == '/') wa->service_url[--n] = '\0';

    if (base_http_cfg) wa->http_cfg = *base_http_cfg;
    else http_client_config_default(&wa->http_cfg);
    /* Local loopback call: no redirects, and a single attempt -- retrying a
     * timed-out send could deliver the same announcement twice. */
    wa->http_cfg.max_retries = 1;
    wa->http_cfg.follow_redirects = false;
    if (wa->http_cfg.timeout_seconds < WHATSAPP_TIMEOUT_MIN)
        wa->http_cfg.timeout_seconds = WHATSAPP_TIMEOUT_MIN;
    wa->dry_run = dry_run;
}

void whatsapp_client_cleanup(whatsapp_client_t* wa) {
    if (!wa) return;
    memset(wa->shared_secret, 0, sizeof(wa->shared_secret));
}

bool whatsapp_client_enabled(const whatsapp_client_t* wa) {
    return wa && wa->service_url[0] && wa->shared_secret[0];
}

/* Cut s to at most max_bytes without splitting a UTF-8 sequence. */
static void truncate_utf8_bytes(char* s, size_t max_bytes) {
    size_t len = strlen(s);
    if (len <= max_bytes) return;
    size_t cut = max_bytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    s[cut] = '\0';
}

char* whatsapp_format_message(const announcement_t* ann) {
    if (!ann) return NULL;
    size_t cap = 16384;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    size_t w = 0;
    int n;

    const char* title = (ann->title && ann->title[0]) ? ann->title : "(sans titre)";
    n = snprintf(out + w, cap - w, "*%.1000s*", title);
    if (n < 0) { free(out); return NULL; }
    w += (size_t)n < cap - w ? (size_t)n : cap - w - 1;

    if (ann->published && ann->published[0]) {
        n = snprintf(out + w, cap - w, "\n\n%.200s", ann->published);
        if (n > 0) w += (size_t)n < cap - w ? (size_t)n : cap - w - 1;
    }
    if (ann->summary && ann->summary[0]) {
        n = snprintf(out + w, cap - w, "\n\n%.2000s", ann->summary);
        if (n > 0) w += (size_t)n < cap - w ? (size_t)n : cap - w - 1;
    }
    if (ann->link && ann->link[0]) {
        n = snprintf(out + w, cap - w, "\n\n%.1000s", ann->link);
        if (n > 0) w += (size_t)n < cap - w ? (size_t)n : cap - w - 1;
    }

    if (strlen(out) > WHATSAPP_MAX_MESSAGE) {
        truncate_utf8_bytes(out, WHATSAPP_MAX_MESSAGE - 3);
        strcat(out, "\xE2\x80\xA6");
    }
    return out;
}

bool send_to_whatsapp_channel(whatsapp_client_t* wa, const char* text) {
    if (!wa || !text || !text[0]) return false;
    if (!whatsapp_client_enabled(wa)) return false;

    if (wa->dry_run) {
        printf("    [dry-run] -> WhatsApp channel\n");
        const char* p = text;
        const char* nl;
        while ((nl = strchr(p, '\n'))) {
            printf("      %.*s\n", (int)(nl - p), p);
            p = nl + 1;
        }
        if (*p) printf("      %s\n", p);
        return true;
    }

    char url[MAX_URL_LEN + 64];
    int un = snprintf(url, sizeof(url), "%s%s", wa->service_url, WHATSAPP_SEND_PATH);
    if (un < 0 || (size_t)un >= sizeof(url)) {
        fprintf(stderr, "WhatsApp send skipped: WHATSAPP_SERVICE_URL too long\n");
        return false;
    }

    char header[MAX_API_KEY_LEN + 32];
    int hn = snprintf(header, sizeof(header), "X-Internal-Secret: %s", wa->shared_secret);
    if (hn < 0 || (size_t)hn >= sizeof(header)) {
        fprintf(stderr, "WhatsApp send skipped: WHATSAPP_SHARED_SECRET too long\n");
        return false;
    }
    const char* headers[] = { header, NULL };

    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "text", text);
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return false;

    http_response_t resp;
    http_response_init(&resp);
    isae_error_t err = http_post_json(url, body, &resp, &wa->http_cfg, headers);
    free(body);

    bool ok = false;
    if (err == ISAE_OK) {
        cJSON* parsed = cJSON_Parse(resp.body ? resp.body : "");
        cJSON* success = parsed ? cJSON_GetObjectItem(parsed, "success") : NULL;
        if (success && cJSON_IsTrue(success)) {
            ok = true;
        } else {
            cJSON* e = parsed ? cJSON_GetObjectItem(parsed, "error") : NULL;
            fprintf(stderr, "WhatsApp send failed: %s\n",
                    (e && cJSON_IsString(e)) ? e->valuestring : "unexpected response");
        }
        cJSON_Delete(parsed);
    } else {
        fprintf(stderr, "WhatsApp send failed (%s, HTTP %ld): %s\n",
                isae_strerror(err), resp.status_code,
                resp.body ? resp.body : "service unreachable");
    }
    http_response_cleanup(&resp);
    return ok;
}
