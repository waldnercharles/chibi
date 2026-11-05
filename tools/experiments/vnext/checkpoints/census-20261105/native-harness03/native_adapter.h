#ifndef CENSUS_NATIVE_ADAPTER_H
#define CENSUS_NATIVE_ADAPTER_H
#include <stdint.h>
#include <stdbool.h>
#include "foreign.h"
/* Test-only boundary. Every entry is verified against the produced module's
 * logical signature/export before passing its address. Calls are synchronous;
 * caller retains the JIT owner until return. No address is retained. */
const ChibiVnextForeignBinding *chibi_census_test_bindings(void);
uint64_t chibi_census_test_binding_count(void);
/* Fixed 47-byte Census fixture plus capability/report checks; no suite I/O. */
int32_t chibi_census_test_lifetime(const void *census, const void *read,
    const void *length, const void *merge, const void *sub,
    const void *loops, const void *report);
/* One complete external corpus pass, two guard placements per case. */
int32_t chibi_census_test_corpus(const void *census);
#endif
