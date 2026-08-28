#include "blaze_c.h"

// Blaze modules
#include <sourcemeta/blaze/compiler.h>
#include <sourcemeta/blaze/evaluator.h>
#include <sourcemeta/blaze/foundation.h>

// Sourcemeta Core JSON AST
#include <sourcemeta/core/json.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

/* -------------------------------------------------------------------------
 * Internal Utility & Type Deduction
 * ------------------------------------------------------------------------- */

namespace {

inline sourcemeta::core::JSON parse_json_input(const char* input) {
    return sourcemeta::core::parse_json(input);
}

} // namespace

using BlazeSchemaType = decltype(sourcemeta::blaze::compile(
    std::declval<sourcemeta::core::JSON>(),
    sourcemeta::blaze::schema_walker,
    sourcemeta::blaze::schema_resolver,
    sourcemeta::blaze::default_schema_compiler,
    sourcemeta::blaze::Mode::FastValidation
));

/* -------------------------------------------------------------------------
 * Internal Wrapper Structures
 * ------------------------------------------------------------------------- */

struct blaze_schema {
    BlazeSchemaType obj;
};

struct blaze_evaluator {
    sourcemeta::blaze::Evaluator obj;
};

struct blaze_error_item {
    std::string instance_location;
    std::string schema_location;
    std::string message;
};

struct blaze_annotation_item {
    std::string instance_location;
    std::string keyword;
    std::string value_json;
};

struct blaze_result {
    bool holds_validity{false};
    std::vector<blaze_error_item> errors;
    std::vector<blaze_annotation_item> annotations;
};

struct blaze_error_iterator {
    std::vector<blaze_error_item> errors;
    ptrdiff_t index{-1};
};

struct blaze_annotation_iterator {
    std::vector<blaze_annotation_item> annotations;
    ptrdiff_t index{-1};
};

/* -------------------------------------------------------------------------
 * C ABI Implementations (extern "C")
 * ------------------------------------------------------------------------- */

extern "C" {

/* --- Lifecycle & Schema Compilation --- */

blaze_schema_t* blaze_schema_compile(const char* schema_json, blaze_mode_t mode) {
    if (!schema_json) {
        return nullptr;
    }

    (void)mode; // Blaze compiler runs in Mode::FastValidation

    try {
        const auto parsed_schema = parse_json_input(schema_json);

        auto native_schema = sourcemeta::blaze::compile(
            parsed_schema,
            sourcemeta::blaze::schema_walker,
            sourcemeta::blaze::schema_resolver,
            sourcemeta::blaze::default_schema_compiler,
            sourcemeta::blaze::Mode::FastValidation
        );

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

/* --- Execution & Instance Evaluation --- */

blaze_result_t* blaze_evaluator_evaluate(blaze_evaluator_t* evaluator,
                                         const blaze_schema_t* schema,
                                         const char* instance_json) {
    if (!evaluator || !schema || !instance_json) {
        return nullptr;
    }

    try {
        const auto instance = parse_json_input(instance_json);
        const bool is_valid = evaluator->obj.validate(schema->obj, instance);

        auto* res = new blaze_result{};
        res->holds_validity = is_valid;

        if (!is_valid) {
            blaze_error_item err;
            err.instance_location = ""; // Root path
            err.schema_location = "";
            err.message = "Instance validation failed against compiled schema";
            res->errors.push_back(std::move(err));
        }

        return res;
    } catch (const std::exception& e) {
        auto* res = new blaze_result{};
        res->holds_validity = false;
        blaze_error_item err;
        err.instance_location = "";
        err.schema_location = "";
        err.message = e.what();
        res->errors.push_back(std::move(err));
        return res;
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
        iterator->errors = result->errors;
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

    iterator->index = total;
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
        iterator->annotations = result->annotations;
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

    iterator->index = total;
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