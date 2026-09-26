/* tests/test_whatsapp.c -- WhatsApp client: formatting, non-fatal behavior,
 * and per-target path routing.
 *
 * Build (no network; http_post_json is stubbed in this file):
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -O2 \
 *     -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *     -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu \
 *     -I/usr/include/libxml2 \
 *     src/whatsapp.c third_party/cjson/cJSON.c tests/test_whatsapp.c \
 *     -lm -o /tmp/test_whatsapp
 *
 * No network access needed: every send goes through the stub http_post_json
 * defined below, which records the URL it was called with and returns a
 * configurable error / response.
 */
#include "isae_monitor/whatsapp.h"
#include "isae_monitor/httpclient.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("[ OK ] %s\n", m); else { printf("[FAIL] %s\n", m); failures++; } } while (0)

/* ------------------------------------------------------------------ */
/* Stub http_post_json + helpers. Records the URL the caller picked so
 * the path-routing tests can assert channel vs group. Returns whatever
 * the test configured via stub_setup(). */
/* ------------------------------------------------------------------ */

static char g_last_url[512];
static int  g_last_url_set = 0;
/* Configurable stub behavior. */
static isae_error_t g_ret = ISAE_ERR_NETWORK;
static const char*  g_resp_body = NULL;   /* owned by the test */
static long         g_resp_status = 0;

static void stub_reset(void) {
    g_last_url[0] = '\0';
    g_last_url_set = 0;
    g_ret = ISAE_ERR_NETWORK;
    g_resp_status = 0;
    if (g_resp_body) { free((void*)g_resp_body); g_resp_body = NULL; }
}

static void stub_ok(const char* body) {
    g_ret = ISAE_OK;
    g_resp_status = 200;
    if (g_resp_body) free((void*)g_resp_body);
    g_resp_body = strdup(body);
}

static void stub_fail(isae_error_t err) {
    g_ret = err;
    g_resp_status = 0;
    if (g_resp_body) { free((void*)g_resp_body); g_resp_body = NULL; }
}

/* Stub implementations of the httpclient.c symbols this TU needs.
 * We do NOT link httpclient.c so there is no duplicate-symbol conflict. */
isae_error_t http_post_json(const char* url, const char* json_body,
                            http_response_t* response,
                            const http_client_config_t* config,
                            const char* const* extra_headers) {
    (void)json_body; (void)config; (void)extra_headers;
    if (url) {
        size_t n = strlen(url);
        if (n >= sizeof(g_last_url)) n = sizeof(g_last_url) - 1;
        memcpy(g_last_url, url, n);
        g_last_url[n] = '\0';
        g_last_url_set = 1;
    }
    if (g_resp_body) {
        response->body = strdup(g_resp_body);
        response->body_size = strlen(g_resp_body);
    }
    response->status_code = g_resp_status;
    return g_ret;
}

void http_response_init(http_response_t* resp) {
    if (!resp) return;
    memset(resp, 0, sizeof(*resp));
}

void http_response_cleanup(http_response_t* resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->content_type);
    free(resp->retry_after);
    memset(resp, 0, sizeof(*resp));
}

void http_client_config_default(http_client_config_t* config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->timeout_seconds = 20;
    config->max_retries = 3;
    config->follow_redirects = true;
    config->max_redirects = 5;
    config->disable_tls_verify = true;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    announcement_t ann;
    memset(&ann, 0, sizeof(ann));
    ann.title = "Examens finaux";
    ann.published = "2026-09-01";
    ann.summary = "Le calendrier est publie.";
    ann.link = "https://example.org/a";

    char* msg = whatsapp_format_message(&ann);
    CHECK(msg && strcmp(msg, "*Examens finaux*\n\n2026-09-01\n\nLe calendrier est publie.\n\nhttps://example.org/a") == 0,
          "format: title/published/summary/link");
    free(msg);

    ann.summary = "";
    ann.link = "";
    msg = whatsapp_format_message(&ann);
    CHECK(msg && strcmp(msg, "*Examens finaux*\n\n2026-09-01") == 0, "format: empty summary/link omitted");
    free(msg);

    char big[6000];
    memset(big, 0, sizeof(big));
    for (size_t i = 0; i + 2 < 5000; i += 2) { big[i] = (char)0xC3; big[i + 1] = (char)0xA9; }  /* e-acute */
    ann.summary = big;
    /* summary is capped at 2000 bytes in the formatter, so force overflow via title+link too */
    static char long_link[1100]; memset(long_link, 'a', 1099); ann.link = long_link;
    msg = whatsapp_format_message(&ann);
    CHECK(msg && strlen(msg) <= WHATSAPP_MAX_MESSAGE, "format: bounded length");
    free(msg);

    /* ---------- disabled client: both send functions are no-ops ---------- */
    whatsapp_client_t wa;
    whatsapp_client_init(&wa, "", "", NULL, false);
    CHECK(!whatsapp_client_enabled(&wa), "disabled when url/secret empty");
    CHECK(!send_to_whatsapp_channel(&wa, "x"), "channel: disabled client returns false (no-op)");
    CHECK(!send_to_whatsapp_group(&wa, "x"), "group: disabled client returns false (no-op)");

    /* ---------- init invariants ---------- */
    http_client_config_t cfg; http_client_config_default(&cfg);
    whatsapp_client_init(&wa, "http://127.0.0.1:1/", "secret", &cfg, false);
    CHECK(wa.service_url[strlen(wa.service_url) - 1] != '/', "trailing slash trimmed");
    CHECK(wa.http_cfg.max_retries == 1, "no retries on send");

    /* ---------- service down (stub returns ISAE_ERR_NETWORK) ---------- */
    stub_reset();
    stub_fail(ISAE_ERR_NETWORK);
    CHECK(!send_to_whatsapp_channel(&wa, "hello"), "channel: service down returns false, does not crash");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-channel-message") != NULL,
          "channel: stub recorded the channel path");

    stub_reset();
    stub_fail(ISAE_ERR_NETWORK);
    CHECK(!send_to_whatsapp_group(&wa, "hello"), "group: service down returns false, does not crash");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-group-message") != NULL,
          "group: stub recorded the group path");

    /* ---------- path routing: the one genuinely new behavior ---------- */
    stub_reset();
    stub_ok("{\"success\":true,\"id\":\"msg1\"}");
    CHECK(send_to_whatsapp_channel(&wa, "hello"), "channel: success path returns true");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-channel-message") != NULL,
          "channel: hits /send-channel-message");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-group-message") == NULL,
          "channel: does NOT hit /send-group-message");

    stub_reset();
    stub_ok("{\"success\":true,\"id\":\"msg2\"}");
    CHECK(send_to_whatsapp_group(&wa, "hello"), "group: success path returns true");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-group-message") != NULL,
          "group: hits /send-group-message");
    CHECK(g_last_url_set && strstr(g_last_url, "/send-channel-message") == NULL,
          "group: does NOT hit /send-channel-message");

    /* ---------- success:false in the JSON body ---------- */
    stub_reset();
    stub_ok("{\"success\":false,\"error\":\"whatsapp not connected\"}");
    CHECK(!send_to_whatsapp_channel(&wa, "hello"), "channel: success:false -> false");
    stub_reset();
    stub_ok("{\"success\":false,\"error\":\"whatsapp not connected\"}");
    CHECK(!send_to_whatsapp_group(&wa, "hello"), "group: success:false -> false");

    /* ---------- dry-run: both functions print, neither hits the stub ---------- */
    whatsapp_client_init(&wa, "http://127.0.0.1:1", "secret", &cfg, true);
    stub_reset();
    CHECK(send_to_whatsapp_channel(&wa, "hello"), "channel: dry-run returns true without network");
    CHECK(!g_last_url_set, "channel: dry-run does NOT call http_post_json");

    stub_reset();
    CHECK(send_to_whatsapp_group(&wa, "hello"), "group: dry-run returns true without network");
    CHECK(!g_last_url_set, "group: dry-run does NOT call http_post_json");

    whatsapp_client_cleanup(&wa);
    stub_reset();

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
