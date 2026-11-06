#include "authoring/source_package_io.h"
#include "backend.h"
#include "execution/function_builder.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "package_portable_consumer.h"
#include "package_portable_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
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
 * All ten new packages are imported through their final public APIs, including
 * named outputs, nil errors and typed failures with no partial codec output. */

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
    finish_call();
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

static bool compile_consumer()
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
}

static bool run_consumer(behavior_backend backend)
{
    char error[256];
    native_prepared_context linking = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&linking);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    packages = compiler_artifact_read(
        closure.bytes,
        closure.length,
        &linker,
        backend,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "portable authored %s load: %s\n", behavior_backend_name(backend), error);
    }
    REQUIRE(packages);
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
    behavior_execution_scope execution = { .remaining = CALL_BUDGET };
    behavior_run_scope_begin(&runs);
    behavior_execution_scope_begin(&execution);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_execution_scope_end(&execution);
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
    REQUIRE(status == FLOW_OK && context.returned && output.i == 10);
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

void test_language_package_portable_compiler()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(portable_public_consumer_compiles_with_authored_compiler);
}
