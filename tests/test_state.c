/* Round-trip + migration + corruption-quarantine tests for state.c.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *          -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu -I/usr/include/libxml2 \
 *          src/state.c third_party/cjson/cJSON.c tests/test_state.c -o /tmp/test_state -lm
 * Run:   /tmp/test_state
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "isae_monitor/state.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("[ OK ] %s\n", msg); } \
    else      { fail++; printf("[FAIL] %s\n", msg); } \
} while (0)

static const char* TMP_DIR = "/tmp/isae_state_test";

#define UNUSED __attribute__((unused))

static void cleanup_tmp(void) {
    /* best effort */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TMP_DIR);
    int r UNUSED = system(cmd);
    (void)r;
}

static char* read_file_to_string(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

int main(void) {
    cleanup_tmp();
    mkdir(TMP_DIR, 0755);

    /* --- Test 1: empty state, save, reload --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen1.json", TMP_DIR);
        state_manager_t s;
        CHECK(state_init(&s, path, 1000) == ISAE_OK, "init");
        CHECK(state_save(&s) == ISAE_OK, "save empty state is a no-op (dirty=false)");

        /* Mutations flip dirty, so save writes a file. */
        CHECK(state_mark_general_sent(&s, "tag:blogger.com,2025:post-1") == ISAE_OK, "mark_general_sent");
        CHECK(state_mark_classified(&s, "tag:blogger.com,2025:post-1", "informatique") == ISAE_OK, "mark_classified");
        CHECK(state_save(&s) == ISAE_OK, "save after marks");
        state_cleanup(&s);

        /* Reload */
        state_init(&s, path, 1000);
        state_load(&s);
        const state_entry_t* e = state_get(&s, "tag:blogger.com,2025:post-1");
        CHECK(e != NULL, "entry loaded");
        CHECK(e && e->g == true, "g=true preserved");
        CHECK(e && strcmp(e->c, "informatique") == 0, "c='informatique' preserved");
        state_cleanup(&s);
    }

    /* --- Test 2: two-phase -- general sent but classification pending --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen2.json", TMP_DIR);
        state_manager_t s;
        state_init(&s, path, 1000);
        state_mark_general_sent(&s, "id-only-general");
        state_save(&s);
        state_cleanup(&s);

        state_init(&s, path, 1000);
        state_load(&s);
        CHECK(state_needs_general(&s, "id-only-general") == false, "g set -> needs_general=false");
        CHECK(state_needs_classification(&s, "id-only-general") == true, "c empty -> needs_classification=true");
        state_cleanup(&s);
    }

    /* --- Test 3: v0 migration (plain list) --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen3.json", TMP_DIR);
        FILE* f = fopen(path, "wb");
        fputs("[\"tag:old-1\",\"tag:old-2\"]", f);
        fclose(f);

        state_manager_t s;
        state_init(&s, path, 1000);
        state_load(&s);
        const state_entry_t* e = state_get(&s, "tag:old-1");
        CHECK(e != NULL, "v0 migration: entry present");
        CHECK(e && e->g == true, "v0 migration: g=true (was already sent)");
        CHECK(e && e->c[0] == '\0', "v0 migration: c empty (pending)");
        state_save(&s);
        state_cleanup(&s);

        /* After save, file is v2. */
        char* contents = read_file_to_string(path);
        CHECK(contents && strstr(contents, "\"version\":2") != NULL, "v0 migrated to v2 on save");
        free(contents);
    }

    /* --- Test 4: v1 migration (general_sent + classified) --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen4.json", TMP_DIR);
        FILE* f = fopen(path, "wb");
        fputs("{\"general_sent\":[\"g1\"],\"classified\":[\"c1\"]}", f);
        fclose(f);

        state_manager_t s;
        state_init(&s, path, 1000);
        state_load(&s);
        const state_entry_t* g1 = state_get(&s, "g1");
        const state_entry_t* c1 = state_get(&s, "c1");
        CHECK(g1 && g1->g == true, "v1: g1 has g=true");
        CHECK(g1 && g1->c[0] == '\0', "v1: g1 has c empty (was only in general_sent)");
        CHECK(c1 && c1->g == true, "v1: c1 has g=true (Python sets g for classified too)");
        CHECK(c1 && strcmp(c1->c, "unknown") == 0, "v1: c1 has c='unknown'");
        state_cleanup(&s);
    }

    /* --- Test 5: corruption quarantine --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen5.json", TMP_DIR);
        FILE* f = fopen(path, "wb");
        fputs("{ this is not valid JSON", f);
        fclose(f);

        state_manager_t s;
        state_init(&s, path, 1000);
        state_load(&s);
        CHECK(s.recovered_from_corruption == true, "corrupt file sets recovered flag");
        struct stat st;
        char corrupt_path[256];
        snprintf(corrupt_path, sizeof(corrupt_path), "%s.corrupt", path);
        CHECK(stat(corrupt_path, &st) == 0, "corrupt file moved to .corrupt");
        state_cleanup(&s);
    }

    /* --- Test 6: pruning --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen6.json", TMP_DIR);
        state_manager_t s;
        state_init(&s, path, 5); /* history=5 */
        for (int i = 0; i < 20; i++) {
            char k[64];
            snprintf(k, sizeof(k), "tag:post-%d", i);
            state_mark_general_sent(&s, k);
        }
        state_save(&s);
        CHECK(s.count == 5, "pruned to history=5");
        state_cleanup(&s);
    }

    /* --- Test 7: bootstrap value preserved --- */
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/seen7.json", TMP_DIR);
        state_manager_t s;
        state_init(&s, path, 1000);
        state_mark_all_seen(&s, "bootstrap-id", NULL);
        state_save(&s);
        state_cleanup(&s);

        state_init(&s, path, 1000);
        state_load(&s);
        const state_entry_t* e = state_get(&s, "bootstrap-id");
        CHECK(e && e->g == true, "bootstrap: g=true");
        CHECK(e && strcmp(e->c, "bootstrap") == 0, "bootstrap: c='bootstrap'");
        CHECK(state_needs_classification(&s, "bootstrap-id") == false, "bootstrap: needs_classification=false (c truthy)");
        state_cleanup(&s);
    }

    cleanup_tmp();
    printf("\n%d passed, %d failed.\n", pass, fail);
    return fail ? 1 : 0;
}
