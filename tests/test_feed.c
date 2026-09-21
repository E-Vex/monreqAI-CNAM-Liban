/* Atom feed parsing tests, mirroring tests/test_pipeline.py.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *          -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu -I/usr/include/libxml2 \
 *          src/models.c src/feed.c third_party/cjson/cJSON.c tests/test_feed.c \
 *          -o /tmp/test_feed -lm -lcurl -lxml2
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "isae_monitor/feed.h"
#include "isae_monitor/models.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("[ OK ] %s\n", msg); } \
    else      { fail++; printf("[FAIL] %s\n", msg); } \
} while (0)

static const char* ATOM_SAMPLE =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
"  <title>ISAE Annonces</title>\n"
"  <entry>\n"
"    <id>tag:blogger.com,2025:post-1003</id>\n"
"    <title>Reunion Genie Informatique</title>\n"
"    <link rel=\"alternate\" href=\"https://annonces.isae.edu.lb/2025/03/post-1003.html\"/>\n"
"    <link rel=\"self\" href=\"https://www.blogger.com/feeds/123/posts/default/1003\"/>\n"
"    <published>2025-03-15T10:00:00Z</published>\n"
"    <summary>&lt;p&gt;Reunion de rentree pour les etudiants&lt;/p&gt;</summary>\n"
"  </entry>\n"
"  <entry>\n"
"    <id>tag:blogger.com,2025:post-1002</id>\n"
"    <title>Café &amp; thé offerts</title>\n"
"    <link rel=\"alternate\" href=\"https://annonces.isae.edu.lb/2025/03/post-1002.html\"/>\n"
"    <published>2025-03-14T08:00:00Z</published>\n"
"    <summary>Petit déjeuner à la cafétéria</summary>\n"
"  </entry>\n"
"  <entry>\n"
"    <id>tag:blogger.com,2025:post-1001</id>\n"
"    <title></title>\n"
"    <link rel=\"alternate\" href=\"https://annonces.isae.edu.lb/2025/03/post-1001.html\"/>\n"
"    <published>2025-03-13T08:00:00Z</published>\n"
"    <summary>Reunion</summary>\n"
"  </entry>\n"
"  <entry>\n"
"    <title>Entry with no id, no link</title>\n"
"    <published>2025-03-12T08:00:00Z</published>\n"
"    <summary>Should be dropped</summary>\n"
"  </entry>\n"
"</feed>\n";

int main(void) {
    feed_result_t fr;
    feed_result_init(&fr);
    isae_error_t err = feed_parse_xml(ATOM_SAMPLE, strlen(ATOM_SAMPLE), &fr);
    CHECK(err == ISAE_OK, "feed_parse_xml returns OK");

    /* Three valid entries (one with empty title -> kept with fallback).
     * One entry with no id and no link -> dropped. */
    CHECK(fr.count == 3, "3 entries kept (empty-id entry dropped)");

    /* Reversed to oldest-first: post-1001, post-1002, post-1003 */
    CHECK(strcmp(fr.entries[0].id, "tag:blogger.com,2025:post-1001") == 0, "reversed: entry 0 is post-1001");
    CHECK(strcmp(fr.entries[1].id, "tag:blogger.com,2025:post-1002") == 0, "reversed: entry 1 is post-1002");
    CHECK(strcmp(fr.entries[2].id, "tag:blogger.com,2025:post-1003") == 0, "reversed: entry 2 is post-1003");

    /* rel=alternate preferred over rel=self */
    CHECK(strstr(fr.entries[2].link, "post-1003.html") != NULL, "link uses rel=alternate href");

    /* Empty title -> "(sans titre)" */
    CHECK(strcmp(fr.entries[0].title, "(sans titre)") == 0, "empty title falls back to (sans titre)");

    /* HTML entities unescaped once (not twice) */
    CHECK(strcmp(fr.entries[1].title, "Café & thé offerts") == 0, "title entities unescaped once");

    /* strip_html on summary */
    CHECK(strstr(fr.entries[2].summary, "<p>") == NULL, "summary has <p> tags stripped");
    CHECK(strstr(fr.entries[2].summary, "Reunion de rentree pour les etudiants") != NULL, "summary text preserved");

    /* Convert to announcement_t */
    announcement_t ann;
    err = feed_entry_to_announcement(&fr.entries[2], &ann);
    CHECK(err == ISAE_OK, "feed_entry_to_announcement OK");
    CHECK(ann.id && strcmp(ann.id, "tag:blogger.com,2025:post-1003") == 0, "announcement.id copied");
    CHECK(ann.link && strstr(ann.link, "post-1003.html") != NULL, "announcement.link copied");
    announcement_cleanup(&ann);

    feed_result_cleanup(&fr);
    printf("\n%d passed, %d failed.\n", pass, fail);
    return fail ? 1 : 0;
}
