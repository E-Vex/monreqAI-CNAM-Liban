/* tests/test_config.c -- config_has_whatsapp_group / _channel predicates.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -O2 \
 *     -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *     -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu \
 *     -I/usr/include/libxml2 \
 *     src/config.c src/departments.c third_party/cjson/cJSON.c \
 *     tests/test_config.c -lm -o /tmp/test_config
 *
 * No .env is loaded -- config_init is bypassed so the test is deterministic
 * from getenv() alone. Existing env vars always win over .env anyway, so
 * even if .env were loaded, the test's explicit setenv calls would take
 * precedence.
 */
#include "isae_monitor/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("[ OK ] %s\n", m); else { printf("[FAIL] %s\n", m); failures++; } } while (0)

int main(void) {
    settings_t s;

    /* Start from a clean slate. */
    unsetenv("WHATSAPP_GROUP_JID");
    unsetenv("WHATSAPP_CHANNEL_JID");
    unsetenv("WHATSAPP_SERVICE_URL");
    unsetenv("WHATSAPP_SHARED_SECRET");
    memset(&s, 0, sizeof(s));
    config_load(&s);
    CHECK(!config_has_whatsapp_group(&s), "group: false when WHATSAPP_GROUP_JID unset");
    CHECK(!config_has_whatsapp_channel(&s), "channel: false when WHATSAPP_CHANNEL_JID unset");
    CHECK(!config_has_whatsapp(&s), "sidecar: not configured when all WHATSAPP_* unset");

    /* Set just the group JID. */
    setenv("WHATSAPP_GROUP_JID", "120363012345678901@g.us", 1);
    memset(&s, 0, sizeof(s));
    config_load(&s);
    CHECK(config_has_whatsapp_group(&s), "group: true when WHATSAPP_GROUP_JID set");
    CHECK(!config_has_whatsapp_channel(&s), "channel: false when only group JID is set");
    CHECK(!config_has_whatsapp(&s), "sidecar: still not configured when only JID is set");

    /* Channel only. */
    unsetenv("WHATSAPP_GROUP_JID");
    setenv("WHATSAPP_CHANNEL_JID", "120363012345678901@newsletter", 1);
    memset(&s, 0, sizeof(s));
    config_load(&s);
    CHECK(!config_has_whatsapp_group(&s), "group: false when only channel JID is set");
    CHECK(config_has_whatsapp_channel(&s), "channel: true when WHATSAPP_CHANNEL_JID set");

    /* Both. */
    setenv("WHATSAPP_GROUP_JID", "120363012345678901@g.us", 1);
    memset(&s, 0, sizeof(s));
    config_load(&s);
    CHECK(config_has_whatsapp_group(&s), "group: true alongside channel");

    /* Strip surrounding quotes (matches the channel JID loading behavior). */
    setenv("WHATSAPP_GROUP_JID", "\"120363012345678901@g.us\"", 1);
    memset(&s, 0, sizeof(s));
    config_load(&s);
    CHECK(config_has_whatsapp_group(&s) && strcmp(s.whatsapp_group_jid, "120363012345678901@g.us") == 0,
          "group: surrounding quotes stripped on load");

    /* Cleanup. */
    unsetenv("WHATSAPP_GROUP_JID");
    unsetenv("WHATSAPP_CHANNEL_JID");

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
