/**
 * @file feed.h
 * @brief Atom feed fetching and parsing
 */

#ifndef ISAE_MONITOR_FEED_H
#define ISAE_MONITOR_FEED_H

#include "common.h"
#include "models.h"
#include "config.h"

/* Feed error codes */
typedef enum {
    FEED_OK = 0,
    FEED_ERR_HTTP = -1,
    FEED_ERR_PARSE = -2,
    FEED_ERR_EMPTY = -3
} feed_error_t;

/* Maximum number of announcements to fetch */
#define MAX_ANNOUNCEMENTS 50

/* Feed result structure */
typedef struct {
    announcement_t* announcements;
    size_t count;
    size_t capacity;
} feed_result_t;

/* Initialize feed result */
void feed_result_init(feed_result_t* result);

/* Free feed result resources */
void feed_result_cleanup(feed_result_t* result);

/* Fetch and parse the announcements feed */
feed_error_t feed_fetch(const settings_t* settings, feed_result_t* result);

/* Get error message for feed error */
const char* feed_error_string(feed_error_t error);

#endif /* ISAE_MONITOR_FEED_H */
