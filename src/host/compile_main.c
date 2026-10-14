#include "authoring/project_compile.h"
#include "authoring/project_migration.h"
#include "authoring/workspace_catalog.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "support/rooted_files.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool workspace;

/* Explicit final application composition, shared with the prepared player. */
static bool additional_pack(int index)
{
    const registry_pack *pack = registry_pack_at(index);
    if ((!pack->type_count && !pack->enum_count) ||
        (pack->types == TYPE_PACK.types && pack->enums == TYPE_PACK.enums &&
         pack->type_count == TYPE_PACK.type_count &&
         pack->enum_count == TYPE_PACK.enum_count)) {
        return false;
    }
    for (int i = 0; i < index; i++) {
        const registry_pack *previous = registry_pack_at(i);
        if (pack->types == previous->types && pack->enums == previous->enums &&
            pack->type_count == previous->type_count &&
            pack->enum_count == previous->enum_count) {
            return false;
        }
    }
    return true;
}
int type_catalog_pack_count()
{
    int count = 1 + (workspace ? authoring_workspace_type_pack_count() : 0);
    for (int i = 0; i < registry_pack_count(); i++) {
        count += additional_pack(i);
    }
    return count;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    if (!index) {
        return TYPE_PACK;
    }
    if (workspace) {
        int count = authoring_workspace_type_pack_count();
        if (index <= count) {
            return authoring_workspace_type_pack_at(index - 1);
        }
        index -= count;
    }
    for (int i = 0; index > 0 && i < registry_pack_count(); i++) {
        if (additional_pack(i) && !--index) {
            const registry_pack *pack = registry_pack_at(i);
            return (
                type_catalog_pack
            ){ pack->types, pack->type_count, pack->enums, pack->enum_count };
        }
    }
    abort();
}
static int usage(const char *program)
{
    fprintf(stderr, "Usage: %s --project-root PATH --output FILE [--scene PATH] [--authoring]\n", program);
    fprintf(stderr, "       %s --project-root PATH --migrate-copy NEW-DIRECTORY\n", program);
    return 2;
}
int main(int argc, char **argv)
{
    const char *project = NULL, *scene = NULL, *output = NULL, *migrate = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--authoring") && !workspace) {
            workspace = true;
            continue;
        }
        if (i + 1 == argc) {
            return usage(argv[0]);
        }
        if (!strcmp(argv[i], "--project-root") && !project) {
            project = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--scene") && !scene) {
            scene = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--migrate-copy") && !migrate) {
            migrate = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--output") && !output) {
            output = argv[++i];
            continue;
        }
        return usage(argv[0]);
    }
    if (migrate && project && !output && !scene && !workspace) {
        char *leaf = NULL;
        rooted_files *parent = rooted_files_open_parent(migrate, &leaf);
        char *directory = parent ? rooted_files_host_path(parent) : NULL;
        char *result = NULL;
        char error[512] = { 0 };
        bool ok = directory &&
                  project_migration_copy(project, directory, leaf, &result, error, sizeof error);
        if (ok) {
            printf("Migration copy ready: %s\n", result);
            rooted_files *files = rooted_files_open(result);
            unsigned char *report = NULL;
            size_t bytes = 0;
            if (files &&
                rooted_files_read(files, ".chibi/migration-report.txt", 4096, &report, &bytes)) {
                fwrite(report, 1, bytes, stdout);
            }
            free(report);
            if (files) {
                rooted_files_close(files);
            }
        } else {
            fprintf(stderr, "%s\n", *error ? error : "Cannot open exclusive migration destination parent");
        }
        free(result);
        free(directory);
        free(leaf);
        if (parent) {
            rooted_files_close(parent);
        }
        return ok ? 0 : 1;
    }
    if (migrate || !project || !*project || !output || !*output) {
        return usage(argv[0]);
    }
    return authoring_project_compile(project, scene, output, workspace);
}
