#include "execution/service.h"
#include "execution/status.h"
#include "host/clock_package.h"
#include "host_packages_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

enum
{
    COMPILE_BUDGET = 200000000,
    CALL_BUDGET = 2000,
};

/* This scalar transport proves the native package boundary without choosing
 * the public clock package's result type or duration API. */
static const char
    CLOCK[] = "package clock\n"
              "pub Read :: () -> (nanoseconds: i64, domain: u64, status: int, "
              "platform_error: int) { s := Sample(); return "
              "(s.nanoseconds, s.domain, s.status, s.platform_error); }\n";
static const char APP
    [] = "package app\nimport watch \"host:clock\"\nimport time \"core:time\"\n"
         "Check :: (nanoseconds: i64, domain: u64, status: int, error: int) -> "
         "int { "
         "s := watch.Read(); "
         "if (s.nanoseconds != nanoseconds) { return 1; } "
         "if (s.domain != domain) { return 2; } "
         "if (s.status != status) { return 3; } "
         "if (s.platform_error != error) { return 4; } "
         "duration: time.Duration = {nanoseconds: nanoseconds}; "
         "if time.Nanoseconds(duration) != nanoseconds { return 5; } "
         "duration.nanoseconds = 1500000000; "
         "if time.Seconds(duration) != 1.5 { return 6; } return 0; }\n"
         "Twice :: (earlier: i64, later: i64, domain: u64) -> int { "
         "first := watch.Read(); second := watch.Read(); "
         "if (first.nanoseconds != earlier || second.nanoseconds != later) { "
         "return 1; } "
         "if (first.domain != domain || second.domain != domain) { return 2; } "
         "if (first.status != 0 || second.status != 0 || "
         "first.platform_error != 0 || second.platform_error != 0) { return 3; "
         "} "
         "return 0; }\n";

typedef struct
{
    host_clock_status status;
    int64_t nanoseconds, increment;
    int error, calls;
} clock_fixture;

static compiler_host *compiler;
static native_capabilities *native, *without_clock;
static compiler_host_bytes closure;
static package_collection *packages;
static const execution_module *app_module;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static service_binding *services;

static host_clock_status sample_fixture(void *state, int64_t *nanoseconds, int *error)
{
    clock_fixture *fixture = state;
    *nanoseconds = fixture->nanoseconds;
    *error = fixture->error;
    fixture->nanoseconds += fixture->increment;
    fixture->calls++;
    return fixture->status;
}

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    afree(services);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}

static void cleanup()
{
    finish_call();
    if (packages) {
        package_collection_free(packages);
    }
    packages = NULL;
    app_module = NULL;
    free(closure.bytes);
    closure = (compiler_host_bytes){ 0 };
    compiler_host_free(compiler);
    compiler = NULL;
    native_capabilities_free(native);
    native_capabilities_free(without_clock);
    native = without_clock = NULL;
}

static bool load_compiler()
{
    const native_definition_pack definitions[] = { NATIVE_TEXT_DEFINITION_PACK,
                                                   NATIVE_ARRAY_DEFINITION_PACK };
    char error[256];
    native = native_capabilities_from_definition_packs(host_clock_package(), 1, definitions, 2, error, sizeof error);
    REQUIRE(native);
    without_clock = native_capabilities_from_definition_packs(NULL, 0, definitions, 2, error, sizeof error);
    REQUIRE(without_clock);
    const compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core",
    };
    compiler = compiler_bootstrap_load(&options, error, sizeof error);
    if (!compiler) {
        fprintf(stderr, "host packages compiler bootstrap: %s\n", error);
    }
    REQUIRE(compiler);
    return true;
}

static char *compile_input(
    const char *app,
    const char *clock,
    const char *clock_path,
    const native_capabilities *target,
    size_t *length
)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "/project");
    yyjson_mut_val *names = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_arr_add_str(doc, names, "host");
    yyjson_mut_arr_add_str(doc, names, "core");
    yyjson_mut_val *roots = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_arr_add_str(doc, roots, "/host");
    yyjson_mut_arr_add_str(doc, roots, "/sdk/core");
    yyjson_mut_val *paths = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_arr_add_str(doc, paths, "/project/app/App.chibi");
    yyjson_mut_arr_add_str(doc, paths, clock_path);
    yyjson_mut_arr_add_str(doc, paths, "/sdk/core/time/Time.chibi");
    yyjson_mut_val *sources = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_arr_add_str(doc, sources, app);
    yyjson_mut_arr_add_str(doc, sources, clock);
    yyjson_mut_arr_add_str(doc, sources, CORE_TIME_SOURCE);
    yyjson_mut_arr_add_false(doc, arguments);
    size_t target_length = 0, effects_length = 0;
    const script_catalog *catalog = native_capabilities_catalog(target);
    char *target_json = compiler_target_json(catalog, &target_length);
    char *effects_json = compiler_target_effects_json(catalog, &effects_length);
    yyjson_doc *target_doc = yyjson_read(target_json, target_length, 0);
    yyjson_doc *effects_doc = yyjson_read(effects_json, effects_length, 0);
    yyjson_val *values = yyjson_doc_get_root(target_doc);
    for (size_t i = 0; i < yyjson_arr_size(values); i++) {
        yyjson_mut_arr_append(arguments, yyjson_val_mut_copy(doc, yyjson_arr_get(values, i)));
    }
    yyjson_mut_arr_append(arguments, yyjson_val_mut_copy(doc, yyjson_doc_get_root(effects_doc)));
    char *json = yyjson_mut_write(doc, 0, length);
    yyjson_doc_free(target_doc);
    yyjson_doc_free(effects_doc);
    free(target_json);
    free(effects_json);
    yyjson_mut_doc_free(doc);
    return json;
}

/* CompileWithEffects returns a tagged Rejected diagnostic. Require its source
 * coordinates as well as its message so a transport failure cannot pass. */
static bool rejects(
    const char *name,
    const char *app,
    const char *clock,
    const char *path,
    const native_capabilities *target,
    const char *expected
)
{
    size_t length = 0;
    char *input = compile_input(app, clock, path, target, &length);
    REQUIRE(input);
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileWithEffects:callable"),
        input,
        length,
        COMPILE_BUDGET,
        &result
    );
    free(input);
    yyjson_doc *doc = called ? yyjson_read(result.json, result.length, 0) : NULL;
    yyjson_val *outcome = yyjson_arr_get(yyjson_doc_get_root(doc), 0);
    yyjson_val *diagnostic = yyjson_arr_get(yyjson_obj_get(outcome, "values"), 0);
    const char *message = yyjson_get_str(yyjson_obj_get(diagnostic, "message"));
    bool rejected = called && result.flow_status == FLOW_OK && message && *message &&
                    yyjson_get_int(yyjson_obj_get(diagnostic, "line")) > 0 &&
                    yyjson_get_int(yyjson_obj_get(diagnostic, "column")) > 0 &&
                    (!expected || !strcmp(message, expected));
    if (!rejected) {
        fprintf(
            stderr,
            "host packages compiler %s: %s\n",
            name,
            called ? result.json : result.diagnostic
        );
    }
    yyjson_doc_free(doc);
    free(result.json);
    REQUIRE(rejected);
    return true;
}

static bool compile_clock()
{
    size_t length = 0;
    char *input = compile_input(APP, CLOCK, "/host/clock/Clock.chibi", native, &length);
    REQUIRE(input);
    bool called = compiler_host_invoke_bytes(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileClosureWithEffects:callable"),
        input,
        length,
        COMPILE_BUDGET,
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &closure
    );
    free(input);
    if (!called || !closure.length) {
        fprintf(stderr, "host packages compiler closure: %s\n", closure.diagnostic);
    }
    REQUIRE(called && closure.flow_status == FLOW_OK && closure.length);
    REQUIRE(closure.counts.sljit_entries && !closure.counts.bytecode_entries);
    REQUIRE(!closure.counts.overflow && !closure.counts.inspection_bytecode_fallbacks);
    return true;
}

static bool load_closure(behavior_backend backend)
{
    if (packages) {
        package_collection_free(packages);
    }
    packages = NULL;
    app_module = NULL;
    const native_prepared_context bindings = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&bindings);
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
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
        fprintf(stderr, "host packages compiler load: %s\n", error);
    }
    REQUIRE(packages && package_collection_count(packages) == 3);
    for (int i = 0; i < package_collection_count(packages); i++) {
        const execution_module *module = package_collection_module(packages, i);
        if (execution_module_id(module) == root) {
            app_module = module;
        }
    }
    REQUIRE(app_module && execution_module_backend(app_module) == backend);
    REQUIRE(execution_module_effects(app_module).host_sink);
    return true;
}

static bool call(const char *name, const pin_value *arguments, int count, host_clock_grant *grant, behavior_backend backend)
{
    finish_call();
    execution_module_function_info function = { 0 };
    for (int i = 0; i < execution_module_function_count(app_module); i++) {
        execution_module_function_info item = execution_module_function_at(app_module, i);
        if (item.entry && !strcmp(item.entry, name)) {
            function = item;
        }
    }
    REQUIRE(function.body && (function.effects & SCRIPT_EFFECT_HOST));
    if (grant) {
        apush(services, ((service_binding){ HOST_CLOCK_SERVICE, grant }));
    }
    if (behavior_function_resumable(function.body)) {
        behavior_function_activation_init(function.body, &activation);
    }
    pin_value result = { .type = PIN_I32, .i = -999 };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function.body) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &result,
        .budget = CALL_BUDGET,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function.body, &context, services, 0));
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = behavior_function_run(function.body, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    bool agreed = status == FLOW_OK && context.returned &&
                  result.type == PIN_I32 && !result.i;
    bool entered = backend == BEHAVIOR_SLJIT
                     ? scope.counts.sljit_entries > 0 && !scope.counts.bytecode_entries
                     : scope.counts.bytecode_entries > 0 && !scope.counts.sljit_entries;
    if (!agreed) {
        fprintf(
            stderr,
            "host packages compiler %s: status %d, result %d\n",
            name,
            status,
            result.i
        );
    }
    finish_call();
    REQUIRE(agreed && entered && !scope.counts.wasm_entries);
    REQUIRE(!scope.counts.overflow && !scope.counts.inspection_bytecode_fallbacks);
    return true;
}

static bool check(
    behavior_backend backend,
    host_clock_grant *grant,
    int64_t nanoseconds,
    uint64_t domain,
    int status,
    int error
)
{
    const pin_value arguments[] = {
        { .type = PIN_I64, .i64 = nanoseconds },
        { .type = PIN_U64, .u64 = domain },
        { .type = PIN_I32, .i = status },
        { .type = PIN_I32, .i = error },
    };
    return call("Check", arguments, 4, grant, backend);
}

static bool samples(behavior_backend backend)
{
    REQUIRE(load_closure(backend));
    clock_fixture fixture = { .status = HOST_CLOCK_OK,
                              .nanoseconds = INT64_C(9007199254740993),
                              .error = EIO };
    host_clock_grant grant;
    host_clock_grant_init(&grant, sample_fixture, &fixture);
    REQUIRE(check(backend, NULL, 0, 0, HOST_CLOCK_SAMPLE_DENIED, 0));
    REQUIRE(!fixture.calls);
    REQUIRE(check(backend, &grant, fixture.nanoseconds, grant.domain, HOST_CLOCK_SAMPLE_OK, 0));
    fixture.status = HOST_CLOCK_FAILED;
    REQUIRE(check(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, EIO));
    fixture.status = HOST_CLOCK_UNAVAILABLE;
    REQUIRE(check(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_UNAVAILABLE, 0));
    fixture.status = HOST_CLOCK_OK;
    fixture.nanoseconds = -1;
    REQUIRE(check(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, ERANGE));
    REQUIRE(fixture.calls == 4);
    fixture.nanoseconds = INT64_C(9007199254740993);
    fixture.increment = 1;
    const pin_value arguments[] = {
        { .type = PIN_I64, .i64 = INT64_C(9007199254740993) },
        { .type = PIN_I64, .i64 = INT64_C(9007199254740994) },
        { .type = PIN_U64, .u64 = grant.domain },
    };
    REQUIRE(call("Twice", arguments, 3, &grant, backend));
    REQUIRE(fixture.calls == 6);
    REQUIRE(check(backend, NULL, 0, 0, HOST_CLOCK_SAMPLE_DENIED, 0));
    REQUIRE(fixture.calls == 6);
    return true;
}

TEST_CASE(host_packages_compile_link_and_run_through_stage_one)
{
    REQUIRE(load_compiler());
    REQUIRE(compile_clock());
    REQUIRE(samples(BEHAVIOR_BYTECODE));
    REQUIRE(samples(BEHAVIOR_SLJIT));
    static const struct
    {
        const char *name, *source;
    } rejected[] = {
        { "missing import", "package app\nMain :: () -> int { s := watch.Read(); return s.status; }\n" },
        { "private kernel without import", "package app\nMain :: () -> int { s := Sample(); return s.status; }\n" },
        { "private imported kernel",
          "package app\nimport watch \"host:clock\"\n"
          "Main :: () -> int { s := watch.Sample(); return s.status; }\n" },
    };
    for (int i = 0; i < (int)(sizeof rejected / sizeof *rejected); i++) {
        REQUIRE(
            rejects(rejected[i].name, rejected[i].source, CLOCK, "/host/clock/Clock.chibi", native, NULL)
        );
    }
    REQUIRE(rejects(
        "native member case",
        APP,
        "package clock\npub Read :: () -> (nanoseconds: i64, domain: u64, "
        "status: int, platform_error: int) { s := sample(); return "
        "(s.nanoseconds, s.domain, s.status, s.platform_error); }\n",
        "/host/clock/Clock.chibi",
        native,
        NULL
    ));
    REQUIRE(rejects(
        "wrong owning package",
        "package app\nimport watch \"host:other\"\n"
        "Main :: () -> int { return watch.Read(); }\n",
        "package other\npub Read :: () -> int { s := Sample(); return "
        "s.status; }\n",
        "/host/other/Other.chibi",
        native,
        "Host package is unavailable for this target"
    ));
    REQUIRE(
        rejects("unavailable target", APP, CLOCK, "/host/clock/Clock.chibi", without_clock, "Host package is unavailable for this target")
    );
    return true;
}

void test_language_host_packages_compiler()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(host_packages_compile_link_and_run_through_stage_one);
    pu_clear_setup();
}
