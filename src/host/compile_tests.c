#include "authoring/package_compile.h"
#include "authoring/package_tests.h"
#include "authoring/project_compile.h"
#include "authoring/source_package_io.h"
#include "execution/backend.h"
#include "execution/function.h"
#include "host/native_default.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
int authoring_project_test(const char *, const char *)
{
    fprintf(stderr, "Package tests need a native host\n");
    return 1;
}
#else
/* Runs each test, prints its outcome and adds to the totals. */
static void run_tests(
    const package_collection *packages,
    behavior_backend backend,
    int workers,
    int *total,
    int *failed
)
{
    package_test *tests = package_tests_find_compiled(packages);
    int count = acount(tests);
    package_test_result *results = xcalloc((size_t)count, sizeof *results);
    task_pool *pool = task_pool_acquire_workers(workers);
    task_pool_begin(pool, count > 1);
    package_tests_run(tests, count, backend, PACKAGE_TEST_BUDGET, task_pool_tasks(pool), results);
    task_pool_end(pool);
    task_pool_release(pool);
    *total += count;
    for (int i = 0; i < count; i++) {
        package_test_failure failure = results[i].failure;
        bool passed = results[i].passed;
        printf(
            "%s %s.%s\n",
            passed ? "PASS" : "FAIL",
            tests[i].package,
            tests[i].name
        );
        if (passed) {
            continue;
        }
        (*failed)++;
        if (failure.path) {
            printf(
                "  %s:%d:%d: %s\n",
                failure.path,
                failure.line,
                failure.column,
                failure.message
            );
        } else {
            printf("  %s\n", failure.message);
        }
    }
    free(results);
    afree(tests);
}
/* Where each tested package compiles: a package of its own that imports it, so
 * its closure holds only it and what it imports. One closure per package keeps
 * each compile within its budget however many packages declare tests, as Go
 * builds one test binary per package. */
#define TESTS_PACKAGE "__tests__"

/* An `@test` attribute alone on a line, as a test function is declared. */
static bool has_test_marker(const source_package_input *file)
{
    static const char marker[] = "@test";
    size_t length = sizeof marker - 1;
    for (size_t at = 0; at + length <= file->size; at++) {
        bool starts = at == 0 || file->text[at - 1] == '\n';
        bool ends = at + length == file->size || file->text[at + length] == '\n' ||
                    file->text[at + length] == '\r';
        if (starts && ends && !memcmp(file->text + at, marker, length)) {
            return true;
        }
    }
    return false;
}

/* A failing file's expected first diagnostic, as Zig's test cases end with:
 * the text after "// error: " on its last line, `LINE:COL: message`, or NULL. */
static const char *expected_error(const source_package_input *file, size_t *length)
{
    static const char marker[] = "// error: ";
    size_t end = file->size;
    while (end && (file->text[end - 1] == '\n' || file->text[end - 1] == '\r')) {
        end--;
    }
    size_t start = end;
    while (start && file->text[start - 1] != '\n') {
        start--;
    }
    if (end - start <= sizeof marker - 1 ||
        memcmp(file->text + start, marker, sizeof marker - 1)) {
        return NULL;
    }
    *length = end - start - (sizeof marker - 1);
    return file->text + start + sizeof marker - 1;
}

/* The file directly in the directory named by the first `prefix` bytes of
 * `path` that expects a compile error, or NULL. */
static const source_package_input *rejected_file(
    const source_package_snapshot_options *inputs,
    const char *path,
    size_t prefix
)
{
    for (int i = 0; i < inputs->file_count; i++) {
        const char *candidate = inputs->files[i].path;
        size_t length;
        if (!strncmp(candidate, path, prefix) && candidate[prefix] == '/' &&
            !strchr(candidate + prefix + 1, '/') &&
            expected_error(&inputs->files[i], &length)) {
            return &inputs->files[i];
        }
    }
    return NULL;
}

/* Whether a file of the directory named by the first `prefix` bytes of `path`
 * declares a test or expects a compile error. */
static bool directory_has_tests(const source_package_snapshot_options *inputs, const char *path, size_t prefix)
{
    for (int i = 0; i < inputs->file_count; i++) {
        const char *candidate = inputs->files[i].path;
        if (!strncmp(candidate, path, prefix) && candidate[prefix] == '/' &&
            !strchr(candidate + prefix + 1, '/') &&
            has_test_marker(&inputs->files[i])) {
            return true;
        }
    }
    return rejected_file(inputs, path, prefix) != NULL;
}

/* The import path, below `project:`, of each project package outside the
 * collections that declares a test: "." for files directly in the root. The
 * caller owns the array and its strings. */
static char **tested_packages(const source_package_snapshot_options *inputs)
{
    size_t root = strlen(inputs->project_root);
    char **packages = NULL;
    const char *previous = NULL;
    size_t previous_length = 0;
    bool root_seen = false;
    for (int i = 0; i < inputs->file_count; i++) {
        const char *path = inputs->files[i].path;
        bool collected = false;
        for (int j = 0; j < inputs->collection_count; j++) {
            size_t length = strlen(inputs->collections[j].root);
            collected |= !strncmp(path, inputs->collections[j].root, length) &&
                         path[length] == '/';
        }
        if (collected || strncmp(path, inputs->project_root, root) || path[root] != '/') {
            continue;
        }
        const char *slash = strrchr(path + root + 1, '/');
        if (!slash) {
            /* The project root is itself a package when files sit directly in it. */
            if (!root_seen && directory_has_tests(inputs, path, root)) {
                root_seen = true;
                apush(packages, xstrdup("."));
            }
            continue;
        }
        size_t length = (size_t)(slash - (path + root + 1));
        if (previous && previous_length == length &&
            !memcmp(previous, path + root + 1, length)) {
            continue;
        }
        previous = path + root + 1;
        previous_length = length;
        if (directory_has_tests(inputs, path, root + 1 + length)) {
            char *package = xmalloc(length + 1);
            memcpy(package, previous, length);
            package[length] = 0;
            apush(packages, package);
        }
    }
    return packages;
}

/* The closure of one tested package, or NULL with why it failed. */
static package_collection *compile_tests(
    const source_package_snapshot_options *inputs,
    const char *tested,
    const native_capabilities *native,
    behavior_backend backend,
    package_compile_error *error
)
{
    size_t text_size = strlen(tested) + 48;
    char *package = xmalloc(text_size);
    snprintf(package, text_size, "package tests\nimport tested \"project:%s\"\n", tested);
    size_t path_size = strlen(inputs->project_root) + sizeof "/" TESTS_PACKAGE
                                                             "/Tests.chibi";
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/" TESTS_PACKAGE "/Tests.chibi", inputs->project_root);
    package_collection *packages = package_compile(inputs, path, package, native, backend, error);
    free(path);
    free(package);
    return packages;
}
/* Whether `tested`, whose `file` expects a compile error, failed with exactly
 * that first diagnostic; prints the outcome as a test's. */
static bool rejected_as_expected(
    const char *tested,
    const source_package_input *file,
    const package_collection *packages,
    const package_compile_error *error
)
{
    size_t length;
    const char *expected = expected_error(file, &length);
    char actual[sizeof error->message + 32];
    snprintf(
        actual,
        sizeof actual,
        "%d:%d: %s",
        error->line,
        error->column,
        error->message
    );
    bool passed = !packages && !strcmp(error->path, file->path) &&
                  strlen(actual) == length && !memcmp(actual, expected, length);
    printf("%s %s.compile_error\n", passed ? "PASS" : "FAIL", tested);
    if (!passed) {
        printf("  %s: expected %.*s, ", file->path, (int)length, expected);
        if (packages) {
            printf("but it compiled\n");
        } else {
            printf("got %s:%s\n", error->path, actual);
        }
    }
    return passed;
}
/* Tests run without a game, so they get the natives that need none of it, and
 * the compiler's raw-syntax and JSON bridges, so the compiler's own packages
 * can hold tests. */
int authoring_project_test(const char *root, const char *core)
{
    /* Independent of the repository selector runner's process-level job limit. */
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
    char error[512] = { 0 };
    const native_definition_pack compiler[] = { COMPILER_SYNTAX_DEFINITION_PACK,
                                                COMPILER_JSON_DEFINITION_PACK };
    native_capabilities
        *native = host_native_standalone_catalog_new(NULL, 0, compiler, 2, error, sizeof error);
    if (!native) {
        fprintf(stderr, "Test preparation failed: %s\n", error);
        return 1;
    }
    source_package_collection collections[] = { { "core", core } };
    source_package_disk_options options = { root, collections, 1 };
    source_package_snapshot_options inputs = { 0 };
    source_package_load_error load = { 0 };
    if (!source_package_files_load(&options, &inputs, &load)) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
        native_capabilities_free(native);
        return 1;
    }
    behavior_backend backend = behavior_default_backend();
    char **tested = tested_packages(&inputs);
    int total = 0, failed = 0;
    bool compiled = true;
    for (int i = 0; i < acount(tested); i++) {
        size_t root = strlen(inputs.project_root), directory = strlen(tested[i]);
        char *path = xmalloc(root + directory + 2);
        snprintf(path, root + directory + 2, "%s/%s", inputs.project_root, tested[i]);
        const source_package_input *rejected = rejected_file(
            &inputs,
            path,
            strcmp(tested[i], ".") ? root + 1 + directory : root
        );
        free(path);
        package_compile_error error;
        package_collection *packages = compile_tests(&inputs, tested[i], native, backend, &error);
        if (rejected) {
            total++;
            failed += !rejected_as_expected(tested[i], rejected, packages, &error);
        } else if (!packages && *error.path) {
            fprintf(
                stderr,
                "%s:%d:%d: %s\n",
                error.path,
                error.line,
                error.column,
                error.message
            );
        } else if (!packages) {
            fprintf(stderr, "%s\n", error.message);
        } else {
            run_tests(packages, backend, workers, &total, &failed);
        }
        compiled &= packages != NULL || rejected;
        if (packages) {
            package_collection_free(packages);
        }
        free(tested[i]);
    }
    afree(tested);
    source_package_files_release(&inputs);
    native_capabilities_free(native);
    if (!compiled) {
        return 1;
    }
    printf("%d tests, %d failed\n", total, failed);
    return failed ? 1 : 0;
}

#endif
