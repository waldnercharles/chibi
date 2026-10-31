#include "authoring/package_compile.h"
#include "host/application.h"

#include <stdio.h>
#include <stdlib.h>

/* chibi-program-request PACKAGE CORE OUTPUT writes the request the launcher's
 * package compile sends for the application package in PACKAGE, with the core
 * collection CORE, to OUTPUT. The SDK build compiles it once on the prepared
 * compiler and embeds the closure in the launcher, so the launcher compiles
 * nothing when it starts. It links what the launcher links, so the request
 * names the launcher's catalog. PACKAGE and CORE are absolute. */
int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: chibi-program-request PACKAGE CORE OUTPUT\n");
        return 2;
    }
    char error[512];
    native_capabilities *native = chibi_application_catalog_new(error, sizeof error);
    if (!native) {
        fprintf(stderr, "Cannot prepare application capabilities: %s\n", error);
        return 1;
    }
    const source_package_collection core = { "core", argv[2] };
    const source_package_disk_options disk = { argv[1], &core, 1 };
    source_package_snapshot_options inputs = { 0 };
    source_package_load_error load = { 0 };
    int status = 1;
    if (!source_package_files_load(&disk, &inputs, &load)) {
        fprintf(stderr, "%s: %s\n", load.path, load.message);
    } else {
        package_compile_error failure;
        size_t length = 0;
        char *request = package_compile_request(&inputs, native, &length, &failure);
        FILE *output = request ? fopen(argv[3], "wb") : NULL;
        if (!request) {
            fprintf(stderr, "%s: %s\n", argv[1], failure.message);
        } else if (!output) {
            perror(argv[3]);
        } else {
            bool written = fwrite(request, 1, length, output) == length;
            status = fclose(output) || !written;
            if (status) {
                fprintf(stderr, "%s: cannot write the request\n", argv[3]);
            }
        }
        free(request);
        source_package_files_release(&inputs);
    }
    native_capabilities_free(native);
    return status;
}
