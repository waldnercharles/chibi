#include "host/build_host_internal.h"
#include "host/build_io.h"
#include "support/array.h"
#include "support/sha256.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *string_result(build_host *host, pin_value *result, const char *text)
{
    result->str = build_values_text(&host->values, text, strlen(text));
    return result->str ? NULL : host->values.error;
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
    for (int argument_index = 0; argument_index < (N); argument_index++) {     \
        if (!build_values_string(&arguments[argument_index])) {                \
            return "invalid build string arguments";                           \
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
    char *path = arguments[0].str[0] ? build_host_executable(host, arguments[0].str)
                                     : NULL;
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
    const char *failure = NULL;
    if (!build_io_read(
            host->root,
            arguments[0].str,
            host->deadline,
            host->options->cancelled,
            &text,
            &failure
        )) {
        return failure;
    }
    failure = string_result(host, results, text);
    free(text);
    return failure;
}
static const char *build_file_exists(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    results[0].b = build_io_exists(host->root, arguments[0].str);
    return NULL;
}
static const char *build_make_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    results[0].b = build_io_mkdir(host->root, arguments[0].str);
    return NULL;
}
static const char *build_resolve_path(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char *path = build_io_resolve(host->root, arguments[0].str);
    const char *failure = string_result(host, results, path ? path : "");
    free(path);
    return failure;
}
static const char *build_write_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(2);
    results[0].b = build_io_write(
        host->root,
        arguments[0].str,
        arguments[1].str,
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
    if (memchr(host->last_stdout, 0, host->last_stdout_size)) {
        return "build stdout is not text";
    }
    return string_result(host, results, host->last_stdout);
}
static const char *build_last_stderr(void *service, const pin_value *, pin_value *results)
{
    READY_HOST();
    if (host->last_stderr_truncated) {
        return "build stderr truncated";
    }
    if (memchr(host->last_stderr, 0, host->last_stderr_size)) {
        return "build stderr is not text";
    }
    return string_result(host, results, host->last_stderr);
}
static const char *build_report(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    chibi_build_result *result = host->result;
    size_t size = strlen(arguments[0].str);
    size_t remaining = sizeof(result->stderr_text) - result->stderr_size - 1;
    size_t copy = size < remaining ? size : remaining;
    memcpy(result->stderr_text + result->stderr_size, arguments[0].str, copy);
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
    const char *failure = build_io_kind(host->root, arguments[0].str, &kind);
    return failure ? failure : string_result(host, results, kind);
}
static const char *build_file_stamp(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char stamp[BUILD_IO_STAMP_SIZE];
    const char *failure = build_io_stamp(host->root, arguments[0].str, stamp);
    return failure ? failure : string_result(host, results, stamp);
}
static const char *build_list_directory(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char **names;
    const char *failure = build_io_list(
        host->root,
        arguments[0].str,
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
        char *name = build_values_text(&host->values, names[i], strlen(names[i]));
        failure = name ? NULL : host->values.error;
        array->items[i] = (pin_value){ .type = PIN_STR, .str = name };
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
    STRING_ARGUMENTS(2);
    results[0].b = build_io_copy(
        host->root,
        arguments[0].str,
        arguments[1].str,
        host->deadline,
        host->options->cancelled
    );
    return NULL;
}
static const char *build_rename(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(2);
    results[0].b = build_io_rename(host->root, arguments[0].str, arguments[1].str);
    return NULL;
}
static const char *build_remove(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    results[0].b = build_io_remove(host->root, arguments[0].str);
    return NULL;
}
static const char *build_hash_file(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char digest[65];
    const char *failure = build_io_hash(
        host->root,
        arguments[0].str,
        host->deadline,
        host->options->cancelled,
        digest
    );
    return failure ? failure : string_result(host, results, digest);
}
static const char *build_hash_text(void *service, const pin_value *arguments, pin_value *results)
{
    READY_HOST();
    STRING_ARGUMENTS(1);
    char digest[65];
    sha256_hex(arguments[0].str, strlen(arguments[0].str), digest);
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
#undef VALUE_CALLBACK
#undef STRING_ARGUMENTS
#undef READY_HOST

static const script_catalog_parameter text_parameter = { .name = "text",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_parameter array_parameter = {
    .name = "arguments",
    .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY }
};
static const script_catalog_parameter two_text_parameters[] = {
    { .name = "text", .type = { .storage = PIN_STR } },
    { .name = "other", .type = { .storage = PIN_STR } },
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
static const script_catalog_parameter path_parameter = { .name = "path",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_parameter move_parameters[] = {
    { .name = "source", .type = { .storage = PIN_STR } },
    { .name = "destination", .type = { .storage = PIN_STR } },
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
#define BUILD_CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULT)                  \
    { .id = ID,                                                                \
      .name = NAME,                                                            \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .parameters = PARAMETERS,                                                \
      .parameter_count = COUNT,                                                \
      .results = &RESULT,                                                      \
      .result_count = 1,                                                       \
      .effects = SCRIPT_EFFECT_HOST }
static const script_catalog_capability capabilities[] = {
    BUILD_CAPABILITY(901, "build.run", &array_parameter, 1, int_result),
    BUILD_CAPABILITY(902, "build.environment", &text_parameter, 1, text_result),
    BUILD_CAPABILITY(903, "build.arguments", NULL, 0, array_result),
    BUILD_CAPABILITY(904, "build.platform", NULL, 0, text_result),
    BUILD_CAPABILITY(905, "build.root", NULL, 0, text_result),
    BUILD_CAPABILITY(906, "build.program_directory", NULL, 0, text_result),
    BUILD_CAPABILITY(907, "build.find_executable", &text_parameter, 1, text_result),
    BUILD_CAPABILITY(908, "build.read_file", &text_parameter, 1, text_result),
    BUILD_CAPABILITY(909, "build.file_exists", &text_parameter, 1, bool_result),
    BUILD_CAPABILITY(910, "build.make_directory", &text_parameter, 1, bool_result),
    BUILD_CAPABILITY(911, "build.resolve_path", &text_parameter, 1, text_result),
    BUILD_CAPABILITY(912, "build.write_file", two_text_parameters, 2, bool_result),
    BUILD_CAPABILITY(913, "build.last_stdout", NULL, 0, text_result),
    BUILD_CAPABILITY(914, "build.last_stderr", NULL, 0, text_result),
    BUILD_CAPABILITY(915, "build.report", &text_parameter, 1, int_result),
    BUILD_CAPABILITY(916, "build.join", join_parameters, 2, text_result),
    BUILD_CAPABILITY(917, "build.starts_with", two_text_parameters, 2, bool_result),
    BUILD_CAPABILITY(918, "build.before", two_text_parameters, 2, text_result),
    BUILD_CAPABILITY(919, "build.after", two_text_parameters, 2, text_result),
    BUILD_CAPABILITY(920, "build.lines", &text_parameter, 1, array_result),
    BUILD_CAPABILITY(921, "build.trim", &text_parameter, 1, text_result),
    BUILD_CAPABILITY(922, "build.concat", concat_parameters, 2, array_result),
    BUILD_CAPABILITY(923, "build.length", &array_parameter, 1, int_result),
    BUILD_CAPABILITY(924, "build.probe", &array_parameter, 1, int_result),
    BUILD_CAPABILITY(925, "build.environment_names", NULL, 0, array_result),
    BUILD_CAPABILITY(926, "build.file_kind", &path_parameter, 1, text_result),
    BUILD_CAPABILITY(927, "build.file_stamp", &path_parameter, 1, text_result),
    BUILD_CAPABILITY(928, "build.list_directory", &path_parameter, 1, array_result),
    BUILD_CAPABILITY(929, "build.copy_file", move_parameters, 2, bool_result),
    BUILD_CAPABILITY(930, "build.rename", move_parameters, 2, bool_result),
    BUILD_CAPABILITY(931, "build.remove", &path_parameter, 1, bool_result),
    BUILD_CAPABILITY(932, "build.hash_file", &path_parameter, 1, text_result),
    BUILD_CAPABILITY(933, "build.hash_text", &text_parameter, 1, text_result),
};
#undef BUILD_CAPABILITY
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = sizeof(capabilities) /
                                                            sizeof(*capabilities) };
#define BUILD_CALL(FUNCTION)                                                   \
    { .service = "tool.build",                                                 \
      .invoke = FUNCTION,                                                      \
      .missing_service = "build host service is unavailable" }
static const node_host_call calls[] = {
    BUILD_CALL(build_host_run_step),
    BUILD_CALL(build_host_environment),
    BUILD_CALL(build_arguments),
    BUILD_CALL(build_platform),
    BUILD_CALL(build_root),
    BUILD_CALL(build_program_directory),
    BUILD_CALL(build_find_executable),
    BUILD_CALL(build_read_file),
    BUILD_CALL(build_file_exists),
    BUILD_CALL(build_make_directory),
    BUILD_CALL(build_resolve_path),
    BUILD_CALL(build_write_file),
    BUILD_CALL(build_last_stdout),
    BUILD_CALL(build_last_stderr),
    BUILD_CALL(build_report),
    BUILD_CALL(value_join),
    BUILD_CALL(value_starts_with),
    BUILD_CALL(value_before),
    BUILD_CALL(value_after),
    BUILD_CALL(value_lines),
    BUILD_CALL(value_trim),
    BUILD_CALL(value_concat),
    BUILD_CALL(value_length),
    BUILD_CALL(build_host_run_probe),
    BUILD_CALL(build_host_environment_names),
    BUILD_CALL(build_file_kind),
    BUILD_CALL(build_file_stamp),
    BUILD_CALL(build_list_directory),
    BUILD_CALL(build_copy_file),
    BUILD_CALL(build_rename),
    BUILD_CALL(build_remove),
    BUILD_CALL(build_hash_file),
    BUILD_CALL(build_hash_text),
};
#undef BUILD_CALL
bool build_host_resolve(void *, uint64_t id, script_lower_host *out)
{
    if (id < 901 || id >= 901 + sizeof(calls) / sizeof(*calls)) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &calls[id - 901], .access = &access };
    return true;
}

const script_catalog *build_host_catalog()
{
    return &catalog;
}
