#include "backend.h"
#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "package_compiler_chibi_source.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/map.h"
#include "types/union.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real source goes through both authored compiler backends and both target
 * backends. Fixed expected effect masks also guard against matching mistakes
 * in the source oracle. No bootstrap or runtime seed is read or regenerated. */
static compiler_fixture compiler, oracle;
static arena input, detached;
static package_collection *loaded;

static void cleanup()
{
    if (loaded) {
        package_collection_free(loaded);
        loaded = NULL;
    }
    compiler_fixture_free(&compiler);
    compiler_fixture_free(&oracle);
    arena_free(&input);
    arena_free(&detached);
}
static pin_value text(const char *value)
{
    pin_value result = { 0 };
    if (pin_value_text(&input, &result, value, strlen(value))) {
        abort();
    }
    return result;
}
static pin_value texts(const char *const *values, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(values[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool field(pin_value target, const char *name, size_t value)
{
    const type_field *member = pin_type_field_path(pin_value_type(&target), name);
    REQUIRE(member && member->type == PIN_I32 && value <= INT32_MAX);
    REQUIRE(
        !pin_write_field(&input, target.ptr, member, (pin_value){ .type = PIN_I32, .i = (int)value })
    );
    return true;
}
static bool target(int backend, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(
        &compiler,
        backend,
        "project:package_compiler",
        "Target",
        &input
    );
    REQUIRE(record);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .ptr = arena_alloc(&input, record->type.size),
                        .record = record,
                        .type_name = record->type.name };
    memset(out->ptr, 0, record->type.size);
    REQUIRE(field(*out, "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(field(*out, "pointer_size", sizeof(void *)));
    REQUIRE(field(*out, "value_size", sizeof(pin_value)));
    REQUIRE(field(*out, "payload_offset", offsetof(pin_value, d)));
#define CONTEXT(NAME)                                                          \
    REQUIRE(field(*out, #NAME "_offset", offsetof(behavior_function_context, NAME)))
    CONTEXT(arguments);
    CONTEXT(delta_seconds);
    CONTEXT(results);
    CONTEXT(error_status);
#undef CONTEXT
    REQUIRE(field(*out, "type_offset", offsetof(pin_value, type)));
    REQUIRE(field(*out, "container_offset", offsetof(pin_value, container)));
    REQUIRE(field(*out, "name_offset", offsetof(pin_value, type_name)));
    REQUIRE(field(*out, "record_offset", offsetof(pin_value, record)));
    REQUIRE(field(*out, "schema_offset", offsetof(pin_value, schema)));
    REQUIRE(field(*out, "count_offset", offsetof(pin_array, count)));
    REQUIRE(field(*out, "items_offset", offsetof(pin_array, items)));
    REQUIRE(field(*out, "data_offset", offsetof(pin_array, data)));
    REQUIRE(field(*out, "stride_offset", offsetof(pin_array, stride)));
    REQUIRE(field(*out, "map_count_offset", offsetof(pin_map, count)));
    REQUIRE(field(*out, "data_failure", BEHAVIOR_FUNCTION_HOST_DATA));
    REQUIRE(field(*out, "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT));
    REQUIRE(field(*out, "assert_failure", BEHAVIOR_FUNCTION_DATA));
    return true;
}
static execution_module_function_info function(const execution_module *module, const char *name)
{
    char key[256];
    snprintf(key, sizeof key, "project:facts:1:%s:callable", name);
    uint64_t id = script_catalog_id("package-symbol", key);
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info found = execution_module_function_at(module, i);
        if (found.id == id) {
            return found;
        }
    }
    return (execution_module_function_info){ 0 };
}
TEST_CASE(package_argument_effects_match_across_compilers)
{
    const source_package_input compiler_sources[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    const native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(
        &compiler,
        compiler_sources,
        (int)(sizeof compiler_sources / sizeof *compiler_sources),
        packs,
        2
    ));
    source_package_input sources[] = {
        { "facts/Main.chibi",
          "package facts\nimport helper \"../helper\"\n"
          "Window :: union { Held(value: view int[]) }\n"
          "pub Owned :: (values: int[]) -> int { return length(values); }\n"
          "pub Imported :: (values: int[]) -> bool { return "
          "helper.Has(values); }\n"
          "pub View :: (window: view int[]) -> int { return length(window); }\n"
          "pub ViewMethod :: (window: view int[]) -> int { return "
          "window.length(); }\n"
          "pub ViewSlice :: (window: view int[]) -> int[]? { return "
          "window.slice(0..<1); }\n"
          "pub Deep :: (nested: Window[]) -> int { return length(nested); }\n"
          "pub StoreView :: (window: view int[]) -> Window { return "
          "Window.Held(window); }\n"
          "pub Identity :: (pointer: ^int) -> bool { return pointer == "
          "pointer; }\n"
          "pub ScopedRead :: (pointer: ^int) -> int { return pointer^; }\n"
          "pub ViewIndex :: (window: view int[]) -> int { return window[0]; }\n"
          "Keep :: (window: view int[]?) -> bool { return true; }\n"
          "pub OptionalView :: (window: view int[]?) -> bool { return "
          "Keep(window); }\n"
          "IgnoreMap :: (values: [int: Window]) -> int { return 1; }\n"
          "pub MapView :: (values: [int: Window]) -> int { return "
          "IgnoreMap(values); }\n"
          "pub Dereference :: () -> int { value: int = 1; pointer := &value; "
          "return pointer^; }\n",
          0 },
        { "helper/Helper.chibi", "package helper\npub Has :: (values: int[]) -> bool { value: int = 2; return contains(values, value); }\n", 0 },
    };
    for (int i = 0; i < 2; i++) {
        sources[i].size = strlen(sources[i].text);
    }
    REQUIRE(compiler_fixture_prepare(&oracle, sources, 2));
    const char *names[] = { "Owned",      "Imported",     "View",
                            "ViewMethod", "ViewSlice",    "Deep",
                            "Identity",   "Dereference",  "ScopedRead",
                            "ViewIndex",  "OptionalView", "MapView",
                            "StoreView" };
    const unsigned expected[] = { 0,
                                  0,
                                  SCRIPT_EFFECT_READ,
                                  SCRIPT_EFFECT_READ,
                                  SCRIPT_EFFECT_READ,
                                  SCRIPT_EFFECT_READ,
                                  0,
                                  SCRIPT_EFFECT_READ,
                                  0,
                                  SCRIPT_EFFECT_READ,
                                  SCRIPT_EFFECT_READ,
                                  SCRIPT_EFFECT_READ,
                                  0 };
    const char *paths[] = { sources[0].path, sources[1].path };
    const char *source_text[] = { sources[0].text, sources[1].text };
    const char *collection_names[] = { "core" },
               *collection_paths[] = { "/sdk/core" };
    bool correct = true;
    for (int b = 0; b < 2; b++) {
        pin_value args[13] = {
            text("/project"),           texts(collection_names, 1),
            texts(collection_paths, 1), texts(paths, 2),
            texts(source_text, 2),      { .type = PIN_BOOL, .b = true }
        };
        REQUIRE(target(b, &args[6]));
        args[7] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
        int count = execution_prepared_instruction_count();
        const char **instructions = malloc((size_t)count * sizeof *instructions);
        REQUIRE(instructions);
        for (int i = 0; i < count; i++) {
            instructions[i] = execution_prepared_instruction_name(i);
        }
        args[8] = texts(instructions, count);
        free(instructions);
        REQUIRE(compiler_fixture_natives(
            &compiler,
            b,
            "project:package_compiler",
            native_capabilities_catalog(oracle.native),
            &input,
            &args[9],
            &args[12]
        ));
        pin_value out;
        REQUIRE(compiler_fixture_call(
            &compiler,
            b,
            "project:package_compiler",
            "CompileWithEffects",
            args,
            13,
            20000000,
            &detached,
            &out
        ));
        const pin_union *result = out.ptr;
        if (out.type == PIN_UNION && result && result->count == 1) {
            pin_value diagnostic = result->values[0], message;
            const type_field *member = pin_type_field_path(pin_value_type(&diagnostic), "message");
            if (member && !pin_read_field(&input, diagnostic.ptr, member, &message)) {
                pin_string said = pin_text(message);
                pin_value line = { 0 }, column = { 0 };
                const type_field *line_field = pin_type_field_path(pin_value_type(&diagnostic), "line");
                const type_field *column_field = pin_type_field_path(pin_value_type(&diagnostic), "column");
                if (line_field) {
                    (void)pin_read_field(&input, diagnostic.ptr, line_field, &line);
                }
                if (column_field) {
                    (void)pin_read_field(&input, diagnostic.ptr, column_field, &column);
                }
                fprintf(
                    stderr,
                    "argument-effects compile %d:%d: %.*s\n",
                    line.i,
                    column.i,
                    (int)said.length,
                    said.bytes
                );
            }
        }
        REQUIRE(out.type == PIN_UNION && result && result->count == 2);
        const pin_array *array = result->values[1].ptr;
        REQUIRE(result->values[1].type == PIN_U8 && array && array->count);
        unsigned char *bytes = malloc((size_t)array->count);
        REQUIRE(bytes);
        for (int i = 0; i < array->count; i++) {
            bytes[i] = pin_array_get(array, i).u8;
        }
        native_prepared_context context = { .native = oracle.native };
        execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
        for (int backend = 0; backend < 2; backend++) {
            size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
            uint64_t root;
            char error[256];
            loaded = compiler_artifact_read(
                bytes,
                (size_t)array->count,
                &linker,
                backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
                &remaining,
                &root,
                error,
                sizeof error
            );
            if (!loaded) {
                fprintf(stderr, "argument-effects load: %s\n", error);
            }
            REQUIRE(loaded);
            const execution_module *module = NULL;
            for (int i = 0; i < package_collection_count(loaded); i++) {
                const execution_module *candidate = package_collection_module(loaded, i);
                if (execution_module_id(candidate) == root) {
                    module = candidate;
                }
            }
            REQUIRE(module);
            const authoring_package *reference = compiler_fixture_package(&oracle, backend, "project:facts");
            REQUIRE(reference);
            for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
                execution_module_function_info actual = function(module, names[i]);
                execution_module_function_info original = function(reference->module, names[i]);
                REQUIRE(actual.body && original.body);
                const script_function *source_function = NULL;
                for (int f = 0; f < reference->analysis->function_count; f++) {
                    const script_function *candidate = &reference->analysis->functions[f];
                    if (!strcmp(reference->analysis->symbols[candidate->symbol].name, names[i])) {
                        source_function = candidate;
                    }
                }
                REQUIRE(source_function);
                bool parallel = source_function->pure && !actual.effects &&
                                behavior_function_hosts_pure(actual.body);
                bool expected_parallel = i == 0 || i == 1 || i == 6 || i == 8 ||
                                         i == 12;
                if (actual.effects != expected[i] ||
                    original.effects != expected[i] || parallel != expected_parallel) {
                    fprintf(
                        stderr,
                        "argument-effects %s compiler=%d target=%d source=%u "
                        "pure=%d stage-one=%u expected=%u parallel=%d "
                        "expected_parallel=%d\n",
                        names[i],
                        b,
                        backend,
                        original.effects,
                        source_function->pure,
                        actual.effects,
                        expected[i],
                        parallel,
                        expected_parallel
                    );
                    correct = false;
                }
            }
            package_collection_free(loaded);
            loaded = NULL;
        }
        free(bytes);
        arena_free(&input);
        arena_free(&detached);
    }
    REQUIRE(correct);
    return true;
}
void test_language_argument_effects_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_argument_effects_match_across_compilers);
    pu_clear_setup();
}
