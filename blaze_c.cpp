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
 * Internal Utility & Safe JSON Access Helpers
 * ------------------------------------------------------------------------- */

namespace {

inline sourcemeta::core::JSON parse_json_input(const char* input) {
    return sourcemeta::core::parse_json(input);
}

inline const sourcemeta::core::JSON* json_try_get(const sourcemeta::core::JSON& val, std::string_view key) {
    if (!val.is_object()) return nullptr;
    try {
        return &val.at(key);
    } catch (...) {
        return nullptr;
    }
}

inline bool json_has_key(const sourcemeta::core::JSON& val, std::string_view key) {
    return json_try_get(val, key) != nullptr;
}

std::string json_type_name(const sourcemeta::core::JSON& val) {
    if (val.is_null()) return "null";
    if (val.is_boolean()) return "boolean";
    if (val.is_integer()) return "integer";
    if (val.is_real()) return "number";
    if (val.is_string()) return "string";
    if (val.is_array()) return "array";
    if (val.is_object()) return "object";
    return "unknown";
}

std::string to_json_string(const sourcemeta::core::JSON& val) {
    std::ostringstream ss;
    ss << val;
    return ss.str();
}

} // namespace

/* -------------------------------------------------------------------------
 * Type Deduction for In-Tree Blaze Return Types
 * ------------------------------------------------------------------------- */

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
    sourcemeta::core::JSON raw_schema;
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
 * Diagnostic Drilldown & Annotation Extractors
 * ------------------------------------------------------------------------- */

namespace {

void collect_annotations(const sourcemeta::core::JSON& schema,
                         const std::string& current_path,
                         std::vector<blaze_annotation_item>& annotations) {
    if (!schema.is_object()) return;

    for (const auto& keyword : {"title", "description", "default", "examples"}) {
        if (const auto* val = json_try_get(schema, keyword)) {
            annotations.push_back(blaze_annotation_item{
                current_path,
                keyword,
                to_json_string(*val)
            });
        }
    }

    if (const auto* props = json_try_get(schema, "properties")) {
        if (props->is_object()) {
            try {
                for (const auto& [prop_key, prop_schema] : *props) {
                    std::string subpath = current_path + "/" + std::string(prop_key);
                    collect_annotations(prop_schema, subpath, annotations);
                }
            } catch (...) {}
        }
    }
}

void diagnose_instance(const sourcemeta::core::JSON& schema,
                       const sourcemeta::core::JSON& instance,
                       const std::string& inst_path,
                       const std::string& schema_path,
                       std::vector<blaze_error_item>& errors) {
    if (!schema.is_object()) return;

    // 1. Check type assertion
    if (const auto* type_node = json_try_get(schema, "type")) {
        if (type_node->is_string()) {
            std::string expected_type = type_node->to_string();
            std::string actual_type = json_type_name(instance);

            bool type_match = (expected_type == actual_type) ||
                              (expected_type == "number" && actual_type == "integer");

            if (!type_match) {
                errors.push_back(blaze_error_item{
                    inst_path.empty() ? "/" : inst_path,
                    schema_path + "/type",
                    "Expected type '" + expected_type + "', but found " + actual_type
                });
                return;
            }
        }
    }

    // 2. Check required properties (for objects)
    if (const auto* req_node = json_try_get(schema, "required")) {
        if (req_node->is_array() && instance.is_object()) {
            for (std::size_t i = 0; i < req_node->size(); ++i) {
                try {
                    std::string req_key = req_node->at(i).to_string();
                    if (!json_has_key(instance, req_key)) {
                        errors.push_back(blaze_error_item{
                            inst_path + "/" + req_key,
                            schema_path + "/required",
                            "Missing required property '" + req_key + "'"
                        });
                    }
                } catch (...) {}
            }
        }
    }

    // 3. Check numeric constraints (minimum, maximum)
    if (instance.is_integer() || instance.is_real()) {
        double val = instance.is_integer() ? static_cast<double>(instance.to_integer()) : instance.to_real();
        if (const auto* min_node = json_try_get(schema, "minimum")) {
            double min_val = min_node->is_integer() ? static_cast<double>(min_node->to_integer()) : min_node->to_real();
            if (val < min_val) {
                errors.push_back(blaze_error_item{
                    inst_path.empty() ? "/" : inst_path,
                    schema_path + "/minimum",
                    "Value " + to_json_string(instance) + " is less than minimum " + to_json_string(*min_node)
                });
            }
        }
        if (const auto* max_node = json_try_get(schema, "maximum")) {
            double max_val = max_node->is_integer() ? static_cast<double>(max_node->to_integer()) : max_node->to_real();
            if (val > max_val) {
                errors.push_back(blaze_error_item{
                    inst_path.empty() ? "/" : inst_path,
                    schema_path + "/maximum",
                    "Value " + to_json_string(instance) + " exceeds maximum " + to_json_string(*max_node)
                });
            }
        }
    }

    // 4. Check string constraints (minLength)
    if (instance.is_string()) {
        std::size_t len = instance.to_string().size();
        if (const auto* min_l_node = json_try_get(schema, "minLength")) {
            if (min_l_node->is_integer()) {
                std::size_t min_l = static_cast<std::size_t>(min_l_node->to_integer());
                if (len < min_l) {
                    errors.push_back(blaze_error_item{
                        inst_path.empty() ? "/" : inst_path,
                        schema_path + "/minLength",
                        "String length is shorter than minLength " + std::to_string(min_l)
                    });
                }
            }
        }
    }

    // 5. Recursively inspect properties
    if (const auto* props = json_try_get(schema, "properties")) {
        if (props->is_object() && instance.is_object()) {
            try {
                for (const auto& [prop_key, prop_schema] : *props) {
                    if (const auto* inst_val = json_try_get(instance, prop_key)) {
                        std::string sub_inst_path = inst_path + "/" + std::string(prop_key);
                        std::string sub_schema_path = schema_path + "/properties/" + std::string(prop_key);
                        diagnose_instance(prop_schema, *inst_val, sub_inst_path, sub_schema_path, errors);
                    }
                }
            } catch (...) {}
        }
    }
}

} // namespace

/* -------------------------------------------------------------------------
 * C ABI Implementations (extern "C")
 * ------------------------------------------------------------------------- */

extern "C" {

/* --- Lifecycle & Schema Compilation --- */

blaze_schema_t* blaze_schema_compile(const char* schema_json, blaze_mode_t mode) {
    if (!schema_json) return nullptr;
    (void)mode;

    try {
        const auto parsed_schema = parse_json_input(schema_json);

        auto native_schema = sourcemeta::blaze::compile(
            parsed_schema,
            sourcemeta::blaze::schema_walker,
            sourcemeta::blaze::schema_resolver,
            sourcemeta::blaze::default_schema_compiler,
            sourcemeta::blaze::Mode::FastValidation
        );

        return new blaze_schema{ std::move(native_schema), parsed_schema };
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

        // Collect annotations for form tooltips
        collect_annotations(schema->raw_schema, "", res->annotations);

        // If validation failed, extract exact error paths
        if (!is_valid) {
            diagnose_instance(schema->raw_schema, instance, "", "", res->errors);

            if (res->errors.empty()) {
                res->errors.push_back(blaze_error_item{
                    "/",
                    "/",
                    "Instance validation failed against compiled schema"
                });
            }
        }

        return res;
    } catch (const std::exception& e) {
        auto* res = new blaze_result{};
        res->holds_validity = false;
        res->errors.push_back(blaze_error_item{ "/", "/", e.what() });
        return res;
    } catch (...) {
        return nullptr;
    }
}

void blaze_result_destroy(blaze_result_t* result) {
    delete result;
}

bool blaze_result_is_valid(const blaze_result_t* result) {
    if (!result) return false;
    return result->holds_validity;
}

/* --- Diagnostic Errors Interface --- */

blaze_error_iterator_t* blaze_result_get_errors(const blaze_result_t* result) {
    if (!result) return nullptr;

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
    if (!iterator) return false;

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
    if (!result) return nullptr;

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
    if (!iterator) return false;

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
