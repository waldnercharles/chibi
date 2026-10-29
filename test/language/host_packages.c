#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "execution/service.h"
#include "execution/status.h"
#include "host/clock_package.h"
#include "host_packages_source.h"
#include "runtime/core_definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Test-local scalar wrappers exercise imports and the native boundary without
 * fixing the public clock package's result or duration API. */
static const char
    CLOCK[] = "package clock\n"
              "pub Read :: () -> (nanoseconds: i64, domain: u64, status: int, "
              "platform_error: int) { sample := Sample(); return "
              "(sample.nanoseconds, "
              "sample.domain, sample.status, sample.platform_error); }\n";
static const char CHECK
    [] = "package app\nimport watch \"host:clock\"\n"
         "Check :: (nanoseconds: i64, domain: u64, status: int, error: int) -> "
         "int { sample := watch.Read(); "
         "if (sample.nanoseconds != nanoseconds) { return 1; } "
         "if (sample.domain != domain) { return 2; } "
         "if (sample.status != status) { return 3; } "
         "if (sample.platform_error != error) { return 4; } return 0; }\n";
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static native_capabilities *native;
static authoring_packages *packages;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static service_binding *services;
static pin_value result;
static compiler_fixture time_fixture;
static int ambient_calls;

typedef struct
{
    host_clock_status status;
    int64_t nanoseconds, increment;
    int error, calls;
} clock_fixture;

static host_clock_status read_fixture(void *state, int64_t *nanoseconds, int *error)
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
static void setup()
{
    char error[256];
    native = native_capabilities_new(host_clock_package(), 1, error, sizeof error);
    assert(native);
}
static void cleanup()
{
    finish_call();
    compiler_fixture_free(&time_fixture);
    authoring_packages_free(packages);
    packages = NULL;
    native_capabilities_free(native);
    native = NULL;
}
static bool prepare_at(const char *app, const char *host, const char *path, behavior_backend backend)
{
    finish_call();
    authoring_packages_free(packages);
    packages = NULL;
    const source_package_input files[] = {
        { "/project/app/App.chibi", app, strlen(app) },
        { path, host, strlen(host) },
    };
    const source_package_collection collection = { "host", "/host" };
    const source_package_snapshot_options options = {
        .project_root = "/project",
        .files = files,
        .file_count = 2,
        .collections = &collection,
        .collection_count = 1,
    };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot);
    script_lower_bindings bindings = authoring_native_bindings(native);
    packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(native),
        &bindings,
        backend,
        NULL
    );
    REQUIRE(packages);
    return true;
}
static bool prepare(const char *app, behavior_backend backend)
{
    return prepare_at(app, CLOCK, "/host/clock/Clock.chibi", backend);
}
static const authoring_package *package(const char *identity)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *item = authoring_packages_at(packages, i);
        if (item && item->identity && !strcmp(item->identity, identity)) {
            return item;
        }
    }
    return NULL;
}
static bool valid()
{
    if (authoring_packages_valid(packages)) {
        return true;
    }
    const char *error = authoring_packages_error(packages);
    if (error) {
        fprintf(stderr, "host packages: %s\n", error);
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *item = authoring_packages_at(packages, i);
        for (int j = 0; item && j < item->diagnostic_count; j++) {
            fprintf(
                stderr,
                "%s:%d: %s\n",
                item->identity,
                item->diagnostics[j].range.line,
                item->diagnostics[j].message
            );
        }
    }
    return false;
}
static const script_function *function(const authoring_package *item, const char *name)
{
    for (int i = 0; item && item->analysis && i < item->analysis->function_count; i++) {
        const script_function *candidate = &item->analysis->functions[i];
        if (!strcmp(item->analysis->symbols[candidate->symbol].name, name)) {
            return candidate;
        }
    }
    return NULL;
}
static bool call(
    const char *name,
    behavior_backend backend,
    const pin_value *arguments,
    int count,
    host_clock_grant *grant,
    int expected
)
{
    finish_call();
    const authoring_package *app = package("project:app");
    const script_function *source = function(app, name);
    REQUIRE(app && app->valid && app->module && source);
    uint64_t id = app->analysis->symbols[source->symbol].id;
    const behavior_function *body = NULL;
    for (int i = 0; i < execution_module_function_count(app->module); i++) {
        execution_module_function_info info = execution_module_function_at(app->module, i);
        if (info.id == id) {
            body = info.body;
        }
    }
    REQUIRE(body && execution_module_backend(app->module) == backend);
    if (grant) {
        apush(services, ((service_binding){ HOST_CLOCK_SERVICE, grant }));
    }
    if (behavior_function_resumable(body)) {
        behavior_function_activation_init(body, &activation);
    }
    result = (pin_value){ .type = PIN_I32, .i = -999 };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(body) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &result,
        .budget = 2000,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(body, &context, services, 0));
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = behavior_function_run(body, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(result.type == PIN_I32 && result.i == expected);
    REQUIRE(!scope.counts.overflow && !scope.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_SLJIT
            ? scope.counts.sljit_entries > 0 && !scope.counts.bytecode_entries
            : scope.counts.bytecode_entries > 0 && !scope.counts.sljit_entries
    );
    finish_call();
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
    return call("Check", backend, arguments, 4, grant, 0);
}

TEST_CASE(host_packages_use_import_aliases_and_lexical_shadowing)
{
    const char *app = "package app\nimport watch \"host:clock\"\n"
                      "Row :: struct { Read: int; }\n"
                      "Alias :: () -> int { sample := watch.Read(); return "
                      "sample.status; }\n"
                      "Shadow :: () -> int { watch: Row = {Read: 7}; return "
                      "watch.Read; }\n";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(app, backends[b]) && valid());
        REQUIRE(call("Alias", backends[b], NULL, 0, NULL, HOST_CLOCK_SAMPLE_DENIED));
        REQUIRE(call("Shadow", backends[b], NULL, 0, NULL, 7));
        const script_function *shadow = function(package("project:app"), "Shadow");
        REQUIRE(shadow && shadow->pure && !(shadow->effects & SCRIPT_EFFECT_HOST));
    }
    return true;
}
TEST_CASE(host_package_private_kernel_precedes_an_ambient_native)
{
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
    native_capabilities_free(native);
    char error[256];
    native = native_capabilities_new(packs, 2, error, sizeof error);
    REQUIRE(native);
    const char
        *app = "package app\nimport watch \"host:clock\"\n"
               "Private :: (expected: i64, domain: u64) -> int { s := "
               "watch.Read(); "
               "if (s.nanoseconds != expected || s.domain != domain || "
               "s.status != 0 || "
               "s.platform_error != 0) { return 1; } return 0; }\n"
               "Ambient :: () -> int { s := Sample(); "
               "if (s.nanoseconds != 11 || s.domain != 13 || s.status != 17 || "
               "s.platform_error != 19) { return 2; } return 0; }\n";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(app, backends[b]) && valid());
        clock_fixture fixture = { .status = HOST_CLOCK_OK,
                                  .nanoseconds = INT64_C(9007199254740993) };
        host_clock_grant grant;
        host_clock_grant_init(&grant, read_fixture, &fixture);
        const pin_value arguments[] = {
            { .type = PIN_I64, .i64 = fixture.nanoseconds },
            { .type = PIN_U64, .u64 = grant.domain },
        };
        ambient_calls = 0;
        REQUIRE(call("Private", backends[b], arguments, 2, &grant, 0));
        REQUIRE(fixture.calls == 1 && !ambient_calls);
        REQUIRE(call("Ambient", backends[b], NULL, 0, NULL, 0));
        REQUIRE(fixture.calls == 1 && ambient_calls == 1);
    }
    return true;
}
TEST_CASE(host_packages_reject_missing_private_and_inexact_members)
{
    const char *apps[] = {
        "package app\nMain :: () -> int { sample := watch.Read(); return "
        "sample.status; }\n",
        "package app\nMain :: () -> int { sample := Sample(); return "
        "sample.status; }\n",
        "package app\nimport watch \"host:clock\"\nMain :: () -> int { sample "
        ":= watch.Sample(); return sample.status; }\n",
        "package app\nimport watch \"host:clock\"\nMain :: () -> int { sample "
        ":= watch.read(); return sample.status; }\n",
        "package app\nimport watch \"host:clock\"\nMain :: () -> int { sample "
        ":= Watch.Read(); return sample.status; }\n",
        "package app\nimport watch \"host:clock\"\nMain :: () -> int { watch: "
        "int = 7; sample := watch.Read(); return sample.status; }\n",
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)(sizeof apps / sizeof *apps); i++) {
            REQUIRE(prepare(apps[i], backends[b]));
            REQUIRE(!authoring_packages_valid(packages));
            const authoring_package *app = package("project:app");
            REQUIRE(app && !app->valid && !app->module && app->diagnostic_count > 0);
        }
        REQUIRE(prepare_at(
            "package app\nimport other \"host:other\"\nMain :: () -> int { "
            "return other.Read(); }\n",
            "package other\npub Read :: () -> int { sample := Sample(); return "
            "sample.status; }\n",
            "/host/other/Other.chibi",
            backends[b]
        ));
        REQUIRE(!authoring_packages_valid(packages));
        const authoring_package *other = package("host:other");
        REQUIRE(other && !other->valid && !other->module && other->diagnostic_count > 0);
        REQUIRE(prepare_at(
            CHECK,
            "package clock\npub Read :: () -> (nanoseconds: i64, domain: u64, "
            "status: int, platform_error: int) { return sample(); }\n",
            "/host/clock/Clock.chibi",
            backends[b]
        ));
        REQUIRE(!authoring_packages_valid(packages));
        const authoring_package *clock = package("host:clock");
        REQUIRE(clock && !clock->valid && !clock->module && clock->diagnostic_count > 0);
    }
    return true;
}
TEST_CASE(host_packages_report_an_unavailable_target_before_execution)
{
    native_capabilities_free(native);
    char error[256];
    native = native_capabilities_new(NULL, 0, error, sizeof error);
    REQUIRE(native);
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(CHECK, backends[b]));
        REQUIRE(!authoring_packages_valid(packages));
        const authoring_package *clock = package("host:clock");
        REQUIRE(clock && !clock->valid && clock->diagnostic_count == 1);
        REQUIRE(!strcmp(clock->diagnostics[0].message, "Host package is unavailable for this target"));
    }
    return true;
}

TEST_CASE(host_clock_grants_transport_recoverable_statuses_and_exact_values)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(CHECK, backends[b]) && valid());
        clock_fixture fixture = { .status = HOST_CLOCK_OK,
                                  .nanoseconds = INT64_C(9007199254740993),
                                  .error = EIO };
        host_clock_grant grant;
        host_clock_grant_init(&grant, read_fixture, &fixture);
        REQUIRE(check(backends[b], NULL, 0, 0, HOST_CLOCK_SAMPLE_DENIED, 0));
        REQUIRE(!fixture.calls);
        REQUIRE(
            check(backends[b], &grant, fixture.nanoseconds, grant.domain, HOST_CLOCK_SAMPLE_OK, 0)
        );
        REQUIRE(fixture.calls == 1);
        fixture.status = HOST_CLOCK_FAILED;
        REQUIRE(check(backends[b], &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, EIO));
        fixture.status = HOST_CLOCK_UNAVAILABLE;
        REQUIRE(check(backends[b], &grant, 0, 0, HOST_CLOCK_SAMPLE_UNAVAILABLE, 0));
        fixture.status = HOST_CLOCK_OK;
        fixture.nanoseconds = -1;
        REQUIRE(check(backends[b], &grant, 0, 0, HOST_CLOCK_SAMPLE_FAILED, ERANGE));
        fixture.nanoseconds = INT64_MAX;
        REQUIRE(check(backends[b], &grant, INT64_MAX, grant.domain, HOST_CLOCK_SAMPLE_OK, 0));
        REQUIRE(fixture.calls == 5);
        REQUIRE(check(backends[b], NULL, 0, 0, HOST_CLOCK_SAMPLE_DENIED, 0));
        REQUIRE(fixture.calls == 5);
    }
    return true;
}
TEST_CASE(host_clock_samples_keep_values_after_later_reads)
{
    const char *app =
        "package app\nimport watch \"host:clock\"\n"
        "Check :: (earlier: i64, later: i64, domain: u64) -> int { "
        "first := watch.Read(); second := watch.Read(); "
        "if (first.nanoseconds != earlier || second.nanoseconds != later) { "
        "return 1; } "
        "if (first.domain != domain || second.domain != domain) { return 2; } "
        "if (first.status != 0 || second.status != 0 || first.platform_error "
        "!= 0 || second.platform_error != 0) { return 3; } return 0; }\n";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(app, backends[b]) && valid());
        clock_fixture fixture = { .status = HOST_CLOCK_OK,
                                  .nanoseconds = INT64_C(9007199254740993),
                                  .increment = 1 };
        host_clock_grant grant;
        host_clock_grant_init(&grant, read_fixture, &fixture);
        const pin_value arguments[] = {
            { .type = PIN_I64, .i64 = INT64_C(9007199254740993) },
            { .type = PIN_I64, .i64 = INT64_C(9007199254740994) },
            { .type = PIN_U64, .u64 = grant.domain },
        };
        REQUIRE(call("Check", backends[b], arguments, 3, &grant, 0));
        REQUIRE(fixture.calls == 2);
    }
    return true;
}
TEST_CASE(host_clock_effects_follow_imported_wrappers)
{
    const script_catalog_capability *capability = script_catalog_capability_by_id(
        native_capabilities_catalog(native),
        HOST_CLOCK_SAMPLE_ID
    );
    const native_capability_binding
        *binding = native_capabilities_find(native, HOST_CLOCK_SAMPLE_ID, 0);
    REQUIRE(capability && capability->effects == SCRIPT_EFFECT_HOST);
    REQUIRE(binding && binding->abi && binding->call && binding->access);
    REQUIRE(binding->access->flags & NODE_EFFECT_HOST_SINK);
    REQUIRE(binding->call->optional_service && !strcmp(binding->call->service, HOST_CLOCK_SERVICE));
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(CHECK, backends[b]) && valid());
        const script_function *wrapper = function(package("host:clock"), "Read");
        const script_function *caller = function(package("project:app"), "Check");
        REQUIRE(wrapper && !wrapper->pure && (wrapper->effects & SCRIPT_EFFECT_HOST));
        REQUIRE(caller && !caller->pure && (caller->effects & SCRIPT_EFFECT_HOST));
    }
    return true;
}
TEST_CASE(host_clock_grants_have_distinct_domains)
{
    host_clock_grant grants[64];
    clock_fixture fixture = { .status = HOST_CLOCK_OK };
    for (int i = 0; i < 64; i++) {
        host_clock_grant_init(&grants[i], read_fixture, &fixture);
        REQUIRE(grants[i].domain);
        for (int j = 0; j < i; j++) {
            REQUIRE(grants[j].domain != grants[i].domain);
        }
    }
    REQUIRE(!fixture.calls);
    return true;
}
TEST_CASE(host_clock_platform_samples_are_nondecreasing)
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) &&                            \
    (defined(__APPLE__) || defined(__linux__))
    int64_t previous = 0;
#endif
    for (int i = 0; i < 64; i++) {
        int64_t nanoseconds = -1;
        int error = EIO;
        host_clock_status status = host_clock_read(&nanoseconds, &error);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) &&                            \
    (defined(__APPLE__) || defined(__linux__))
        REQUIRE(status == HOST_CLOCK_OK && !error && nanoseconds >= previous);
        previous = nanoseconds;
#else
        REQUIRE(status == HOST_CLOCK_UNAVAILABLE && !nanoseconds && !error);
#endif
    }
    return true;
}

TEST_CASE(core_time_value_contracts_run_as_authored_tests)
{
    const source_package_input files[] = {
        { "/compiler/TimeTests.chibi",
          CORE_TIME_TEST_SOURCE,
          sizeof CORE_TIME_TEST_SOURCE - 1 },
        { "/sdk/core/time/Time.chibi", CORE_TIME_SOURCE, sizeof CORE_TIME_SOURCE - 1 },
    };
    const native_definition_pack core = CORE_DEFINITION_PACK;
    REQUIRE(
        compiler_fixture_prepare_packs(&time_fixture, files, sizeof files / sizeof *files, &core, 1)
    );
    REQUIRE(compiler_fixture_tests(&time_fixture));
    return true;
}

void test_language_host_packages()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(host_packages_use_import_aliases_and_lexical_shadowing);
    RUN_TEST_CASE(host_package_private_kernel_precedes_an_ambient_native);
    RUN_TEST_CASE(host_packages_reject_missing_private_and_inexact_members);
    RUN_TEST_CASE(host_packages_report_an_unavailable_target_before_execution);
    RUN_TEST_CASE(host_clock_grants_transport_recoverable_statuses_and_exact_values);
    RUN_TEST_CASE(host_clock_samples_keep_values_after_later_reads);
    RUN_TEST_CASE(host_clock_effects_follow_imported_wrappers);
    RUN_TEST_CASE(host_clock_grants_have_distinct_domains);
    RUN_TEST_CASE(host_clock_platform_samples_are_nondecreasing);
    RUN_TEST_CASE(core_time_value_contracts_run_as_authored_tests);
    pu_clear_setup();
}
