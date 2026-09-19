/**
 * @file pipeline.c
 * @brief Main orchestration pipeline - fetches feeds, classifies, sends notifications
 * 
 * Python equivalent: main.py run_loop function
 */

#include "isae_monitor/pipeline.h"
#include "isae_monitor/feed.h"
#include "isae_monitor/classifier.h"
#include "isae_monitor/telegram.h"
#include "isae_monitor/state.h"
#include "isae_monitor/config.h"
#include "isae_monitor/models.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Check if announcement is already processed */
static int is_announcement_processed(const state_t* state, const char* entry_id) {
    if (!state || !entry_id) return 0;
    
    for (size_t i = 0; i < state->processed_ids_count; i++) {
        if (strcmp(state->processed_ids[i], entry_id) == 0) {
            return 1; /* Already processed */
        }
    }
    return 0;
}

/* Add entry ID to processed list */
static int mark_announcement_processed(state_t* state, const char* entry_id) {
    if (!state || !entry_id) return ISAE_ERR_INVALID_PARAM;
    
    /* Check if already in list */
    if (is_announcement_processed(state, entry_id)) {
        return ISAE_OK;
    }
    
    /* Expand array if needed */
    if (state->processed_ids_count >= state->processed_ids_capacity) {
        size_t new_cap = state->processed_ids_capacity == 0 ? 64 : state->processed_ids_capacity * 2;
        char** new_data = realloc(state->processed_ids, new_cap * sizeof(char*));
        if (!new_data) return ISAE_ERR_MEMORY;
        
        state->processed_ids = new_data;
        state->processed_ids_capacity = new_cap;
    }
    
    /* Add to list */
    state->processed_ids[state->processed_ids_count] = strdup(entry_id);
    if (!state->processed_ids[state->processed_ids_count]) {
        return ISAE_ERR_MEMORY;
    }
    
    state->processed_ids_count++;
    return ISAE_OK;
}

/* Generate a unique ID for feed entry */
static void generate_entry_id(const announcement_t* entry, char* id_out, size_t out_size) {
    if (!entry || !id_out || out_size < 64) return;
    
    /* Use link as primary ID, fallback to title + timestamp */
    if (strlen(entry->link) > 0) {
        /* Hash the link to create a shorter ID */
        unsigned long hash = 5381;
        const char* str = entry->link;
        int c;
        
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
        
        snprintf(id_out, out_size, "entry_%lx", hash);
    } else if (strlen(entry->title) > 0 && strlen(entry->published) > 0) {
        unsigned long hash = 5381;
        const char* str = entry->title;
        int c;
        
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
        
        snprintf(id_out, out_size, "entry_%lx_%s", hash, entry->published);
    } else {
        snprintf(id_out, out_size, "entry_%ld", (long)time(NULL));
    }
}

int pipeline_run_once(const settings_t* config, state_t* state) {
    if (!config || !state) return ISAE_ERR_INVALID_PARAM;
    
    int ret = ISAE_OK;
    int announcements_found = 0;
    int announcements_sent = 0;
    
    fprintf(stderr, "[PIPELINE] Starting feed processing cycle...\n");
    
    /* Initialize libxml2 */
    xmlInitParser();
    
    /* Process each department feed */
    for (DepartmentId dept_id = DEPT_COMPUTER_SCIENCE; dept_id < DEPT_COUNT; dept_id++) {
        const char* feed_url = department_get_feed_url(dept_id);
        if (!feed_url) continue;
        
        const char* dept_name = department_to_string(dept_id);
        fprintf(stderr, "[PIPELINE] Fetching feed for %s: %s\n", dept_name, feed_url);
        
        /* Fetch feed */
        char* feed_xml = feed_fetch(feed_url, config->http_timeout_sec);
        if (!feed_xml) {
            fprintf(stderr, "[PIPELINE] Failed to fetch feed for %s\n", dept_name);
            continue;
        }
        
        /* Parse feed */
        Feed* feed = feed_create();
        if (!feed) {
            free(feed_xml);
            continue;
        }
        
        ret = feed_parse(feed_xml, feed);
        free(feed_xml);
        
        if (ret != ISAE_OK) {
            fprintf(stderr, "[PIPELINE] Failed to parse feed for %s: %d\n", dept_name, ret);
            feed_free(feed);
            continue;
        }
        
        fprintf(stderr, "[PIPELINE] Found %zu entries in %s feed\n", 
                feed->entries ? feed->entries->size : 0, dept_name);
        
        /* Process each entry */
        if (feed->entries) {
            for (size_t i = 0; i < feed->entries->size; i++) {
                FeedEntry* entry = &feed->entries->data[i];
                
                if (!entry->title || !entry->link) continue;
                
                /* Generate unique ID */
                char entry_id[128];
                generate_entry_id(entry, entry_id, sizeof(entry_id));
                
                /* Skip if already processed */
                if (is_announcement_processed(state, entry_id)) {
                    fprintf(stderr, "[PIPELINE] Skipping already processed: %s\n", entry->title);
                    continue;
                }
                
                announcements_found++;
                
                /* Build announcement text for classification */
                char ann_text[4096];
                snprintf(ann_text, sizeof(ann_text), "%s %s",
                        entry->title ? entry->title : "",
                        entry->summary ? entry->summary : "");
                
                /* Classify announcement */
                char classified_dept[DEPT_NAME_MAX];
                ClassificationMethod method;
                ret = classify_announcement(config, ann_text, classified_dept, 
                                           sizeof(classified_dept), &method);
                
                if (ret != ISAE_OK || strlen(classified_dept) == 0) {
                    /* Use feed's department as fallback */
                    strncpy(classified_dept, dept_name, sizeof(classified_dept) - 1);
                    classified_dept[sizeof(classified_dept) - 1] = '\0';
                    method = METHOD_NONE;
                }
                
                fprintf(stderr, "[PIPELINE] Classified \"%s\" as %s (method: %s)\n",
                        entry->title, classified_dept, 
                        classification_method_to_string(method));
                
                /* Create announcement struct */
                Announcement ann = {
                    .title = entry->title,
                    .summary = entry->summary,
                    .link = entry->link,
                    .department = classified_dept,
                    .published = entry->published,
                    .published_ts = entry->published_ts
                };
                
                /* Send Telegram notification */
                ret = telegram_notify_announcement(config, &ann, classified_dept);
                if (ret == ISAE_OK) {
                    announcements_sent++;
                    
                    /* Mark as processed */
                    ret = mark_announcement_processed(state, entry_id);
                    if (ret != ISAE_OK) {
                        fprintf(stderr, "[PIPELINE] Warning: failed to mark as processed: %d\n", ret);
                    }
                } else {
                    fprintf(stderr, "[PIPELINE] Failed to send notification: %d\n", ret);
                }
            }
        }
        
        feed_free(feed);
    }
    
    xmlCleanupParser();
    
    fprintf(stderr, "[PIPELINE] Cycle complete: %d found, %d sent\n", 
            announcements_found, announcements_sent);
    
    /* Save state */
    if (announcements_sent > 0 || state->processed_ids_count > 0) {
        ret = state_save(state, config->state_file);
        if (ret != ISAE_OK) {
            fprintf(stderr, "[PIPELINE] Warning: failed to save state: %d\n", ret);
        }
    }
    
    return announcements_found > 0 ? ISAE_OK : ret;
}

int pipeline_run_loop(const Config* config, State* state) {
    if (!config || !state) return ISAE_ERR_INVALID_PARAM;
    
    fprintf(stderr, "[PIPELINE] Starting monitoring loop (interval: %d seconds)\n",
            config->poll_interval_sec);
    
    while (1) {
        int ret = pipeline_run_once(config, state);
        if (ret != ISAE_OK) {
            fprintf(stderr, "[PIPELINE] Error in pipeline run: %d\n", ret);
        }
        
        /* Sleep before next iteration */
        sleep(config->poll_interval_sec);
    }
    
    return ISAE_OK; /* Never reached */
}
