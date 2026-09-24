#ifndef ISAE_MONITOR_PIPELINE_H
#define ISAE_MONITOR_PIPELINE_H

#include "common.h"
#include "config.h"
#include "state.h"
#include "classifier.h"
#include "telegram.h"
#include "whatsapp.h"
#include "feed.h"
#include <signal.h>

/* Per-run report. Mirrors Python isae_monitor.pipeline.RunReport. */
typedef struct {
    size_t fetched;
    size_t pending;
    size_t general_sent;
    size_t whatsapp_sent;
    size_t department_sent;
    size_t fallback_used;
    /* Per-category count of classified announcements. */
    char   categories_seen[NUM_CATEGORIES][MAX_CATEGORY_NAME];
    int    categories_count[NUM_CATEGORIES];
    int    category_count;
    /* Errors collected during the run. */
    char   errors[16][256];
    int    error_count;
} pipeline_report_t;

void pipeline_report_init(pipeline_report_t* r);
void pipeline_report_add_error(pipeline_report_t* r, const char* msg);
void pipeline_report_add_classified(pipeline_report_t* r, const char* category);
char* pipeline_report_render(const pipeline_report_t* r);  /* caller frees */

/* Pipeline context. One-shot semantics -- the Python tool runs from
 * cron every 20min, not as a daemon. */
typedef struct {
    settings_t settings;
    state_manager_t state;
    classifier_t classifier;
    telegram_client_t telegram;
    whatsapp_client_t whatsapp;
    pipeline_report_t report;
    volatile sig_atomic_t interrupted;
} pipeline_t;

/* Initialize pipeline. Loads .env, reads env vars, initializes state,
 * classifier, and telegram client. The state is loaded from disk here
 * (with corruption-quarantine semantics); state.recovered_from_corruption
 * is set if the file was bad. */
isae_error_t pipeline_init(pipeline_t* pipeline);

void pipeline_cleanup(pipeline_t* pipeline);

/* Run one pass: fetch feed, diff against state, send+classify per item,
 * save state after every announcement. Honors settings.dry_run,
 * settings.bootstrap, settings.quiet. */
isae_error_t pipeline_run(pipeline_t* pipeline);

/* Stop the pipeline (signal handler hook). Sets the interrupted flag
 * so the in-progress run can finish its current announcement and exit
 * with ISAE_ERR_INTERRUPTED. */
void pipeline_stop(pipeline_t* pipeline);

/* Print the 9-department table (the --list-departments action). */
void pipeline_list_departments(const settings_t* settings);

#endif /* ISAE_MONITOR_PIPELINE_H */
