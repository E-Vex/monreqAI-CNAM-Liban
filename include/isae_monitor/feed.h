#ifndef ISAE_MONITOR_FEED_H
#define ISAE_MONITOR_FEED_H

#include "common.h"
#include "models.h"

/* Maximum announcements per feed */
#define MAX_ANNOUNCEMENTS_PER_FEED 50

/* Feed entry (parsed from RSS/Atom) */
typedef struct {
    char title[MAX_TITLE_LEN];
    char summary[MAX_SUMMARY_LEN];
    char url[MAX_URL_LEN];
    char published[MAX_PUBLISHED_DATE];
    char author[256];
} feed_entry_t;

/* Parsed feed result */
typedef struct {
    feed_entry_t* entries;
    size_t count;
    char feed_title[256];
    char feed_url[MAX_URL_LEN];
} feed_result_t;

/* Initialize feed result */
void feed_result_init(feed_result_t* result);

/* Free feed result resources */
void feed_result_cleanup(feed_result_t* result);

/* Fetch and parse feed from URL */
isae_error_t feed_fetch(const char* feed_url, feed_result_t* result, 
                        int timeout_seconds);

/* Parse Atom/RSS XML content */
isae_error_t feed_parse_xml(const char* xml_content, size_t xml_size,
                            feed_result_t* result);

#endif /* ISAE_MONITOR_FEED_H */
