#include "blaze_c.h"


// Blaze modules
#include <sourcemeta/blaze/compiler.h>
#include <sourcemeta/blaze/evaluator.h>
#include <sourcemeta/blaze/foundation.h>

// Sourcemeta Core JSON AST
#include <sourcemeta/core/json.h>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/* -------------------------------------------------------------------------
 * Internal Representation & Struct Definitions
 * ------------------------------------------------------------------------- */

struct blaze_schema {
    sourcemeta::blaze::Schema obj;
};

struct blaze_evaluator {
    sourcemeta::blaze::Evaluator obj;
};

struct blaze_result {
    sourcemeta::blaze::Result obj;
    bool holds_validity{false};
};

struct blaze_error_item {
    std::string instance_location;
    std::string schema_location;
    std::string message;
};

struct blaze_error_iterator {
    std::vector<blaze_error_item> errors;
    ptrdiff_t index{-1};
};

struct blaze_annotation_item {
    std::string instance_location;
    std::string keyword;
    std::string value_json;
};

struct blaze_annotation_iterator {
    std::vector<blaze_annotation_item> annotations;
    ptrdiff_t index{-1};
};

/* -------------------------------------------------------------------------
 * Internal Utility Helpers
 * ------------------------------------------------------------------------- */

namespace {

// Serializes a sourcemeta::core::JSON AST node into a valid JSON string literal
std::string stringify_json(const sourcemeta::core::JSON& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

// Formats JSON Pointer paths into standard RFC 6901 strings
template <typename T>
std::string location_to_string(const T& location) {
    std::ostringstream stream;
    stream << location;
    return stream.str();
}

} // namespace

/* -------------------------------------------------------------------------
 * C ABI Implementations (extern "C")
 * ------------------------------------------------------------------------- */

extern "C" {

/* --- Lifecycle & Schema Compilation --- */

blaze_schema_t* blaze_schema_compile(const char* schema_json, blaze_mode_t mode) {
    if (!schema_json) {
        return nullptr;
    }

    try {
        const auto native_mode = (mode == BLAZE_MODE_FULL_DIAGNOSTICS)
            ? sourcemeta::blaze::Mode::FullDiagnostics
            : sourcemeta::blaze::Mode::FastValidation;

        // Parse JSON Schema AST
        const auto parsed_schema = sourcemeta::core::JSON::parse(schema_json);

        // Compile to Blaze internal intermediate instructions
        auto native_schema = sourcemeta::blaze::compile(parsed_schema, native_mode);

        return new blaze_schema{ std::move(native_schema) };
    } catch (...) {
        return nullptr;
    }
}

void blaze_schema_destroy(blaze_schema_t* schema) {
    delete schema;
}

blaze_evaluator_t* blaze_evaluator_create(void) {
    try {
        return new blaze_evaluator{};
    } catch (...) {
        return nullptr;
    }
}

void blaze_evaluator_destroy(blaze_evaluator_t* evaluator) {
    delete evaluator;
}

/* --- Execution & Interactive Instance Evaluation --- */

blaze_result_t* blaze_evaluator_evaluate(blaze_evaluator_t* evaluator,
                                         const blaze_schema_t* schema,
                                         const char* instance_json) {
    if (!evaluator || !schema || !instance_json) {
        return nullptr;
    }

    try {
        const auto instance = sourcemeta::core::JSON::parse(instance_json);
        auto native_result = evaluator->obj.validate(schema->obj, instance);
        const bool is_valid = static_cast<bool>(native_result);

        return new blaze_result{ std::move(native_result), is_valid };
    } catch (...) {
        return nullptr;
    }
}

void blaze_result_destroy(blaze_result_t* result) {
    delete result;
}

bool blaze_result_is_valid(const blaze_result_t* result) {
    if (!result) {
        return false;
    }
    return result->holds_validity;
}

/* --- Diagnostic Errors Interface --- */

blaze_error_iterator_t* blaze_result_get_errors(const blaze_result_t* result) {
    if (!result) {
        return nullptr;
    }

    try {
        auto* iterator = new blaze_error_iterator{};

        // Bootstrap snapshot: Eagerly extract and deep-copy diagnostics into flat heap memory
        for (const auto& error : result->obj.errors()) {
            blaze_error_item item;
            item.instance_location = location_to_string(error.instance_location());
            item.schema_location = location_to_string(error.schema_location());
            item.message = std::string(error.message());

            iterator->errors.push_back(std::move(item));
        }

        return iterator;
    } catch (...) {
        return nullptr;
    }
}

void blaze_error_iterator_destroy(blaze_error_iterator_t* iterator) {
    delete iterator;
}

bool blaze_error_iterator_next(blaze_error_iterator_t* iterator) {
    if (!iterator) {
        return false;
    }

    const auto total = static_cast<ptrdiff_t>(iterator->errors.size());
    if (iterator->index + 1 < total) {
        ++iterator->index;
        return true;
    }

    iterator->index = total; // Advance to end sentinel
    return false;
}

const char* blaze_error_get_instance_location(const blaze_error_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->errors.size())) {
        return nullptr;
    }
    return iterator->errors[static_cast<size_t>(iterator->index)].instance_location.c_str();
}

const char* blaze_error_get_schema_location(const blaze_error_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->errors.size())) {
        return nullptr;
    }
    return iterator->errors[static_cast<size_t>(iterator->index)].schema_location.c_str();
}

const char* blaze_error_get_message(const blaze_error_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->errors.size())) {
        return nullptr;
    }
    return iterator->errors[static_cast<size_t>(iterator->index)].message.c_str();
}

/* --- Annotations Interface --- */

blaze_annotation_iterator_t* blaze_result_get_annotations(const blaze_result_t* result) {
    if (!result) {
        return nullptr;
    }

    try {
        auto* iterator = new blaze_annotation_iterator{};

        // Bootstrap snapshot: Eagerly extract metadata and stringify values to avoid invalidation
        for (const auto& annotation : result->obj.annotations()) {
            blaze_annotation_item item;
            item.instance_location = location_to_string(annotation.instance_location());
            item.keyword = std::string(annotation.keyword());
            item.value_json = stringify_json(annotation.value());

            iterator->annotations.push_back(std::move(item));
        }

        return iterator;
    } catch (...) {
        return nullptr;
    }
}

void blaze_annotation_iterator_destroy(blaze_annotation_iterator_t* iterator) {
    delete iterator;
}

bool blaze_annotation_iterator_next(blaze_annotation_iterator_t* iterator) {
    if (!iterator) {
        return false;
    }

    const auto total = static_cast<ptrdiff_t>(iterator->annotations.size());
    if (iterator->index + 1 < total) {
        ++iterator->index;
        return true;
    }

    iterator->index = total; // Advance to end sentinel
    return false;
}

const char* blaze_annotation_get_instance_location(const blaze_annotation_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->annotations.size())) {
        return nullptr;
    }
    return iterator->annotations[static_cast<size_t>(iterator->index)].instance_location.c_str();
}

const char* blaze_annotation_get_keyword(const blaze_annotation_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->annotations.size())) {
        return nullptr;
    }
    return iterator->annotations[static_cast<size_t>(iterator->index)].keyword.c_str();
}

const char* blaze_annotation_get_value_json(const blaze_annotation_iterator_t* iterator) {
    if (!iterator || iterator->index < 0 ||
        iterator->index >= static_cast<ptrdiff_t>(iterator->annotations.size())) {
        return nullptr;
    }
    return iterator->annotations[static_cast<size_t>(iterator->index)].value_json.c_str();
}

} // extern "C"