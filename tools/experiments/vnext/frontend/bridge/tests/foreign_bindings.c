/* SPDX-License-Identifier: GPL-3.0-only */
/* Test-only metadata for the unmodified slice_probe.c implementation. */
#include <stdbool.h>
#include <stdint.h>
#include "foreign.h"

#if !defined(__linux__) || !defined(__x86_64__)
#error "this fixture is admitted only for Linux x86-64 System V"
#endif

extern int64_t chibi_slice_echo(int64_t value);
extern bool chibi_slice_not(bool value);
extern int64_t chibi_slice_clear(void);
extern int64_t chibi_slice_mark(int64_t value);
extern int64_t chibi_slice_trace(void);
extern int64_t chibi_slice_weighted8(int64_t a, int64_t b, int64_t c, int64_t d,
                                   int64_t e, int64_t f, int64_t g, int64_t h);

/* Independent fixture expectations for Model v3's scalar IDs, not a second
 * generic schema. A signature mismatch must reject before any callable cast. */
static const uint32_t i64_parameters[8] = {2, 2, 2, 2, 2, 2, 2, 2};
static const uint32_t bool_parameter[1] = {1};
static const uint8_t library_name[] = "libslice_probe.a";

/* GCC's function-to-object pointer conversion is target-specific here. Both
 * pointer representations and the descriptor layout are pinned for this ABI.
 * The original code and immutable metadata stay live for the caller process. */
_Static_assert(sizeof(void (*)(void)) == sizeof(void *), "System V code pointer width");
#define BIND(symbol, parameters, count, result) \
    {library_name, sizeof(library_name) - 1, \
     (const uint8_t *)#symbol, sizeof(#symbol) - 1, \
     parameters, count, result, (const void *)(symbol)}
static const ChibiVnextForeignBinding bindings[] = {
    BIND(chibi_slice_echo, i64_parameters, 1, 2),
    BIND(chibi_slice_not, bool_parameter, 1, 1),
    BIND(chibi_slice_clear, NULL, 0, 2),
    BIND(chibi_slice_mark, i64_parameters, 1, 2),
    BIND(chibi_slice_trace, NULL, 0, 2),
    BIND(chibi_slice_weighted8, i64_parameters, 8, 2),
};
#undef BIND

const ChibiVnextForeignBinding *chibi_vnext_test_bindings(void) { return bindings; }
uint64_t chibi_vnext_test_binding_count(void) { return sizeof(bindings) / sizeof(bindings[0]); }
