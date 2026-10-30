#include "authoring/project_compile.h"
#include "runtime/component_catalog.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Explicit final application composition, shared with the prepared player. */
int type_catalog_pack_count()
{
    return 1;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index == 0);
    (void)index;
    return TYPE_PACK;
}
static int usage(const char *program, FILE *stream)
{
    fprintf(stream, "Usage: %s --project-root PATH --test\nGame projects are archived.\n", program);
    return stream == stdout ? 0 : 2;
}
#ifdef __EMSCRIPTEN__
/* The browser-target compiler only prepares projects for export. */
static int test(const char *)
{
    fprintf(stderr, "This compiler prepares browser exports; it runs no tests\n");
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
#endif
/* chibi-compile runs a folder's package tests; the game-project compile path
 * is archived. */
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        return usage(argv[0], stdout);
    }
    if (argc != 4 || strcmp(argv[1], "--project-root") || strcmp(argv[3], "--test")) {
        return usage(argv[0], stderr);
    }
    return test(argv[2]);
}
