/* tests/test_whatsapp.c -- WhatsApp client: formatting + non-fatal behavior.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -Iinclude \
 *   -Ithird_party/cjson src/{models,departments,httpclient,whatsapp}.c \
 *   third_party/cjson/cJSON.c tests/test_whatsapp.c -lcurl -lxml2 -lm -o /tmp/test_whatsapp
 * No network access needed: the "service down" case targets a closed port. */
#include "isae_monitor/whatsapp.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("[ OK ] %s\n", m); else { printf("[FAIL] %s\n", m); failures++; } } while (0)

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

    whatsapp_client_t wa;
    whatsapp_client_init(&wa, "", "", NULL, false);
    CHECK(!whatsapp_client_enabled(&wa), "disabled when url/secret empty");
    CHECK(!send_to_whatsapp_channel(&wa, "x"), "disabled client: send is a no-op returning false");

    http_client_config_t cfg; http_client_config_default(&cfg);
    whatsapp_client_init(&wa, "http://127.0.0.1:1/", "secret", &cfg, false);
    CHECK(wa.service_url[strlen(wa.service_url) - 1] != '/', "trailing slash trimmed");
    CHECK(wa.http_cfg.max_retries == 1, "no retries on send");
    CHECK(!send_to_whatsapp_channel(&wa, "hello"), "service down: returns false, does not crash");

    whatsapp_client_init(&wa, "http://127.0.0.1:1", "secret", &cfg, true);
    CHECK(send_to_whatsapp_channel(&wa, "hello"), "dry-run: returns true without network");
    whatsapp_client_cleanup(&wa);

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
