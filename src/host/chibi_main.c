#include "host/application.h"
#include "support/file_io.h"
#include "support/xalloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *bin = file_executable_dir();
    size_t size = strlen(bin) + sizeof "/../share/chibi/core";
    char *path = xmalloc(size);
    snprintf(path, size, "%s/../share/chibi/core", bin);
    char *core = realpath(path, NULL);
    int status = 1;
    if (!core) {
        fprintf(stderr, "The SDK has no core at %s: %s\n", path, strerror(errno));
    } else {
        status = chibi_application_run(
            embedded_programs,
            embedded_program_count,
            bin,
            core,
            argc,
            (const char *const *)argv
        );
    }
    free(core);
    free(path);
    free(bin);
    return status;
}
