/**
 * @file main.c
 * @brief Command line entry point for ISAE Monitor
 */

#include "isae_monitor/common.h"
#include "isae_monitor/config.h"
#include "isae_monitor/departments.h"
#include "isae_monitor/pipeline.h"
#include "isae_monitor/feed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    bool dry_run;
    bool bootstrap;
    bool list_departments;
    bool check;
    bool quiet;
    bool version;
} cli_args_t;

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nWatch ISSAE / Cnam Liban announcements and route to Telegram channels.\n\n");
    printf("Options:\n");
    printf("  --dry-run           Classify and print, but send nothing\n");
    printf("  --bootstrap         Mark all current feed entries as seen without notifying\n");
    printf("  --list-departments  Show every department and its channel variable\n");
    printf("  --check             Print resolved configuration and exit\n");
    printf("  --quiet             Only print the summary\n");
    printf("  --version           Print version and exit\n");
    printf("  --help              Show this help message\n");
}

static isae_error_t parse_args(int argc, char** argv, cli_args_t* args) {
    if (!args) return ISAE_ERR_INVALID_ARG;
    
    memset(args, 0, sizeof(cli_args_t));
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            args->dry_run = true;
        } else if (strcmp(argv[i], "--bootstrap") == 0) {
            args->bootstrap = true;
        } else if (strcmp(argv[i], "--list-departments") == 0) {
            args->list_departments = true;
        } else if (strcmp(argv[i], "--check") == 0) {
            args->check = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            args->quiet = true;
        } else if (strcmp(argv[i], "--version") == 0) {
            args->version = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return ISAE_ERR_INVALID_ARG;
        }
    }
    
    return ISAE_OK;
}

static int list_departments(const settings_t* settings) {
    departments_init();
    size_t count = departments_get_count();
    
    printf("%zu departments at ISSAE / Cnam Liban:\n\n", count);
    
    /* Find max width for French names */
    size_t max_width = 0;
    for (size_t i = 0; i < count; i++) {
        const department_t* dept = departments_get_by_index(i);
        size_t len = strlen(dept->name_fr);
        if (len > max_width) max_width = len;
    }
    
    for (size_t i = 0; i < count; i++) {
        const department_t* dept = departments_get_by_index(i);
        
        /* Check if configured */
        const char* state = "-";
        const char* configured_chat = settings_get_department_channel(settings, dept->key);
        if (configured_chat != NULL && strlen(configured_chat) > 0) {
            state = "configured";
        }
        
        printf("  %-*s  %-32s %s\n", (int)max_width, dept->name_fr, 
               department_get_env_var(dept), state);
    }
    
    printf("\n  plus the pseudo-categories 'general' (TELEGRAM_CHANNEL_GENERAL) "
           "and 'other' (never routed).\n");
    
    return 0;
}

int main(int argc, char** argv) {
    cli_args_t args;
    isae_error_t err;
    
    err = parse_args(argc, argv, &args);
    if (err != ISAE_OK) {
        return 1;
    }
    
    if (args.version) {
        printf("isae-monitor %s\n", ISAE_MONITOR_VERSION);
        return 0;
    }
    
    /* Initialize departments */
    departments_init();
    
    /* Load settings */
    settings_t settings;
    err = settings_from_env(&settings);
    if (err != ISAE_OK) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    if (args.list_departments) {
        return list_departments(&settings);
    }
    
    if (args.check) {
        char summary[1024];
        err = settings_summary(&settings, summary, sizeof(summary));
        if (err == ISAE_OK) {
            printf("%s\n", summary);
        }
        
        config_problems_t problems;
        err = settings_get_problems(&settings, &problems);
        if (err == ISAE_OK && problems.count > 0) {
            printf("\nWarnings:\n");
            for (size_t i = 0; i < problems.count; i++) {
                printf("  - %s\n", problems.messages[i]);
            }
        }
        
        return settings_has_telegram(&settings) ? 0 : 1;
    }
    
    bool verbose = !args.quiet;
    
    if (verbose) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        printf("[%s] checking ISSAE announcements...\n", timestamp);
        
        config_problems_t problems;
        err = settings_get_problems(&settings, &problems);
        if (err == ISAE_OK) {
            for (size_t i = 0; i < problems.count; i++) {
                printf("  warning: %s\n", problems.messages[i]);
            }
        }
    }
    
    /* Run pipeline */
    run_report_t report;
    run_report_init(&report);
    
    err = pipeline_run(&settings, args.dry_run, args.bootstrap, verbose, &report);
    
    if (err == ISAE_ERR_FEED) {
        fprintf(stderr, "feed error: could not reach the feed\n");
        return 2;
    }
    
    if (err == ISAE_ERR_INTERRUPTED) {
        fprintf(stderr, "\ninterrupted\n");
        return 130;
    }
    
    if (verbose || err != ISAE_OK) {
        char rendered[2048];
        err = run_report_render(&report, rendered, sizeof(rendered));
        if (err == ISAE_OK) {
            printf("\n%s\n", rendered);
        }
    }
    
    return report.error_count > 0 ? 1 : 0;
}
