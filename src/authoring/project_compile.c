#include "authoring/project_compile.h"

#include "authoring/graph_catalog.h"
#include "authoring/project_prepare.h"
#include "authoring/renderer_catalog.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"

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
    char *temporary[] = { write_temporary(output, artifact->data, artifact->size), NULL };
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
int authoring_project_compile(const char *project, const char *scene, const char *output)
{
    char error[512] = { 0 };
    native_capabilities *native = authoring_renderer_catalog_new(error, sizeof error);
    if (!native) {
        fprintf(stderr, "Project preparation failed: %s\n", error);
        return 1;
    }
    authoring_graph_catalog_config config = {
        .native = native,
        .types = GAME_TYPE_PACK,
        .components = { GAME_REGISTRY_COMPONENTS, GAME_REGISTRY_COMPONENT_COUNT }
    };
    authoring_graph_catalog *graphs = authoring_graph_catalog_new(config, error, sizeof error);
    if (!graphs) {
        fprintf(stderr, "Project preparation failed: %s\n", error);
        native_capabilities_free(native);
        return 1;
    }
    int pack_count = 0;
    const registry_pack *const *packs = authoring_graph_catalog_packs(graphs, &pack_count);
    registry_set_packs(packs, pack_count);
    authoring_project_artifact artifact = { 0 };
    authoring_project_options options = {
        .project = project,
        .scene = scene,
        .native = native,
        .components = { GAME_REGISTRY_COMPONENTS, GAME_REGISTRY_COMPONENT_COUNT },
        .modules = GAME_WORLD_MODULES_ALL
    };
    bool valid = authoring_project_prepare(&options, &artifact, error, sizeof error);
    registry_set_packs(NULL, 0);
    authoring_graph_catalog_free(graphs);
    native_capabilities_free(native);
    if (valid) {
        valid = write_artifact(output, &artifact, error, sizeof error);
    }
    if (valid) {
        printf("Prepared project: %s (%zu bytes)\n", output, artifact.size);
    } else {
        fprintf(stderr, "Project preparation failed: %s\n", error);
    }
    authoring_project_artifact_free(&artifact);
    return valid ? 0 : 1;
}
