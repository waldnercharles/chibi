#include "authoring/project_compile.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int count = 1;
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
    fprintf(stderr, "Usage: %s --project-root PATH --output FILE [--scene PATH]\n", program);
    return 2;
}
int main(int argc, char **argv)
{
    const char *project = NULL, *scene = NULL, *output = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
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
        if (!strcmp(argv[i], "--output") && !output) {
            output = argv[++i];
            continue;
        }
        return usage(argv[0]);
    }
    if (!project || !*project || !output || !*output) {
        return usage(argv[0]);
    }
    return authoring_project_compile(project, scene, output);
}
