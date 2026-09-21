/* Sanity test for models.c text normalization.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *          -Iinclude -Ithird_party/cjson \
 *          src/models.c tests/test_normalize.c -o /tmp/test_normalize -lm
 * Run:   /tmp/test_normalize
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "isae_monitor/models.h"
#include <stdio.h>
#include <string.h>

struct tc { const char* name; const char* input; const char* want; } cases[] = {
    { "Génie Électrique -> genie electrique", "Génie Électrique", "genie electrique" },
    { "ÉCOLE -> ecole",                       "ÉCOLE",            "ecole" },
    { "Arabic alef variants fold",            "أَهْلًا إعلان",      "اهلا اعلان" },
    { "Arabic ta-marbuta -> ha",              "مدرسة",            "مدرسه" },
    { "Arabic alef-maqsura -> ya",            "على",              "علي" },
    { "HTML entity &eacute;",                 "caf&eacute;",      "cafe" },
    { "HTML entity &#233;",                   "caf&#233;",        "cafe" },
    { "HTML entity &#xE9;",                   "caf&#xE9;",        "cafe" },
    { "HTML entity &amp;",                    "a &amp; b",        "a & b" },
    { "HTML entity &nbsp;",                   "a&nbsp;b",         "a b" },
    { "strip_html simple",                     "<p>Bonjour</p>",  "Bonjour" },
    { "strip_html entities (accents preserved)", "<b>caf&eacute;</b>", "café" },
    { "collapse whitespace",                   "  a\n  b\t c  ",  "a b c" },
    { "ASCII passthrough",                     "Hello World",     "hello world" },
    { NULL, NULL, NULL }
};

int main(void) {
    int failed = 0;
    char out[8192];
    for (int i = 0; cases[i].name; i++) {
        out[0] = '\0';
        isae_error_t err;
        if (strstr(cases[i].name, "strip_html")) {
            err = strip_html(cases[i].input, out, sizeof(out));
        } else {
            err = normalize_text(cases[i].input, out, sizeof(out));
        }
        if (err != ISAE_OK) {
            printf("[FAIL] %s -> error %d\n", cases[i].name, err);
            failed++;
            continue;
        }
        if (strcmp(out, cases[i].want) != 0) {
            printf("[FAIL] %s\n       got:  '%s'\n       want: '%s'\n",
                   cases[i].name, out, cases[i].want);
            failed++;
        } else {
            printf("[ OK ] %s -> '%s'\n", cases[i].name, out);
        }
    }
    if (failed) {
        printf("\n%d test(s) failed.\n", failed);
        return 1;
    }
    printf("\nAll tests passed.\n");
    return 0;
}

