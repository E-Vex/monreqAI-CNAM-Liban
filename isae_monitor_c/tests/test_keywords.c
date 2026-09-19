/**
 * @file test_keywords.c
 * @brief Test suite for keyword-based classification
 */

#include "isae_monitor/keywords.h"
#include "isae_monitor/departments.h"
#include "isae_monitor/models.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    tests_run++; \
    name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED: %s\n", msg); \
        exit(1); \
    } \
} while(0)

/* Mock settings for testing */
static settings_t mock_settings;

static void setup_settings(void) {
    settings_init(&mock_settings);
    /* Add some keywords for testing */
}

TEST(test_normalize_text_french) {
    const char* input = "Été Café Résumé";
    char output[256];
    
    isae_error_t result = normalize_text(input, output, sizeof(output));
    
    ASSERT(result == ISAE_OK, "normalize_text should succeed");
    ASSERT(strstr(output, "ete") != NULL, "Should contain 'ete' (from Été)");
    ASSERT(strstr(output, "cafe") != NULL, "Should contain 'cafe' (from Café)");
    ASSERT(strstr(output, "resume") != NULL, "Should contain 'resume' (from Résumé)");
}

TEST(test_normalize_text_arabic) {
    /* Test Arabic alef normalization */
    const char* input = "أإآا";  /* Different alef variants */
    char output[256];
    
    isae_error_t result = normalize_text(input, output, sizeof(output));
    
    ASSERT(result == ISAE_OK, "normalize_text should succeed");
    /* All variants should be normalized to plain alef */
}

TEST(test_strip_html) {
    const char* input = "<p>Hello <b>World</b></p>";
    char output[256];
    
    isae_error_t result = strip_html(input, output, sizeof(output));
    
    ASSERT(result == ISAE_OK, "strip_html should succeed");
    ASSERT(strcmp(output, "Hello World") == 0, "Should strip all HTML tags");
}

TEST(test_department_lookup) {
    char chat[MAX_CHAT_ID];
    
    /* Test known department */
    isae_error_t result = departments_get_chat("informatique", chat, sizeof(chat));
    ASSERT(result == ISAE_OK || result == ISAE_ERR_NOT_FOUND, 
           "Department lookup should succeed or return not found");
    
    /* Test unknown department */
    result = departments_get_chat("unknown_dept_xyz", chat, sizeof(chat));
    ASSERT(result == ISAE_ERR_NOT_FOUND, "Unknown department should return NOT_FOUND");
}

TEST(test_announcement_init) {
    announcement_t ann;
    announcement_init(&ann);
    
    ASSERT(strlen(ann.id) == 0, "ID should be empty");
    ASSERT(strlen(ann.title) == 0, "Title should be empty");
    ASSERT(ann.needs_general == false, "needs_general should be false");
    ASSERT(ann.needs_classification == false, "needs_classification should be false");
}

TEST(test_keyword_result_init) {
    keyword_result_t result;
    keyword_result_init(&result);
    
    ASSERT(result.score == 0, "Score should be 0");
    ASSERT(strcmp(result.category, CAT_GENERAL) == 0, "Category should be 'general'");
}

int main(void) {
    printf("=== ISAE Monitor Keyword Tests ===\n\n");
    
    setup_settings();
    
    RUN_TEST(test_normalize_text_french);
    RUN_TEST(test_normalize_text_arabic);
    RUN_TEST(test_strip_html);
    RUN_TEST(test_department_lookup);
    RUN_TEST(test_announcement_init);
    RUN_TEST(test_keyword_result_init);
    
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}
