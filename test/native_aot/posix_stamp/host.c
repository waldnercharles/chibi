#include "host/build_io.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    for (int index = 1; index < argc; index++) {
        char stamp[BUILD_IO_STAMP_SIZE];
        const char *error = build_io_stamp("/", argv[index], stamp);
        if (error) {
            fprintf(stderr, "%s: %s\n", argv[index], error);
            return 1;
        }
        if (puts(stamp) == EOF) {
            perror("POSIX stamp output");
            return 1;
        }
    }
    if (fflush(stdout) == EOF) {
        perror("POSIX stamp output");
        return 1;
    }
    return 0;
}
