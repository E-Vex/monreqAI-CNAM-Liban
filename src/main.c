/**
 * ISAE Monitor - Main Entry Point
 *
 * Faithful C port of Python isae_monitor/cli.py.
 *
 * CLI flags (mirror Python):
 *   --dry-run            classify and print, send nothing
 *   --bootstrap          mark current feed entries as seen, send nothing
 *   --check              validate config + state, exit 0 if has_telegram else 1
 *   --list-departments   print the 9-department table
 *   --quiet              suppress per-announcement progress output
 *   --version            print version and exit
 *   --help               print usage and exit
 *
 * Exit codes (mirror Python):
 *   0   success
 *   1   errors in run report, OR --check with has_telegram=false
 *   2   feed fetch / parse failure
 *   130 SIGINT caught mid-run
 */

#include "isae_monitor/pipeline.h"
#include "isae_monitor/config.h"
#include "isae_monitor/departments.h"

#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_interrupted = 0;
static pipeline_t* g_pipeline = NULL;

static void signal_handler(int signum) {
    (void)signum;
    g_interrupted = 1;
    if (g_pipeline) pipeline_stop(g_pipeline);
}

static void print_version(void) {
    printf("isae_monitor (monreqAI-CNAM-Liban C edition) version %s\n", ISAE_VERSION_STRING);
    printf("Pure C11 implementation -- no Python runtime.\n");
}

static void print_usage(const char* progname) {
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("monreqAI-CNAM-Liban -- ISAE / Cnam-Liban announcement monitor.\n\n");
    printf("Watches the institute's Atom feed, sends each new post to a\n");
    printf("Telegram general channel, classifies it via Gemini / OpenRouter /\n");
    printf("keyword fallback, and forwards it to the matching department channel.\n\n");
    printf("The tool is one-shot (run it from cron every 20 min).\n\n");
    printf("Options:\n");
    printf("  --dry-run            Classify and print, send nothing to Telegram.\n");
    printf("  --bootstrap          Mark every current feed entry as seen without\n");
    printf("                       notifying. Use on first install to avoid a backlog flood.\n");
    printf("  --check              Print resolved config and exit (0 if telegram is usable, 1 if not).\n");
    printf("  --list-departments   Print the 9-department table and exit.\n");
    printf("  --quiet              Suppress per-announcement progress output.\n");
    printf("  --version            Print version information and exit.\n");
    printf("  --help               Show this help message and exit.\n\n");
    printf("Configuration (read from environment / .env):\n");
    printf("  FEED_URL, STATE_FILE, STATE_HISTORY\n");
    printf("  GEMINI_API_KEYS (comma-separated), GEMINI_MODEL\n");
    printf("  OPENROUTER_API_KEY, OPENROUTER_MODEL\n");
    printf("  TELEGRAM_BOT_TOKEN, TELEGRAM_CHANNEL_GENERAL,\n");
    printf("  TELEGRAM_CHANNEL_<DEPT> (one per department)\n");
    printf("  REQUEST_TIMEOUT, MAX_RETRIES, SEND_INTERVAL\n\n");
    printf("Source: https://github.com/E-Vex/monreqAI-CNAM-Liban\n");
}

static void warning_printer(const char* msg, void* user) {
    (void)user;
    fprintf(stderr, "warning: %s\n", msg);
}

int main(int argc, char* argv[]) {
    bool dry_run = false;
    bool bootstrap = false;
    bool check = false;
    bool list_departments = false;
    bool quiet = false;

    static struct option long_options[] = {
        {"dry-run",         no_argument, 0, 'd'},
        {"bootstrap",       no_argument, 0, 'b'},
        {"check",           no_argument, 0, 'c'},
        {"list-departments",no_argument, 0, 'L'},
        {"quiet",           no_argument, 0, 'q'},
        {"version",         no_argument, 0, 'v'},
        {"help",            no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "dbcLqvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd': dry_run = true; break;
            case 'b': bootstrap = true; break;
            case 'c': check = true; break;
            case 'L': list_departments = true; break;
            case 'q': quiet = true; break;
            case 'v': print_version(); return EXIT_SUCCESS;
            case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    /* Signal handlers. Use sigaction so SA_RESTART is off (interrupting
     * system calls returns EINTR rather than auto-retrying). */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* no SA_RESTART */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* --list-departments does NOT require config or feed access. */
    if (list_departments) {
        settings_t settings;
        config_init(&settings);
        config_load(&settings);
        pipeline_list_departments(&settings);
        return EXIT_SUCCESS;
    }

    /* Initialize pipeline (loads .env, reads env vars, opens state file). */
    pipeline_t pipeline_buf;
    g_pipeline = &pipeline_buf;
    isae_error_t err = pipeline_init(g_pipeline);
    if (err != ISAE_OK) {
        fprintf(stderr, "Failed to initialize pipeline: %s\n", isae_strerror(err));
        pipeline_cleanup(g_pipeline);
        return EXIT_FAILURE;
    }

    /* --check prints config + problems, then exits 0 if has_telegram else 1. */
    if (check) {
        char* summary = config_summary(&g_pipeline->settings);
        if (summary) { fputs(summary, stdout); free(summary); }
        config_problems(&g_pipeline->settings, warning_printer, NULL);
        bool has_tg = config_has_telegram(&g_pipeline->settings);
        pipeline_cleanup(g_pipeline);
        return has_tg ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Apply CLI mode flags. */
    g_pipeline->settings.dry_run = dry_run;
    g_pipeline->settings.bootstrap = bootstrap;
    g_pipeline->settings.quiet = quiet;
    /* Propagate dry_run to the Telegram client. */
    g_pipeline->telegram.dry_run = dry_run;
    g_pipeline->whatsapp.dry_run = dry_run;

    /* Verbose timestamp + warnings. */
    if (!quiet) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        printf("monreqAI v%s -- run at %s\n", ISAE_VERSION_STRING, ts);
        if (!config_has_ai(&g_pipeline->settings)) {
            fprintf(stderr, "warning: no AI provider configured -- keyword fallback will be used for every announcement.\n");
        }
        if (!config_has_telegram(&g_pipeline->settings)) {
            fprintf(stderr, "warning: telegram not fully configured -- notifications will be skipped.\n");
        }
        if (dry_run) printf("(dry-run: no Telegram messages will be sent)\n");
        if (bootstrap) printf("(bootstrap: marking current feed entries as seen)\n");
    }

    /* Run one pass. */
    err = pipeline_run(g_pipeline);

    /* Render the run report. */
    if (!quiet || err == ISAE_ERR_FEED) {
        char* report = pipeline_report_render(&g_pipeline->report);
        if (report) { fputs(report, stdout); free(report); }
    }

    /* Determine exit code. */
    int exit_code;
    if (g_interrupted || err == ISAE_ERR_INTERRUPTED) {
        fprintf(stderr, "interrupted\n");
        exit_code = 130;
    } else if (err == ISAE_ERR_FEED) {
        fprintf(stderr, "feed error: %s\n", isae_strerror(err));
        exit_code = 2;
    } else if (err != ISAE_OK) {
        fprintf(stderr, "pipeline error: %s\n", isae_strerror(err));
        exit_code = 1;
    } else if (g_pipeline->report.error_count > 0) {
        /* Mirror Python: any error in the report -> exit 1. */
        exit_code = 1;
    } else {
        exit_code = 0;
    }

    pipeline_cleanup(g_pipeline);
    return exit_code;
}
