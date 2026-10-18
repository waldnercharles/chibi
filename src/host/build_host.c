#include "host/build_host.h"

#include "authoring/source_prepare.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/process.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BUILD_MAX_ARGUMENTS 256
#define BUILD_MAX_ARGUMENT_BYTES 65536
#define BUILD_MAX_STEPS 64

static const char *const environment_names[] = {
    "PATH",
    "HOME",
    "TMPDIR",
    "TMP",
    "TEMP",
    "SDKROOT",
    "DEVELOPER_DIR",
    "CHIBI_TEST_DEADLINE",
    "CHIBI_BUILD_WORKER",
};
typedef struct
{
    const chibi_build_options *options;
    chibi_build_result *result;
    char *root;
    char *environment[sizeof(environment_names) / sizeof(*environment_names) + 1];
    double deadline;
} build_host;

double chibi_build_now()
{
    double now = chibi_process_now();
    return now < 0 ? NAN : now;
}
static const char *environment_value(const build_host *host, const char *name)
{
    for (int i = 0; host->environment[i]; i++) {
        const char *row = host->environment[i];
        size_t length = strlen(name);
        if (!strncmp(row, name, length) && row[length] == '=') {
            return row + length + 1;
        }
    }
    return "";
}
static const char *host_ready(build_host *host)
{
    if (host->options->cancelled && *host->options->cancelled) {
        return "build process cancelled";
    }
    double now = chibi_build_now();
    if (!isfinite(now)) {
        return "build process host failure";
    }
    return now >= host->deadline ? "build process timeout" : NULL;
}
static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}
static char *executable(build_host *host, const char *name)
{
    if (name[0] == '/') {
        return xstrdup(name);
    }
    if (strchr(name, '/')) {
        return join(host->root, name);
    }
    const char *path = NULL;
    for (int i = 0; host->environment[i]; i++) {
        if (!strncmp(host->environment[i], "PATH=", 5)) {
            path = host->environment[i] + 5;
            break;
        }
    }
    if (!path) {
        return NULL;
    }
    for (;;) {
        const char *end = strchr(path, ':');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        char *directory = xmalloc(length + 1);
        memcpy(directory, path, length);
        directory[length] = 0;
        char *base = directory[0] == '/' ? xstrdup(directory)
                                         : join(host->root, directory);
        char *candidate = join(base, name);
        free(base);
        free(directory);
        if (!access(candidate, X_OK)) {
            return candidate;
        }
        free(candidate);
        if (!end) {
            break;
        }
        path = end + 1;
    }
    return NULL;
}
static const char *run_step(void *service, const pin_value *arguments, pin_value *results)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    const char *failure = host_ready(host);
    if (failure) {
        return failure;
    }
    if (host->result->steps >= BUILD_MAX_STEPS) {
        return "build step limit exceeded";
    }
    const pin_array *array = arguments[0].ptr;
    if (arguments[0].type != PIN_STR || arguments[0].container != PIN_CONTAINER_ARRAY ||
        !array || array->element_type != PIN_STR || array->count < 1 ||
        array->count > BUILD_MAX_ARGUMENTS) {
        return "invalid build process arguments";
    }
    size_t bytes = 0;
    for (int i = 0; i < array->count; i++) {
        const pin_value *value = &array->items[i];
        if (value->type != PIN_STR ||
            value->container != PIN_CONTAINER_SINGLE || !value->str) {
            return "invalid build process arguments";
        }
        size_t length = strlen(value->str) + 1;
        if (length > BUILD_MAX_ARGUMENT_BYTES - bytes) {
            return "invalid build process arguments";
        }
        bytes += length;
    }
    if (!array->items[0].str[0]) {
        return "invalid build process arguments";
    }
    char *program = executable(host, array->items[0].str);
    if (!program) {
        return "build process spawn failed";
    }
    char **argv = xmalloc((array->count + 1) * sizeof(*argv));
    for (int i = 0; i < array->count; i++) {
        argv[i] = xstrdup(array->items[i].str);
    }
    argv[array->count] = NULL;
    chibi_build_result *result = host->result;
    chibi_process_output out = {
        .data = result->stdout_text + result->stdout_size,
        .capacity = sizeof(result->stdout_text) - result->stdout_size,
    };
    chibi_process_output err = {
        .data = result->stderr_text + result->stderr_size,
        .capacity = sizeof(result->stderr_text) - result->stderr_size,
    };
    result->steps++;
    chibi_process_capture_result child = chibi_process_capture(
        program,
        (const char *const *)argv,
        host->root,
        (const char *const *)host->environment,
        host->deadline,
        host->options->cancelled,
        &out,
        &err
    );
    for (int i = 0; i < array->count; i++) {
        free(argv[i]);
    }
    free(argv);
    free(program);
    result->stdout_size += out.size;
    result->stderr_size += err.size;
    result->stdout_truncated |= out.truncated;
    result->stderr_truncated |= err.truncated;
    switch (child.error) {
        case CHIBI_PROCESS_OK: results[0].i = child.status; return NULL;
        case CHIBI_PROCESS_INVALID_INPUT:
            return "invalid build process arguments";
        case CHIBI_PROCESS_SPAWN_FAILED: return "build process spawn failed";
        case CHIBI_PROCESS_TIMEOUT: return "build process timeout";
        case CHIBI_PROCESS_CANCELLED: return "build process cancelled";
        case CHIBI_PROCESS_HOST_FAILED: return "build process host failure";
    }
    return "build process host failure";
}
static const char *read_environment(void *service, const pin_value *arguments, pin_value *results)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    if (arguments[0].type != PIN_STR ||
        arguments[0].container != PIN_CONTAINER_SINGLE || !arguments[0].str) {
        return "invalid build environment name";
    }
    for (size_t i = 0; i < sizeof(environment_names) / sizeof(*environment_names); i++) {
        if (!strcmp(arguments[0].str, environment_names[i])) {
            results[0].str = environment_value(host, arguments[0].str);
            return NULL;
        }
    }
    return "build environment name is not allowed";
}
static const script_catalog_parameter step_argument = {
    .name = "arguments",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY },
};
static const script_catalog_parameter status_result = { .name = "status",
                                                        .type = { .storage = PIN_I32 } };
static const script_catalog_parameter environment_name = { .name = "name",
                                                           .type = { .storage = PIN_STR } };
static const script_catalog_parameter environment_result = { .name = "value",
                                                             .type = { .storage = PIN_STR } };
static const script_catalog_capability capabilities[] = {
    { .id = 901,
      .name = "RunBuildStep",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &step_argument,
      .parameter_count = 1,
      .results = &status_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = 902,
      .name = "BuildEnvironment",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &environment_name,
      .parameter_count = 1,
      .results = &environment_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = 2 };
static const node_host_call calls[] = {
    { .service = "tool.build",
      .invoke = run_step,
      .missing_service = "build host service is unavailable" },
    { .service = "tool.build",
      .invoke = read_environment,
      .missing_service = "build host service is unavailable" },
};
static bool resolve_host(void *, uint64_t id, script_lower_host *out)
{
    if (id != 901 && id != 902) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &calls[id - 901], .access = &access };
    return true;
}
static void diagnose(chibi_build_result *result, const char *message)
{
    snprintf(result->diagnostic, sizeof(result->diagnostic), "%s", message);
}
bool chibi_build_invoke(const chibi_build_options *options, chibi_build_result *result)
{
    *result = (chibi_build_result){ 0 };
    if (!options || !options->root || !options->source_path ||
        !options->source_text || !options->entry || !options->entry[0] ||
        !isfinite(options->deadline) || options->deadline <= 0 ||
        (options->backend != BEHAVIOR_BYTECODE && options->backend != BEHAVIOR_SLJIT) ||
        !behavior_backend_available(options->backend)) {
        diagnose(result, "invalid build host options");
        return false;
    }
    build_host host = { .options = options,
                        .result = result,
                        .deadline = options->deadline };
    host.root = realpath(options->root, NULL);
    if (!host.root) {
        diagnose(result, "invalid build root");
        return false;
    }
    int count = 0;
    for (size_t i = 0; i < sizeof(environment_names) / sizeof(*environment_names); i++) {
        const char *value = getenv(environment_names[i]);
        if (!value) {
            continue;
        }
        size_t size = strlen(environment_names[i]) + strlen(value) + 2;
        host.environment[count] = xmalloc(size);
        snprintf(host.environment[count++], size, "%s=%s", environment_names[i], value);
    }
    const char *inherited = NULL;
    for (int i = 0; host.environment[i]; i++) {
        if (!strncmp(host.environment[i], "CHIBI_TEST_DEADLINE=", 20)) {
            inherited = host.environment[i] + 20;
            break;
        }
    }
    const char *failure = NULL;
    if (inherited) {
        char *end;
        double deadline = strtod(inherited, &end);
        if (end == inherited || *end || !isfinite(deadline) || deadline <= 0) {
            failure = "invalid inherited build deadline";
        } else if (deadline < host.deadline) {
            host.deadline = deadline;
        }
    }
    authoring_source *source = NULL;
    behavior_function_context context = { 0 };
    behavior_function_activation activation = { 0 };
    arena storage = { 0 };
    service_binding *services = NULL;
    bool success = false;
    if (!failure) {
        failure = host_ready(&host);
    }
    if (failure) {
        goto done;
    }
    const script_lower_bindings bindings = { .host = resolve_host };
    source = authoring_source_prepare(
        options->source_path,
        options->source_text,
        &catalog,
        &bindings,
        options->backend
    );
    if (!source || !source->valid) {
        failure = source && source->diagnostic_count
                    ? source->diagnostics[0].message
                    : "build source preparation failed";
        goto done;
    }
    uint64_t entry_id = 0;
    for (int i = 0; i < source->analysis->function_count; i++) {
        const script_function *function = &source->analysis->functions[i];
        if (function->kind != SCRIPT_FUNCTION_AUTHORED || function->symbol < 0 ||
            strcmp(source->analysis->symbols[function->symbol].name, options->entry)) {
            continue;
        }
        if (entry_id || function->parameter_count ||
            function->type_parameter_count || function->result_count != 1 ||
            source->analysis->symbols[function->results[0]].type.storage != PIN_I32 ||
            source->analysis->symbols[function->results[0]].type.container !=
                PIN_CONTAINER_SINGLE) {
            failure = "build entry must be one zero-argument int function";
            goto done;
        }
        entry_id = source->analysis->symbols[function->symbol].id;
    }
    const behavior_function *entry = NULL;
    for (int i = 0; i < execution_module_function_count(source->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(source->module, i);
        if (info.kind == EXECUTION_FUNCTION_AUTHORED && info.id == entry_id) {
            entry = info.body;
        }
    }
    if (!entry) {
        failure = "build entry was not found";
        goto done;
    }
    failure = host_ready(&host);
    if (failure) {
        goto done;
    }
    apush(services, ((service_binding){ "tool.build", &host }));
    bool resumable = behavior_function_resumable(entry);
    if (resumable) {
        behavior_function_activation_init(entry, &activation);
    }
    pin_value value = { .type = PIN_I32 };
    context = (behavior_function_context){ .activation = resumable ? &activation : NULL,
                                           .results = &value,
                                           .budget = 100000 };
    behavior_function_bind_storage(&context, &storage);
    if (!behavior_function_bind(entry, &context, services, 0)) {
        failure = "build host binding failed";
        goto done;
    }
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    result->flow_status = behavior_function_run(entry, &context, options->backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    result->counts = scope.counts;
    if (result->flow_status != FLOW_OK) {
        failure = context.error_message ? context.error_message
                                        : "build source execution failed";
        goto done;
    }
    failure = host_ready(&host);
    if (!failure) {
        result->value = value.i;
        success = true;
    }
done:
    if (failure) {
        diagnose(result, failure);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    afree(services);
    authoring_source_free(source);
    for (int i = 0; i < count; i++) {
        free(host.environment[i]);
    }
    free(host.root);
    return success;
}
