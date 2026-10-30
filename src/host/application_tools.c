#include "host/application_tools.h"

#include "host/application_io.h"
#include "host/process.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>
#ifndef _WIN32
#include <sys/utsname.h>
#endif

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

static int64_t modified(const char *path)
{
    struct stat info;
    if (stat(path, &info)) {
        return -1;
    }
#if defined(_WIN32)
    struct timespec time = { .tv_sec = info.st_mtime };
#elif defined(__APPLE__)
    struct timespec time = info.st_mtimespec;
#else
    struct timespec time = info.st_mtim;
#endif
    return (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

typedef struct visit
{
    dev_t device;
    ino_t inode;
    const struct visit *parent;
} visit;

static bool visiting(const visit *path, const struct stat *info)
{
#ifdef _WIN32
    (void)path;
    (void)info;
#else
    for (; path; path = path->parent) {
        if (path->device == info->st_dev && path->inode == info->st_ino) {
            return true;
        }
    }
#endif
    return false;
}

static bool changed(const char *directory, int64_t since, const visit *parent)
{
    struct stat info;
    if (stat(directory, &info) || modified(directory) > since) {
        return true;
    }
    if (visiting(parent, &info)) {
        return false;
    }
    visit self = { info.st_dev, info.st_ino, parent };
    DIR *dir = opendir(directory);
    if (!dir) {
        return true;
    }
    bool different = false;
    struct dirent *entry;
    while (!different && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") ||
            !strcmp(name, ".chibi") || !strcmp(name, ".git")) {
            continue;
        }
        char *path = join(directory, name);
        struct stat child;
        if (!stat(path, &child) && S_ISDIR(child.st_mode)) {
            different = changed(path, since, &self);
        } else {
            int64_t time = modified(path);
            different = time < 0 || time > since;
        }
        free(path);
    }
    closedir(dir);
    return different;
}

bool application_tools_changed_since(const char *directory, int64_t since)
{
    return changed(directory, since, NULL);
}

static application_tools *begin(void *service)
{
    application_tools *tools = service;
    assert(tools);
    arena_reset(&tools->scratch);
    return tools;
}

static void text(application_tools *tools, pin_value *out, const char *value)
{
    value = value ? value : "";
    const char *error = pin_value_text(&tools->scratch, out, value, strlen(value));
    assert(!error);
    (void)error;
}

static pin_array *array(application_tools *tools, pin_value *out, pin_type type, int count)
{
    pin_array *values = arena_alloc(&tools->scratch, sizeof *values);
    *values = (pin_array){ .element_type = type, .count = count, .capacity = count };
    values->items = arena_alloc(&tools->scratch, (size_t)count * sizeof *values->items);
    *out = (pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = values };
    return values;
}

static void strings(application_tools *tools, pin_value *out, yyjson_val *source)
{
    int count = (int)yyjson_arr_size(source);
    pin_array *values = array(tools, out, PIN_STR, count);
    for (int i = 0; i < count; i++) {
        text(tools, &values->items[i], yyjson_get_str(yyjson_arr_get(source, (size_t)i)));
    }
}

static bool formats_valid(yyjson_val *formats)
{
    if (!yyjson_is_obj(formats) || !yyjson_obj_size(formats)) {
        return false;
    }
    size_t index, count;
    yyjson_val *name, *format;
    yyjson_obj_foreach(formats, index, count, name, format)
    {
        if (!yyjson_is_int(yyjson_obj_get(format, "version"))) {
            return false;
        }
    }
    return true;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(yyjson_get_str(*(yyjson_val *const *)a), yyjson_get_str(*(yyjson_val *const *)b));
}

static const char *sdk_identity(void *service, const pin_value *, pin_value *results)
{
    application_tools *tools = begin(service);
    for (int i = 1; i <= 3; i++) {
        text(tools, &results[i], "");
    }
    results[0].b = false;
    results[4].i = 0;
    array(tools, &results[5], PIN_STR, 0);
    array(tools, &results[6], PIN_I32, 0);
    text(tools, &results[7], "");
    array(tools, &results[8], PIN_STR, 0);
    array(tools, &results[9], PIN_STR, 0);
    results[10].b = false;
    char *path = join(tools->share, "engine.json");
    yyjson_read_err read = { 0 };
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, &read);
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *build = yyjson_obj_get(root, "build_id"),
               *formats = yyjson_obj_get(root, "artifact_formats");
    bool valid = yyjson_is_str(yyjson_obj_get(root, "sdk_version")) &&
                 yyjson_is_int(yyjson_obj_get(root, "project_version")) &&
                 formats_valid(formats) && (!build || yyjson_is_str(build));
    if (!valid) {
        size_t size = strlen(path) + (read.msg ? strlen(read.msg) : 0) + 32;
        char *error = arena_alloc(&tools->scratch, size);
        snprintf(
            error,
            size,
            "Invalid build identity %s%s%s",
            path,
            document ? "" : ": ",
            document   ? ""
            : read.msg ? read.msg
                       : "cannot read identity"
        );
        text(tools, &results[1], error);
        yyjson_doc_free(document);
        free(path);
        return NULL;
    }
    results[0].b = true;
    text(tools, &results[2], yyjson_get_str(yyjson_obj_get(root, "sdk_version")));
    text(tools, &results[3], build ? yyjson_get_str(build) : "unpackaged");
    results[4].i = yyjson_get_int(yyjson_obj_get(root, "project_version"));
    size_t count = yyjson_obj_size(formats), index, total;
    yyjson_val **names = arena_alloc(&tools->scratch, count * sizeof *names),
               *name, *format;
    yyjson_obj_foreach(formats, index, total, name, format)
    {
        names[index] = name;
    }
    qsort(names, count, sizeof *names, by_name);
    pin_array *labels = array(tools, &results[5], PIN_STR, (int)count);
    pin_array *versions = array(tools, &results[6], PIN_I32, (int)count);
    for (size_t i = 0; i < count; i++) {
        const char *label = yyjson_get_str(names[i]);
        text(tools, &labels->items[i], label);
        versions->items[i] = (pin_value){
            .type = PIN_I32,
            .i = yyjson_get_int(yyjson_obj_get(yyjson_obj_get(formats, label), "version"))
        };
    }
    text(tools, &results[7], yyjson_get_str(yyjson_obj_get(root, "system")));
    strings(tools, &results[8], yyjson_obj_get(root, "processors"));
    strings(tools, &results[9], yyjson_obj_get(root, "backends"));
    results[10].b = yyjson_is_arr(yyjson_obj_get(root, "backends"));
    yyjson_doc_free(document);
    free(path);
    return NULL;
}

static const char *sdk_platform(void *service, const pin_value *, pin_value *results)
{
    application_tools *tools = begin(service);
#ifdef _WIN32
    text(tools, &results[0], "Windows");
    text(tools, &results[1], getenv("PROCESSOR_ARCHITECTURE"));
#else
    struct utsname name;
    bool valid = uname(&name) == 0;
    text(tools, &results[0], valid ? name.sysname : "");
    text(tools, &results[1], valid ? name.machine : "");
#endif
    return NULL;
}

static const char *sdk_native_export_supported(void *service, const pin_value *, pin_value *results)
{
    begin(service);
#if defined(__linux__) && defined(__x86_64__)
    results[0].b = true;
#else
    results[0].b = false;
#endif
    return NULL;
}

static const char *sdk_executable_suffix(void *service, const pin_value *, pin_value *results)
{
    application_tools *tools = begin(service);
    text(tools, &results[0], CHIBI_EXECUTABLE_SUFFIX);
    return NULL;
}

#define TEXT(NAME)                                                             \
    { .name = NAME, .type = { .storage = PIN_STR }, .c_string = true }
#define VALUE(NAME, TYPE)                                                      \
    {                                                                          \
        .name = NAME, .type = {.storage = TYPE }                               \
    }
#define ARRAY(NAME, TYPE)                                                      \
    {                                                                          \
        .name = NAME, .type = {                                                \
            .storage = TYPE,                                                   \
            .container = PIN_CONTAINER_ARRAY                                   \
        }                                                                      \
    }
static const script_catalog_parameter identity_results[] = {
    VALUE("valid", PIN_BOOL),
    TEXT("error"),
    TEXT("version"),
    TEXT("build"),
    VALUE("project_version", PIN_I32),
    ARRAY("formats", PIN_STR),
    ARRAY("format_versions", PIN_I32),
    TEXT("system"),
    ARRAY("processors", PIN_STR),
    ARRAY("backends", PIN_STR),
    VALUE("backends_valid", PIN_BOOL)
};
static const script_catalog_parameter platform_results[] = { TEXT("system"),
                                                             TEXT("machine") };
static const script_catalog_parameter bool_result[] = { VALUE("value", PIN_BOOL) };
static const script_catalog_parameter text_result[] = { TEXT("value") };
#undef TEXT
#undef VALUE
#undef ARRAY

#define TOOLS(X)                                                                                  \
    X(20004, "sdk.identity", NULL, 0, identity_results, 11, sdk_identity)                         \
    X(20005, "sdk.platform", NULL, 0, platform_results, 2, sdk_platform)                          \
    X(20006, "sdk.native_export_supported", NULL, 0, bool_result, 1, sdk_native_export_supported) \
    X(20007, "sdk.executable_suffix", NULL, 0, text_result, 1, sdk_executable_suffix)
#define CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, FUNCTION) \
    { .id = ID,                                                                  \
      .name = NAME,                                                              \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                        \
      .parameters = PARAMETERS,                                                  \
      .parameter_count = COUNT,                                                  \
      .results = RESULTS,                                                        \
      .result_count = RESULT_COUNT,                                              \
      .effects = SCRIPT_EFFECT_HOST },
static const script_catalog_capability capabilities[] = { TOOLS(CAPABILITY) };
#undef CAPABILITY
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = sizeof capabilities /
                                                            sizeof *capabilities };
#define CALL(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, FUNCTION)     \
    static const node_host_call call_##ID = {                                  \
        .service = APPLICATION_TOOLS_SERVICE,                                  \
        .invoke = FUNCTION,                                                    \
        .missing_service = "application tools service is unavailable"          \
    };
TOOLS(CALL)
#undef CALL
static const node_access host_sink = { .flags = NODE_EFFECT_HOST_SINK };
#define BINDING(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, FUNCTION)  \
    { .id = ID, .call = &call_##ID, .access = &host_sink },
static const native_capability_binding bindings[] = { TOOLS(BINDING) };
#undef BINDING
#undef TOOLS

const native_capability_pack *application_tools_natives()
{
    static const native_capability_pack pack = { .catalog = &catalog,
                                                 .bindings = bindings,
                                                 .binding_count = sizeof bindings /
                                                                  sizeof *bindings };
    return &pack;
}

void application_tools_free(application_tools *tools)
{
    arena_free(&tools->scratch);
}
