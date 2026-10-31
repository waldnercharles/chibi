#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "build_workflow_source.h"
#include "execution/module.h"
#include "execution/status.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <string.h>

/* Recording-only host. No processes, filesystem effects, clocks or game services. */
#define BUILD_MAX_CALLS 2
#define BUILD_MAX_ARGUMENTS 6
#define BUILD_MAX_ARGUMENT_BYTES 128

typedef struct
{
    int calls, argument_counts[BUILD_MAX_CALLS];
    int statuses[BUILD_MAX_CALLS];
    char arguments[BUILD_MAX_CALLS][BUILD_MAX_ARGUMENTS][BUILD_MAX_ARGUMENT_BYTES];
} build_recording;

static build_recording recording;
static int callback_entries;
static authoring_packages *packages;
static execution_module *module;
static const behavior_function *entry;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static service_binding *services;
static pin_value result;
static bool ran_expected_backend;

static const script_catalog_parameter arguments_parameter = {
    .name = "arguments",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY },
};
static const script_catalog_parameter status_result = {
    .name = "status",
    .type = { .storage = PIN_I32 },
};
static const script_catalog_capability run_step = {
    .id = 901,
    .name = "build.run",
    .kind = SCRIPT_CAPABILITY_FUNCTION,
    .parameters = &arguments_parameter,
    .parameter_count = 1,
    .results = &status_result,
    .result_count = 1,
    .effects = SCRIPT_EFFECT_HOST,
};
static const script_catalog catalog = {
    .capabilities = &run_step,
    .capability_count = 1,
};

static const char *record_step(void *service, const pin_value *arguments, pin_value *results)
{
    callback_entries++;
    build_recording *record = service;
    if (!record) {
        return "build recording service is unavailable";
    }
    if (record->calls >= BUILD_MAX_CALLS) {
        return "build recording call limit exceeded";
    }
    const pin_array *argv = arguments[0].ptr;
    if (arguments[0].type != PIN_STR || arguments[0].container != PIN_CONTAINER_ARRAY ||
        !argv || argv->element_type != PIN_STR || argv->count < 1 ||
        argv->count > BUILD_MAX_ARGUMENTS) {
        return "invalid build recording arguments";
    }
    /* Validate before copying, and retain only owned bytes after the callback. */
    for (int i = 0; i < argv->count; i++) {
        if (argv->items[i].type != PIN_STR ||
            argv->items[i].container != PIN_CONTAINER_SINGLE ||
            pin_text(argv->items[i]).length >= BUILD_MAX_ARGUMENT_BYTES ||
            memchr(pin_text(argv->items[i]).bytes, 0, pin_text(argv->items[i]).length)) {
            return "invalid build recording string";
        }
    }
    int call = record->calls;
    for (int i = 0; i < argv->count; i++) {
        pin_string text = pin_text(argv->items[i]);
        memcpy(record->arguments[call][i], text.bytes, text.length);
        record->arguments[call][i][text.length] = 0;
    }
    record->argument_counts[call] = argv->count;
    results[0].i = record->statuses[call];
    record->calls++;
    return NULL;
}
static const node_host_call recording_call = {
    .service = "test.build.recording",
    .invoke = record_step,
    .missing_service = "build recording service is unavailable",
};
static bool resolve_host(void *, uint64_t id, script_lower_host *out)
{
    if (id != run_step.id) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &recording_call, .access = &access };
    return true;
}
static void release_invocation()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    afree(services);
    services = NULL;
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}
static void cleanup()
{
    release_invocation();
    execution_module_free(module);
    authoring_packages_free(packages);
    module = NULL;
    packages = NULL;
    entry = NULL;
    recording = (build_recording){ 0 };
    callback_entries = 0;
}
/* text as the one file of a build program's package, as the build host
 * prepares it. */
static authoring_packages *prepare_text(
    const char *text,
    const script_lower_bindings *bindings,
    behavior_backend backend
)
{
    const source_package_input file = { "/workflow/BuildWorkflow.chibi",
                                        text,
                                        strlen(text) };
    const source_package_snapshot_options options = {
        .project_root = "/workflow",
        .files = &file,
        .file_count = 1
    };
    return authoring_packages_prepare(source_package_snapshot_new(&options), &catalog, bindings, backend, NULL);
}
/* Invalid packages, which compiled nothing and called no host. */
static bool rejected(const authoring_packages *prepared)
{
    return !authoring_packages_valid(prepared) && callback_entries == 0 &&
           recording.calls == 0;
}
static bool prepare(behavior_backend backend)
{
    const script_lower_bindings bindings = { .host = resolve_host };
    packages = prepare_text(build_workflow_source, &bindings, backend);
    if (!authoring_packages_valid(packages)) {
        return false;
    }
    module = authoring_packages_at(packages, 0)->module;
    execution_module_retain(module);
    authoring_packages_free(packages);
    packages = NULL;
    int authored = 0;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info function = execution_module_function_at(module, i);
        if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
            entry = function.body;
            authored++;
        }
    }
    return authored == 1 && entry && execution_module_backend(module) == backend &&
           execution_module_effects(module).host_sink;
}
static bool execute(bool with_service, int *status)
{
    release_invocation();
    ran_expected_backend = false;
    callback_entries = 0;
    if (with_service) {
        apush(services, ((service_binding){ "test.build.recording", &recording }));
    }
    bool resumable = behavior_function_resumable(entry);
    if (resumable) {
        behavior_function_activation_init(entry, &activation);
    }
    result = (pin_value){ .type = PIN_I32, .i = -999 };
    context = (behavior_function_context){
        .activation = resumable ? &activation : NULL,
        .results = &result,
        .budget = 1000,
    };
    behavior_function_bind_storage(&context, &storage);
    if (!behavior_function_bind(entry, &context, services, 0)) {
        return false;
    }
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    behavior_backend backend = execution_module_backend(module);
    *status = behavior_function_run(entry, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&scope);
    ran_expected_backend = !scope.counts.overflow &&
                           !scope.counts.inspection_bytecode_fallbacks &&
                           (backend == BEHAVIOR_BYTECODE
                                ? scope.counts.bytecode_entries > 0 &&
                                      !scope.counts.sljit_entries &&
                                      !scope.counts.wasm_entries
                            : backend == BEHAVIOR_SLJIT
                                ? scope.counts.sljit_entries > 0 &&
                                      !scope.counts.bytecode_entries
                                : scope.counts.wasm_entries > 0 &&
                                      !scope.counts.bytecode_entries);
    return true;
}
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };

TEST_CASE(build_workflow_preserves_order_and_argument_boundaries)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        int status = 0;
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == 0 && recording.calls == 2);
        const char *configure[] = {
            "cmake",
            "-S",
            ".",
            "-B",
            "build self-hosting",
            "-DCHIBI_NOTE=space \"quote\";$literal*",
        };
        const char *build[] = { "cmake", "--build", "build self-hosting" };
        REQUIRE(recording.argument_counts[0] == 6 && recording.argument_counts[1] == 3);
        for (int i = 0; i < 6; i++) {
            REQUIRE(!strcmp(recording.arguments[0][i], configure[i]));
        }
        for (int i = 0; i < 3; i++) {
            REQUIRE(!strcmp(recording.arguments[1][i], build[i]));
        }
    }
    return true;
}
TEST_CASE(build_workflow_configure_failure_skips_build)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        recording.statuses[0] = 37;
        recording.statuses[1] = 91;
        int status = 0;
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == 37 && recording.calls == 1);
    }
    return true;
}
TEST_CASE(build_workflow_build_failure_propagates)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        recording.statuses[1] = 23;
        int status = 0;
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == 23 && recording.calls == 2);
    }
    return true;
}
TEST_CASE(build_workflow_negative_status_propagates)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        recording.statuses[0] = -9;
        int status = 0;
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == -9 && recording.calls == 1);
    }
    return true;
}
TEST_CASE(build_workflow_fresh_invocation_does_not_retain_status)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        int status = 0;
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == 0 && recording.calls == 2);
        recording = (build_recording){ .statuses = { 19, 0 } };
        REQUIRE(execute(true, &status) && status == FLOW_OK && ran_expected_backend);
        REQUIRE(callback_entries == recording.calls);
        REQUIRE(result.i == 19 && recording.calls == 1);
    }
    return true;
}
TEST_CASE(build_workflow_missing_binding_fails_before_effects)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        const script_lower_bindings bindings = { 0 };
        packages = prepare_text(build_workflow_source, &bindings, backends[b]);
        REQUIRE(rejected(packages));
    }
    return true;
}
TEST_CASE(build_workflow_missing_service_fails_without_recording)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(backends[b]));
        int status = FLOW_OK;
        /* Binding needs no world; the callback rejects the missing service. */
        REQUIRE(execute(false, &status));
        REQUIRE(status == FLOW_ERR_INVALID_GRAPH && ran_expected_backend);
        REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(context.error_message);
        REQUIRE(!strcmp(context.error_message, "build recording service is unavailable"));
        REQUIRE(result.type == PIN_I32 && result.i == -999);
        REQUIRE(callback_entries == 1 && recording.calls == 0);
    }
    return true;
}
TEST_CASE(build_workflow_invalid_source_fails_before_effects)
{
    const char
        *invalid = "package invalid\nBuild :: () -> int { return 1 + ; }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        const script_lower_bindings bindings = { .host = resolve_host };
        packages = prepare_text(invalid, &bindings, backends[b]);
        REQUIRE(rejected(packages));
    }
    return true;
}
void test_language_build_workflow()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(build_workflow_preserves_order_and_argument_boundaries);
    RUN_TEST_CASE(build_workflow_configure_failure_skips_build);
    RUN_TEST_CASE(build_workflow_build_failure_propagates);
    RUN_TEST_CASE(build_workflow_negative_status_propagates);
    RUN_TEST_CASE(build_workflow_fresh_invocation_does_not_retain_status);
    RUN_TEST_CASE(build_workflow_missing_binding_fails_before_effects);
    RUN_TEST_CASE(build_workflow_missing_service_fails_without_recording);
    RUN_TEST_CASE(build_workflow_invalid_source_fails_before_effects);
    pu_clear_setup();
}
