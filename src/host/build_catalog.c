#include "host/build_host_internal.h"
#include "host/build_io.h"
#include "support/array.h"
#include "support/sha256.h"
#include "types/text.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *string_result(build_host *host, pin_value *result, const char *text)
{
    result->text = build_values_text(&host->values, text, strlen(text));
    return result->text ? NULL : host->values.error;
}
#define READY_HOST()                                                           \
    build_host *host = service;                                                \
    if (!host) {                                                               \
        return "build host service is unavailable";                            \
    }                                                                          \
    const char *ready_error = build_host_ready(host);                          \
    if (ready_error) {                                                         \
        return ready_error;                                                    \
    }
#define STRING_ARGUMENTS(N)                                                    \
    const char *strings[N];                                                    \
    for (int argument_index = 0; argument_index < (N); argument_index++) {     \
        const char *string_error = build_values_cstring(                       \
            &host->values,                                                     \
            arguments[argument_index],                                         \
            &strings[argument_index]                                           \
        );                                                                     \
        if (string_error) {                                                    \
            return string_error;                                               \
        }                                                                      \
    }
#define BOOL_STRING_ARGUMENTS(N)                                               \
    const char *strings[N];                                                    \
    for (int argument_index = 0; argument_index < (N); argument_index++) {     \
        const char *string_error = build_values_cstring(                       \
            &host->values,                                                     \
            arguments[argument_index],                                         \
            &strings[argument_index]                                           \
        );                                                                     \
        if (string_error) {                                                    \
            results[0].b = false;                                              \
            return NULL;                                                       \
        }                                                                      \
    }
static const char *build_arguments(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    results[0].ptr = host->arguments;
    return NULL;
}
static const char *build_platform(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
#if defined(__APPLE__)
    return string_result(host, results, "Darwin");
#else
    return string_result(host, results, "Linux");
#endif
}
static const char *build_root(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    return string_result(host, results, host->root);
}
static const char *build_program_directory(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    return string_result(host, results, host->program_directory);
}
static const char *build_find_executable(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char *path = strings[0][0] ? build_host_executable(host, strings[0]) : NULL;
    struct stat info;
    bool exists = path && !stat(path, &info) && S_ISREG(info.st_mode) &&
                  !access(path, X_OK);
    const char *failure = string_result(host, results, exists ? path : "");
    free(path);
    return failure;
}
static const char *build_read_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char *text = NULL;
    size_t length = 0;
    const char *failure = NULL;
    if (!build_io_read(host->root, strings[0], host->deadline, host->options->cancelled, &text, &length, &failure)) {
        return failure;
    }
    results[0].text = build_values_text(&host->values, text, length);
    failure = results[0].text ? NULL : host->values.error;
    free(text);
    return failure;
}
static const char *build_file_exists(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(1);
    results[0].b = build_io_exists(host->root, strings[0]);
    return NULL;
}
static const char *build_make_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(1);
    results[0].b = build_io_mkdir(host->root, strings[0]);
    return NULL;
}
static const char *build_reserve_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(1);
    results[0].b = build_io_reserve_directory(host->root, strings[0]);
    return NULL;
}
static const char *build_publish_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(2);
    results[0].b = build_io_publish_directory(host->root, strings[0], strings[1]);
    return NULL;
}
static const char *build_resolve_path(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char *path = build_io_resolve(host->root, strings[0]);
    const char *failure = string_result(host, results, path ? path : "");
    free(path);
    return failure;
}
static const char *build_write_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(1);
    if (!build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    pin_string payload = pin_text(arguments[1]);
    results[0].b = build_io_write(
        host->root,
        strings[0],
        payload.bytes,
        payload.length,
        host->deadline,
        host->options->cancelled
    );
    return NULL;
}
static const char *build_last_stdout(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    if (host->last_stdout_truncated) {
        return "build stdout truncated";
    }
    results[0].text = build_values_text(&host->values, host->last_stdout, host->last_stdout_size);
    return results[0].text ? NULL : host->values.error;
}
static const char *build_last_stderr(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    if (host->last_stderr_truncated) {
        return "build stderr truncated";
    }
    results[0].text = build_values_text(&host->values, host->last_stderr, host->last_stderr_size);
    return results[0].text ? NULL : host->values.error;
}
static const char *build_report(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    if (!build_values_string(arguments)) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]);
    chibi_build_result *result = host->result;
    size_t size = text.length;
    size_t remaining = sizeof(result->stderr_text) - result->stderr_size - 1;
    size_t copy = size < remaining ? size : remaining;
    memcpy(result->stderr_text + result->stderr_size, text.bytes, copy);
    result->stderr_size += copy;
    remaining -= copy;
    if (remaining) {
        result->stderr_text[result->stderr_size++] = '\n';
    }
    result->stderr_text[result->stderr_size] = 0;
    result->stderr_truncated |= copy != size || !remaining;
    results[0].i = 0;
    return NULL;
}
static const char *build_file_kind(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    const char *kind;
    const char *failure = build_io_kind(host->root, strings[0], &kind);
    return failure ? failure : string_result(host, results, kind);
}
static const char *build_file_stamp(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char stamp[BUILD_IO_STAMP_SIZE];
    const char *failure = build_io_stamp(host->root, strings[0], stamp);
    return failure ? failure : string_result(host, results, stamp);
}
static const char *build_list_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char **names;
    const char *failure = build_io_list(
        host->root,
        strings[0],
        host->deadline,
        host->options->cancelled,
        &names
    );
    if (failure) {
        return failure;
    }
    pin_array *array = build_values_array(&host->values, acount(names));
    failure = array ? NULL : host->values.error;
    for (int i = 0; !failure && i < acount(names); i++) {
        const pin_string *name = build_values_text(&host->values, names[i], strlen(names[i]));
        failure = name ? NULL : host->values.error;
        array->items[i] = (pin_value){ .type = PIN_STR, .text = name };
    }
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    results[0].ptr = array;
    return failure;
}
static const char *build_copy_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(2);
    results[0].b = build_io_copy(
        host->root,
        strings[0],
        strings[1],
        host->deadline,
        host->options->cancelled
    );
    return NULL;
}
static const char *build_rename(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(2);
    results[0].b = build_io_rename(host->root, strings[0], strings[1]);
    return NULL;
}
static const char *build_remove(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(1);
    results[0].b = build_io_remove(host->root, strings[0]);
    return NULL;
}
static const char *build_hash_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char digest[65];
    const char *failure = build_io_hash(
        host->root,
        strings[0],
        host->deadline,
        host->options->cancelled,
        digest
    );
    return failure ? failure : string_result(host, results, digest);
}
static const char *build_byte_list(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char *text = NULL;
    const char *failure = build_io_byte_list(
        host->root,
        strings[0],
        host->deadline,
        host->options->cancelled,
        &text
    );
    if (failure) {
        return failure;
    }
    failure = string_result(host, results, text);
    free(text);
    return failure;
}
static const char *build_embed_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    BOOL_STRING_ARGUMENTS(3);
    bool written;
    const char *failure = build_io_embed(
        host->root,
        strings[0],
        strings[1],
        strings[2],
        host->deadline,
        host->options->cancelled,
        &written
    );
    results[0].b = written;
    return failure;
}
static const char *build_hash_text(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    if (!build_values_string(arguments)) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]);
    char digest[65];
    sha256_hex(text.bytes, text.length, digest);
    return string_result(host, results, digest);
}
#define VALUE_CALLBACK(NAME)                                                                       \
    static const char *value_##NAME(void *service, const pin_value *arguments, pin_value *results) \
    {                                                                                              \
        READY_HOST();                                                                              \
        return build_values_##NAME(&host->values, arguments, results);                             \
    }
VALUE_CALLBACK(join)
VALUE_CALLBACK(starts_with)
VALUE_CALLBACK(before)
VALUE_CALLBACK(after)
VALUE_CALLBACK(lines)
VALUE_CALLBACK(trim)
VALUE_CALLBACK(concat)
VALUE_CALLBACK(length)
VALUE_CALLBACK(replace)
#undef VALUE_CALLBACK
#undef BOOL_STRING_ARGUMENTS
#undef STRING_ARGUMENTS
#undef READY_HOST

static const script_catalog_parameter text_parameter = { .name = "text",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_parameter c_text_parameter = { .name = "text",
                                                           .type = { .storage = PIN_STR },
                                                           .c_string = true };
static const script_catalog_parameter write_parameters[] = {
    { .name = "text", .type = { .storage = PIN_STR }, .c_string = true },
    { .name = "other", .type = { .storage = PIN_STR } },
};
static const script_catalog_parameter array_parameter = {
    .name = "arguments",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY }
};
static const script_catalog_parameter two_text_parameters[] = {
    { .name = "text", .type = { .storage = PIN_STR } },
    { .name = "other", .type = { .storage = PIN_STR } },
};
static const script_catalog_parameter replace_parameters[] = {
    { .name = "text", .type = { .storage = PIN_STR } },
    { .name = "find", .type = { .storage = PIN_STR } },
    { .name = "with", .type = { .storage = PIN_STR } },
};
static const script_catalog_parameter join_parameters[] = {
    { .name = "arguments",
      .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } },
    { .name = "separator", .type = { .storage = PIN_STR } },
};
static const script_catalog_parameter concat_parameters[] = {
    { .name = "left", .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } },
    { .name = "right", .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } },
};
static const script_catalog_parameter embed_parameters[] = {
    { .name = "input", .type = { .storage = PIN_STR }, .c_string = true },
    { .name = "output", .type = { .storage = PIN_STR }, .c_string = true },
    { .name = "symbol", .type = { .storage = PIN_STR }, .c_string = true },
};
static const script_catalog_parameter handle_parameter = { .name = "handle",
                                                           .type = { .storage = PIN_I32 } };
static const script_catalog_parameter wait_parameters[] = {
    { .name = "handle", .type = { .storage = PIN_I32 } },
    { .name = "milliseconds", .type = { .storage = PIN_I32 } },
};
static const script_catalog_parameter path_parameter = { .name = "path",
                                                         .type = { .storage = PIN_STR },
                                                         .c_string = true };
static const script_catalog_parameter move_parameters[] = {
    { .name = "source", .type = { .storage = PIN_STR }, .c_string = true },
    { .name = "destination", .type = { .storage = PIN_STR }, .c_string = true },
};
static const script_catalog_parameter text_result = { .name = "value",
                                                      .type = { .storage = PIN_STR } };
static const script_catalog_parameter int_result = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
static const script_catalog_parameter bool_result = { .name = "value",
                                                      .type = { .storage = PIN_BOOL } };
static const script_catalog_parameter array_result = {
    .name = "value",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY }
};
#define BUILD_NATIVES(X)                                                                        \
    X(901, "build.run", &array_parameter, 1, int_result, build_host_run_step)                   \
    X(902, "build.environment", &c_text_parameter, 1, text_result, build_host_environment)      \
    X(903, "build.arguments", NULL, 0, array_result, build_arguments)                           \
    X(904, "build.platform", NULL, 0, text_result, build_platform)                              \
    X(905, "build.root", NULL, 0, text_result, build_root)                                      \
    X(906, "build.program_directory", NULL, 0, text_result, build_program_directory)            \
    X(907, "build.find_executable", &c_text_parameter, 1, text_result, build_find_executable)   \
    X(908, "build.read_file", &c_text_parameter, 1, text_result, build_read_file)               \
    X(909, "build.file_exists", &c_text_parameter, 1, bool_result, build_file_exists)           \
    X(910, "build.make_directory", &c_text_parameter, 1, bool_result, build_make_directory)     \
    X(911, "build.resolve_path", &c_text_parameter, 1, text_result, build_resolve_path)         \
    X(912, "build.write_file", write_parameters, 2, bool_result, build_write_file)              \
    X(913, "build.last_stdout", NULL, 0, text_result, build_last_stdout)                        \
    X(914, "build.last_stderr", NULL, 0, text_result, build_last_stderr)                        \
    X(915, "build.report", &text_parameter, 1, int_result, build_report)                        \
    X(916, "build.join", join_parameters, 2, text_result, value_join)                           \
    X(917, "build.starts_with", two_text_parameters, 2, bool_result, value_starts_with)         \
    X(918, "build.before", two_text_parameters, 2, text_result, value_before)                   \
    X(919, "build.after", two_text_parameters, 2, text_result, value_after)                     \
    X(920, "build.lines", &text_parameter, 1, array_result, value_lines)                        \
    X(921, "build.trim", &text_parameter, 1, text_result, value_trim)                           \
    X(922, "build.concat", concat_parameters, 2, array_result, value_concat)                    \
    X(923, "build.length", &array_parameter, 1, int_result, value_length)                       \
    X(924, "build.probe", &array_parameter, 1, int_result, build_host_run_probe)                \
    X(925, "build.environment_names", NULL, 0, array_result, build_host_environment_names)      \
    X(926, "build.file_kind", &path_parameter, 1, text_result, build_file_kind)                 \
    X(927, "build.file_stamp", &path_parameter, 1, text_result, build_file_stamp)               \
    X(928, "build.list_directory", &path_parameter, 1, array_result, build_list_directory)      \
    X(929, "build.copy_file", move_parameters, 2, bool_result, build_copy_file)                 \
    X(930, "build.rename", move_parameters, 2, bool_result, build_rename)                       \
    X(931, "build.remove", &path_parameter, 1, bool_result, build_remove)                       \
    X(932, "build.hash_file", &path_parameter, 1, text_result, build_hash_file)                 \
    X(933, "build.hash_text", &text_parameter, 1, text_result, build_hash_text)                 \
    X(934, "build.start", &array_parameter, 1, int_result, build_host_start)                    \
    X(935, "build.wait", &handle_parameter, 1, int_result, build_host_wait)                     \
    X(936, "build.embed_file", embed_parameters, 3, bool_result, build_embed_file)              \
    X(937, "build.replace", replace_parameters, 3, text_result, value_replace)                  \
    X(938, "build.byte_list", &path_parameter, 1, text_result, build_byte_list)                 \
    X(939, "build.reserve_directory", &path_parameter, 1, bool_result, build_reserve_directory) \
    X(940, "build.publish_directory", move_parameters, 2, bool_result, build_publish_directory) \
    X(941, "build.wait_for", wait_parameters, 2, int_result, build_host_wait_for)               \
    X(942, "build.kill", &handle_parameter, 1, int_result, build_host_kill)                     \
    X(943, "build.spawn", &array_parameter, 1, int_result, build_host_spawn)
#define CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULT, FUNCTION)              \
    { .id = ID,                                                                \
      .name = NAME,                                                            \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .parameters = PARAMETERS,                                                \
      .parameter_count = COUNT,                                                \
      .results = &RESULT,                                                      \
      .result_count = 1,                                                       \
      .effects = SCRIPT_EFFECT_HOST },
static const script_catalog_capability capabilities[] = { BUILD_NATIVES(CAPABILITY) };
#undef CAPABILITY
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = sizeof(capabilities) /
                                                            sizeof(*capabilities) };
#define CALL(ID, NAME, PARAMETERS, COUNT, RESULT, FUNCTION)                    \
    static const node_host_call call_##ID = {                                  \
        .service = "tool.build",                                               \
        .invoke = FUNCTION,                                                    \
        .missing_service = "build host service is unavailable"                 \
    };
BUILD_NATIVES(CALL)
#undef CALL
static const node_access host_sink = { .flags = NODE_EFFECT_HOST_SINK };
#define BINDING(ID, NAME, PARAMETERS, COUNT, RESULT, FUNCTION)                 \
    { .id = ID, .call = &call_##ID, .access = &host_sink },
static const native_capability_binding bindings[] = { BUILD_NATIVES(BINDING) };
#undef BINDING
#undef BUILD_NATIVES

const native_capability_pack *build_host_natives()
{
    static const native_capability_pack pack = { .catalog = &catalog,
                                                 .bindings = bindings,
                                                 .binding_count = sizeof bindings /
                                                                  sizeof *bindings };
    return &pack;
}
