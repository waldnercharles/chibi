#ifndef CHIBI_TEST_MEMCMP_NONNULL_H
#define CHIBI_TEST_MEMCMP_NONNULL_H

#include <string.h>

/* Test instrumentation only. Darwin's declaration omits the nonnull
 * contract that glibc supplies for these pointer arguments. */
#ifdef __cplusplus
extern "C" {
#endif
int memcmp(const void *, const void *, size_t) __attribute__((nonnull(1, 2)));
#ifdef __cplusplus
}
#endif

#endif
