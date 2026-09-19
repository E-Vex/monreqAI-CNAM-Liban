/**
 * ISAE Monitor - Main Entry Point
 * 
 * Maps Python: main.py -> C: main.c
 * 
 * CLI argument parsing and program entry point.
 */

#include "isae_monitor/pipeline.h"
#include <signal.h>
#include <getopt.h>
#include <sys/types.h>

static pipeline_t g_pipeline;
static volatile sig_atomic_t g_interrupted = 0;

static void signal_handler(int signum) {
    (void)signum;
    g_interrupted = 1;
    if (g_pipeline.running) {
        pipeline_stop(&g_pipeline);
    }
}

static void print_version(void) {
    printf("isae_monitor version %s\n", ISAE_VERSION_STRING);
    printf("Pure C11 implementation - No Python runtime\n");
}

static void print_usage(const char* progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("\nISAE School Announcement Monitor\n\n");
    printf("Options:\n");
    printf("  -i, --interval SEC   Polling interval in seconds (default: 300)\n");
    printf("  -o, --once           Run once and exit (no continuous monitoring)\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -v, --version        Show version information\n");
    printf("\nEnvironment Variables:\n");
    printf("  ISAE_STATE_FILE      Path to state file (default: ~/.isae_monitor_state.json)\n");
    printf("  ISAE_POLL_INTERVAL   Default polling interval\n");
    printf("  ISAE_HTTP_TIMEOUT    HTTP request timeout in seconds\n");
    printf("  GEMINI_API_KEY       Google Gemini API key for classification\n");
    printf("  OPENROUTER_API_KEY   OpenRouter API key for classification\n");
    printf("  TELEGRAM_BOT_TOKEN   Telegram bot token for notifications\n");
    printf("  TELEGRAM_CHAT_IDS    Comma-separated Telegram chat IDs\n");
    printf("\nExample:\n");
    printf("  export GEMINI_API_KEY=\"your-key\"\n");
    printf("  export TELEGRAM_BOT_TOKEN=\"bot-token\"\n");
    printf("  export TELEGRAM_CHAT_IDS=\"123456789\"\n");
    printf("  %s --interval 60\n", progname);
}

int main(int argc, char* argv[]) {
    int interval = 300;
    bool once_mode = false;
    
    /* Parse command line arguments */
    static struct option long_options[] = {
        {"interval", required_argument, 0, 'i'},
        {"once",     no_argument,       0, 'o'},
        {"help",     no_argument,       0, 'h'},
        {"version",  no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "i:ohv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                interval = atoi(optarg);
                if (interval <= 0) {
                    fprintf(stderr, "Error: Invalid interval value\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'o':
                once_mode = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case 'v':
                print_version();
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    /* Setup signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    printf("ISAE Monitor C v%s\n", ISAE_VERSION_STRING);
    printf("========================\n\n");
    
    /* Initialize pipeline */
    isae_error_t err = pipeline_init(&g_pipeline);
    if (err != ISAE_OK) {
        fprintf(stderr, "Failed to initialize pipeline: %s\n", isae_strerror(err));
        return EXIT_FAILURE;
    }
    
    /* Override interval from command line */
    if (once_mode) {
        err = pipeline_run_once(&g_pipeline);
    } else {
        err = pipeline_run(&g_pipeline, interval);
    }
    
    if (err != ISAE_OK && err != ISAE_OK) {
        fprintf(stderr, "Pipeline error: %s\n", isae_strerror(err));
        pipeline_cleanup(&g_pipeline);
        return EXIT_FAILURE;
    }
    
    /* Cleanup */
    pipeline_cleanup(&g_pipeline);
    
    printf("Goodbye!\n");
    return EXIT_SUCCESS;
}
