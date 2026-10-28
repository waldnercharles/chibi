#include "authoring/package_tests.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "package_portable_source.h"
#include "runtime/core_definition.h"
#include "runtime/text_format.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "types/array.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static compiler_fixture fixture;
static arena producer, retained;
static native_capabilities *profiles[2];
static char *compiler_target, *values_target;
static package_module serialized[3];
static int serialized_count;
static unsigned char *value_closure;
static compiler_host *value_host;
static compiler_host_result value_result;
static yyjson_doc *value_json;

static void cleanup()
{
    compiler_host_free(value_host);
    value_host = NULL;
    yyjson_doc_free(value_json);
    value_json = NULL;
    free(value_result.json);
    value_result = (compiler_host_result){ 0 };
    free(value_closure);
    value_closure = NULL;
    for (int i = 0; i < serialized_count; i++) {
        free((void *)serialized[i].data);
    }
    serialized_count = 0;
    compiler_fixture_free(&fixture);
    arena_free(&producer);
    arena_free(&retained);
    for (int i = 0; i < 2; i++) {
        native_capabilities_free(profiles[i]);
        profiles[i] = NULL;
    }
    free(compiler_target);
    free(values_target);
    compiler_target = values_target = NULL;
}

static bool collection_is_empty(const authoring_packages *packages, const char *collection)
{
    package_test *tests = package_tests_find_collection(packages, collection);
    bool empty = !acount(tests);
    afree(tests);
    return empty;
}

static bool core_discovery_is_ordered(const authoring_packages *packages)
{
    package_test *first = package_tests_find_collection(packages, "core");
    package_test *second = package_tests_find_collection(packages, "core");
    int count = acount(first);
    bool ordered = count > 0 && count == acount(second);
    for (int i = 0; ordered && i < count; i++) {
        ordered = !strcmp(first[i].path, second[i].path) &&
                  !strcmp(first[i].name, second[i].name) &&
                  first[i].line == second[i].line &&
                  first[i].column == second[i].column;
        if (!i) {
            continue;
        }
        int path_order = strcmp(first[i - 1].path, first[i].path);
        bool position_order = first[i - 1].line < first[i].line ||
                              (first[i - 1].line == first[i].line &&
                               first[i - 1].column <= first[i].column);
        ordered &= path_order < 0 || (!path_order && position_order);
    }
    afree(first);
    afree(second);
    return ordered;
}

TEST_CASE(portable_packages_run_authored_tests)
{
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    enum
    {
        SOURCES = sizeof sources / sizeof *sources
    };
    static const char project_source[] = "package portable_discovery\n"
                                         "import _ \"core:bytes\"\n"
                                         "import _ \"core:math/bits\"\n"
                                         "import _ \"core:hash/fnv\"\n"
                                         "import _ \"core:hash/adler32\"\n"
                                         "import _ \"core:hash/crc32\"\n"
                                         "@test ProjectSentinel :: () {}\n";
    source_package_input files[2 + SOURCES];
    char paths[SOURCES][256];
    files[0] = (source_package_input){ "/sdk/core/strings/Strings.chibi",
                                       CORE_STRINGS_SOURCE,
                                       sizeof CORE_STRINGS_SOURCE - 1 };
    files[1] = (source_package_input){ "/compiler/discovery/Fixture.chibi",
                                       project_source,
                                       sizeof project_source - 1 };
    for (int i = 0; i < SOURCES; i++) {
        int core = snprintf(
            paths[i],
            sizeof paths[i],
            "/sdk/core/%s",
            sources[i].path
        );
        REQUIRE(core > 0 && (size_t)core < sizeof paths[i]);
        files[2 + i] = sources[i];
        files[2 + i].path = paths[i];
    }
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 2 + SOURCES, core, 1));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(behavior_backend_available(backends[b]));
        package_test *project_tests = package_tests_find(fixture.owners[b]);
        package_test *explicit_project = package_tests_find_collection(fixture.owners[b], "project");
        bool project_only = acount(project_tests) == 1 &&
                            acount(explicit_project) == 1 &&
                            !strcmp(project_tests[0].package, "portable_discovery") &&
                            !strcmp(project_tests[0].name, "ProjectSentinel") &&
                            !strcmp(project_tests[0].path, explicit_project[0].path);
        afree(project_tests);
        afree(explicit_project);
        REQUIRE(project_only);
        REQUIRE(collection_is_empty(fixture.owners[b], "core_extra"));
        REQUIRE(collection_is_empty(fixture.owners[b], "cor"));
        REQUIRE(collection_is_empty(fixture.owners[b], "unknown"));
        REQUIRE(core_discovery_is_ordered(fixture.owners[b]));
    }
    REQUIRE(compiler_fixture_tests_in_collection(&fixture, "core", PACKAGE_TEST_BUDGET));
    return true;
}

TEST_CASE(text_from_bytes_preserves_storage_and_lifetime)
{
    unsigned char expected[256];
    for (int i = 0; i < 256; i++) {
        expected[i] = (unsigned char)i;
    }
    /* Typed null, boxed empty, packed empty, boxed bytes, packed bytes. */
    for (int shape = 0; shape < 5; shape++) {
        int count = shape >= 3 ? 256 : 0;
        pin_array *array = NULL;
        if (shape == 1 || shape == 3) {
            array = pin_array_snapshot_allocate(&producer, PIN_U8, "u8", count);
            for (int i = 0; i < count; i++) {
                array->items[i] = (pin_value){ .type = PIN_U8, .u8 = expected[i] };
            }
        }
        if (shape == 2 || shape == 4) {
            array = arena_alloc(&producer, sizeof *array);
            *array = (pin_array){ .element_type = PIN_U8,
                                  .type_name = "u8",
                                  .count = count,
                                  .capacity = count,
                                  .stride = 1 };
            if (count) {
                array->data = arena_alloc(&producer, (size_t)count);
                memcpy(array->data, expected, (size_t)count);
            }
        }
        pin_value input = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY, .ptr = array };
        pin_value output = { 0 };
        REQUIRE(!NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &input, 1, &output));
        pin_string text = pin_text(output);
        REQUIRE(text.length == (uint32_t)count);
        REQUIRE(!count || !memcmp(text.bytes, expected, (size_t)count));
        if (count && array->stride) {
            memset(array->data, 0x41, (size_t)count);
        }
        if (count && !array->stride) {
            for (int i = 0; i < count; i++) {
                array->items[i].u8 = 0x41;
            }
        }
        REQUIRE(!count || !memcmp(text.bytes, expected, (size_t)count));
        arena_free(&producer);
        text = pin_text(output);
        REQUIRE(text.length == (uint32_t)count && !text.bytes[count]);
        REQUIRE(!count || !memcmp(text.bytes, expected, (size_t)count));
    }
    return true;
}

TEST_CASE(text_from_bytes_rejects_invalid_inputs_without_output)
{
    pin_value output = { .type = PIN_I32, .i = 42 };
    pin_value wrong_type = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &wrong_type, 1, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    pin_value scalar = { .type = PIN_U8, .u8 = 7 };
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &scalar, 1, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    pin_array invalid = { .element_type = PIN_U8, .count = -1 };
    pin_value input = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY, .ptr = &invalid };
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &input, 1, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    pin_array wrong_element = { .element_type = PIN_I32, .type_name = "int" };
    input.ptr = &wrong_element;
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &input, 1, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    pin_value wrong_item = { .type = PIN_I32, .i = 7 };
    pin_array wrong_payload = { .element_type = PIN_U8,
                                .type_name = "u8",
                                .count = 1,
                                .capacity = 1,
                                .items = &wrong_item };
    input.ptr = &wrong_payload;
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &input, 1, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    REQUIRE(NATIVE_TEXT_FROM_BYTES_CALL.value(&retained, &scalar, 0, &output));
    REQUIRE(output.type == PIN_I32 && output.i == 42);
    return true;
}

TEST_CASE(text_from_bytes_keeps_compiler_profile_separate)
{
    const native_definition_pack packs[] = { NATIVE_COMPILER_TEXT_DEFINITION_PACK,
                                             NATIVE_TEXT_DEFINITION_PACK };
    char error[256];
    for (int i = 0; i < 2; i++) {
        profiles[i] = native_capabilities_from_definition_packs(NULL, 0, &packs[i], 1, error, sizeof error);
        REQUIRE(profiles[i]);
    }
    const script_catalog *compiler = native_capabilities_catalog(profiles[0]);
    const script_catalog *public = native_capabilities_catalog(profiles[1]);
    REQUIRE(
        script_catalog_find_capability(compiler, "text_from_bytes", SCRIPT_CAPABILITY_FUNCTION) < 0
    );
    REQUIRE(
        script_catalog_find_capability(public, "text_from_bytes", SCRIPT_CAPABILITY_FUNCTION) >= 0
    );
    const char *original[] = { "format_text",  "text_equal", "text_byte_length",
                               "text_byte_at", "text_slice", "text_bytes" };
    for (int i = 0; i < compiler->capability_count; i++) {
        bool known = false;
        for (size_t j = 0; j < sizeof original / sizeof *original; j++) {
            known |= !strcmp(compiler->capabilities[i].name, original[j]);
        }
        REQUIRE(known);
    }
    for (size_t i = 0; i < sizeof original / sizeof *original; i++) {
        int a = script_catalog_find_capability(compiler, original[i], SCRIPT_CAPABILITY_FUNCTION);
        int b = script_catalog_find_capability(public, original[i], SCRIPT_CAPABILITY_FUNCTION);
        REQUIRE(a >= 0 && b >= 0);
        REQUIRE(compiler->capabilities[a].id == public->capabilities[b].id);
        REQUIRE(
            script_catalog_capability_abi(compiler, &compiler->capabilities[a]) ==
            script_catalog_capability_abi(public, &public->capabilities[b])
        );
    }
    size_t compiler_length = 0, values_length = 0;
    compiler_target = compiler_host_compiler_target_json(&compiler_length, error, sizeof error);
    values_target = compiler_host_values_target_json(&values_length, error, sizeof error);
    REQUIRE(compiler_target && compiler_length && values_target && values_length);
    REQUIRE(!strstr(compiler_target, "text_from_bytes"));
    REQUIRE(strstr(values_target, "text_from_bytes"));
    REQUIRE(strstr(compiler_target, "text_bytes") && strstr(values_target, "text_bytes"));
    return true;
}

TEST_CASE(text_from_bytes_links_only_in_public_value_profile)
{
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    static const char
        app[] = "package value_profile\nimport bytes \"core:bytes\"\n"
                "pub Read :: () -> string { return "
                "bytes.ToString([u8(65),u8(0),u8(66)]); }\n";
    source_package_input files[] = {
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "/sdk/core/bytes/Bytes.chibi", NULL, 0 },
        { "/compiler/value_profile/ValueProfile.chibi", app, sizeof app - 1 }
    };
    int found = 0;
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        if (!strcmp(sources[i].path, "bytes/Bytes.chibi")) {
            files[1].text = sources[i].text;
            files[1].size = sources[i].size;
            found++;
        }
    }
    REQUIRE(found == 1);
    REQUIRE(
        compiler_fixture_prepare_backends(&fixture, COMPILER_FIXTURE_BYTECODE, files, 3, NULL, 0)
    );
    const authoring_packages *owner = fixture.owners[0];
    int count = authoring_packages_count(owner);
    REQUIRE(count > 0 && count <= 3);
    char error[256];
    for (int p = 0; p < count; p++) {
        const authoring_package *package = authoring_packages_at(owner, p);
        unsigned char *prepared = NULL;
        size_t size = 0;
        bool written = execution_prepared_write(package->module, &prepared, &size, error, sizeof error);
        serialized[serialized_count++] = (package_module){
            package->identity,
            execution_module_id(package->module),
            prepared,
            size
        };
        REQUIRE(written);
    }
    const authoring_package *root = compiler_fixture_package(&fixture, 0, "project:value_profile");
    REQUIRE(root);
    size_t length = 0;
    REQUIRE(compiler_artifact_write(
        serialized,
        serialized_count,
        execution_module_id(root->module),
        &value_closure,
        &length,
        error,
        sizeof error
    ));
    for (int i = 0; i < serialized_count; i++) {
        free((void *)serialized[i].data);
    }
    serialized_count = 0;
    compiler_fixture_free(&fixture);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        value_host = compiler_host_load_values(value_closure, length, backends[b], error, sizeof error);
        REQUIRE(value_host);
        REQUIRE(compiler_host_invoke(
            value_host,
            compiler_host_entry("project:value_profile:1:Read:callable"),
            "[]",
            2,
            100000,
            &value_result
        ));
        REQUIRE(value_result.flow_status == FLOW_OK);
        REQUIRE(
            !value_result.counts.overflow &&
            !value_result.counts.inspection_bytecode_fallbacks &&
            !value_result.counts.size_bytecode_fallbacks
        );
        /* Values-only calls open a pure shared-budget scope, which uses the
         * canonical instruction stream even for a compiled load configuration.
         * The public package suite separately proves compiled execution. */
        REQUIRE(
            value_result.counts.bytecode_entries + value_result.counts.sljit_entries +
                value_result.counts.wasm_entries + value_result.counts.native_entries >
            0
        );
        value_json = yyjson_read(value_result.json, value_result.length, COMPILER_HOST_JSON_READ_FLAGS);
        REQUIRE(value_json);
        yyjson_val *outputs = yyjson_doc_get_root(value_json);
        REQUIRE(yyjson_is_arr(outputs) && yyjson_arr_size(outputs) == 1);
        yyjson_val *text = yyjson_arr_get(outputs, 0);
        static const char expected[] = { 'A', 0, 'B' };
        REQUIRE(yyjson_is_str(text) && yyjson_get_len(text) == sizeof expected);
        REQUIRE(!memcmp(yyjson_get_str(text), expected, sizeof expected));
        yyjson_doc_free(value_json);
        value_json = NULL;
        free(value_result.json);
        value_result = (compiler_host_result){ 0 };
        compiler_host_free(value_host);
        value_host = NULL;
        value_host = compiler_host_load(value_closure, length, backends[b], error, sizeof error);
        REQUIRE(!value_host);
        REQUIRE(strstr(error, "missing or incompatible host capability"));
    }
    return true;
}

void test_language_package_portable()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(text_from_bytes_preserves_storage_and_lifetime);
    RUN_TEST_CASE(text_from_bytes_rejects_invalid_inputs_without_output);
    RUN_TEST_CASE(text_from_bytes_keeps_compiler_profile_separate);
    RUN_TEST_CASE(text_from_bytes_links_only_in_public_value_profile);
    RUN_TEST_CASE(portable_packages_run_authored_tests);
}
