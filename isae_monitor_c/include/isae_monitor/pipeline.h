/**
 * @file pipeline.h
 * @brief Main orchestration - fetch, diff, classify, deliver, persist
 */

#ifndef ISAE_MONITOR_PIPELINE_H
#define ISAE_MONITOR_PIPELINE_H

#include "common.h"
#include "config.h"
#include "models.h"
#include "state.h"
#include "classifier.h"
#include "telegram.h"

/* Run report structure */
typedef struct {
    int32_t fetched;
    int32_t pending;
    int32_t general_sent;
    int32_t department_sent;
    
    /* Category counts */
    struct {
        char category[MAX_DEPARTMENT_KEY];
        int32_t count;
    } classified[15];
    size_t classified_count;
    
    int32_t fallback_used;
    char errors[20][256];
    size_t error_count;
} run_report_t;

/* Initialize run report */
void run_report_init(run_report_t* report);

/* Render report as string */
isae_error_t run_report_render(const run_report_t* report,
                                char* buffer, size_t buffer_size);

/* Select pending announcements (those needing work) */
isae_error_t pipeline_select_pending(announcement_t* announcements,
                                      size_t ann_count,
                                      state_t* state,
                                      announcement_t** pending,
                                      size_t* pending_count);

/* Run the main pipeline */
isae_error_t pipeline_run(const settings_t* settings,
                           bool dry_run,
                           bool bootstrap,
                           bool verbose,
                           run_report_t* report);

#endif /* ISAE_MONITOR_PIPELINE_H */
