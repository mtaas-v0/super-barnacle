#include "blaze_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("=== Blaze C Wrapper Test Suite ===\n");

    /* 1. Compile Schema with full diagnostics enabled */
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

    printf("[1] Compiling Schema...\n");
    blaze_schema_t* schema = blaze_schema_compile(schema_json, BLAZE_MODE_FULL_DIAGNOSTICS);
    assert(schema != NULL && "Schema compilation failed!");

    /* 2. Create Reusable Evaluator */
    printf("[2] Creating Evaluator...\n");
    blaze_evaluator_t* evaluator = blaze_evaluator_create();
    assert(evaluator != NULL && "Evaluator allocation failed!");

    /* 3. Test Pass: Valid Instance JSON */
    printf("[3] Evaluating Valid Instance...\n");
    const char* valid_instance = "{\"name\": \"Alice\", \"age\": 30}";
    blaze_result_t* res_valid = blaze_evaluator_evaluate(evaluator, schema, valid_instance);
    assert(res_valid != NULL);
    assert(blaze_result_is_valid(res_valid) == true && "Valid instance incorrectly failed validation!");
    
    /* 4. Test Annotations Extraction on Valid Instance */
    printf("    Inspecting Annotations...\n");
    blaze_annotation_iterator_t* ann_it = blaze_result_get_annotations(res_valid);
    assert(ann_it != NULL);
    int annotation_count = 0;
    while (blaze_annotation_iterator_next(ann_it)) {
        annotation_count++;
        const char* inst_loc = blaze_annotation_get_instance_location(ann_it);
        const char* keyword = blaze_annotation_get_keyword(ann_it);
        const char* val_json = blaze_annotation_get_value_json(ann_it);
        printf("    -> Annotation [%s] at '%s' = %s\n", 
               keyword ? keyword : "N/A", 
               inst_loc ? inst_loc : "", 
               val_json ? val_json : "null");
    }
    blaze_annotation_iterator_destroy(ann_it);
    blaze_result_destroy(res_valid);

    /* 5. Test Fail: Interactive Invalid Keystroke (Wrong type for 'age' + missing 'name') */
    printf("[4] Evaluating Invalid Instance (Interactive Editor Scenario)...\n");
    const char* invalid_instance = "{\"age\": -5}";
    blaze_result_t* res_invalid = blaze_evaluator_evaluate(evaluator, schema, invalid_instance);
    assert(res_invalid != NULL);
    assert(blaze_result_is_valid(res_invalid) == false && "Invalid instance passed validation!");

    /* 6. Test Diagnostic Errors & JSON Pointer Extraction */
    printf("    Extracting Red-Squiggly Diagnostics...\n");
    blaze_error_iterator_t* err_it = blaze_result_get_errors(res_invalid);
    assert(err_it != NULL);

    int error_count = 0;
    while (blaze_error_iterator_next(err_it)) {
        error_count++;
        const char* inst_loc = blaze_error_get_instance_location(err_it);
        const char* schema_loc = blaze_error_get_schema_location(err_it);
        const char* message = blaze_error_get_message(err_it);

        printf("    -> Error #%d:\n", error_count);
        printf("       Instance Location: %s\n", inst_loc ? inst_loc : "(root)");
        printf("       Schema Rule:       %s\n", schema_loc ? schema_loc : "(unknown)");
        printf("       Message:           %s\n", message ? message : "(none)");
    }
    assert(error_count > 0 && "Expected error diagnostics, but none were produced!");
    blaze_error_iterator_destroy(err_it);
    blaze_result_destroy(res_invalid);

    /* 7. Cleanup */
    printf("[5] Freeing allocated resources...\n");
    blaze_evaluator_destroy(evaluator);
    blaze_schema_destroy(schema);

    printf("=== All Tests Passed Successfully! ===\n");
    return 0;
}
