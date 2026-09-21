/* Keyword classification tests, mirroring tests/test_classify.py.
 * Build: gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *          -Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu -I/usr/include/libxml2 \
 *          src/models.c src/departments.c src/keywords.c third_party/cjson/cJSON.c tests/test_keywords.c \
 *          -o /tmp/test_keywords -lm
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "isae_monitor/keywords.h"
#include "isae_monitor/models.h"
#include "isae_monitor/departments.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int pass = 0, fail = 0;

static void run_case(const char* name, const char* title, const char* summary, const char* want) {
    announcement_t ann;
    announcement_init(&ann);
    if (title)   ann.title   = strdup(title);
    if (summary) ann.summary = strdup(summary);

    keyword_result_t r;
    isae_error_t err = keywords_classify(&ann, &r);
    if (err != ISAE_OK) {
        printf("[FAIL] %s -> error %d\n", name, err);
        fail++;
    } else if (strcmp(r.category, want) != 0) {
        printf("[FAIL] %s\n       got:  '%s'\n       want: '%s'\n", name, r.category, want);
        fail++;
    } else {
        printf("[ OK ] %s -> '%s'\n", name, r.category);
        pass++;
    }
    announcement_cleanup(&ann);
}

int main(void) {
    departments_init();

    /* Title-only department matches (drawn from tests/test_classify.py). */
    run_case("No 9 Genie Electrique - Oraux probatoires",
             "No 9 Genie Electrique - Oraux probatoires", NULL, "electrique");
    run_case("Informatique - Resultats des examens",
             "Informatique - Resultats des examens", NULL, "informatique");
    run_case("Genie civil - Examens d'admission",
             "Génie civil - Examens d'admission", NULL, "civil");
    run_case("Master Economie et gestion",
             "Master Economie et gestion", NULL, "economie");
    run_case("Programmation Java : bibliothèques et patterns - NFA035",
             "Programmation Java : bibliothèques et patterns - NFA035", NULL, "informatique");
    run_case("cours intensif niveau DELF B2",
             "cours intensif niveau DELF B2", NULL, "langues");

    /* OTHER_MARKERS wins over departments. */
    run_case("Offre d'emploi: Comptable / Auditeur (Dekwaneh)",
             "Offre d'emploi: Comptable / Auditeur (Dekwaneh)", NULL, "other");

    /* Arabic general marker. */
    run_case("Arabic: announcement to all students",
             "إعلان إلى طلاب بيروت وكافة المراكز", NULL, "general");

    /* Arabic department match. */
    run_case("Arabic: civil engineering circular",
             "تعميم لطلاب الهندسة المدنية", NULL, "civil");

    /* Title beats summary on tie (TITLE_WEIGHT=3 vs SUMMARY_WEIGHT=1). */
    run_case("Title outweighs summary",
             "Génie Civil - reunion",
             "rappel: cours d'informatique annulé",
             "civil");

    /* Conservative default: nothing matches. */
    run_case("Unmatched falls to GENERAL",
             "Note de service 42", NULL, "general");

    /* Tie test: two departments scoring equally -> GENERAL (no decision). */
    run_case("Tie: informatique + economie in same title",
             "Informatique Economie",
             NULL,
             "general");

    /* Department-specific multi-word bonus. */
    run_case("Multi-word keyword 'genie mecanique'",
             "Reunion genie mecanique",
             NULL,
             "mecanique");

    printf("\n%d passed, %d failed.\n", pass, fail);
    return fail ? 1 : 0;
}
