#include "blaze_c.h"

// Blaze modules
#include <sourcemeta/blaze/compiler.h>
#include <sourcemeta/blaze/evaluator.h>
#include <sourcemeta/blaze/foundation.h>

// Sourcemeta Core JSON AST
#include <sourcemeta/core/json.h>

#include <cstdio>
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
 * Internal Debug Logger
 * ------------------------------------------------------------------------- */

static bool g_debug_logging = true;

#define DBG_LOG(fmt, ...) \
    do { \
        if (g_debug_logging) { \
            std::fprintf(stderr, "[BLAZE_C_DEBUG] %s:%d: " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            std::fflush(stderr); \
        } \
    } while(0)

extern "C" void blaze_set_debug_logging(bool enabled) {
    g_debug_logging = enabled;
}

/* -------------------------------------------------------------------------
 * Internal Helpers
 * ------------------------------------------------------------------------- */

namespace {

inline sourcemeta::core::JSON parse_json_input(const char* input) {
    return sourcemeta::core::parse_json(input);
}

inline const sourcemeta::core::JSON* json_try_get(const sourcemeta::core::JSON& val, std::string_view key) {
    try {
        if (!val.is_object()) return nullptr;
        return &val.at(std::string(key));
    } catch (...) {
        return nullptr;
    }
}

inline bool json_has_key(const sourcemeta::core::JSON& val, std::string_view key) {
    return json_try_get(val, key) != nullptr;
}

std::string to_json_string(const sourcemeta::core::JSON& val) {
    try {
        std::ostringstream ss;
        ss << val;
        return ss.str();
    } catch (...) {
        return "null";
    }
}

std::string clean_string(const sourcemeta::core::JSON& val) {
    try {
        if (!val.is_string()) return "";
        std::string s = std::string(val.to_string());
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    } catch (...) {
        return "";
    }
}

double clean_number(const sourcemeta::core::JSON& val) {
    try {
        if (val.is_integer()) return static_cast<double>(val.to_integer());
        if (val.is_real()) return val.to_real();
    } catch (...) {}
    return 0.0;
}

std::string json_type_name(const sourcemeta::core::JSON& val) {
    try {
        if (val.is_null()) return "null";
        if (val.is_boolean()) return "boolean";
        if (val.is_integer()) return "integer";
        if (val.is_real()) return "number";
        if (val.is_string()) return "string";
        if (val.is_array()) return "array";
        if (val.is_object()) return "object";
    } catch (...) {}
    return "unknown";
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
    std::string schema_str;
    std::unique_ptr<sourcemeta::core::JSON> raw_schema;
    std::unique_ptr<BlazeSchemaType> obj;
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

    const auto* props = json_try_get(schema, "properties");
    const auto* req_node = json_try_get(schema, "required");
    if (props && props->is_object() && req_node && req_node->is_array()) {
        for (std::size_t i = 0; i < req_node->size(); ++i) {
            try {
                std::string prop_key = clean_string(req_node->at(i));
                if (const auto* prop_schema = json_try_get(*props, prop_key)) {
                    std::string subpath = current_path + "/" + prop_key;
                    collect_annotations(*prop_schema, subpath, annotations);
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
            std::string expected_type = clean_string(*type_node);
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

    // 2. Check required properties
    const auto* req_node = json_try_get(schema, "required");
    const auto* props = json_try_get(schema, "properties");

    if (req_node && req_node->is_array() && instance.is_object()) {
        for (std::size_t i = 0; i < req_node->size(); ++i) {
            try {
                std::string req_key = clean_string(req_node->at(i));
                if (!json_has_key(instance, req_key)) {
                    errors.push_back(blaze_error_item{
                        inst_path + "/" + req_key,
                        schema_path + "/required",
                        "Missing required property '" + req_key + "'"
                    });
                } else if (props && props->is_object()) {
                    if (const auto* prop_schema = json_try_get(*props, req_key)) {
                        if (const auto* inst_val = json_try_get(instance, req_key)) {
                            std::string sub_inst_path = inst_path + "/" + req_key;
                            std::string sub_schema_path = schema_path + "/properties/" + req_key;
                            diagnose_instance(*prop_schema, *inst_val, sub_inst_path, sub_schema_path, errors);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 3. Check numeric constraints
    if (instance.is_integer() || instance.is_real()) {
        double val = clean_number(instance);
        if (const auto* min_node = json_try_get(schema, "minimum")) {
            double min_val = clean_number(*min_node);
            if (val < min_val) {
                errors.push_back(blaze_error_item{
                    inst_path.empty() ? "/" : inst_path,
                    schema_path + "/minimum",
                    "Value " + to_json_string(instance) + " is less than minimum " + to_json_string(*min_node)
                });
            }
        }
        if (const auto* max_node = json_try_get(schema, "maximum")) {
            double max_val = clean_number(*max_node);
            if (val > max_val) {
                errors.push_back(blaze_error_item{
                    inst_path.empty() ? "/" : inst_path,
                    schema_path + "/maximum",
                    "Value " + to_json_string(instance) + " exceeds maximum " + to_json_string(*max_node)
                });
            }
        }
    }

    // 4. Check string constraints
    if (instance.is_string()) {
        std::size_t len = clean_string(instance).size();
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
}

} // namespace

/* -------------------------------------------------------------------------
 * C ABI Implementations (extern "C")
 * ------------------------------------------------------------------------- */

extern "C" {

blaze_schema_t* blaze_schema_compile(const char* schema_json, blaze_mode_t mode) {
    DBG_LOG("Entering (mode=%d, schema_ptr=%p)", mode, (void*)schema_json);
    if (!schema_json) {
        DBG_LOG("Error: schema_json is NULL");
        return nullptr;
    }
    (void)mode;

    try {
        DBG_LOG("Step 1: Allocating blaze_schema container...");
        auto schema = std::make_unique<blaze_schema>();

        DBG_LOG("Step 2: Copying raw schema string...");
        schema->schema_str = schema_json;

        DBG_LOG("Step 3: Parsing JSON schema AST with sourcemeta::core::parse_json...");
        schema->raw_schema = std::make_unique<sourcemeta::core::JSON>(
            parse_json_input(schema->schema_str.c_str())
        );

        DBG_LOG("Step 4: Compiling schema with sourcemeta::blaze::compile...");
        auto compiled = sourcemeta::blaze::compile(
            *(schema->raw_schema),
            sourcemeta::blaze::schema_walker,
            sourcemeta::blaze::schema_resolver,
            sourcemeta::blaze::default_schema_compiler,
            sourcemeta::blaze::Mode::FastValidation
        );

        DBG_LOG("Step 5: Storing compiled bytecode template...");
        schema->obj = std::make_unique<BlazeSchemaType>(std::move(compiled));

        blaze_schema_t* result = schema.release();
        DBG_LOG("Success! Returning blaze_schema_t* = %p", (void*)result);
        return result;
    } catch (const std::exception& e) {
        DBG_LOG("C++ Exception caught during compilation: %s", e.what());
        return nullptr;
    } catch (...) {
        DBG_LOG("Unknown C++ exception caught during compilation");
        return nullptr;
    }
}

void blaze_schema_destroy(blaze_schema_t* schema) {
    DBG_LOG("Entering (schema=%p)", (void*)schema);
    delete schema;
    DBG_LOG("Destroyed schema successfully");
}

blaze_evaluator_t* blaze_evaluator_create(void) {
    DBG_LOG("Entering evaluator creation...");
    try {
        auto* ev = new blaze_evaluator{};
        DBG_LOG("Created blaze_evaluator_t* = %p", (void*)ev);
        return ev;
    } catch (...) {
        DBG_LOG("Failed to create evaluator");
        return nullptr;
    }
}

void blaze_evaluator_destroy(blaze_evaluator_t* evaluator) {
    DBG_LOG("Entering (evaluator=%p)", (void*)evaluator);
    delete evaluator;
    DBG_LOG("Destroyed evaluator successfully");
}

blaze_result_t* blaze_evaluator_evaluate(blaze_evaluator_t* evaluator,
                                         const blaze_schema_t* schema,
                                         const char* instance_json) {
    DBG_LOG("Entering (evaluator=%p, schema=%p, instance=%p)",
            (void*)evaluator, (void*)schema, (void*)instance_json);

    if (!evaluator) { DBG_LOG("Error: evaluator is NULL"); return nullptr; }
    if (!schema) { DBG_LOG("Error: schema is NULL"); return nullptr; }
    if (!schema->obj) { DBG_LOG("Error: schema->obj is NULL"); return nullptr; }
    if (!schema->raw_schema) { DBG_LOG("Error: schema->raw_schema is NULL"); return nullptr; }
    if (!instance_json) { DBG_LOG("Error: instance_json is NULL"); return nullptr; }

    try {
        DBG_LOG("Step 1: Parsing instance JSON AST...");
        const auto instance = parse_json_input(instance_json);

        DBG_LOG("Step 2: Running evaluator->obj.validate()...");
        const bool is_valid = evaluator->obj.validate(*(schema->obj), instance);
        DBG_LOG("Step 3: Validation returned bool = %s", is_valid ? "true" : "false");

        auto* res = new blaze_result{};
        res->holds_validity = is_valid;

        DBG_LOG("Step 4: Collecting annotations...");
        collect_annotations(*(schema->raw_schema), "", res->annotations);

        if (!is_valid) {
            DBG_LOG("Step 5: Instance invalid. Extracting diagnostic paths...");
            diagnose_instance(*(schema->raw_schema), instance, "", "", res->errors);

            if (res->errors.empty()) {
                DBG_LOG("Step 5b: No sub-errors found. Emitting fallback root error.");
                res->errors.push_back(blaze_error_item{
                    "/",
                    "/",
                    "Instance validation failed against compiled schema"
                });
            }
        }

        DBG_LOG("Success! Returning blaze_result_t* = %p", (void*)res);
        return res;
    } catch (const std::exception& e) {
        DBG_LOG("C++ Exception in evaluate: %s", e.what());
        auto* res = new blaze_result{};
        res->holds_validity = false;
        res->errors.push_back(blaze_error_item{ "/", "/", e.what() });
        return res;
    } catch (...) {
        DBG_LOG("Unknown exception in evaluate");
        return nullptr;
    }
}

void blaze_result_destroy(blaze_result_t* result) {
    DBG_LOG("Entering (result=%p)", (void*)result);
    delete result;
    DBG_LOG("Destroyed result successfully");
}

bool blaze_result_is_valid(const blaze_result_t* result) {
    if (!result) {
        DBG_LOG("blaze_result_is_valid: result is NULL -> false");
        return false;
    }
    return result->holds_validity;
}

blaze_error_iterator_t* blaze_result_get_errors(const blaze_result_t* result) {
    DBG_LOG("Entering (result=%p)", (void*)result);
    if (!result) return nullptr;

    try {
        auto* iterator = new blaze_error_iterator{};
        iterator->errors = result->errors;
        DBG_LOG("Created error iterator with %zu error(s)", iterator->errors.size());
        return iterator;
    } catch (...) {
        return nullptr;
    }
}

void blaze_error_iterator_destroy(blaze_error_iterator_t* iterator) {
    DBG_LOG("Entering (iterator=%p)", (void*)iterator);
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

blaze_annotation_iterator_t* blaze_result_get_annotations(const blaze_result_t* result) {
    DBG_LOG("Entering (result=%p)", (void*)result);
    if (!result) return nullptr;

    try {
        auto* iterator = new blaze_annotation_iterator{};
        iterator->annotations = result->annotations;
        DBG_LOG("Created annotation iterator with %zu item(s)", iterator->annotations.size());
        return iterator;
    } catch (...) {
        return nullptr;
    }
}

void blaze_annotation_iterator_destroy(blaze_annotation_iterator_t* iterator) {
    DBG_LOG("Entering (iterator=%p)", (void*)iterator);
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
