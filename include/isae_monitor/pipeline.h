#ifndef ISAE_MONITOR_PIPELINE_H
#define ISAE_MONITOR_PIPELINE_H

#include "common.h"
#include "config.h"
#include "state.h"
#include "classifier.h"

/* Pipeline context */
typedef struct {
    settings_t settings;
    state_manager_t state;
    classifier_t classifier;
    bool running;
} pipeline_t;

/* Initialize pipeline */
isae_error_t pipeline_init(pipeline_t* pipeline);

/* Cleanup pipeline resources */
void pipeline_cleanup(pipeline_t* pipeline);

/* Run one iteration of the monitoring loop */
isae_error_t pipeline_run_once(pipeline_t* pipeline);

/* Run continuous monitoring */
isae_error_t pipeline_run(pipeline_t* pipeline, int interval_seconds);

/* Stop the pipeline */
void pipeline_stop(pipeline_t* pipeline);

#endif /* ISAE_MONITOR_PIPELINE_H */
