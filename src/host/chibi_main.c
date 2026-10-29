#include "host/application.h"
#include "runtime/component_catalog.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The standard reflected value layouts supply native vector defaults. The
 * application capability selection excludes running-game services. */
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

static char *resource(const char *bin, const char *name)
{
    size_t size = strlen(bin) + strlen(name) + sizeof "/../share/chibi/";
    char *path = xmalloc(size);
    snprintf(path, size, "%s/../share/chibi/%s", bin, name);
    char *resolved = realpath(path, NULL);
    if (!resolved) {
        fprintf(stderr, "The SDK has no %s at %s: %s\n", name, path, strerror(errno));
    }
    free(path);
    return resolved;
}

int main(int argc, char **argv)
{
    char *bin = file_executable_dir();
    char *application = resource(bin, "apps/chibi");
    char *core = application ? resource(bin, "core") : NULL;
    int status = core
                   ? chibi_application_run(application, bin, core, argc, (const char *const *)argv)
                   : 1;
    free(core);
    free(application);
    free(bin);
    return status;
}
