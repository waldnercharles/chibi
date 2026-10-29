#include "authoring/package_tests.h"
#include "authoring/source_package_io.h"
#include "backend.h"
#include "execution/function_builder.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_compiler_chibi.h"
#include "package_core_source.h"
#include "package_portable_consumer.h"
#include "package_portable_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* The authored public consumer is compiled by the current authored compiler. Its target is
 * the public text/array/core catalog, independently of the compiler seed's
 * private text profile. Only the produced closure is loaded for execution.
 * All five new packages are imported through their final public APIs, including
 * scalar/byte operations, binary preservation and independent value copies. */

enum
{
    COMPILE_BUDGET = 200000000,
    CALL_BUDGET = 1000000
};
static native_capabilities *native;
static compiler_host *compiler;
static compiler_host_bytes closure;
static package_collection *packages;
static char *request;
static yyjson_mut_doc *request_doc;
static arena storage;
static behavior_managed_heap managed;
static behavior_function_activation activation;
static behavior_function_context context;
static pin_value output;
static compiler_fixture metadata[5];
static char *saved_workers;
static bool workers_changed;
static const struct
{
    const char *path, *identity;
    int tests;
} tested_packages[] = {
    { "bytes", "project:bytes", 6 },
    { "math/bits", "project:math/bits", 7 },
    { "hash/fnv", "project:hash/fnv", 3 },
    { "hash/adler32", "project:hash/adler32", 4 },
    { "hash/crc32", "project:hash/crc32", 3 },
};

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    output = (pin_value){ 0 };
}

static void cleanup()
{
    if (workers_changed) {
        if (saved_workers) {
            setenv("CHIBI_TEST_WORKERS", saved_workers, 1);
        } else {
            unsetenv("CHIBI_TEST_WORKERS");
        }
    }
    free(saved_workers);
    saved_workers = NULL;
    workers_changed = false;
    finish_call();
    for (int p = 0; p < 5; p++) {
        compiler_fixture_free(&metadata[p]);
    }
    if (packages) {
        package_collection_free(packages);
    }
    packages = NULL;
    compiler_host_free(compiler);
    compiler = NULL;
    native_capabilities_free(native);
    native = NULL;
    free(closure.bytes);
    closure = (compiler_host_bytes){ 0 };
    free(request);
    request = NULL;
    yyjson_mut_doc_free(request_doc);
    request_doc = NULL;
}

#ifndef __EMSCRIPTEN__
/* The generated header holds actual shipped sources. Test files are omitted:
 * this verifies a consumer using public imports, separately from package tests. */
static bool prepare_request(size_t *length)
{
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    request_doc = yyjson_mut_doc_new(NULL);
    REQUIRE(request_doc);
    yyjson_mut_val *arguments = yyjson_mut_arr(request_doc);
    yyjson_mut_doc_set_root(request_doc, arguments);
    yyjson_mut_arr_add_str(request_doc, arguments, "/project");
    yyjson_mut_arr_add_str(request_doc, yyjson_mut_arr_add_arr(request_doc, arguments), "core");
    yyjson_mut_arr_add_str(request_doc, yyjson_mut_arr_add_arr(request_doc, arguments), "/sdk/core");
    yyjson_mut_val *paths = yyjson_mut_arr_add_arr(request_doc, arguments);
    yyjson_mut_val *texts = yyjson_mut_arr_add_arr(request_doc, arguments);
    yyjson_mut_arr_add_str(request_doc, paths, "/project/consumer/Main.chibi");
    yyjson_mut_arr_add_str(request_doc, texts, CONSUMER);
    yyjson_mut_arr_add_str(request_doc, paths, "/sdk/core/strings/Strings.chibi");
    yyjson_mut_arr_add_str(request_doc, texts, CORE_STRINGS_SOURCE);
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        const char *name = strrchr(sources[i].path, '/');
        if (name && !strcmp(name, "/Tests.chibi")) {
            continue;
        }
        char path[256];
        int count = snprintf(path, sizeof path, "/sdk/core/%s", sources[i].path);
        REQUIRE(count > 0 && (size_t)count < sizeof path);
        yyjson_mut_arr_add_strcpy(request_doc, paths, path);
        yyjson_mut_arr_add_str(request_doc, texts, sources[i].text);
    }
    yyjson_mut_arr_add_false(request_doc, arguments);
    size_t target_length = 0;
    char *target_json = compiler_target_json(native_capabilities_catalog(native), &target_length);
    REQUIRE(target_json);
    yyjson_doc *target_doc = yyjson_read(target_json, target_length, 0);
    free(target_json);
    REQUIRE(target_doc);
    yyjson_val *target = yyjson_doc_get_root(target_doc);
    size_t target_count = yyjson_arr_size(target);
    for (size_t i = 0; i < target_count; i++) {
        yyjson_mut_arr_append(arguments, yyjson_val_mut_copy(request_doc, yyjson_arr_get(target, i)));
    }
    yyjson_doc_free(target_doc);
    REQUIRE(target_count == 6);
    request = yyjson_mut_write(request_doc, 0, length);
    yyjson_mut_doc_free(request_doc);
    request_doc = NULL;
    REQUIRE(request);
    return true;
}

#endif

static bool prepare_native()
{
    char error[256];
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             CORE_DEFINITION_PACK };
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 3, error, sizeof error);
    if (!native) {
        fprintf(stderr, "portable compiler public catalog: %s\n", error);
    }
    REQUIRE(native);
    return true;
}

static bool compile_consumer()
{
    REQUIRE(prepare_native());
#ifdef __EMSCRIPTEN__
    // Use the target's embedded compiler; Node cannot read a native host seed.
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    enum
    {
        SOURCES = sizeof sources / sizeof *sources
    };
    source_package_input files[2 + SOURCES];
    char paths[SOURCES][256];
    files[0] = (source_package_input){ "/project/consumer/Main.chibi",
                                       CONSUMER,
                                       sizeof CONSUMER - 1 };
    files[1] = (source_package_input){ "/sdk/core/strings/Strings.chibi",
                                       CORE_STRINGS_SOURCE,
                                       sizeof CORE_STRINGS_SOURCE - 1 };
    int count = 2;
    for (int i = 0; i < SOURCES; i++) {
        const char *name = strrchr(sources[i].path, '/');
        if (name && !strcmp(name, "/Tests.chibi")) {
            continue;
        }
        int size = snprintf(
            paths[i],
            sizeof paths[i],
            "/sdk/core/%s",
            sources[i].path
        );
        REQUIRE(size > 0 && (size_t)size < sizeof paths[i]);
        files[count] = sources[i];
        files[count++].path = paths[i];
    }
    REQUIRE(package_compiler_closure(
        0,
        files,
        count,
        COMPILE_BUDGET,
        &closure.bytes,
        &closure.length
    ));
    return true;
#else
    char error[256];
    compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core"
    };
    compiler = compiler_bootstrap_load(&options, error, sizeof error);
    if (!compiler) {
        fprintf(stderr, "portable compiler bootstrap: %s\n", error);
    }
    REQUIRE(compiler);
    size_t length = 0;
    REQUIRE(prepare_request(&length));
    bool compiled = compiler_host_invoke_bytes(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileClosure:callable"),
        request,
        length,
        COMPILE_BUDGET,
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &closure
    );
    if (!compiled) {
        fprintf(stderr, "portable authored compile: %s\n", closure.diagnostic);
    } else if (!closure.length) {
        compiler_host_result diagnostic = { 0 };
        bool explained = compiler_host_invoke(
            compiler,
            compiler_host_entry("project:package_compiler:1:Compile:callable"),
            request,
            length,
            COMPILE_BUDGET,
            &diagnostic
        );
        fprintf(
            stderr,
            "portable authored rejection: %s\n",
            explained ? diagnostic.json : diagnostic.diagnostic
        );
        free(diagnostic.json);
    }
    REQUIRE(compiled && closure.length);
    return true;
#endif
}

static bool load_closure(behavior_backend backend, uint64_t *root)
{
    char error[256];
    native_prepared_context linking = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&linking);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    *root = 0;
    packages = compiler_artifact_read(
        closure.bytes,
        closure.length,
        &linker,
        backend,
        &remaining,
        root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "portable authored %s load: %s\n", behavior_backend_name(backend), error);
    }
    REQUIRE(packages);
    return true;
}

static bool run_consumer(behavior_backend backend)
{
    uint64_t root = 0;
    REQUIRE(load_closure(backend, &root));
    const behavior_function *function = NULL;
    uint64_t entry = compiler_host_entry("project:consumer:1:Check:callable");
    for (int i = 0; i < package_collection_count(packages); i++) {
        execution_module *module = package_collection_module(packages, i);
        if (execution_module_id(module) != root) {
            continue;
        }
        for (int j = 0; j < execution_module_function_count(module); j++) {
            execution_module_function_info info = execution_module_function_at(module, j);
            if (info.id == entry && info.kind == EXECUTION_FUNCTION_AUTHORED) {
                function = info.body;
            }
        }
    }
    REQUIRE(function);
    int argument_count = 0, result_count = 0;
    behavior_function_arguments(function, &argument_count);
    const pin_value *results = behavior_function_results(function, &result_count);
    REQUIRE(argument_count == 0 && result_count == 1);
    REQUIRE(results[0].type == PIN_I32 && results[0].container == PIN_CONTAINER_SINGLE);
    output = results[0];
    behavior_function_prepare_results(function, &output, &storage);
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    behavior_managed_heap_init(&managed);
    context = (behavior_function_context){ .managed_heap = &managed,
                                           .activation = resumable ? &activation : NULL,
                                           .results = &output,
                                           .budget = CALL_BUDGET };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK || !context.returned) {
        fprintf(
            stderr,
            "portable authored %s call: %s (status %d)\n",
            behavior_backend_name(backend),
            context.error_message ? context.error_message : "did not return",
            status
        );
    }
    REQUIRE(status == FLOW_OK && context.returned && output.i == 5);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(!runs.counts.size_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? runs.counts.bytecode_entries > 0
                                     : !runs.counts.bytecode_entries
    );
    REQUIRE(backend != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    finish_call();
    package_collection_free(packages);
    packages = NULL;
    return true;
}

TEST_CASE(portable_public_consumer_compiles_with_authored_compiler)
{
    REQUIRE(compile_consumer());
    REQUIRE(run_consumer(BEHAVIOR_BYTECODE));
    REQUIRE(run_consumer(TEST_COMPILED_BACKEND));
    return true;
}

static bool prepare_test_metadata()
{
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    REQUIRE(sizeof sources / sizeof *sources == 10);
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    int total = 0;
    for (int p = 0; p < 5; p++) {
        source_package_input files[3];
        int count = 0;
        size_t prefix = strlen(tested_packages[p].path);
        for (int i = 0; i < 10; i++) {
            if (!strncmp(sources[i].path, tested_packages[p].path, prefix) &&
                sources[i].path[prefix] == '/') {
                REQUIRE(count < 2);
                files[count++] = sources[i];
            }
        }
        REQUIRE(count == 2);
        files[count++] = (source_package_input){
            "/sdk/core/strings/Strings.chibi",
            CORE_STRINGS_SOURCE,
            sizeof CORE_STRINGS_SOURCE - 1
        };
        REQUIRE(compiler_fixture_prepare_packs(&metadata[p], files, count, core, 1));
        for (int b = 0; b < 2; b++) {
            package_test *tests = package_tests_find(metadata[p].owners[b]);
            int found = acount(tests);
            afree(tests);
            REQUIRE(found == tested_packages[p].tests);
        }
        total += tested_packages[p].tests;
    }
    REQUIRE(total == 23);
    return true;
}

static const execution_module *project_module(const char *identity)
{
    const execution_module *found = NULL;
    for (int i = 0; i < package_collection_count(packages); i++) {
        if (!strcmp(package_collection_name(packages, i), identity)) {
            if (found) {
                return NULL;
            }
            found = package_collection_module(packages, i);
        }
    }
    return found;
}

/* The shared runner uses one worker for this matrix so its backend counters
 * include every test. Each test retains its ordinary instruction budget. */
static bool run_authored_package(int package, int backend)
{
    const execution_module *module = project_module(tested_packages[package].identity);
    REQUIRE(module);
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    bool passed = compiler_fixture_module_tests(&metadata[package], backend, module, PACKAGE_TEST_BUDGET);
    behavior_run_scope_end(&runs);
    REQUIRE(passed);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(!runs.counts.size_bytecode_fallbacks);
    REQUIRE(
        backend == 0
            ? runs.counts.bytecode_entries >= (uint64_t)tested_packages[package].tests
            : !runs.counts.bytecode_entries
    );
    REQUIRE(backend != 0 || (!runs.counts.sljit_entries && !runs.counts.wasm_entries));
    REQUIRE(
        backend != 1 || TEST_COMPILED_BACKEND != BEHAVIOR_SLJIT ||
        runs.counts.sljit_entries >= (uint64_t)tested_packages[package].tests
    );
    REQUIRE(
        backend != 1 || TEST_COMPILED_BACKEND != BEHAVIOR_WASM ||
        runs.counts.wasm_entries >= (uint64_t)tested_packages[package].tests
    );
    return true;
}

TEST_CASE(portable_adjacent_tests_run_through_both_authored_compiler_backends)
{
    REQUIRE(prepare_native());
    REQUIRE(prepare_test_metadata());
    const char *workers = getenv("CHIBI_TEST_WORKERS");
    if (workers) {
        saved_workers = malloc(strlen(workers) + 1);
        REQUIRE(saved_workers);
        strcpy(saved_workers, workers);
    }
    REQUIRE(!setenv("CHIBI_TEST_WORKERS", "1", 1));
    workers_changed = true;
    static const source_package_input sources[] = { PORTABLE_SOURCE_INPUTS };
    source_package_input files[11];
    for (int i = 0; i < 10; i++) {
        files[i] = sources[i];
    }
    files[10] = (source_package_input){ "/sdk/core/strings/Strings.chibi",
                                        CORE_STRINGS_SOURCE,
                                        sizeof CORE_STRINGS_SOURCE - 1 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int compiler_backend = 0; compiler_backend < 2; compiler_backend++) {
        REQUIRE(package_compiler_closure(
            compiler_backend,
            files,
            11,
            COMPILE_BUDGET,
            &closure.bytes,
            &closure.length
        ));
        for (int backend = 0; backend < 2; backend++) {
            uint64_t root = 0;
            REQUIRE(load_closure(backends[backend], &root));
            // Check all five project modules before executing any test bodies.
            for (int p = 0; p < 5; p++) {
                REQUIRE(project_module(tested_packages[p].identity));
            }
            int executed = 0;
            for (int p = 0; p < 5; p++) {
                REQUIRE(run_authored_package(p, backend));
                executed += tested_packages[p].tests;
            }
            REQUIRE(executed == 23);
            fprintf(
                pu_output(),
                "Stage-one compiler %s -> target %s: %d authored tests\n",
                behavior_backend_name(backends[compiler_backend]),
                behavior_backend_name(backends[backend]),
                executed
            );
            package_collection_free(packages);
            packages = NULL;
        }
        free(closure.bytes);
        closure = (compiler_host_bytes){ 0 };
    }
    return true;
}

void test_language_package_portable_compiler()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(portable_public_consumer_compiles_with_authored_compiler);
    RUN_TEST_CASE(portable_adjacent_tests_run_through_both_authored_compiler_backends);
}
