#ifndef ISAE_MONITOR_FEED_H
#define ISAE_MONITOR_FEED_H

#include "common.h"
#include "models.h"
#include "httpclient.h"

/* Maximum announcements per feed. Python uses max-results=25 in the URL,
 * but we keep headroom for feeds that ignore the cap. */
#define MAX_ANNOUNCEMENTS_PER_FEED 50

/* A single parsed feed entry. Mirrors Python's Announcement dataclass
 * at the point immediately after feedparser.parse(). The 'id' field is
 * the Atom <id> (or the link if missing) and is used as the dedup key
 * in seen.json. */
typedef struct {
    char id[MAX_URL_LEN];            /* Atom <id>; falls back to link */
    char title[MAX_TITLE_LEN];       /* defaults to "(sans titre)" */
    char link[MAX_URL_LEN];          /* Atom <link rel=alternate href> */
    char published[MAX_PUBLISHED_DATE]; /* defaults to "date inconnue" */
    char summary[MAX_SUMMARY_LEN];   /* HTML-stripped, capped at 500 chars */
} feed_entry_t;

typedef struct {
    feed_entry_t* entries;
    size_t count;
    char feed_title[256];
} feed_result_t;

void feed_result_init(feed_result_t* result);
void feed_result_cleanup(feed_result_t* result);

/* Fetch the feed over HTTP and parse it. The result is ordered oldest-
 * first (the Blogger feed is newest-first, mirroring Python's reverse).
 * Entries with an empty <id> are dropped (Python behavior). The summary
 * is HTML-stripped and truncated to 500 chars (Python's feed-side cap).
 *
 * On a parse failure where some entries were still recoverable, returns
 * ISAE_OK with whatever entries parsed (Python tolerates bozo when
 * entries exist). On a total parse failure or HTTP error, returns
 * ISAE_ERR_FEED / ISAE_ERR_HTTP respectively. */
isae_error_t feed_fetch(const char* feed_url,
                        const http_client_config_t* http_cfg,
                        feed_result_t* result);

/* Parse XML content (Atom or RSS) into feed_result_t.
 * Same semantics as feed_fetch() but without the HTTP step.
 * Exposed for testing. */
isae_error_t feed_parse_xml(const char* xml_content, size_t xml_size,
                            feed_result_t* result);

/* Convert a feed_entry_t into an announcement_t (heap-allocated strings).
 * The caller owns the result and must announcement_cleanup() it. */
isae_error_t feed_entry_to_announcement(const feed_entry_t* entry,
                                         announcement_t* ann);

#endif /* ISAE_MONITOR_FEED_H */
