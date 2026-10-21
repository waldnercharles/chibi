#include "host/build_host.h"

#include "authoring/native_bindings.h"
#include "authoring/source_package_files.h"
#include "authoring/source_package_prepare.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/build_host_internal.h"
#include "host/build_values.h"
#include "host/process.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    "CC",
    "CXX",
    "CFLAGS",
    "CXXFLAGS",
    "CPPFLAGS",
    "LDFLAGS",
    "MACOSX_DEPLOYMENT_TARGET",
    "CMAKE_GENERATOR",
    "CMAKE_GENERATOR_PLATFORM",
    "CMAKE_GENERATOR_TOOLSET",
    "EMSDK",
    "EMSDK_NODE",
    "EM_CACHE",
    "EMCC_CFLAGS",
    "OBJC",
    "OBJCXX",
    "OBJCFLAGS",
    "OBJCXXFLAGS",
    "CMAKE_PREFIX_PATH",
    "CMAKE_INCLUDE_PATH",
    "CMAKE_LIBRARY_PATH",
    "CMAKE_FRAMEWORK_PATH",
    "CMAKE_APPBUNDLE_PATH",
    "CPATH",
    "C_INCLUDE_PATH",
    "CPLUS_INCLUDE_PATH",
    "OBJC_INCLUDE_PATH",
    "LIBRARY_PATH",
    "PKG_CONFIG_PATH",
    "PKG_CONFIG_LIBDIR",
    "PKG_CONFIG_SYSROOT_DIR",
    "CMAKE_TOOLCHAIN_FILE",
    "EMMAKEN_CMAKE_TOOLCHAIN",
    "CHIBI_FETCHCONTENT_CACHE_DIR",
    "CHIBI_DEPENDENCY_PACKAGE_CACHE_DIR",
    "CHIBI_DEPENDENCY_PACKAGES",
    "XDG_CACHE_HOME",
    "CMAKE_BUILD_PARALLEL_LEVEL",
    "MallocNanoZone",
    "ASAN_OPTIONS",
    "UBSAN_OPTIONS",
    "LSAN_OPTIONS",
    "CHIBI_DEPENDENCY_PACKAGE_KEEP",
    "CHIBI_DEPENDENCY_PACKAGE_JOBS",
    /* The tools tools/toolchain/StageOne.chibi runs. */
    "CHIBI_COMPILE",
    "CHIBI_STAGE_ONE_HOST",
    "EM_CONFIG",
    "CCACHE_DIR",
    "CCACHE_BASEDIR",
    "CCACHE_DISABLE",
    "CCACHE_CONFIGPATH",
    "CCACHE_TEMPDIR",
    "LANG",
    "LC_ALL",
    "ZERO_AR_DATE",
};
_Static_assert(
    sizeof(environment_names) / sizeof(*environment_names) < BUILD_ENVIRONMENT_CAPACITY,
    "build environment capacity"
);
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
const char *build_host_ready(build_host *host)
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
char *build_host_executable(build_host *host, const char *name)
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
        struct stat info;
        if (!stat(candidate, &info) && S_ISREG(info.st_mode) &&
            !access(candidate, X_OK)) {
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
/* A reported step adds its output to the invocation's result; a probe's
 * output is only data for build.last_stdout and build.last_stderr. */
static const char *run_step(void *service, const pin_value *arguments, pin_value *results, bool reported)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    const char *failure = build_host_ready(host);
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
    char *program = build_host_executable(host, array->items[0].str);
    if (!program) {
        return "build process spawn failed";
    }
    char **argv = xmalloc((array->count + 1) * sizeof(*argv));
    for (int i = 0; i < array->count; i++) {
        argv[i] = xstrdup(array->items[i].str);
    }
    argv[array->count] = NULL;
    chibi_build_result *result = host->result;
    chibi_process_output out = { .data = host->last_stdout,
                                 .capacity = BUILD_STEP_OUTPUT_CAPACITY };
    chibi_process_output err = { .data = host->last_stderr,
                                 .capacity = BUILD_STEP_OUTPUT_CAPACITY };
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
    if (reported) {
        size_t out_remaining = sizeof(result->stdout_text) - result->stdout_size - 1;
        size_t err_remaining = sizeof(result->stderr_text) - result->stderr_size - 1;
        size_t out_copy = out.size < out_remaining ? out.size : out_remaining;
        size_t err_copy = err.size < err_remaining ? err.size : err_remaining;
        memcpy(result->stdout_text + result->stdout_size, out.data, out_copy);
        memcpy(result->stderr_text + result->stderr_size, err.data, err_copy);
        result->stdout_size += out_copy;
        result->stderr_size += err_copy;
        result->stdout_text[result->stdout_size] = 0;
        result->stderr_text[result->stderr_size] = 0;
        result->stdout_truncated |= out.truncated || out_copy != out.size;
        result->stderr_truncated |= err.truncated || err_copy != err.size;
    }
    host->last_stdout_size = out.size;
    host->last_stderr_size = err.size;
    host->last_stdout_truncated = out.truncated;
    host->last_stderr_truncated = err.truncated;
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
const char *build_host_run_step(void *service, const pin_value *arguments, pin_value *results)
{
    return run_step(service, arguments, results, true);
}
const char *build_host_run_probe(void *service, const pin_value *arguments, pin_value *results)
{
    return run_step(service, arguments, results, false);
}
const char *build_host_environment(void *service, const pin_value *arguments, pin_value *results)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    const char *failure = build_host_ready(host);
    if (failure) {
        return failure;
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
const char *build_host_environment_names(void *service, const pin_value *, pin_value *results)
{
    build_host *host = service;
    if (!host) {
        return "build host service is unavailable";
    }
    const char *failure = build_host_ready(host);
    if (failure) {
        return failure;
    }
    int count = 0;
    while (host->environment[count]) {
        count++;
    }
    pin_array *names = build_values_array(&host->values, count);
    if (!names) {
        return host->values.error;
    }
    for (int i = 0; i < count; i++) {
        const char *row = host->environment[i];
        char *name = build_values_text(&host->values, row, strcspn(row, "="));
        if (!name) {
            return host->values.error;
        }
        names->items[i] = (pin_value){ .type = PIN_STR, .str = name };
    }
    results[0].ptr = names;
    return NULL;
}
static void diagnose(chibi_build_result *result, const char *message)
{
    snprintf(result->diagnostic, sizeof(result->diagnostic), "%s", message);
}
/* The program is the one file of the package at its directory; with a core
 * collection, it may import core: packages. */
static authoring_packages *prepare(
    const build_host *host,
    const native_capabilities *native,
    const script_lower_bindings *bindings
)
{
    const chibi_build_options *options = host->options;
    source_package_input *files = NULL;
    source_package_load_error load = { 0 };
    if (options->core && !source_package_files_read(options->core, &files, &load)) {
        diagnose(host->result, load.message);
        return NULL;
    }
    const char *slash = strrchr(options->source_path, '/');
    char *path = join(host->program_directory, slash ? slash + 1 : options->source_path);
    apush(
        files,
        ((source_package_input){ path, options->source_text, strlen(options->source_text) })
    );
    const source_package_collection core = { "core", options->core };
    const source_package_snapshot_options snapshot = {
        .project_root = host->program_directory,
        .files = files,
        .file_count = acount(files),
        .collections = &core,
        .collection_count = options->core ? 1 : 0
    };
    authoring_packages *packages = authoring_packages_prepare(
        source_package_snapshot_new(&snapshot),
        native_capabilities_catalog(native),
        bindings,
        options->backend,
        NULL
    );
    free(path);
    asetlen(files, acount(files) - 1);
    source_package_files_free(files);
    return packages;
}
/* The first diagnostic of invalid packages. */
static const char *invalid(const authoring_packages *packages)
{
    const source_package_snapshot_error *error = source_package_snapshot_error_at(
        authoring_packages_snapshot(packages)
    );
    if (error) {
        return error->diagnostic.message;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (package->diagnostic_count) {
            return package->diagnostics[0].message;
        }
    }
    const char *message = authoring_packages_error(packages);
    return message ? message : "build source preparation failed";
}
/* A build program runs alone: nothing resumes a wait or runs a task. */
static int refuse_wait(behavior_function_context *context, int)
{
    context->error_message = "a build program cannot wait";
    return FLOW_ERR_INVALID_GRAPH;
}
static int refuse_task(behavior_function_context *context, int)
{
    context->error_message = "a build program cannot start a task";
    return FLOW_ERR_INVALID_GRAPH;
}
bool chibi_build_invoke(const chibi_build_options *options, chibi_build_result *result)
{
    *result = (chibi_build_result){ 0 };
    if (!options || !options->root || !options->source_path ||
        !options->source_text || !options->entry || !options->entry[0] ||
        options->argument_count < 0 || options->argument_count > BUILD_MAX_ARGUMENTS ||
        (options->argument_count && !options->arguments) ||
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
    host.last_stdout = xmalloc(BUILD_STEP_OUTPUT_CAPACITY);
    host.last_stderr = xmalloc(BUILD_STEP_OUTPUT_CAPACITY);
    host.last_stdout[0] = host.last_stderr[0] = 0;
    char *program_path = options->source_path[0] == '/'
                           ? xstrdup(options->source_path)
                           : join(host.root, options->source_path);
    char *slash = strrchr(program_path, '/');
    if (slash) {
        slash[slash == program_path ? 1 : 0] = 0;
    }
    host.program_directory = realpath(program_path, NULL);
    free(program_path);
    if (!host.program_directory) {
        free(host.root);
        free(host.last_stdout);
        free(host.last_stderr);
        diagnose(result, "invalid build program directory");
        return false;
    }
    int count = 0;
    size_t environment_bytes = 0;
    bool environment_valid = true;
    for (size_t i = 0; i < sizeof(environment_names) / sizeof(*environment_names); i++) {
        const char *value = getenv(environment_names[i]);
        if (!value) {
            continue;
        }
        size_t value_size = strnlen(value, BUILD_TEXT_LIMIT + 1);
        size_t size = strlen(environment_names[i]) + value_size + 2;
        if (size > BUILD_TEXT_LIMIT - environment_bytes) {
            environment_valid = false;
            break;
        }
        environment_bytes += size;
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
    const char *failure = environment_valid ? NULL : "build environment limit exceeded";
    if (inherited && !failure) {
        char *end;
        double deadline = strtod(inherited, &end);
        if (end == inherited || *end || !isfinite(deadline) || deadline <= 0) {
            failure = "invalid inherited build deadline";
        } else if (deadline < host.deadline) {
            host.deadline = deadline;
        }
    }
    native_capabilities *native = NULL;
    authoring_packages *packages = NULL;
    char error[256];
    behavior_function_context context = { 0 };
    behavior_function_activation activation = { 0 };
    arena storage = { 0 };
    service_binding *services = NULL;
    bool success = false;
    size_t argument_bytes = 0;
    if (!failure) {
        host.arguments = build_values_array(&host.values, options->argument_count);
        failure = host.arguments ? NULL : host.values.error;
    }
    for (int i = 0; !failure && i < options->argument_count; i++) {
        const char *text = options->arguments[i];
        size_t bytes = text ? strnlen(text, BUILD_MAX_ARGUMENT_BYTES) + 1 : 0;
        if (!text || bytes > BUILD_MAX_ARGUMENT_BYTES - argument_bytes) {
            failure = "invalid build host arguments";
            break;
        }
        argument_bytes += bytes;
        char *copy = build_values_text(&host.values, text, bytes - 1);
        if (!copy) {
            failure = host.values.error;
            break;
        }
        host.arguments->items[i] = (pin_value){ .type = PIN_STR, .str = copy };
    }
    if (!failure) {
        failure = build_host_ready(&host);
    }
    if (failure) {
        goto done;
    }
    /* Text and arrays are the natives core: packages use, as in the compiler host. */
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK };
    native = native_capabilities_from_definition_packs(build_host_natives(), 1, packs, 2, error, sizeof error);
    if (!native) {
        failure = error;
        goto done;
    }
    const script_lower_bindings bindings = authoring_native_bindings(native);
    packages = prepare(&host, native, &bindings);
    if (!packages) {
        goto done;
    }
    if (!authoring_packages_valid(packages)) {
        failure = invalid(packages);
        goto done;
    }
    const authoring_package *program = NULL;
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        if (!strcmp(authoring_packages_at(packages, i)->identity, "project:")) {
            program = authoring_packages_at(packages, i);
        }
    }
    assert(program);
    uint64_t entry_id = 0;
    const script_analysis *analysis = program->analysis;
    for (int i = 0; i < analysis->function_count; i++) {
        const script_function *function = &analysis->functions[i];
        if (function->kind != SCRIPT_FUNCTION_AUTHORED || function->symbol < 0 ||
            strcmp(analysis->symbols[function->symbol].name, options->entry)) {
            continue;
        }
        if (entry_id || function->parameter_count || function->result_count != 1 ||
            analysis->symbols[function->results[0]].type.storage != PIN_I32 ||
            analysis->symbols[function->results[0]].type.container !=
                PIN_CONTAINER_SINGLE) {
            failure = "build entry must be one zero-argument int function";
            goto done;
        }
        entry_id = analysis->symbols[function->symbol].id;
    }
    const behavior_function *entry = NULL;
    for (int i = 0; i < execution_module_function_count(program->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(program->module, i);
        if (info.kind == EXECUTION_FUNCTION_AUTHORED && info.id == entry_id) {
            entry = info.body;
        }
    }
    if (!entry) {
        failure = "build entry was not found";
        goto done;
    }
    failure = build_host_ready(&host);
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
                                           .budget = 100000,
                                           .begin_command = refuse_wait,
                                           .start_task = refuse_task };
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
        failure = context.error_message
                    ? context.error_message
                    : behavior_function_failure_text(context.failure);
        goto done;
    }
    failure = build_host_ready(&host);
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
    authoring_packages_free(packages);
    native_capabilities_free(native);
    for (int i = 0; i < count; i++) {
        free(host.environment[i]);
    }
    free(host.root);
    free(host.program_directory);
    free(host.last_stdout);
    free(host.last_stderr);
    arena_free(&host.values.memory);
    return success;
}
