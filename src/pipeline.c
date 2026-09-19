/**
 * ISAE Monitor - Pipeline Implementation
 * 
 * Maps Python: pipeline.py -> C: pipeline.c
 * 
 * Main orchestration: fetches feeds, classifies announcements, sends notifications.
 */

#include "isae_monitor/pipeline.h"
#include "isae_monitor/departments.h"
#include "isae_monitor/feed.h"
#include "isae_monitor/telegram.h"
#include <unistd.h>

isae_error_t pipeline_init(pipeline_t* pipeline) {
    if (!pipeline) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    memset(pipeline, 0, sizeof(pipeline_t));
    pipeline->running = false;
    
    /* Load configuration */
    config_init(&pipeline->settings);
    isae_error_t err = config_load(&pipeline->settings);
    if (err != ISAE_OK) {
        fprintf(stderr, "Warning: Configuration load had issues\n");
    }
    
    config_validate(&pipeline->settings);
    
    /* Initialize state */
    err = state_init(&pipeline->state, pipeline->settings.state_file);
    if (err != ISAE_OK) {
        fprintf(stderr, "Error: Failed to initialize state manager\n");
        return err;
    }
    
    err = state_load(&pipeline->state);
    if (err != ISAE_OK) {
        fprintf(stderr, "Warning: Failed to load state, starting fresh\n");
    }
    
    /* Initialize classifier */
    classifier_init(&pipeline->classifier, &pipeline->settings);
    
    /* Initialize departments */
    departments_init();
    
    pipeline->running = true;
    
    return ISAE_OK;
}

void pipeline_cleanup(pipeline_t* pipeline) {
    if (!pipeline) return;
    
    classifier_cleanup(&pipeline->classifier);
    state_cleanup(&pipeline->state);
    pipeline->running = false;
}

static isae_error_t process_announcement(pipeline_t* pipeline,
                                          const feed_entry_t* entry,
                                          const char* dept_key) {
    if (!pipeline || !entry || !dept_key) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    /* Create announcement from feed entry */
    announcement_t ann;
    announcement_init(&ann);
    
    ann.title = strdup(entry->title);
    ann.summary = strdup(entry->summary);
    ann.url = strdup(entry->url);
    ann.published = strdup(entry->published);
    ann.department_key = strdup(dept_key);
    
    if (!ann.title || !ann.url) {
        announcement_cleanup(&ann);
        return ISAE_ERR_MEMORY;
    }
    
    /* Normalize text for classification */
    char combined[MAX_TEXT_NORMALIZED_LEN];
    snprintf(combined, sizeof(combined), "%s %s",
             ann.title ? ann.title : "",
             ann.summary ? ann.summary : "");
    
    ann.normalized_text = malloc(MAX_TEXT_NORMALIZED_LEN);
    if (ann.normalized_text) {
        normalize_text(combined, ann.normalized_text, MAX_TEXT_NORMALIZED_LEN);
    }
    
    /* Generate hash for deduplication */
    char hash[MAX_HASH_LEN];
    isae_error_t err = announcement_hash(&ann, hash, sizeof(hash));
    if (err != ISAE_OK) {
        announcement_cleanup(&ann);
        return err;
    }
    
    /* Check if already processed */
    if (state_contains(&pipeline->state, hash)) {
        announcement_cleanup(&ann);
        return ISAE_OK;  /* Already processed */
    }
    
    /* Classify announcement */
    classification_result_t classif_result;
    err = classifier_classify(&pipeline->classifier, &ann, &classif_result);
    if (err != ISAE_OK) {
        fprintf(stderr, "Classification failed: %s\n", isae_strerror(err));
        /* Continue with default category */
        strncpy(classif_result.category, "GENERAL", MAX_CATEGORY_NAME - 1);
    }
    
    /* Add to state */
    err = state_add(&pipeline->state, hash, dept_key, classif_result.category);
    if (err != ISAE_OK) {
        fprintf(stderr, "Failed to add to state: %s\n", isae_strerror(err));
    }
    
    /* Send Telegram notification if configured */
    if (pipeline->settings.telegram_bot_token[0] && 
        pipeline->settings.telegram_chat_ids[0]) {
        err = telegram_notify(pipeline->settings.telegram_bot_token,
                              pipeline->settings.telegram_chat_ids,
                              &ann,
                              classif_result.category,
                              pipeline->settings.http_timeout);
        if (err == ISAE_OK) {
            state_mark_notified(&pipeline->state, hash);
            printf("✓ Notified: [%s] %s\n", classif_result.category, ann.title);
        } else {
            fprintf(stderr, "Telegram notification failed: %s\n", isae_strerror(err));
        }
    } else {
        printf("✓ New: [%s] %s (%s)\n", classif_result.category, ann.title, dept_key);
    }
    
    classification_result_cleanup(&classif_result);
    announcement_cleanup(&ann);
    
    return ISAE_OK;
}

isae_error_t pipeline_run_once(pipeline_t* pipeline) {
    if (!pipeline) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    printf("Fetching feeds from %zu departments...\n", departments_count());
    
    int total_new = 0;
    
    /* Iterate through all departments */
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* dept = departments_get_at(i);
        if (!dept) continue;
        
        printf("  Fetching %s (%s)...\n", dept->name, dept->key);
        
        /* Fetch and parse feed */
        feed_result_t feed;
        isae_error_t err = feed_fetch(dept->feed_url, &feed, pipeline->settings.http_timeout);
        if (err != ISAE_OK) {
            fprintf(stderr, "  Failed to fetch %s: %s\n", dept->key, isae_strerror(err));
            continue;
        }
        
        printf("  Found %zu entries\n", feed.count);
        
        /* Process each entry */
        for (size_t j = 0; j < feed.count; j++) {
            err = process_announcement(pipeline, &feed.entries[j], dept->key);
            if (err == ISAE_OK) {
                total_new++;
            }
        }
        
        feed_result_cleanup(&feed);
    }
    
    /* Save state */
    isae_error_t err = state_save(&pipeline->state);
    if (err != ISAE_OK) {
        fprintf(stderr, "Failed to save state: %s\n", isae_strerror(err));
    }
    
    printf("Processed %d announcements\n", total_new);
    
    return ISAE_OK;
}

isae_error_t pipeline_run(pipeline_t* pipeline, int interval_seconds) {
    if (!pipeline) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    printf("Starting continuous monitoring (interval: %ds)...\n", interval_seconds);
    printf("Press Ctrl+C to stop.\n\n");
    
    pipeline->running = true;
    
    while (pipeline->running) {
        isae_error_t err = pipeline_run_once(pipeline);
        if (err != ISAE_OK) {
            fprintf(stderr, "Pipeline iteration failed: %s\n", isae_strerror(err));
        }
        
        /* Sleep in small increments to allow signal handling */
        for (int i = 0; i < interval_seconds && pipeline->running; i++) {
            sleep(1);
        }
    }
    
    printf("\nShutting down...\n");
    return ISAE_OK;
}

void pipeline_stop(pipeline_t* pipeline) {
    if (!pipeline) return;
    
    pipeline->running = false;
}
