#ifndef CHIBI_TEST_COMPILER_CACHE_H
#define CHIBI_TEST_COMPILER_CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where a test keeps the compiler recovered from the seed and the sources on
 * disk, as `chibi build` keeps it: CHIBI_COMPILER_CACHE names the directory,
 * "off" keeps none, and else it is chibi/compiler-cache in $XDG_CACHE_HOME or
 * ~/.cache, shared by every checkout. Without it every test process spends
 * minutes recovering the same compiler. The returned path is static. */
static inline const char *test_compiler_cache()
{
    const char *named = getenv("CHIBI_COMPILER_CACHE");
    if (named && !strcmp(named, "off")) {
        return NULL;
    }
    if (named && *named) {
        return named;
    }
    static char directory[4096];
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(directory, sizeof directory, "%s/chibi/compiler-cache", xdg);
        return directory;
    }
    if (home && *home) {
        snprintf(directory, sizeof directory, "%s/.cache/chibi/compiler-cache", home);
        return directory;
    }
    return NULL;
}

#endif
