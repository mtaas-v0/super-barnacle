#include "blaze_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

static const char* g_current_step = "Initialization";
static int g_current_line = 0;

#define STEP(desc) \
    do { \
        g_current_step = desc; \
        g_current_line = __LINE__; \
        printf("\n===> [STEP @ Line %d]: %s\n", __LINE__, desc); \
        fflush(stdout); \
    } while(0)

/* Signal handler to intercept and pinpoint SegFaults */
static void crash_handler(int sig) {
    const char* sig_name = "UNKNOWN";
    if (sig == SIGSEGV) sig_name = "SIGSEGV (Segmentation Fault / Bad Pointer)";
    if (sig == SIGABRT) sig_name = "SIGABRT (Aborted / Assertion Failed)";
    if (sig == SIGFPE)  sig_name = "SIGFPE (Arithmetic Exception)";
    if (sig == SIGILL)  sig_name = "SIGILL (Illegal Instruction)";

    fprintf(stderr, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    fprintf(stderr, " CRASH DETECTED!\n");
    fprintf(stderr, " Signal:   %s (%d)\n", sig_name, sig);
    fprintf(stderr, " At Step:  %s\n", g_current_step);
    fprintf(stderr, " Near Line:%d\n", g_current_line);
    fprintf(stderr, " Check the last [BLAZE_C_DEBUG] output right above this message!\n");
    fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    fflush(stderr);
    _exit(sig);
}

int main(void) {
    /* Register crash handlers */
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);
    signal(SIGILL,  crash_handler);

    printf("=========================================================\n");
    printf("   Blaze C Wrapper Diagnostic Test Suite\n");
    printf("=========================================================\n");
    fflush(stdout);

    STEP("Enable verbose debug logging inside blaze_c.cpp");
    blaze_set_debug_logging(true);

    /* JSON Schema string */
    const char* schema_json = 
        "{\n"
        "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n"
        "  \"title\": \"UserProfile\",\n"
        "  \"type\": \"object\",\n"
        "  \"properties\": {\n"
        "    \"name\": { \"type\": \"string\", \"description\": \"User full name\" },\n"
        "    \"age\": { \"type\": \"integer\", \"minimum\": 0 }\n"
        "  },\n"
        "  \"required\": [\"name\", \"age\"]\n"
        "}";

    STEP("Calling blaze_schema_compile()...");
    blaze_schema_t* schema = blaze_schema_compile(schema_json, BLAZE_MODE_FULL_DIAGNOSTICS);
    printf("Returned schema pointer: %p\n", (void*)schema);
    fflush(stdout);
    assert(schema != NULL && "blaze_schema_compile returned NULL");

    STEP("Calling blaze_evaluator_create()...");
    blaze_evaluator_t* evaluator = blaze_evaluator_create();
    printf("Returned evaluator pointer: %p\n", (void*)evaluator);
    fflush(stdout);
    assert(evaluator != NULL && "blaze_evaluator_create returned NULL");

    STEP("Calling blaze_evaluator_evaluate() with VALID instance...");
    const char* valid_instance = "{\"name\": \"Alice\", \"age\": 30}";
    blaze_result_t* res_valid = blaze_evaluator_evaluate(evaluator, schema, valid_instance);
    printf("Returned result pointer: %p\n", (void*)res_valid);
    fflush(stdout);
    assert(res_valid != NULL && "blaze_evaluator_evaluate returned NULL");

    STEP("Calling blaze_result_is_valid() on valid instance...");
    bool is_valid = blaze_result_is_valid(res_valid);
    printf("Result is_valid: %s\n", is_valid ? "true" : "false");
    fflush(stdout);
    assert(is_valid == true && "Valid instance failed validation");

    STEP("Calling blaze_result_get_annotations() on valid instance...");
    blaze_annotation_iterator_t* ann_it = blaze_result_get_annotations(res_valid);
    printf("Returned annotation iterator pointer: %p\n", (void*)ann_it);
    fflush(stdout);
    assert(ann_it != NULL);

    STEP("Iterating annotations...");
    while (blaze_annotation_iterator_next(ann_it)) {
        const char* inst_loc = blaze_annotation_get_instance_location(ann_it);
        const char* keyword = blaze_annotation_get_keyword(ann_it);
        const char* val_json = blaze_annotation_get_value_json(ann_it);
        printf("    -> Annotation [%s] at '%s' = %s\n",
               keyword ? keyword : "",
               inst_loc ? inst_loc : "",
               val_json ? val_json : "null");
        fflush(stdout);
    }

    STEP("Destroying annotation iterator and valid result...");
    blaze_annotation_iterator_destroy(ann_it);
    blaze_result_destroy(res_valid);

    STEP("Calling blaze_evaluator_evaluate() with INVALID instance...");
    const char* invalid_instance = "{\"age\": -5}";
    blaze_result_t* res_invalid = blaze_evaluator_evaluate(evaluator, schema, invalid_instance);
    printf("Returned invalid result pointer: %p\n", (void*)res_invalid);
    fflush(stdout);
    assert(res_invalid != NULL);

    STEP("Calling blaze_result_is_valid() on invalid instance...");
    bool is_invalid_pass = blaze_result_is_valid(res_invalid);
    printf("Result is_valid: %s\n", is_invalid_pass ? "true" : "false");
    fflush(stdout);
    assert(is_invalid_pass == false && "Invalid instance unexpectedly passed");

    STEP("Calling blaze_result_get_errors() on invalid instance...");
    blaze_error_iterator_t* err_it = blaze_result_get_errors(res_invalid);
    printf("Returned error iterator pointer: %p\n", (void*)err_it);
    fflush(stdout);
    assert(err_it != NULL);

    STEP("Iterating error diagnostics...");
    int err_count = 0;
    while (blaze_error_iterator_next(err_it)) {
        err_count++;
        const char* inst_loc = blaze_error_get_instance_location(err_it);
        const char* schema_loc = blaze_error_get_schema_location(err_it);
        const char* msg = blaze_error_get_message(err_it);
        printf("    -> Error #%d: [%s] @ schema[%s] : %s\n",
               err_count,
               inst_loc ? inst_loc : "(root)",
               schema_loc ? schema_loc : "(none)",
               msg ? msg : "");
        fflush(stdout);
    }
    assert(err_count > 0 && "Expected error diagnostics, got 0");

    STEP("Destroying error iterator and invalid result...");
    blaze_error_iterator_destroy(err_it);
    blaze_result_destroy(res_invalid);

    STEP("Destroying schema and evaluator...");
    blaze_evaluator_destroy(evaluator);
    blaze_schema_destroy(schema);

    STEP("Test completed successfully!");
    printf("\n=========================================================\n");
    printf("   All Tests Passed with Zero Segfaults!\n");
    printf("=========================================================\n");
    return 0;
}
