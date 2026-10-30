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
/* Runs each test and prints its outcome; the number that failed. */
static int run_tests(const package_collection *packages, behavior_backend backend, int workers)
{
    package_test *tests = package_tests_find_compiled(packages);
    int count = acount(tests);
    package_test_result *results = xcalloc((size_t)count, sizeof *results);
    task_pool *pool = task_pool_acquire_workers(workers);
    task_pool_begin(pool, count > 1);
    package_tests_run(tests, count, backend, PACKAGE_TEST_BUDGET, task_pool_tasks(pool), results);
    task_pool_end(pool);
    task_pool_release(pool);
    int failed = 0;
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
        failed++;
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
    printf("%d tests, %d failed\n", count, failed);
    free(results);
    afree(tests);
    return failed;
}
/* Where the tests' packages compile: a package of its own that imports every
 * package below the project root that declares a test, so one closure reaches
 * them and what they import. */
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

/* Whether a file of the directory named by the first `prefix` bytes of `path`
 * declares a test. What that directory imports joins the closure anyway. */
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
    return false;
}

/* The text of that package, which imports the directory of each project file
 * outside the collections that declares a test. */
static char *tests_package(const source_package_snapshot_options *inputs)
{
    size_t root = strlen(inputs->project_root);
    char *text = xstrdup("package tests\n");
    const char *previous = NULL;
    size_t previous_length = 0;
    int imports = 0;
    bool root_imported = false;
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
            if (!root_imported && directory_has_tests(inputs, path, root)) {
                root_imported = true;
                size_t used = strlen(text);
                text = xrealloc(text, used + 48);
                snprintf(text + used, 48, "import p%d \"project:.\"\n", imports++);
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
        if (!directory_has_tests(inputs, path, root + 1 + length)) {
            continue;
        }
        size_t used = strlen(text);
        text = xrealloc(text, used + length + 48);
        snprintf(text + used, length + 48, "import p%d \"project:%.*s\"\n", imports++, (int)length, previous);
    }
    return text;
}

static package_collection *compile_tests(
    const char *root,
    const char *core,
    const native_capabilities *native,
    behavior_backend backend
)
{
    source_package_collection collections[] = { { "core", core } };
    source_package_disk_options options = { root, collections, 1 };
    source_package_snapshot_options inputs = { 0 };
    source_package_load_error load = { 0 };
    if (!source_package_files_load(&options, &inputs, &load)) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
        return NULL;
    }
    char *package = tests_package(&inputs);
    size_t path_size = strlen(inputs.project_root) + sizeof "/" TESTS_PACKAGE
                                                            "/Tests.chibi";
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/" TESTS_PACKAGE "/Tests.chibi", inputs.project_root);
    package_compile_error error;
    package_collection *packages = package_compile(&inputs, path, package, native, backend, &error);
    if (!packages && *error.path) {
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
    }
    free(path);
    free(package);
    source_package_files_release(&inputs);
    return packages;
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
    behavior_backend backend = behavior_default_backend();
    package_collection *packages = compile_tests(root, core, native, backend);
    int status = 1;
    if (packages) {
        status = run_tests(packages, backend, workers) ? 1 : 0;
        package_collection_free(packages);
    }
    native_capabilities_free(native);
    return status;
}

#endif
