#include "authoring/program_service.h"
#include "compiler_cache.h"
#include "execution/service.h"
#include "execution/status.h"
#include "host/clock_package.h"
#include "host_packages_source.h"
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#ifndef CHIBI_HOST_PACKAGES_COMPILER_ROOT
#define CHIBI_HOST_PACKAGES_COMPILER_ROOT CHIBI_SOURCE_ROOT "/tools/compiler"
#endif
#ifndef CHIBI_HOST_PACKAGES_CORE_ROOT
#define CHIBI_HOST_PACKAGES_CORE_ROOT CHIBI_SOURCE_ROOT "/packages/core"
#endif

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
/* The shipped host:clock package: Now and Between return an @error ClockError. */
static const char NOW_APP[] =
    "package app\nimport watch \"host:clock\"\nimport time \"core:time\"\n"
    "Failure :: (error: watch.ClockError?, kind: int, code: int) -> int { "
    "if (error) { if (error.kind != kind) { return 5; } "
    "if (error.code != code) { return 6; } return 0; } return 4; }\n"
    "Reading :: (nanoseconds: i64, domain: u64, kind: int, code: int) -> int { "
    "r := watch.Now(); "
    "if (r.error) { if (kind == 0) { return 1; } "
    "if (r.instant.nanoseconds != 0 || r.instant.domain != 0) { return 7; } "
    "return Failure(r.error, kind, code); } "
    "if (kind != 0) { return 8; } "
    "if (r.instant.nanoseconds != nanoseconds) { return 2; } "
    "if (r.instant.domain != domain) { return 3; } return 0; }\n"
    "Elapsed :: (expected: i64) -> int { "
    "a := watch.Now(); if (a.error) { return 1; } "
    "b := watch.Now(); if (b.error) { return 2; } "
    "d := watch.Between(a.instant, b.instant); if (d.error) { return 3; } "
    "if (time.Nanoseconds(d.duration) != expected) { return 4; } return 0; }\n"
    "Mismatch :: () -> int { "
    "x: time.Instant = {nanoseconds: 5, domain: 1}; "
    "y: time.Instant = {nanoseconds: 9, domain: 2}; "
    "d := watch.Between(x, y); "
    "if (d.error) { if (time.Nanoseconds(d.duration) != 0) { return 7; } "
    "return Failure(d.error, 4, 0); } "
    "return 8; }\n";
static const char MIXED_APP
    [] = "package app\nimport watch \"host:clock\"\nimport time \"core:time\"\n"
         "Private :: (expected: i64, domain: u64) -> int { s := watch.Read(); "
         "duration: time.Duration = {nanoseconds: expected}; "
         "if (s.nanoseconds != time.Nanoseconds(duration) || s.domain != "
         "domain || "
         "s.status != 0 || s.platform_error != 0) { return 1; } return 0; }\n"
         "Ambient :: () -> int { s := Sample(); "
         "if (s.nanoseconds != 11 || s.domain != 13 || s.status != 17 || "
         "s.platform_error != 19) { return 2; } return 0; }\n";

typedef struct
{
    host_clock_status status;
    int64_t nanoseconds, increment;
    int error, calls;
} clock_fixture;

static compiler_host *compiler;
static native_capabilities *native, *without_clock, *mixed_native, *handle_native;
static compiler_host_bytes closure;
static package_collection *packages;
static const execution_module *app_module;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static service_binding *services;
static int ambient_calls;

static host_clock_status sample_fixture(void *state, int64_t *nanoseconds, int *error)
{
    clock_fixture *fixture = state;
    *nanoseconds = fixture->nanoseconds;
    *error = fixture->error;
    fixture->nanoseconds += fixture->increment;
    fixture->calls++;
    return fixture->status;
}
static const char *ambient_sample(void *, const pin_value *, pin_value *results)
{
    ambient_calls++;
    results[0].i64 = 11;
    results[1].u64 = 13;
    results[2].i = 17;
    results[3].i = 19;
    return NULL;
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
    native_capabilities_free(mixed_native);
    native_capabilities_free(handle_native);
    native = without_clock = mixed_native = handle_native = NULL;
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
    const native_capability_pack *clock = host_clock_package();
    script_catalog_capability ambient = clock->catalog->capabilities[0];
    ambient.id = script_catalog_id("test-capability", "ambient.Sample");
    ambient.name = "Sample";
    const script_catalog catalog = { .capabilities = &ambient, .capability_count = 1 };
    static const node_host_call callback = { .invoke = ambient_sample };
    const native_capability_binding binding = {
        .id = ambient.id,
        .call = &callback,
        .access = clock->bindings[0].access,
    };
    const native_capability_pack packs[] = {
        { .catalog = &catalog, .bindings = &binding, .binding_count = 1 },
        *clock,
    };
    mixed_native = native_capabilities_from_definition_packs(packs, 2, definitions, 2, error, sizeof error);
    REQUIRE(mixed_native);
    const compiler_bootstrap options = {
        .folder = CHIBI_HOST_PACKAGES_COMPILER_ROOT "/bootstrap",
        .compiler = CHIBI_HOST_PACKAGES_COMPILER_ROOT,
        .core = CHIBI_HOST_PACKAGES_CORE_ROOT,
        .cache = test_compiler_cache(),
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

/* CompileWithEffects returns the rejection diagnostic as its trailing optional
 * result. Require its source coordinates as well as its message so a transport failure cannot pass. */
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
    yyjson_val *outcome = yyjson_arr_get(yyjson_doc_get_root(doc), 2);
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

static bool compile_package(const char *app, const char *clock, const native_capabilities *target)
{
    finish_call();
    if (packages) {
        package_collection_free(packages);
    }
    packages = NULL;
    app_module = NULL;
    free(closure.bytes);
    closure = (compiler_host_bytes){ 0 };
    size_t length = 0;
    char *input = compile_input(app, clock, "/host/clock/Clock.chibi", target, &length);
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
        fprintf(
            stderr,
            "host packages compiler closure: called=%d status=%d length=%zu %s "
            "(%s:%d:%d)\n",
            called,
            closure.flow_status,
            closure.length,
            closure.diagnostic,
            closure.source.asset,
            closure.source.line,
            closure.source.column
        );
    }
    if (called && !closure.length) {
        /* The closure entry gives empty bytes for a rejection; the plain compile gives its diagnostic. */
        input = compile_input(app, clock, "/host/clock/Clock.chibi", target, &length);
        compiler_host_result why = { 0 };
        if (compiler_host_invoke(
                compiler,
                compiler_host_entry("project:package_compiler:1:CompileWithEffects:callable"),
                input,
                length,
                COMPILE_BUDGET,
                &why
            )) {
            fprintf(stderr, "compile gave: %.*s\n", (int)(why.length > 1500 ? 1500 : why.length), why.json);
        }
        free(why.json);
        free(input);
    }
    REQUIRE(called && closure.flow_status == FLOW_OK && closure.length);
    REQUIRE(closure.counts.sljit_entries && !closure.counts.bytecode_entries);
    REQUIRE(!closure.counts.overflow && !closure.counts.inspection_bytecode_fallbacks);
    return true;
}

static bool compile_clock(const char *app, const native_capabilities *target)
{
    return compile_package(app, CLOCK, target);
}

static bool load_closure(behavior_backend backend, const native_capabilities *target)
{
    if (packages) {
        package_collection_free(packages);
    }
    packages = NULL;
    app_module = NULL;
    const native_prepared_context bindings = { .native = target };
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
    /* A call that reads the clock has the host effect; a pure one (a Between mismatch) has none. */
    REQUIRE(function.body && (!grant || (function.effects & SCRIPT_EFFECT_HOST)));
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
    REQUIRE(load_closure(backend, native));
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

static bool reading(
    behavior_backend backend,
    host_clock_grant *grant,
    int64_t nanoseconds,
    uint64_t domain,
    int kind,
    int code
)
{
    const pin_value arguments[] = {
        { .type = PIN_I64, .i64 = nanoseconds },
        { .type = PIN_U64, .u64 = domain },
        { .type = PIN_I32, .i = kind },
        { .type = PIN_I32, .i = code },
    };
    return call("Reading", arguments, 4, grant, backend);
}

static bool now_samples(behavior_backend backend)
{
    clock_fixture fixture = { .status = HOST_CLOCK_OK,
                              .nanoseconds = INT64_C(9007199254740993),
                              .error = EIO };
    host_clock_grant grant;
    host_clock_grant_init(&grant, sample_fixture, &fixture);
    REQUIRE(reading(backend, NULL, 0, 0, HOST_CLOCK_SAMPLE_DENIED, 0));
    REQUIRE(!fixture.calls);
    REQUIRE(reading(backend, &grant, fixture.nanoseconds, grant.domain, HOST_CLOCK_SAMPLE_OK, 0));
    fixture.status = HOST_CLOCK_FAILED;
    REQUIRE(reading(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, EIO));
    fixture.status = HOST_CLOCK_UNAVAILABLE;
    REQUIRE(reading(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_UNAVAILABLE, 0));
    fixture.status = HOST_CLOCK_OK;
    fixture.nanoseconds = -1;
    REQUIRE(reading(backend, &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, ERANGE));
    fixture.nanoseconds = INT64_C(9007199254740993);
    fixture.increment = 7;
    const pin_value expected[] = { { .type = PIN_I64, .i64 = 7 } };
    REQUIRE(call("Elapsed", expected, 1, &grant, backend));
    REQUIRE(call("Mismatch", NULL, 0, NULL, backend));
    return true;
}

TEST_CASE(host_packages_compile_link_and_run_through_stage_one)
{
    REQUIRE(load_compiler());
    REQUIRE(compile_clock(APP, native));
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
    REQUIRE(compile_clock(MIXED_APP, mixed_native));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        REQUIRE(load_closure(backends[b], mixed_native));
        clock_fixture fixture = { .status = HOST_CLOCK_OK,
                                  .nanoseconds = INT64_C(9007199254740993) };
        host_clock_grant grant;
        host_clock_grant_init(&grant, sample_fixture, &fixture);
        const pin_value arguments[] = {
            { .type = PIN_I64, .i64 = fixture.nanoseconds },
            { .type = PIN_U64, .u64 = grant.domain },
        };
        ambient_calls = 0;
        REQUIRE(call("Private", arguments, 2, &grant, backends[b]));
        REQUIRE(fixture.calls == 1 && !ambient_calls);
        REQUIRE(call("Ambient", NULL, 0, NULL, backends[b]));
        REQUIRE(fixture.calls == 1 && ambient_calls == 1);
    }
    REQUIRE(compile_package(NOW_APP, HOST_CLOCK_SOURCE, native));
    for (int b = 0; b < 2; b++) {
        REQUIRE(load_closure(backends[b], native));
        REQUIRE(now_samples(backends[b]));
    }
    return true;
}

/* The catalog compile entry hands the package compiler the target's native
 * types, so a signature can name a handle type such as the program service's
 * Program. Returns the diagnostic message, "" when the sources compile. */
static const char *catalog_compile(const char *source, char *message, size_t capacity)
{
    const char *project = "/project";
    const char *names[] = { "core" }, *roots[] = { "/sdk/core" };
    const char *paths[] = { "/project/app/App.chibi", "/sdk/core/target/Target.chibi" };
    /* The compiler replaces core:target's text with the platform's constants. */
    const char *texts[] = { source, "package target\npub Os: int : 0\n" };
    size_t sizes[] = { strlen(source), strlen(texts[1]) }, length = 0;
    char *input = compiler_package_catalog_request_json(
        project,
        names,
        roots,
        1,
        paths,
        texts,
        sizes,
        2,
        native_capabilities_catalog(handle_native),
        NULL,
        &length
    );
    if (!input) {
        return NULL;
    }
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileWithCatalog:callable"),
        input,
        length,
        COMPILE_BUDGET,
        &result
    );
    free(input);
    yyjson_doc *doc = called ? yyjson_read(result.json, result.length, 0) : NULL;
    yyjson_val *error = yyjson_arr_get(yyjson_doc_get_root(doc), 2);
    yyjson_val *diagnostic = yyjson_arr_get(yyjson_obj_get(error, "values"), 0);
    const char *reported = yyjson_get_str(yyjson_obj_get(diagnostic, "message"));
    if (!called || result.flow_status != FLOW_OK) {
        fprintf(stderr, "host packages catalog compile: %s\n", result.diagnostic);
        snprintf(message, capacity, "%s", "compiler did not run");
    } else {
        snprintf(message, capacity, "%s", reported ? reported : "");
    }
    yyjson_doc_free(doc);
    free(result.json);
    return message;
}

TEST_CASE(host_packages_catalog_handle_types_reach_package_signatures)
{
    const native_definition_pack definitions[] = { CORE_DEFINITION_PACK,
                                                   NATIVE_TEXT_DEFINITION_PACK,
                                                   NATIVE_ARRAY_DEFINITION_PACK };
    const native_capability_pack packs[] = { *program_service_pack() };
    char error[256], message[256];
    handle_native = native_capabilities_from_definition_packs(packs, 1, definitions, 3, error, sizeof error);
    REQUIRE(handle_native);
    const char *bootstrap_error = NULL;
    (void)bootstrap_error;
    const compiler_bootstrap options = {
        .folder = CHIBI_HOST_PACKAGES_COMPILER_ROOT "/bootstrap",
        .compiler = CHIBI_HOST_PACKAGES_COMPILER_ROOT,
        .core = CHIBI_HOST_PACKAGES_CORE_ROOT,
        .cache = test_compiler_cache(),
    };
    compiler = compiler_bootstrap_load(&options, error, sizeof error);
    REQUIRE(compiler);
    REQUIRE(
        catalog_compile(
            "package app\nStopped :: (handle: Program, watch: bool) -> int { "
            "if watch { return 1; } return 0; }\n",
            message,
            sizeof message
        ) &&
        !*message
    );
    /* The program natives take and return the handle type. */
    bool handles = catalog_compile(
        "package app\n"
        "Open :: (path: string, symbol: string, arguments: string[]) -> "
        "Program { return program.open(path, "
        "symbol, arguments); }\n"
        "Close :: (handle: Program) { program.close(handle); }\n"
        "Has :: (handle: Program, name: string) -> bool { return "
        "program.has(handle, name); }\n",
        message,
        sizeof message
    );
    if (!handles || *message) {
        fprintf(stderr, "program handle sources: %s\n", message);
    }
    REQUIRE(handles && !*message);
    /* format_text's optional untyped tail needs no argument. */
    REQUIRE(
        catalog_compile("package app\nPlain :: () -> string { return format_text(\"plain\"); }\n", message, sizeof message) &&
        !*message
    );
    /* core:target carries the compile's platform. */
    REQUIRE(
        catalog_compile(
            "package app\nimport target \"core:target\"\n"
            "Native :: () -> int { if target.Os == target.Windows { return 1; "
            "} return target.PointerBits; }\n",
            message,
            sizeof message
        ) &&
        !*message
    );
    REQUIRE(
        catalog_compile("package app\nStopped :: (handle: Missing) -> int { return 0; }\n", message, sizeof message) &&
        *message
    );
    return true;
}

/* An argument document that outgrows the host's shared quota is refused before
 * the compiler runs, and the diagnostic names the bound and the counts. Each
 * JSON string costs two values and 128 bytes of the 1,000,000-value, 64 MiB
 * budget, so 600,000 extra paths exhaust it while the document is checked. */
TEST_CASE(host_packages_compiler_names_the_exhausted_argument_bound)
{
    REQUIRE(load_compiler());
    size_t length = 0;
    char *input = compile_input(APP, CLOCK, "/host/clock/Clock.chibi", native, &length);
    REQUIRE(input);
    yyjson_doc *parsed = yyjson_read(input, length, 0);
    free(input);
    REQUIRE(parsed);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    REQUIRE(doc);
    yyjson_mut_val *paths = yyjson_mut_arr_get(yyjson_mut_doc_get_root(doc), 3);
    for (int i = 0; i < 600000; i++) {
        REQUIRE(yyjson_mut_arr_add_str(doc, paths, ""));
    }
    char *json = yyjson_mut_write(doc, 0, &length);
    yyjson_mut_doc_free(doc);
    REQUIRE(json);
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileWithEffects:callable"),
        json,
        length,
        COMPILE_BUDGET,
        &result
    );
    free(json);
    free(result.json);
    if (called || !strstr(result.diagnostic, " of 1000000 values, ")) {
        fprintf(stderr, "argument bound: %s\n", called ? "the call ran" : result.diagnostic);
    }
    REQUIRE(!called);
    REQUIRE(
        strstr(result.diagnostic, "invalid or oversized compiler argument document: the document, ")
    );
    REQUIRE(
        strstr(result.diagnostic, " of 1000000 values, ") &&
        strstr(result.diagnostic, " of 67108864 bytes")
    );
    return true;
}

void test_language_host_packages_compiler()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(host_packages_compile_link_and_run_through_stage_one);
    RUN_TEST_CASE(host_packages_catalog_handle_types_reach_package_signatures);
    RUN_TEST_CASE(host_packages_compiler_names_the_exhausted_argument_bound);
    pu_clear_setup();
}
