#include "authoring/native_bindings.h"
#include "authoring/project_compile.h"
#include "authoring/source_package_io.h"
#include "authoring/source_package_prepare.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define RUNNER_SERVICE "tool.runner"
#define RUNNER_ID(N) (UINT64_C(0x72756e6e65720000) + (N))

typedef struct
{
    authoring_program *program;
    arena arguments_storage;
    pin_array arguments;
    void (*interrupt)(int), (*terminate)(int);
} policy_host;

static volatile sig_atomic_t policy_stopped;

static void stop_policy(int)
{
    policy_stopped = 1;
}

static void report_text(const char *text, size_t size)
{
    if (!size) {
        return;
    }
    fwrite(text, 1, size, stderr);
    fputc('\n', stderr);
}

static const char *arguments(void *service, const pin_value *, pin_value *results)
{
    policy_host *host = service;
    results[0].ptr = &host->arguments;
    return NULL;
}

static const char *select_program(void *service, const pin_value *arguments, pin_value *results)
{
    policy_host *host = service;
    arena scratch = { 0 };
    const char *path = NULL;
    const char *error = pin_text_cstring(&scratch, arguments[0], &path, NULL);
    if (!error) {
        results[0].b = authoring_program_select(host->program, path);
    }
    arena_free(&scratch);
    return error;
}

static const char *snapshot(void *service, const pin_value *, pin_value *results)
{
    policy_host *host = service;
    results[0].u64 = authoring_program_snapshot(host->program);
    return NULL;
}

static const char *error_revision(void *service, const pin_value *, pin_value *results)
{
    policy_host *host = service;
    results[0].u64 = authoring_program_error_revision(host->program);
    return NULL;
}

static const char *report_error(void *service, const pin_value *, pin_value *)
{
    policy_host *host = service;
    const char *error = authoring_program_error(host->program);
    report_text(error, strlen(error));
    return NULL;
}

static const char *report(void *, const pin_value *arguments, pin_value *)
{
    pin_string text = pin_text(arguments[0]);
    report_text(text.bytes, text.length);
    return NULL;
}

static const char *compile(void *service, const pin_value *, pin_value *results)
{
    policy_host *host = service;
    results[0].b = authoring_program_compile(host->program);
    return NULL;
}

static const char *prepare(void *service, const pin_value *arguments, pin_value *results)
{
    policy_host *host = service;
    arena scratch = { 0 };
    const char *entry = NULL;
    const char *error = pin_text_cstring(&scratch, arguments[0], &entry, NULL);
    if (!error) {
        results[0].b = authoring_program_prepare(host->program, entry);
    }
    arena_free(&scratch);
    return error;
}

static const char *commit(void *service, const pin_value *, pin_value *)
{
    policy_host *host = service;
    authoring_program_commit(host->program);
    return NULL;
}

static const char *discard(void *service, const pin_value *, pin_value *)
{
    policy_host *host = service;
    authoring_program_discard(host->program);
    return NULL;
}

/* This private CLI host may invoke a separate user program. No borrowed
 * policy argument, result, state or managed value crosses that boundary. */
static const char *call(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *results
)
{
    policy_host *host = service;
    signal(SIGINT, host->interrupt);
    signal(SIGTERM, host->terminate);
    results[0].i = policy_stopped
                     ? 0
                     : authoring_program_call(host->program, context->program);
    signal(SIGINT, stop_policy);
    signal(SIGTERM, stop_policy);
    return NULL;
}

static const char *pause_policy(void *, const pin_value *arguments, pin_value *results)
{
    int milliseconds = arguments[0].i;
    if (milliseconds < 0) {
        return "Runner wait requires nonnegative milliseconds";
    }
#ifdef _WIN32
    if (!policy_stopped) {
        Sleep((DWORD)milliseconds);
    }
#else
    struct timespec remaining = { .tv_sec = milliseconds / 1000,
                                  .tv_nsec = (long)(milliseconds % 1000) * 1000000 };
    while (!policy_stopped && nanosleep(&remaining, &remaining)) {
        if (errno != EINTR) {
            return "Runner wait failed";
        }
    }
#endif
    results[0].b = !policy_stopped;
    return NULL;
}

static const char *stopped(void *, const pin_value *, pin_value *results)
{
    results[0].b = policy_stopped != 0;
    return NULL;
}

static const script_catalog_parameter text_parameter = { .name = "text",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_parameter milliseconds_parameter = {
    .name = "milliseconds",
    .type = { .storage = PIN_I32 }
};
static const script_catalog_parameter bool_result = { .name = "value",
                                                      .type = { .storage = PIN_BOOL } };
static const script_catalog_parameter int_result = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
static const script_catalog_parameter revision_result = { .name = "value",
                                                          .type = { .storage = PIN_U64 } };
static const script_catalog_parameter arguments_result = {
    .name = "value",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY }
};

#define RUNNER_NATIVES(X)                                                                   \
    X(1, "runner.arguments", NULL, 0, &arguments_result, 1, invoke, arguments)              \
    X(2, "runner.select", &text_parameter, 1, &bool_result, 1, invoke, select_program)      \
    X(3, "runner.snapshot", NULL, 0, &revision_result, 1, invoke, snapshot)                 \
    X(4, "runner.error_revision", NULL, 0, &revision_result, 1, invoke, error_revision)     \
    X(5, "runner.report_error", NULL, 0, NULL, 0, invoke, report_error)                     \
    X(6, "runner.report", &text_parameter, 1, NULL, 0, invoke, report)                      \
    X(7, "runner.compile", NULL, 0, &bool_result, 1, invoke, compile)                       \
    X(8, "runner.prepare", &text_parameter, 1, &bool_result, 1, invoke, prepare)            \
    X(9, "runner.commit", NULL, 0, NULL, 0, invoke, commit)                                 \
    X(10, "runner.discard", NULL, 0, NULL, 0, invoke, discard)                              \
    X(11, "runner.call", NULL, 0, &int_result, 1, context, call)                            \
    X(12, "runner.wait", &milliseconds_parameter, 1, &bool_result, 1, invoke, pause_policy) \
    X(13, "runner.stopped", NULL, 0, &bool_result, 1, invoke, stopped)

#define CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = RUNNER_ID(ID),                                                             \
      .name = NAME,                                                                    \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                              \
      .parameters = PARAMETERS,                                                        \
      .parameter_count = COUNT,                                                        \
      .results = RESULTS,                                                              \
      .result_count = RESULT_COUNT,                                                    \
      .effects = SCRIPT_EFFECT_HOST },
static const script_catalog_capability capabilities[] = { RUNNER_NATIVES(CAPABILITY) };
#undef CAPABILITY
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = sizeof capabilities /
                                                            sizeof *capabilities };

#define CALL(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    static const node_host_call call_##ID = {                                    \
        .service = RUNNER_SERVICE,                                               \
        .KIND = FUNCTION,                                                        \
        .missing_service = "Runner host service is unavailable"                  \
    };
RUNNER_NATIVES(CALL)
#undef CALL
static const node_access host_sink = { .flags = NODE_EFFECT_HOST_SINK };
#define BINDING(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = RUNNER_ID(ID), .call = &call_##ID, .access = &host_sink },
static const native_capability_binding bindings[] = { RUNNER_NATIVES(BINDING) };
#undef BINDING
#undef RUNNER_NATIVES
static const native_capability_pack runner_pack = {
    .catalog = &catalog,
    .bindings = bindings,
    .binding_count = sizeof bindings / sizeof *bindings
};

static void report_diagnostic(const char *path, const script_diagnostic *diagnostic)
{
    fprintf(
        stderr,
        "%s:%d:%d: %s\n",
        path,
        diagnostic->range.line,
        diagnostic->range.column,
        diagnostic->message
    );
}

static void report_invalid(const authoring_packages *packages, const char *policy)
{
    const source_package_snapshot_error *error = source_package_snapshot_error_at(
        authoring_packages_snapshot(packages)
    );
    if (error) {
        report_diagnostic(policy, &error->diagnostic);
        return;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; j < package->diagnostic_count; j++) {
            report_diagnostic(policy, &package->diagnostics[j]);
        }
    }
    const char *error_text = authoring_packages_error(packages);
    if (error_text) {
        report_text(error_text, strlen(error_text));
    }
}

static const authoring_package *root(const authoring_packages *packages)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (!strcmp(package->identity, "project:")) {
            return package;
        }
    }
    return NULL;
}

static uint64_t entry(const authoring_package *package)
{
    const script_analysis *analysis = package->analysis;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->scope != analysis->package_scope || strcmp(symbol->name, "Run")) {
            continue;
        }
        for (int j = 0; j < execution_module_function_count(package->module); j++) {
            execution_module_function_info
                function = execution_module_function_at(package->module, j);
            if (function.kind != EXECUTION_FUNCTION_AUTHORED ||
                function.id != symbol->id) {
                continue;
            }
            int parameters = 0, results = 0;
            behavior_function_arguments(function.body, &parameters);
            const pin_value *signature = behavior_function_results(function.body, &results);
            if (!parameters && results == 1 && signature[0].type == PIN_I32 &&
                signature[0].container == PIN_CONTAINER_SINGLE &&
                behavior_function_kind(function.body, BEHAVIOR_BANK_RESULT, 0) ==
                    BEHAVIOR_VALUE) {
                return symbol->id;
            }
        }
    }
    return 0;
}

int authoring_program_policy_run(const char *policy, const char *core, const char *const *args, int count)
{
    assert(policy && core && count >= 0 && (!count || args));
    source_package_load_error load_error = { 0 };
    source_package_input *files = NULL;
    if (!source_package_file_read(policy, &files, &load_error)) {
        fprintf(stderr, "%s: %s\n", load_error.path, load_error.message);
        return 1;
    }
    char *directory = xstrdup(files[0].path);
    char *slash = strrchr(directory, '/');
    assert(slash);
    slash[slash == directory ? 1 : 0] = 0;
    source_package_snapshot_options options = { .project_root = directory,
                                                .files = files,
                                                .file_count = 1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    free(directory);
    source_package_files_free(files);

    const native_definition_pack definitions[] = { CORE_DEFINITION_PACK,
                                                   NATIVE_TEXT_DEFINITION_PACK,
                                                   NATIVE_ARRAY_DEFINITION_PACK };
    char error[512];
    native_capabilities *native = native_capabilities_from_definition_packs(
        &runner_pack,
        1,
        definitions,
        3,
        error,
        sizeof error
    );
    if (!native) {
        source_package_snapshot_free(snapshot);
        report_text(error, strlen(error));
        return 1;
    }
    script_lower_bindings lower = authoring_native_bindings(native);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(native),
        &lower,
        behavior_default_backend(),
        NULL
    );
    if (!authoring_packages_valid(packages)) {
        report_invalid(packages, policy);
        authoring_packages_free(packages);
        native_capabilities_free(native);
        return 1;
    }
    const authoring_package *package = root(packages);
    uint64_t run = package ? entry(package) : 0;
    if (!run) {
        report_text(
            "Runner policy requires Run :: () -> int",
            sizeof "Runner policy requires Run :: () -> int" - 1
        );
        authoring_packages_free(packages);
        native_capabilities_free(native);
        return 1;
    }

    policy_host host = {
        .program = authoring_program_new(core),
        .arguments = { .element_type = PIN_STR, .count = count, .capacity = count }
    };
    host.arguments.items = arena_alloc(
        &host.arguments_storage,
        (size_t)count * sizeof *host.arguments.items
    );
    const char *argument_error = NULL;
    for (int i = 0; !argument_error && i < count; i++) {
        argument_error = pin_value_text(
            &host.arguments_storage,
            &host.arguments.items[i],
            args[i],
            strlen(args[i])
        );
    }
    service_binding *services = NULL;
    apush(services, ((service_binding){ RUNNER_SERVICE, &host }));
    execution_program *runtime = argument_error || !host.program
                                   ? NULL
                                   : execution_program_new(package->module, NULL, services);
    afree(services);
    execution_instance *instance = runtime ? execution_program_attach(runtime, 1)
                                           : NULL;
    int status = 1, result = 1;
    if (instance) {
        policy_stopped = 0;
        host.interrupt = signal(SIGINT, stop_policy);
        host.terminate = signal(SIGTERM, stop_policy);
        if (host.interrupt != SIG_ERR && host.terminate != SIG_ERR) {
            status = execution_instance_call(instance, run, &result, 0, 0, UINT_MAX);
        }
        if (host.interrupt != SIG_ERR) {
            signal(SIGINT, host.interrupt);
        }
        if (host.terminate != SIG_ERR) {
            signal(SIGTERM, host.terminate);
        }
        if (status != FLOW_OK) {
            const execution_program_failure *failure = execution_program_error(runtime);
            if (failure) {
                fprintf(
                    stderr,
                    "%s:%d:%d: %s\n",
                    failure->asset_name,
                    failure->line,
                    failure->column,
                    failure->message
                );
            } else {
                report_text("Runner policy execution failed", sizeof "Runner policy execution failed" - 1);
            }
        }
    } else {
        const char *message = argument_error
                                ? argument_error
                                : "Runner policy runtime preparation failed";
        report_text(message, strlen(message));
    }
    execution_program_free(runtime);
    authoring_packages_free(packages);
    native_capabilities_free(native);
    authoring_program_free(host.program);
    arena_free(&host.arguments_storage);
    return status == FLOW_OK ? result : 1;
}
