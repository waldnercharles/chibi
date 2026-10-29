#include "authoring/program_service.h"

#include "authoring/package_tests.h"
#include "authoring/source_package_io.h"
#include "execution/status.h"
#include "runtime/tasks.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROGRAM_TYPE UINT64_C(0x70726f6772616d01)
#define PROGRAM_CAPABILITY(N) (UINT64_C(0x70726f6772617000) + (N))

typedef struct
{
    uint64_t handle;
    authoring_program *program;
    char *path, *entry, *message;
    source_package_snapshot_options observed;
    arena arguments_storage;
    pin_array arguments;
} program_entry;

typedef struct test_context
{
    const execution_program *runtime;
    const pin_array *arguments;
    struct test_context *previous;
} test_context;

struct program_service
{
    char *core;
    authoring_program_options options;
    service_binding *services;
    program_entry **entries;
    /* Host results are copied by execution/host.c before returning to authored
     * code. This scratch ends on the next callback; it never grows per poll. */
    arena scratch;
    test_context *test;
    bool configured;
};

/* Globally unique generations prevent stale or cross-service aliases. Slots
 * may be reused, but a closed handle never names a later session. */
static _Atomic uint64_t next_handle = 1;
static char *absolute(const char *path);

static program_entry *find(const program_service *service, uint64_t handle)
{
    for (int i = 0; i < acount(service->entries); i++) {
        if (service->entries[i]->handle == handle) {
            return service->entries[i];
        }
    }
    return NULL;
}

static bool message(program_entry *entry, const char *text)
{
    if (entry->message && !strcmp(entry->message, text)) {
        return false;
    }
    free(entry->message);
    entry->message = xstrdup(text);
    return true;
}

static void session_error(program_entry *entry)
{
    const authoring_program_diagnostic *error = authoring_program_error(entry->program);
    const char *text = error->message ? error->message : "Program operation failed";
    if (!error->path) {
        message(entry, text);
        return;
    }
    size_t size = strlen(error->path) + strlen(text) + 64;
    char *formatted = xmalloc(size);
    snprintf(formatted, size, "%s:%d:%d: %s", error->path, error->line, error->column, text);
    message(entry, formatted);
    free(formatted);
}

static bool load_sources(
    const program_service *service,
    const program_entry *entry,
    source_package_snapshot_options *inputs,
    source_package_load_error *error
)
{
    struct stat info;
    source_package_collection collection = { "core", service->core };
    if (!stat(entry->path, &info) && S_ISDIR(info.st_mode)) {
        source_package_disk_options options = { entry->path, &collection, 1 };
        return source_package_files_load(&options, inputs, error);
    }
    source_package_input *files = NULL, *library = NULL;
    if (!source_package_file_read(entry->path, &files, error)) {
        return false;
    }
    if (!source_package_files_read(service->core, &library, error)) {
        source_package_files_free(files);
        return false;
    }
    for (int i = 0; i < acount(library); i++) {
        apush(files, library[i]);
    }
    afree(library);
    char *root = xstrdup(files[0].path);
    char *slash = strrchr(root, '/');
    assert(slash);
    bool keep_slash = slash == root || (slash == root + 2 && root[1] == ':');
    slash[keep_slash ? 1 : 0] = 0;
    source_package_collection *collections = xmalloc(sizeof *collections);
    *collections = (source_package_collection){ xstrdup("core"),
                                                xstrdup(service->core) };
    *inputs = (source_package_snapshot_options){ root, files, acount(files), collections, 1 };
    return true;
}

static bool unchanged(const source_package_snapshot_options *before, const source_package_snapshot_options *after)
{
    if (!before->project_root || before->file_count != after->file_count) {
        return false;
    }
    for (int i = 0; i < before->file_count; i++) {
        const source_package_input *a = &before->files[i], *b = &after->files[i];
        if (strcmp(a->path, b->path) || a->size != b->size ||
            memcmp(a->text, b->text, a->size)) {
            return false;
        }
    }
    return true;
}

static void print_invalid(const authoring_packages *packages)
{
    const source_package_snapshot *snapshot = authoring_packages_snapshot(packages);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        int count = 0;
        const script_package_document
            *documents = source_package_snapshot_documents(snapshot, &count);
        int document = error->diagnostic.range.document;
        fprintf(
            stderr,
            "%s:%d:%d: %s\n",
            document >= 0 && document < count ? documents[document].path : "",
            error->diagnostic.range.line,
            error->diagnostic.range.column,
            error->diagnostic.message
        );
        return;
    }
    const char *message = authoring_packages_error(packages);
    if (message) {
        fprintf(stderr, "%s\n", message);
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; j < package->diagnostic_count; j++) {
            const script_diagnostic *diagnostic = &package->diagnostics[j];
            int document = diagnostic->range.document;
            const script_analysis *analysis = package->analysis;
            const char *path = analysis && document >= 0 && document < analysis->document_count
                                 ? analysis->documents[document].path
                                 : "";
            fprintf(
                stderr,
                "%s:%d:%d: %s\n",
                path,
                diagnostic->range.line,
                diagnostic->range.column,
                diagnostic->message
            );
        }
    }
}

static execution_module *test_module(
    const authoring_packages *packages,
    const package_test *test,
    uint64_t *id
)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        execution_module *module = authoring_packages_at(packages, i)->module;
        for (int j = 0; j < execution_module_function_count(module); j++) {
            execution_module_function_info function = execution_module_function_at(module, j);
            if (function.body == test->body) {
                *id = function.id;
                return module;
            }
        }
    }
    assert(false);
    return NULL;
}

typedef struct
{
    const package_test *tests;
    package_test_result *results;
    const authoring_program_options *options;
    int count, chunks;
} pure_tests;

static void run_pure_tests(int first, int end, uint32_t, void *user)
{
    pure_tests *span = user;
    for (int i = first; i < end; i++) {
        behavior_execution_scope scope = { .remaining = PACKAGE_TEST_BUDGET,
                                           .cancelled = span->options->cancelled,
                                           .user = span->options->cancel_user,
                                           .pure = true };
        behavior_execution_scope_begin(&scope);
        span->results[i].passed = package_test_run(
            &span->tests[i],
            BEHAVIOR_BYTECODE,
            PACKAGE_TEST_BUDGET,
            &span->results[i].failure
        );
        behavior_execution_scope_end(&scope);
    }
}

static void run_pure_chunks(int first, int end, uint32_t, void *user)
{
    pure_tests *span = user;
    for (int chunk = first; chunk < end; chunk++) {
        for (int i = chunk; i < span->count; i += span->chunks) {
            run_pure_tests(i, i + 1, 0, span);
        }
    }
}

static int print_test(const package_test *test, const package_test_result *result)
{
    printf("%s %s.%s\n", result->passed ? "PASS" : "FAIL", test->package, test->name);
    if (result->passed) {
        return 0;
    }
    const package_test_failure *failure = &result->failure;
    printf(
        "  %s:%d:%d: %s\n",
        failure->path ? failure->path : test->path,
        failure->line,
        failure->column,
        failure->message
    );
    return 1;
}

int program_service_test(program_service *service, const char *path)
{
    assert(service && service->configured && path);
    const char *setting = getenv("CHIBI_TEST_WORKERS");
    int workers = 0;
    if (setting) {
        char *end;
        errno = 0;
        long value = strtol(setting, &end, 10);
        if (!*setting || strspn(setting, "0123456789") != strlen(setting) ||
            *end || errno || value < 1 || value > TASK_WORKER_MAX) {
            fprintf(stderr, "CHIBI_TEST_WORKERS must be an integer from 1 to %d\n", TASK_WORKER_MAX);
            return 1;
        }
        workers = (int)value;
    }
    char *selected = absolute(path);
    program_entry entry = { .path = selected };
    source_package_snapshot_options inputs = { 0 };
    source_package_load_error load = { 0 };
    if (!load_sources(service, &entry, &inputs, &load)) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
        free(selected);
        return 1;
    }
    free(selected);
    source_package_snapshot *snapshot = source_package_snapshot_new(&inputs);
    source_package_files_release(&inputs);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(service->options.native),
        &service->options.bindings,
        service->options.backend,
        NULL
    );
    if (!authoring_packages_valid(packages)) {
        print_invalid(packages);
        authoring_packages_free(packages);
        return 1;
    }
    package_test *tests = package_tests_find(packages);
    int failed = 0;
    /* Pure spans borrow immutable functions and own per-invocation heaps; no
     * module references or shared service values are mutated by workers.
     * Impure tests bind explicit services in separate serial runtimes. */
    for (int i = 0; i < acount(tests);) {
        if (tests[i].parallel) {
            int end = i + 1;
            while (end < acount(tests) && tests[end].parallel) {
                end++;
            }
            int count = end - i;
            package_test_result *results = xcalloc((size_t)count, sizeof *results);
            pure_tests span = { .tests = tests + i,
                                .results = results,
                                .options = &service->options,
                                .count = count };
            const task_system *tasks = service->options.test_tasks;
            if (tasks && tasks->workers > 1 && count > 1 && workers != 1) {
                span.chunks = workers && workers < tasks->workers ? workers
                                                                  : tasks->workers;
                if (span.chunks > count) {
                    span.chunks = count;
                }
                void *task = tasks->enqueue(
                    run_pure_chunks,
                    span.chunks,
                    1,
                    &span,
                    tasks->user
                );
                tasks->finish(task, tasks->user);
            } else {
                run_pure_tests(0, count, 0, &span);
            }
            for (int j = i; j < end; j++) {
                failed += print_test(&tests[j], &results[j - i]);
            }
            free(results);
            i = end;
            continue;
        }
        uint64_t id = 0;
        execution_module *module = test_module(packages, &tests[i], &id);
        execution_program *runtime = execution_program_new(module, NULL, service->services);
        execution_instance *instance = execution_program_attach(runtime, 1);
        assert(instance);
        arena argument_storage = { 0 };
        pin_value argument = { 0 };
        const char *argument_error = pin_value_text(
            &argument_storage,
            &argument,
            tests[i].path,
            strlen(tests[i].path)
        );
        assert(!argument_error);
        (void)argument_error;
        pin_array arguments = { .element_type = PIN_STR,
                                .items = &argument,
                                .count = 1,
                                .capacity = 1 };
        test_context context = { runtime, &arguments, service->test };
        service->test = &context;
        behavior_execution_scope scope = { .remaining = PACKAGE_TEST_BUDGET,
                                           .cancelled = service->options.cancelled,
                                           .user = service->options.cancel_user,
                                           .prefer_compiled = true };
        behavior_execution_scope_begin(&scope);
        int result = 0;
        int status = execution_instance_call(instance, id, &result, 0, 0, PACKAGE_TEST_BUDGET);
        behavior_execution_scope_end(&scope);
        const execution_program_failure *error = execution_program_error(runtime);
        package_test_result outcome = {
            .passed = status == FLOW_OK,
            .failure = { .path = error && error->asset_name[0] ? error->asset_name
                                                               : tests[i].path,
                         .line = error ? error->line : tests[i].line,
                         .column = error ? error->column : tests[i].column }
        };
        snprintf(
            outcome.failure.message,
            sizeof outcome.failure.message,
            "%s",
            error && error->message[0] ? error->message : "Test invocation failed"
        );
        failed += print_test(&tests[i], &outcome);
        service->test = context.previous;
        execution_program_free(runtime);
        arena_free(&argument_storage);
        i++;
    }
    printf("%d tests, %d failed\n", acount(tests), failed);
    afree(tests);
    authoring_packages_free(packages);
    return failed ? 1 : 0;
}

program_service *program_service_new(const char *core)
{
    assert(core);
    program_service *service = xcalloc(1, sizeof *service);
    service->core = absolute(core);
    return service;
}

void program_service_configure(program_service *service, const authoring_program_options *options)
{
    assert(service && options && options->native && !service->configured);
    service->options = *options;
    service->options.native = native_capabilities_retain(options->native);
    for (int i = 0; i < acount(options->services); i++) {
        service_binding binding = options->services[i];
        binding.type_name = xstrdup(binding.type_name);
        apush(service->services, binding);
    }
    service->options.services = service->services;
    service->configured = true;
}

static char *absolute(const char *path)
{
    char *canonical = realpath(path, NULL);
    if (canonical) {
        return canonical;
    }
#ifdef _WIN32
    char *full = _fullpath(NULL, path, 0);
    if (full) {
        for (char *part = full; *part; part++) {
            if (*part == '\\') {
                *part = '/';
            }
        }
        return full;
    }
#endif
    if (path[0] == '/' || (path[0] && path[1] == ':')) {
        return xstrdup(path);
    }
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        return xstrdup(path);
    }
    size_t size = strlen(cwd) + strlen(path) + 2;
    char *out = xmalloc(size);
    snprintf(out, size, "%s/%s", cwd, path);
    free(cwd);
    return out;
}

uint64_t program_service_open(
    program_service *service,
    const char *path,
    const char *name,
    const char *const *arguments,
    int count
)
{
    assert(service && service->configured && path && name && count >= 0 && (!count || arguments));
    program_entry *entry = xcalloc(1, sizeof *entry);
    entry->handle = atomic_fetch_add_explicit(&next_handle, 1, memory_order_relaxed);
    assert(entry->handle);
    entry->program = authoring_program_new(&service->options);
    entry->path = absolute(path);
    entry->entry = xstrdup(name);
    entry->arguments = (pin_array){ .element_type = PIN_STR,
                                    .count = count,
                                    .capacity = count };
    entry->arguments.items = arena_alloc(
        &entry->arguments_storage,
        (size_t)count * sizeof *entry->arguments.items
    );
    for (int i = 0; i < count; i++) {
        const char *error = pin_value_text(
            &entry->arguments_storage,
            &entry->arguments.items[i],
            arguments[i],
            strlen(arguments[i])
        );
        assert(!error);
        (void)error;
    }
    apush(service->entries, entry);
    return entry->handle;
}

int program_service_update(program_service *service, uint64_t handle)
{
    program_entry *entry = find(service, handle);
    if (!entry) {
        return -1;
    }
    if (authoring_program_busy(entry->program)) {
        return message(entry, "Program is executing") ? -1 : 0;
    }
    source_package_snapshot_options inputs = { 0 };
    source_package_load_error error = { 0 };
    if (!load_sources(service, entry, &inputs, &error)) {
        size_t size = strlen(error.path) + strlen(error.message) + 3;
        char *text = xmalloc(size);
        snprintf(text, size, "%s: %s", error.path, error.message);
        bool changed = message(entry, text);
        free(text);
        return changed ? -1 : 0;
    }
    if (unchanged(&entry->observed, &inputs)) {
        source_package_files_release(&inputs);
        return 0;
    }
    source_package_files_release(&entry->observed);
    entry->observed = inputs;
    source_package_snapshot *snapshot = source_package_snapshot_new(&entry->observed);
    if (!authoring_program_compile(entry->program, snapshot) ||
        !authoring_program_prepare(entry->program, entry->entry, false) ||
        !authoring_program_commit(entry->program)) {
        session_error(entry);
        authoring_program_discard(entry->program);
        return -1;
    }
    free(entry->message);
    entry->message = NULL;
    return 1;
}

int program_service_call(
    program_service *service,
    uint64_t handle,
    const authoring_program_call_options *options,
    int *result
)
{
    assert(result);
    *result = 0;
    program_entry *entry = find(service, handle);
    if (!entry) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = authoring_program_call(entry->program, options, result);
    if (status != FLOW_OK) {
        session_error(entry);
    } else {
        free(entry->message);
        entry->message = NULL;
    }
    return status;
}

const char *program_service_diagnostic(const program_service *service, uint64_t handle)
{
    program_entry *entry = find(service, handle);
    return entry ? entry->message ? entry->message : ""
                 : "Program handle is closed or belongs to another service";
}

bool program_service_close(program_service *service, uint64_t handle)
{
    for (int i = 0; i < acount(service->entries); i++) {
        program_entry *entry = service->entries[i];
        if (entry->handle != handle) {
            continue;
        }
        if (authoring_program_busy(entry->program)) {
            message(entry, "Program is executing");
            return false;
        }
        authoring_program_free(entry->program);
        source_package_files_release(&entry->observed);
        arena_free(&entry->arguments_storage);
        free(entry->path);
        free(entry->entry);
        free(entry->message);
        free(entry);
        adel(service->entries, i);
        return true;
    }
    return false;
}

const pin_array *program_service_arguments(const program_service *service, const execution_program *caller)
{
    if (!caller) {
        return NULL;
    }
    behavior_managed_heap *heap = execution_program_managed_heap((execution_program *)caller);
    for (int i = 0; i < acount(service->entries); i++) {
        program_entry *entry = service->entries[i];
        const execution_program *runtime = authoring_program_runtime(entry->program);
        if (runtime &&
            execution_program_managed_heap((execution_program *)runtime) == heap) {
            return &entry->arguments;
        }
    }
    for (const test_context *context = service->test; context;
         context = context->previous) {
        if (execution_program_managed_heap((execution_program *)context->runtime) == heap) {
            return context->arguments;
        }
    }
    return NULL;
}

void program_service_free(program_service *service)
{
    if (!service) {
        return;
    }
    while (acount(service->entries)) {
        bool closed = program_service_close(
            service,
            service->entries[acount(service->entries) - 1]->handle
        );
        assert(closed);
        (void)closed;
    }
    afree(service->entries);
    if (service->configured) {
        native_capabilities_free(service->options.native);
    }
    for (int i = 0; i < acount(service->services); i++) {
        free((char *)service->services[i].type_name);
    }
    afree(service->services);
    arena_free(&service->scratch);
    free(service->core);
    free(service);
}

static const char *native_open(void *user, const pin_value *arguments, pin_value *results)
{
    program_service *service = user;
    arena_reset(&service->scratch);
    const char *path = NULL, *entry = NULL;
    const char *error = pin_text_cstring(&service->scratch, arguments[0], &path, NULL);
    if (!error) {
        error = pin_text_cstring(&service->scratch, arguments[1], &entry, NULL);
    }
    const pin_array *array = arguments[2].ptr;
    int count = array ? array->count : 0;
    const char **argv = count
                          ? arena_alloc(&service->scratch, (size_t)count * sizeof *argv)
                          : NULL;
    for (int i = 0; !error && i < count; i++) {
        error = pin_text_cstring(&service->scratch, pin_array_get(array, i), &argv[i], NULL);
    }
    if (!error) {
        results[0].u64 = program_service_open(service, path, entry, argv, count);
    }
    return error;
}

static const char *native_update(void *user, const pin_value *arguments, pin_value *results)
{
    results[0].i = program_service_update(user, arguments[0].u64);
    return NULL;
}

static const char *native_call(
    const behavior_function_context *context,
    void *user,
    const pin_value *arguments,
    pin_value *results
)
{
    int value = 0;
    const authoring_program_call_options options = { .caller = context->program };
    results[0].i = program_service_call(user, arguments[0].u64, &options, &value);
    results[1].i = value;
    return NULL;
}

static const char *native_diagnostic(void *user, const pin_value *arguments, pin_value *results)
{
    program_service *service = user;
    arena_reset(&service->scratch);
    const char *text = program_service_diagnostic(service, arguments[0].u64);
    return pin_value_text(&service->scratch, &results[0], text, strlen(text));
}

static const char *native_close(void *user, const pin_value *arguments, pin_value *)
{
    return program_service_close(user, arguments[0].u64)
             ? NULL
             : "Program is executing, closed, or belongs to another service";
}

static const char *native_test(
    const behavior_function_context *,
    void *user,
    const pin_value *arguments,
    pin_value *results
)
{
    program_service *service = user;
    if (service->test) {
        /* A nested run would get a fresh runtime and instruction budget. */
        results[0].i = 1;
        fprintf(stderr, "program.test cannot run inside a test\n");
        return NULL;
    }
    arena_reset(&service->scratch);
    const char *path = NULL;
    const char *error = pin_text_cstring(&service->scratch, arguments[0], &path, NULL);
    if (!error) {
        results[0].i = program_service_test(service, path);
    }
    return error;
}

static const script_catalog_type program_type = { .id = PROGRAM_TYPE,
                                                  .name = "Program",
                                                  .source_name = "Program",
                                                  .type = { .storage = PIN_HANDLE,
                                                            .nominal = PROGRAM_TYPE },
                                                  .size = sizeof(uint64_t) };
static const script_catalog_parameter open_parameters[] = {
    { .name = "path", .type = { .storage = PIN_STR } },
    { .name = "entry", .type = { .storage = PIN_STR } },
    { .name = "arguments", .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } }
};
static const script_catalog_parameter handle_parameter = {
    .name = "program",
    .type = { .storage = PIN_HANDLE, .nominal = PROGRAM_TYPE }
};
static const script_catalog_parameter int_result = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
static const script_catalog_parameter text_result = { .name = "value",
                                                      .type = { .storage = PIN_STR } };
static const script_catalog_parameter call_results[] = {
    { .name = "status", .type = { .storage = PIN_I32 } },
    { .name = "result", .type = { .storage = PIN_I32 } }
};
#define PROGRAM_NATIVES(X)                                                                       \
    X(1, "program.open", open_parameters, 3, &handle_parameter, 1, invoke, native_open)          \
    X(2, "program.update", &handle_parameter, 1, &int_result, 1, invoke, native_update)          \
    X(3, "program.call", &handle_parameter, 1, call_results, 2, context, native_call)            \
    X(4, "program.diagnostic", &handle_parameter, 1, &text_result, 1, invoke, native_diagnostic) \
    X(5, "program.close", &handle_parameter, 1, NULL, 0, invoke, native_close)                   \
    X(6, "program.test", open_parameters, 1, &int_result, 1, context, native_test)

#define CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = PROGRAM_CAPABILITY(ID),                                                    \
      .name = NAME,                                                                    \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                              \
      .parameters = PARAMETERS,                                                        \
      .parameter_count = COUNT,                                                        \
      .results = RESULTS,                                                              \
      .result_count = RESULT_COUNT,                                                    \
      .effects = SCRIPT_EFFECT_HOST },
static const script_catalog_capability capabilities[] = { PROGRAM_NATIVES(CAPABILITY) };
#undef CAPABILITY
static const script_catalog catalog = { .types = &program_type,
                                        .type_count = 1,
                                        .capabilities = capabilities,
                                        .capability_count = sizeof capabilities /
                                                            sizeof *capabilities };
#define CALL(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    static const node_host_call call_##ID = {                                    \
        .service = PROGRAM_SERVICE,                                              \
        .KIND = FUNCTION,                                                        \
        .missing_service = "Program service is unavailable"                      \
    };
PROGRAM_NATIVES(CALL)
#undef CALL
static const node_access host_sink = { .flags = NODE_EFFECT_HOST_SINK };
#define BINDING(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = PROGRAM_CAPABILITY(ID), .call = &call_##ID, .access = &host_sink },
static const native_capability_binding bindings[] = { PROGRAM_NATIVES(BINDING) };
#undef BINDING
#undef PROGRAM_NATIVES

const native_capability_pack *program_service_pack()
{
    static const native_capability_pack pack = { .catalog = &catalog,
                                                 .bindings = bindings,
                                                 .binding_count = sizeof bindings /
                                                                  sizeof *bindings };
    return &pack;
}
