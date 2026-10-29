#include "authoring/project_compile.h"

#include "authoring/native_bindings.h"
#include "authoring/package_tests.h"
#include "authoring/project_prepare.h"
#include "authoring/renderer_catalog.h"
#include "authoring/source_package_files.h"
#include "execution/backend.h"
#include "execution/function.h"
#include "execution/managed.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/asset_format.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char *temporary_path(const char *path)
{
    size_t length = strlen(path) + sizeof ".tmp.XXXXXX";
    char *temporary = xmalloc(length);
    snprintf(temporary, length, "%s.tmp.XXXXXX", path);
    return temporary;
}
static char *write_temporary(const char *path, const void *data, size_t bytes)
{
    char *temporary = temporary_path(path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    bool valid = file && fwrite(data, 1, bytes, file) == bytes;
    if (file) {
        valid = fclose(file) == 0 && valid;
    } else {
        close(fd);
    }
    if (valid) {
        return temporary;
    }
    unlink(temporary);
    free(temporary);
    return NULL;
}
static bool backup_file(const char *path, char **backup)
{
    struct stat info;
    if (lstat(path, &info)) {
        return errno == ENOENT;
    }
    if (!S_ISREG(info.st_mode)) {
        errno = EINVAL;
        return false;
    }
    char *temporary = temporary_path(path);
    int fd = mkstemp(temporary);
    if (fd < 0) {
        free(temporary);
        return false;
    }
    close(fd);
    if (rename(path, temporary)) {
        unlink(temporary);
        free(temporary);
        return false;
    }
    *backup = temporary;
    return true;
}
/* Creates the directories above `path` that do not exist yet. */
static bool make_parents(const char *path)
{
    assert(path[0]);
    char *directory = xstrdup(path);
    bool valid = true;
    for (char *slash = strchr(directory + 1, '/'); valid && slash;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        valid = !mkdir(directory, 0777) || errno == EEXIST;
        *slash = '/';
    }
    free(directory);
    return valid;
}
static bool write_artifact(
    const char *output,
    const authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    size_t length = strlen(output) + sizeof ".resources.json";
    char *manifest = xmalloc(length);
    snprintf(manifest, length, "%s.resources.json", output);
    const char *paths[] = { output, manifest };
    char *temporary[] = { NULL, NULL };
    if (make_parents(output)) {
        temporary[0] = write_temporary(output, artifact->data, artifact->size);
    }
    char *backups[2] = { 0 };
    bool published[2] = { 0 };
    bool valid = temporary[0] != NULL;
    if (valid) {
        temporary[1] = write_temporary(
            manifest,
            artifact->manifest,
            strlen(artifact->manifest)
        );
        valid = temporary[1] != NULL;
    }
    for (int i = 0; valid && i < 2; i++) {
        valid = backup_file(paths[i], &backups[i]);
    }
    for (int i = 0; valid && i < 2; i++) {
        valid = rename(temporary[i], paths[i]) == 0;
        published[i] = valid;
    }
    if (!valid) {
        snprintf(error, size, "Cannot publish prepared artifact pair: %s", strerror(errno));
        for (int i = 0; i < 2; i++) {
            if (published[i]) {
                unlink(paths[i]);
            }
            if (backups[i] && rename(backups[i], paths[i])) {
                snprintf(error, size, "Cannot restore prior artifact; retained backup: %s", backups[i]);
                /* Preserve the old bytes when external filesystem mutation
                 * prevents restoration; report their exact recovery path. */
                free(backups[i]);
                backups[i] = NULL;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (temporary[i]) {
            unlink(temporary[i]);
            free(temporary[i]);
        }
        if (backups[i]) {
            unlink(backups[i]);
            free(backups[i]);
        }
    }
    free(manifest);
    return valid;
}
bool authoring_application_begin(authoring_application *application, char *error, size_t size)
{
    *application = (authoring_application){ authoring_renderer_catalog_new(error, size) };
    return application->native != NULL;
}
void authoring_application_end(authoring_application *application)
{
    native_capabilities_free(application->native);
}
/* Prepares the project, or assembles it when `modules` is nonnull,
 * and writes the artifact to `output` when there is one. */
static int compile(
    const char *project,
    const char *scene,
    const package_module *modules,
    int count,
    const char *output,
    authoring_cost_report_mode cost_report
)
{
    char error[512] = { 0 };
    authoring_application application;
    if (!authoring_application_begin(&application, error, sizeof error)) {
        fprintf(stderr, "Project preparation failed: %s\n", error);
        return 1;
    }
    authoring_project_artifact artifact = { 0 };
    authoring_project_options options = { .project = project,
                                          .scene = scene,
                                          .native = application.native,
                                          .components = { REGISTRY_COMPONENTS,
                                                          REGISTRY_COMPONENT_COUNT },
                                          .modules = WORLD_MODULES_ALL,
                                          .cost_report = cost_report };
    bool valid = modules
                   ? authoring_project_assemble(&options, modules, count, &artifact, error, sizeof error)
                   : authoring_project_prepare(&options, &artifact, error, sizeof error);
    authoring_application_end(&application);
    if (valid && output) {
        valid = write_artifact(output, &artifact, error, sizeof error);
    }
    if (valid && output) {
        printf("Prepared project: %s (%zu bytes)\n", output, artifact.size);
    } else if (valid && cost_report) {
        fputs(artifact.cost_report, stdout);
    } else if (valid) {
        printf("Checked project: %s\n", project);
    } else {
        fprintf(stderr, "Project preparation failed: %s\n", error);
    }
    authoring_project_artifact_free(&artifact);
    return valid ? 0 : 1;
}
int authoring_project_compile(const char *project, const char *scene, const char *output)
{
    return compile(project, scene, NULL, 0, output, AUTHORING_COST_REPORT_NONE);
}
int authoring_project_explain_costs(const char *project, const char *scene, bool json)
{
    return compile(project, scene, NULL, 0, NULL, json ? AUTHORING_COST_REPORT_JSON : AUTHORING_COST_REPORT_TEXT);
}
int authoring_project_compile_modules(
    const char *project,
    const package_module *modules,
    int count,
    const char *output
)
{
    assert(count >= 0 && (!count || modules) && output);
    static const package_module none;
    return compile(project, NULL, count ? modules : &none, count, output, AUTHORING_COST_REPORT_NONE);
}

static void print_diagnostic(const char *path, const script_diagnostic *diagnostic)
{
    if (!path) {
        fprintf(stderr, "%s\n", diagnostic->message);
        return;
    }
    const script_range *at = &diagnostic->range;
    fprintf(stderr, "%s:%d:%d: %s\n", path, at->line, at->column, diagnostic->message);
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
        print_diagnostic(
            document >= 0 && document < count ? documents[document].path : NULL,
            &error->diagnostic
        );
        return;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; j < package->diagnostic_count; j++) {
            const script_diagnostic *diagnostic = &package->diagnostics[j];
            int document = diagnostic->range.document;
            const script_analysis *analysis = package->analysis;
            bool known = analysis && document >= 0 && document < analysis->document_count;
            print_diagnostic(known ? analysis->documents[document].path : NULL, diagnostic);
        }
    }
}
/* Runs each test and prints its outcome; the number that failed. */
static int run_tests(const authoring_packages *packages, behavior_backend backend, int workers)
{
    package_test *tests = package_tests_find(packages);
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
    source_package_load_error load = { 0 };
    source_package_snapshot *snapshot = source_package_snapshot_load(&options, &load);
    if (!snapshot) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
        native_capabilities_free(native);
        return 1;
    }
    script_lower_bindings bindings = authoring_native_bindings(native);
    behavior_backend backend = behavior_default_backend();
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(native),
        &bindings,
        backend,
        NULL
    );
    int status = 1;
    if (authoring_packages_valid(packages)) {
        status = run_tests(packages, backend, workers) ? 1 : 0;
    } else {
        print_invalid(packages);
    }
    authoring_packages_free(packages);
    native_capabilities_free(native);
    return status;
}
