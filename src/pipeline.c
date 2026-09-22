/**
 * ISAE Monitor - Pipeline
 *
 * Faithful C port of Python isae_monitor/pipeline.py.
 *
 * One-shot semantics: this is a cron-driven tool, NOT a daemon.
 *
 * Per-run flow:
 *   1. Fetch the Atom feed (single URL).
 *   2. Diff against seen.json: find entries where needs_general OR
 *      needs_classification is true.
 *   3. For each pending entry (in feed order, which is oldest-first
 *      because feed_fetch reversed it):
 *      a. If needs_general AND general_channel is set, send to general.
 *         On success: state.mark_general_sent(id).
 *         On failure: record error; g stays false (retries next run).
 *      b. If needs_classification: classify (always succeeds; keyword
 *         fallback). Route to the department channel if configured.
 *         On successful dept delivery: state.mark_classified(id, cat).
 *         On failure: c stays NULL (re-classify next run).
 *      c. state.save() -- persisted after every announcement so SIGKILL
 *         loses at most one item's progress.
 *   4. After the loop, surface classifier dead-key notes into the report.
 */

#include "isae_monitor/pipeline.h"
#include "isae_monitor/departments.h"
#include "isae_monitor/feed.h"
#include "isae_monitor/models.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Report                                                              */
/* ------------------------------------------------------------------ */

void pipeline_report_init(pipeline_report_t* r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

void pipeline_report_add_error(pipeline_report_t* r, const char* msg) {
    if (!r || !msg) return;
    if (r->error_count >= (int)(sizeof(r->errors) / sizeof(r->errors[0]))) return;
    strncpy(r->errors[r->error_count], msg, sizeof(r->errors[0]) - 1);
    r->errors[r->error_count][sizeof(r->errors[0]) - 1] = '\0';
    r->error_count++;
}

void pipeline_report_add_classified(pipeline_report_t* r, const char* category) {
    if (!r || !category) return;
    for (int i = 0; i < r->category_count; i++) {
        if (strcmp(r->categories_seen[i], category) == 0) {
            r->categories_count[i]++;
            return;
        }
    }
    if (r->category_count >= (int)(sizeof(r->categories_seen) / sizeof(r->categories_seen[0]))) return;
    strncpy(r->categories_seen[r->category_count], category, sizeof(r->categories_seen[0]) - 1);
    r->categories_seen[r->category_count][sizeof(r->categories_seen[0]) - 1] = '\0';
    r->categories_count[r->category_count] = 1;
    r->category_count++;
}

char* pipeline_report_render(const pipeline_report_t* r) {
    if (!r) return NULL;
    /* Size the buffer from the actual content instead of a fixed 2048:
     * the fixed size used to overflow when a run collected 16 long
     * errors + categories (snprintf's return value made `w` exceed the
     * buffer size, turning `2048 - w` into a huge size_t and writing
     * past the heap chunk). */
    size_t need = 256;
    for (int i = 0; i < r->category_count; i++) {
        need += strlen(r->categories_seen[i]) + 40;
    }
    for (int i = 0; i < r->error_count; i++) {
        need += strlen(r->errors[i]) + 8;
    }
    char* buf = (char*)malloc(need);
    if (!buf) return NULL;
    size_t w = 0;
    size_t rem = need;
    int n;
#define APPEND(...) do { \
        n = snprintf(buf + w, rem, __VA_ARGS__); \
        if (n < 0) { free(buf); return NULL; } \
        if ((size_t)n >= rem) { w += rem > 0 ? rem - 1 : 0; rem = 1; break; } \
        w += (size_t)n; rem -= (size_t)n; \
    } while (0)

    APPEND("Run summary:\n");
    APPEND("  fetched        : %zu\n", r->fetched);
    APPEND("  pending        : %zu\n", r->pending);
    APPEND("  general_sent   : %zu\n", r->general_sent);
    APPEND("  department_sent: %zu\n", r->department_sent);
    APPEND("  fallback_used  : %zu\n", r->fallback_used);
    if (r->category_count > 0) {
        APPEND("  classified     :\n");
        for (int i = 0; i < r->category_count; i++) {
            APPEND("    %-20s : %d\n", r->categories_seen[i], r->categories_count[i]);
        }
    }
    if (r->error_count > 0) {
        APPEND("  errors (%d)     :\n", r->error_count);
        for (int i = 0; i < r->error_count; i++) {
            APPEND("    %s\n", r->errors[i]);
        }
    }
#undef APPEND
    return buf;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

isae_error_t pipeline_init(pipeline_t* pipeline) {
    if (!pipeline) return ISAE_ERR_INVALID_PARAM;
    memset(pipeline, 0, sizeof(*pipeline));

    departments_init();

    config_init(&pipeline->settings);
    isae_error_t err = config_load(&pipeline->settings);
    if (err != ISAE_OK) return err;

    err = state_init(&pipeline->state, pipeline->settings.state_file, pipeline->settings.state_history);
    if (err != ISAE_OK) return err;
    err = state_load(&pipeline->state);
    if (err != ISAE_OK) return err;

    classifier_init(&pipeline->classifier, &pipeline->settings);

    http_client_config_t http_cfg;
    http_client_config_default(&http_cfg);
    http_cfg.timeout_seconds = pipeline->settings.request_timeout;
    http_cfg.max_retries = pipeline->settings.max_retries;

    telegram_client_init(&pipeline->telegram,
                         pipeline->settings.telegram_bot_token,
                         &http_cfg,
                         pipeline->settings.send_interval,
                         pipeline->settings.dry_run);
    return ISAE_OK;
}

void pipeline_cleanup(pipeline_t* pipeline) {
    if (!pipeline) return;
    /* Best-effort save on exit (matches Python's __exit__ swallowing OSError). */
    (void)state_save(&pipeline->state);
    classifier_cleanup(&pipeline->classifier);
    telegram_client_cleanup(&pipeline->telegram);
    state_cleanup(&pipeline->state);
}

void pipeline_stop(pipeline_t* pipeline) {
    if (!pipeline) return;
    pipeline->interrupted = 1;
}

/* ------------------------------------------------------------------ */
/* List departments (--list-departments)                              */
/* ------------------------------------------------------------------ */

void pipeline_list_departments(const settings_t* settings) {
    if (!settings) return;
    printf("%zu departments at ISSAE / Cnam Liban:\n\n", departments_count());
    /* Compute column widths so the output lines up. */
    size_t name_w = 0;
    size_t key_w = 0;
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        size_t n = strlen(d->name_fr);
        if (n > name_w) name_w = n;
        size_t k = strlen(d->key);
        if (k > key_w) key_w = k;
    }
    for (size_t i = 0; i < departments_count(); i++) {
        const department_t* d = departments_get_at(i);
        const char* env_name = departments_env_var(d->key);
        bool configured = settings->department_channels[i][0] != '\0';
        printf("  %-*s  %-*s  %s  %s\n",
               (int)name_w, d->name_fr,
               (int)key_w, d->key,
               env_name ? env_name : "(unknown)",
               configured ? "configured" : "-");
    }
    printf("\n  plus the pseudo-categories 'general' (TELEGRAM_CHANNEL_GENERAL)\n"
           "  and 'other' (never routed).\n");
}

/* ------------------------------------------------------------------ */
/* One pending announcement                                            */
/* ------------------------------------------------------------------ */

static isae_error_t process_pending(pipeline_t* pipeline, const feed_entry_t* entry) {
    if (!pipeline || !entry) return ISAE_ERR_INVALID_PARAM;
    settings_t* s = &pipeline->settings;
    pipeline_report_t* r = &pipeline->report;

    const char* key = entry->id;

    /* Convert to announcement_t. */
    announcement_t ann;
    isae_error_t err = feed_entry_to_announcement(entry, &ann);
    if (err != ISAE_OK) return err;

    /* --- Phase A: send to general channel (if needed & configured) --- */
    if (state_needs_general(&pipeline->state, key) && s->telegram_channel_general[0]) {
        char* msg = telegram_format_message(&ann, NULL);  /* no label for general */
        if (msg) {
            long status = 0;
            isae_error_t send_err = telegram_send_message(&pipeline->telegram,
                                                          s->telegram_channel_general,
                                                          msg, &status);
            if (send_err == ISAE_OK) {
                state_mark_general_sent(&pipeline->state, key);
                r->general_sent++;
            } else {
                char emsg[256];
                snprintf(emsg, sizeof(emsg), "general send failed: HTTP %ld", status);
                pipeline_report_add_error(r, emsg);
                /* g stays false; retries next run. */
            }
            free(msg);
        }
    }

    /* --- Phase B: classify (always runs if needs_classification,
     * even if general send failed) --- */
    if (state_needs_classification(&pipeline->state, key)) {
        classification_result_t result;
        classification_result_init(&result);
        err = classifier_classify(&pipeline->classifier, &ann, &result);
        if (err != ISAE_OK) {
            /* Should never happen -- keyword fallback always succeeds. */
            char emsg[256];
            snprintf(emsg, sizeof(emsg), "classifier returned error: %s", isae_strerror(err));
            pipeline_report_add_error(r, emsg);
            announcement_cleanup(&ann);
            return err;
        }

        pipeline_report_add_classified(r, result.category);
        if (!result.is_ai) r->fallback_used++;
        if (!s->quiet) {
            printf("    -> %s (via %s)\n",
                    departments_label_for(result.category) ? departments_label_for(result.category) : result.category,
                    result.source);
        }

        /* Route to department channel if configured. */
        const char* dept_chat = config_dept_channel(s, result.category);
        bool delivered = false;
        if (dept_chat) {
            char* msg = telegram_format_message(&ann, result.category);
            if (msg) {
                long status = 0;
                isae_error_t send_err = telegram_send_message(&pipeline->telegram, dept_chat, msg, &status);
                if (send_err == ISAE_OK) {
                    r->department_sent++;
                    delivered = true;
                } else {
                    char emsg[256];
                    snprintf(emsg, sizeof(emsg), "dept send failed for %s: HTTP %ld", result.category, status);
                    pipeline_report_add_error(r, emsg);
                    /* c stays NULL; re-classify next run. */
                }
                free(msg);
            }
        }
        if (delivered) {
            state_mark_classified(&pipeline->state, key, result.category);
        }
        /* ann.category kept for diagnostics, no need to free separately */
    }

    announcement_cleanup(&ann);

    /* Save after every announcement (Python's load-bearing durability). */
    if (pipeline->interrupted) {
        (void)state_save(&pipeline->state);
        return ISAE_ERR_INTERRUPTED;
    }
    return state_save(&pipeline->state);
}

/* ------------------------------------------------------------------ */
/* pipeline_run -- one-shot pass                                       */
/* ------------------------------------------------------------------ */

isae_error_t pipeline_run(pipeline_t* pipeline) {
    if (!pipeline) return ISAE_ERR_INVALID_PARAM;
    pipeline_report_init(&pipeline->report);

    if (pipeline->state.recovered_from_corruption) {
        pipeline_report_add_error(&pipeline->report,
            "state file was corrupt -- moved to .corrupt. Run --bootstrap to mark current feed entries as seen.");
    }

    /* Fetch the feed (single URL, not per-department). */
    feed_result_t feed;
    feed_result_init(&feed);
    http_client_config_t http_cfg = pipeline->telegram.http_cfg;
    isae_error_t err = feed_fetch(pipeline->settings.feed_url, &http_cfg, &feed);
    if (err != ISAE_OK) {
        feed_result_cleanup(&feed);
        return ISAE_ERR_FEED;
    }
    pipeline->report.fetched = feed.count;

    /* Bootstrap mode: mark every current entry as seen, send nothing. */
    if (pipeline->settings.bootstrap) {
        for (size_t i = 0; i < feed.count; i++) {
            state_mark_all_seen(&pipeline->state, feed.entries[i].id, NULL);
        }
        (void)state_save(&pipeline->state);
        if (!pipeline->settings.quiet) {
            printf("Bootstrap: marked %zu feed entries as seen.\n", feed.count);
        }
        feed_result_cleanup(&feed);
        return ISAE_OK;
    }

    /* Find pending entries. */
    size_t pending = 0;
    for (size_t i = 0; i < feed.count; i++) {
        if (state_needs_general(&pipeline->state, feed.entries[i].id) ||
            state_needs_classification(&pipeline->state, feed.entries[i].id)) {
            pending++;
        }
    }
    pipeline->report.pending = pending;

    if (pending == 0) {
        if (!pipeline->settings.quiet) printf("Nothing to do.\n");
        feed_result_cleanup(&feed);
        return ISAE_OK;
    }

    if (!pipeline->settings.quiet) {
        printf("Processing %zu pending announcement(s)...\n", pending);
    }

    /* Process each pending entry. */
    for (size_t i = 0; i < feed.count; i++) {
        const char* key = feed.entries[i].id;
        if (!key || !key[0]) continue;
        if (!state_needs_general(&pipeline->state, key) &&
            !state_needs_classification(&pipeline->state, key)) continue;

        if (!pipeline->settings.quiet) {
            printf("\n  %s\n", feed.entries[i].title[0] ? feed.entries[i].title : "(sans titre)");
        }

        err = process_pending(pipeline, &feed.entries[i]);
        if (err == ISAE_ERR_INTERRUPTED) {
            feed_result_cleanup(&feed);
            return err;
        }
        if (err != ISAE_OK) {
            char emsg[256];
            snprintf(emsg, sizeof(emsg), "process error: %s", isae_strerror(err));
            pipeline_report_add_error(&pipeline->report, emsg);
        }
    }

    /* Surface classifier notes (dead keys, etc) as errors in the report. */
    int notes_count = 0;
    const char* const* notes = classifier_notes(&pipeline->classifier, &notes_count);
    for (int i = 0; i < notes_count; i++) {
        pipeline_report_add_error(&pipeline->report, notes[i]);
    }

    feed_result_cleanup(&feed);
    return ISAE_OK;
}
