#ifndef CHIBI_TEST_COMPILER_CACHE_H
#define CHIBI_TEST_COMPILER_CACHE_H

#include <stdlib.h>
#include <string.h>

#ifndef CHIBI_SOURCE_ROOT
#error "CHIBI_SOURCE_ROOT names the source tree"
#endif

/* Where a test keeps the compiler recovered from the seed and the sources on
 * disk, as `chibi build` keeps it: CHIBI_COMPILER_CACHE names the directory,
 * "off" keeps none, and else it is .chibi/compiler-cache in the source tree.
 * Without it every test process spends minutes recovering the same compiler. */
static inline const char *test_compiler_cache()
{
    const char *named = getenv("CHIBI_COMPILER_CACHE");
    if (named && !strcmp(named, "off")) {
        return NULL;
    }
    return named && *named ? named : CHIBI_SOURCE_ROOT "/.chibi/compiler-cache";
}

#endif
