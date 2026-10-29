#include "authoring/project_compile.h"

#include "authoring/asset_compiler_host.h"
#include "authoring/package_tests.h"
#include "authoring/source_package_io.h"
#include "execution/backend.h"
#include "execution/function.h"
#include "host/native_default.h"
#include "runtime/native_prepared.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "toolchain/compiler_target.h"

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
 * package below the project root, so one closure reaches them all. */
#define TESTS_PACKAGE "__tests__"
#define COMPILE_STORAGE_LIMIT COMPILER_HOST_MAX_CLOSURE_BYTES

/* The text of that package, which imports the directory of each project file
 * outside the collections. */
static char *tests_package(const source_package_snapshot_options *inputs)
{
    size_t root = strlen(inputs->project_root);
    char *text = xstrdup("package tests\n");
    const char *previous = NULL;
    size_t previous_length = 0;
    int imports = 0;
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
            continue;
        }
        size_t length = (size_t)(slash - (path + root + 1));
        if (previous && previous_length == length && !memcmp(previous, path + root + 1, length)) {
            continue;
        }
        previous = path + root + 1;
        previous_length = length;
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
    char *tests_path = xmalloc(strlen(inputs.project_root) + sizeof "/" TESTS_PACKAGE "/Tests.chibi");
    sprintf(tests_path, "%s/" TESTS_PACKAGE "/Tests.chibi", inputs.project_root);
    int count = inputs.file_count + 1;
    const char **paths = xmalloc((size_t)count * sizeof *paths);
    const char **texts = xmalloc((size_t)count * sizeof *texts);
    size_t *sizes = xmalloc((size_t)count * sizeof *sizes);
    const char *names[1], *roots[1];
    paths[0] = tests_path;
    texts[0] = package;
    sizes[0] = strlen(package);
    for (int i = 0; i < inputs.file_count; i++) {
        paths[i + 1] = inputs.files[i].path;
        texts[i + 1] = inputs.files[i].text;
        sizes[i + 1] = inputs.files[i].size;
    }
    for (int i = 0; i < inputs.collection_count; i++) {
        names[i] = inputs.collections[i].name;
        roots[i] = inputs.collections[i].root;
    }
    size_t request_length = 0;
    char *request = compiler_package_request_json(
        inputs.project_root,
        names,
        roots,
        inputs.collection_count,
        paths,
        texts,
        sizes,
        count,
        native_capabilities_catalog(native),
        &request_length
    );
    package_collection *packages = NULL;
    asset_compiler_diagnostic diagnostic;
    size_t closure_length = 0;
    unsigned char *closure = request ? asset_compiler_compile_closure(
                                           request,
                                           request_length,
                                           &closure_length,
                                           NULL,
                                           &diagnostic
                                       )
                                     : NULL;
    if (!closure) {
        const char *path = request && diagnostic.document >= 0 && diagnostic.document < count
                             ? paths[diagnostic.document]
                             : NULL;
        if (path) {
            fprintf(stderr, "%s:%d:%d: %s\n", path, diagnostic.line, diagnostic.column, diagnostic.message);
        } else {
            fprintf(stderr, "%s\n", request ? diagnostic.message : "The target cannot be described");
        }
    } else {
        native_prepared_context context = { .native = native };
        execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
        linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
        linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
        linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
        size_t remaining = COMPILE_STORAGE_LIMIT;
        char error[256] = { 0 };
        packages = compiler_artifact_read(closure, closure_length, &linker, backend, &remaining, &(uint64_t){ 0 }, error, sizeof error);
        if (!packages) {
            fprintf(stderr, "%s\n", error);
        }
    }
    free(closure);
    free(request);
    free(sizes);
    free(texts);
    free(paths);
    free(tests_path);
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
