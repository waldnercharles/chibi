#include "assets/project.h"
#include "authoring/project_compile.h"
#include "authoring/project_format.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <errno.h>
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
    fprintf(
        stderr,
        "Usage: %s --project-root PATH (--output FILE | --check | --test | "
        "--format [--check] | --docs) [--scene PATH]\n",
        program
    );
    return 2;
}
#ifdef __EMSCRIPTEN__
/* The browser-target compiler only prepares projects for export. */
static int test(const char *)
{
    fprintf(stderr, "This compiler prepares browser exports; it runs no tests\n");
    return 2;
}
static int format(const char *, bool)
{
    fprintf(stderr, "This compiler prepares browser exports; it formats nothing\n");
    return 2;
}
static int docs(const char *)
{
    fprintf(stderr, "This compiler prepares browser exports; it writes no documentation\n");
    return 2;
}
#else
/* A folder of the SDK's share/chibi beside this program's directory, such as
 * its core collection; NULL, having said why, when it has none. */
static char *shared(const char *name, const char *what)
{
    char *bin = file_executable_dir();
    size_t size = strlen(bin) + strlen(name) + sizeof "/../share/chibi/";
    char *installed = xmalloc(size);
    snprintf(installed, size, "%s/../share/chibi/%s", bin, name);
    char *folder = realpath(installed, NULL);
    if (!folder) {
        fprintf(stderr, "The SDK has no %s at %s: %s\n", what, installed, strerror(errno));
    }
    free(installed);
    free(bin);
    return folder;
}
static char *core_collection()
{
    return shared("core", "core collection");
}
/* Runs the tests of the packages below project. */
static int test(const char *project)
{
    char *root = realpath(project, NULL);
    if (!root) {
        fprintf(stderr, "%s: %s\n", project, strerror(errno));
        return 1;
    }
    char *core = core_collection();
    int status = core ? authoring_project_test(root, core) : 1;
    free(core);
    free(root);
    return status;
}
/* Writes the documentation site of the root at project with the SDK's site
 * generator. */
static int docs(const char *project)
{
    char *root = realpath(project, NULL);
    if (!root) {
        fprintf(stderr, "%s: %s\n", project, strerror(errno));
        return 1;
    }
    char *generator = shared("docs", "site generator");
    char *core = generator ? core_collection() : NULL;
    int status = core ? authoring_project_docs(root, generator, core) : 1;
    free(core);
    free(generator);
    free(root);
    return status;
}
/* Formats the project's assets with the composition chibi build uses; with
 * check only reports what differs. Fails for an older project, which it does
 * not migrate, an asset that differs under check, an invalid one or a failed
 * write. */
static int format(const char *project, bool check)
{
    char error[512] = { 0 };
    chibi_project read = { 0 };
    bool current = !chibi_project_read(&read, project, error, sizeof error);
    chibi_project_free(&read);
    authoring_application application;
    if (!current || !authoring_application_begin(&application, error, sizeof error)) {
        fprintf(stderr, "format: %s\n", error);
        return 1;
    }
    int differing = authoring_project_format(project, application.native, check);
    authoring_application_end(&application);
    return differing < 0 || (check && differing) ? 1 : 0;
}
#endif
/* --check prepares the project without migrating it or writing anything;
 * --test runs its package tests; --format rewrites its assets canonically, or
 * with --check reports the ones that are not; --docs writes its documentation
 * site. */
int main(int argc, char **argv)
{
    const char *project = NULL, *scene = NULL, *output = NULL;
    bool check = false, tests = false, formats = false, site = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--check") && !check) {
            check = true;
            continue;
        }
        if (!strcmp(argv[i], "--test") && !tests) {
            tests = true;
            continue;
        }
        if (!strcmp(argv[i], "--format") && !formats) {
            formats = true;
            continue;
        }
        if (!strcmp(argv[i], "--docs") && !site) {
            site = true;
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
        if (!strcmp(argv[i], "--output") && !output) {
            output = argv[++i];
            continue;
        }
        return usage(argv[0]);
    }
    int modes = (check && !formats) + tests + formats + site + (output != NULL);
    if (!project || !*project || modes != 1 || (output && !*output) ||
        ((tests || formats || site) && scene)) {
        return usage(argv[0]);
    }
    if (site) {
        return docs(project);
    }
    if (tests) {
        return test(project);
    }
    if (formats) {
        return format(project, check);
    }
    return authoring_project_compile(project, scene, output);
}
