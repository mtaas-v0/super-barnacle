#ifndef BLAZE_C_H
#define BLAZE_C_H

#include <stddef.h>
#include <stdbool.h>

/* --- Shared Library / DLL Export Definitions --- */
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
  #if defined(BLAZE_BUILD_DLL)
    #define BLAZE_API __declspec(dllexport)
  #elif defined(BLAZE_USE_DLL)
    #define BLAZE_API __declspec(dllimport)
  #else
    #define BLAZE_API
  #endif
#else
  #if defined(__GNUC__) && __GNUC__ >= 4
    #define BLAZE_API __attribute__((visibility("default")))
  #else
    #define BLAZE_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Core Opaque Types --- */
typedef struct blaze_schema blaze_schema_t;
typedef struct blaze_evaluator blaze_evaluator_t;
typedef struct blaze_result blaze_result_t;
typedef struct blaze_error_iterator blaze_error_iterator_t;
typedef struct blaze_annotation_iterator blaze_annotation_iterator_t;

/* --- Configuration Enums --- */
typedef enum {
    BLAZE_MODE_FAST_VALIDATION = 0,
    BLAZE_MODE_FULL_DIAGNOSTICS = 1
} blaze_mode_t;

/* --- Lifecycle & Schema Compilation --- */
BLAZE_API blaze_schema_t* blaze_schema_compile(const char* schema_json, blaze_mode_t mode);
BLAZE_API void blaze_schema_destroy(blaze_schema_t* schema);

BLAZE_API blaze_evaluator_t* blaze_evaluator_create(void);
BLAZE_API void blaze_evaluator_destroy(blaze_evaluator_t* evaluator);

/* --- Execution & Evaluation --- */
BLAZE_API blaze_result_t* blaze_evaluator_evaluate(blaze_evaluator_t* evaluator, 
                                                   const blaze_schema_t* schema, 
                                                   const char* instance_json);
BLAZE_API void blaze_result_destroy(blaze_result_t* result);
BLAZE_API bool blaze_result_is_valid(const blaze_result_t* result);

/* --- Errors Interface --- */
BLAZE_API blaze_error_iterator_t* blaze_result_get_errors(const blaze_result_t* result);
BLAZE_API void blaze_error_iterator_destroy(blaze_error_iterator_t* iterator);
BLAZE_API bool blaze_error_iterator_next(blaze_error_iterator_t* iterator);
BLAZE_API const char* blaze_error_get_instance_location(const blaze_error_iterator_t* iterator);
BLAZE_API const char* blaze_error_get_schema_location(const blaze_error_iterator_t* iterator);
BLAZE_API const char* blaze_error_get_message(const blaze_error_iterator_t* iterator);

/* --- Annotations Interface --- */
BLAZE_API blaze_annotation_iterator_t* blaze_result_get_annotations(const blaze_result_t* result);
BLAZE_API void blaze_annotation_iterator_destroy(blaze_annotation_iterator_t* iterator);
BLAZE_API bool blaze_annotation_iterator_next(blaze_annotation_iterator_t* iterator);
BLAZE_API const char* blaze_annotation_get_instance_location(const blaze_annotation_iterator_t* iterator);
BLAZE_API const char* blaze_annotation_get_keyword(const blaze_annotation_iterator_t* iterator);
BLAZE_API const char* blaze_annotation_get_value_json(const blaze_annotation_iterator_t* iterator);

/* --- Diagnostic / Debug Control --- */
BLAZE_API void blaze_set_debug_logging(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* BLAZE_C_H */
