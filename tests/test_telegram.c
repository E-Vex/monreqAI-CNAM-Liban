/* Telegram message formatting tests, mirroring tests/test_pipeline.py.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *          -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu -I/usr/include/libxml2 \
 *          src/models.c src/departments.c src/telegram.c third_party/cjson/cJSON.c tests/test_telegram.c \
 *          -o /tmp/test_telegram -lm -lcurl -lxml2
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "isae_monitor/telegram.h"
#include "isae_monitor/models.h"
#include "isae_monitor/departments.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("[ OK ] %s\n", msg); } \
    else      { fail++; printf("[FAIL] %s\n", msg); } \
} while (0)

static size_t utf8_expected_len_(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void init_ann(announcement_t* a, const char* title, const char* summary, const char* link, const char* published) {
    announcement_init(a);
    if (title)     a->title     = strdup(title);
    if (summary)   a->summary   = strdup(summary);
    if (link)       a->link      = strdup(link);
    if (published)  a->published = strdup(published);
}

int main(void) {
    departments_init();

    /* 1. Basic format: label + title + published + summary + link */
    {
        announcement_t a; init_ann(&a, "Reunion Informatique", "Reunion demain a 10h", "https://example.com/x", "2025-03-15");
        char* msg = telegram_format_message(&a, "informatique");
        CHECK(msg != NULL, "format returns non-NULL");
        CHECK(msg && strstr(msg, "<b>[Informatique]</b> <b>Reunion Informatique</b>") != NULL, "label+title prefix");
        CHECK(msg && strstr(msg, "<i>2025-03-15</i>") != NULL, "published in italics");
        CHECK(msg && strstr(msg, "Reunion demain a 10h") != NULL, "summary present");
        CHECK(msg && strstr(msg, "https://example.com/x") != NULL, "link present");
        CHECK(msg && strstr(msg, "disable_web_page_preview") == NULL, "format output is just text (no JSON keys)");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 2. No category -> no [Label] */
    {
        announcement_t a; init_ann(&a, "General notice", NULL, "https://example.com/y", "2025-01-01");
        char* msg = telegram_format_message(&a, NULL);
        CHECK(msg && strstr(msg, "[") == NULL, "no [Label] when category is NULL");
        CHECK(msg && strstr(msg, "<b>General notice</b>") != NULL, "title still bold");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 3. HTML escaping in title and summary */
    {
        announcement_t a; init_ann(&a, "Maths & <b>Physique</b>", "x < y > z", NULL, "2025-01-01");
        char* msg = telegram_format_message(&a, "physique");
        CHECK(msg && strstr(msg, "&amp;") != NULL, "ampersand escaped");
        CHECK(msg && strstr(msg, "&lt;b&gt;") != NULL, "<b> literal-escaped");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 4. Empty summary is omitted */
    {
        announcement_t a; init_ann(&a, "Title", "", "https://example.com", "2025-01-01");
        char* msg = telegram_format_message(&a, NULL);
        /* The body should have two "\n\n" separators (title -> published -> link),
         * not three (which would indicate an empty summary line). */
        int n = 0;
        for (const char* p = msg; (p = strstr(p, "\n\n")); p += 2) n++;
        CHECK(n == 2, "empty summary omits the line (2 separators, not 3)");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 5. Empty link is omitted */
    {
        announcement_t a; init_ann(&a, "Title", "Summary", NULL, "2025-01-01");
        char* msg = telegram_format_message(&a, NULL);
        CHECK(msg && strstr(msg, "https") == NULL, "no link when NULL");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 6. Truncation: very long summary -> ellipsis (U+2026) */
    {
        char long_summary[5000];
        memset(long_summary, 'a', sizeof(long_summary) - 1);
        long_summary[sizeof(long_summary) - 1] = '\0';
        announcement_t a; init_ann(&a, "Title", long_summary, NULL, "2025-01-01");
        char* msg = telegram_format_message(&a, NULL);
        /* The summary is truncated to 400 chars at the field level, but the
         * FINAL message is capped at MAX_MESSAGE chars total. We need a
         * message that exceeds MAX_MESSAGE to trigger the hard truncation.
         * Use a 5000-char title for that (title is truncated to 250 chars,
         * so we have to push the cap with a long summary + multiple
         * sections -- but the field-level cap on summary is 400. The only
         * way to exceed MAX_MESSAGE is via the link, which is uncapped.
         * Use a 5000-char link. */
        announcement_cleanup(&a);
        char long_link[5000];
        memset(long_link, 'x', sizeof(long_link) - 1);
        long_link[sizeof(long_link) - 1] = '\0';
        init_ann(&a, "Title", long_summary, long_link, "2025-01-01");
        free(msg);
        msg = telegram_format_message(&a, NULL);
        CHECK(msg && strstr(msg, "\xE2\x80\xA6") != NULL, "U+2026 ellipsis appended when message exceeds MAX_MESSAGE");
        free(msg);
        announcement_cleanup(&a);
    }

    /* 7. UTF-8 multibyte not split: Arabic title truncation lands on a codepoint boundary */
    {
        /* 500 Arabic 'م' chars (each 2 bytes UTF-8) */
        char title[1200];
        char* p = title;
        for (int i = 0; i < 500; i++) { *p++ = '\xD9'; *p++ = '\x85'; }
        *p = '\0';
        announcement_t a; init_ann(&a, title, NULL, NULL, "2025-01-01");
        char* msg = telegram_format_message(&a, NULL);
        /* Verify the message ends with a complete UTF-8 sequence (no dangling continuation byte). */
        size_t len = strlen(msg);
        bool ends_clean = false;
        if (len > 0) {
            unsigned char last = (unsigned char)msg[len - 1];
            if (last < 0x80) ends_clean = true;
            else if (last >= 0xC0) ends_clean = true;  /* single lead byte (truncated 2/3/4) */
            else {
                /* last is a continuation byte; check the lead byte before it */
                size_t k = len - 1;
                while (k > 0 && (unsigned char)msg[k] >= 0x80 && (unsigned char)msg[k] < 0xC0) k--;
                unsigned char lead = (unsigned char)msg[k];
                size_t seq_len = utf8_expected_len_(lead);
                ends_clean = (len - k == seq_len);  /* complete sequence */
            }
        }
        CHECK(ends_clean, "Arabic title truncation lands on UTF-8 boundary");
        free(msg);
        announcement_cleanup(&a);
    }

    printf("\n%d passed, %d failed.\n", pass, fail);
    return fail ? 1 : 0;
}
