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
