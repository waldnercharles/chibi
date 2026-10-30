#include "authoring/program_service.h"

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

struct program_service
{
    char *core;
    authoring_program_options options;
    service_binding *services;
    program_entry **entries;
    /* Host results are copied by execution/host.c before returning to authored
     * code. This scratch ends on the next callback; it never grows per poll. */
    arena scratch;
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
    if (!authoring_program_compile(entry->program, &entry->observed) ||
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

int program_service_call_entry(
    program_service *service,
    uint64_t handle,
    const char *name,
    const authoring_program_call_options *options,
    int *result
)
{
    assert(name && result);
    *result = 0;
    program_entry *entry = find(service, handle);
    if (!entry) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = authoring_program_call_entry(entry->program, name, options, result);
    if (status != FLOW_OK) {
        session_error(entry);
    } else {
        free(entry->message);
        entry->message = NULL;
    }
    return status;
}

bool program_service_has(program_service *service, uint64_t handle, const char *name)
{
    program_entry *entry = find(service, handle);
    return entry && authoring_program_has(entry->program, name);
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

static const char *native_call_entry(
    const behavior_function_context *context,
    void *user,
    const pin_value *arguments,
    pin_value *results
)
{
    program_service *service = user;
    arena_reset(&service->scratch);
    const char *name = NULL;
    const char *error = pin_text_cstring(&service->scratch, arguments[1], &name, NULL);
    if (error) {
        return error;
    }
    int value = 0;
    const authoring_program_call_options options = { .caller = context->program };
    results[0].i = program_service_call_entry(service, arguments[0].u64, name, &options, &value);
    results[1].i = value;
    return NULL;
}

static const char *native_has(void *user, const pin_value *arguments, pin_value *results)
{
    program_service *service = user;
    arena_reset(&service->scratch);
    const char *name = NULL;
    const char *error = pin_text_cstring(&service->scratch, arguments[1], &name, NULL);
    if (!error) {
        results[0].b = program_service_has(service, arguments[0].u64, name);
    }
    return error;
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
static const script_catalog_parameter entry_parameters[] = {
    { .name = "program", .type = { .storage = PIN_HANDLE, .nominal = PROGRAM_TYPE } },
    { .name = "name", .type = { .storage = PIN_STR } }
};
static const script_catalog_parameter bool_result = { .name = "value",
                                                      .type = { .storage = PIN_BOOL } };
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
    X(7, "program.call_entry", entry_parameters, 2, call_results, 2, context, native_call_entry) \
    X(8, "program.has", entry_parameters, 2, &bool_result, 1, invoke, native_has)

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
