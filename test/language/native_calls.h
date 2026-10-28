#ifndef CHIBI_TEST_LANGUAGE_NATIVE_CALLS_H
#define CHIBI_TEST_LANGUAGE_NATIVE_CALLS_H
#include "execution/native.h"

/* A library of pure functions, at libraries/Native.chibi, that a Probe
 * behavior calls on each Tick. */
extern const char NATIVE_CALLS_LIBRARY[];
/* Its Small, which calls Wide, a function of more parameters than an entry
 * takes: Small gives 19. */
#define NATIVE_CALLS_WIDE                                                      \
    "    Wide :: (a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: " \
    "int, i: int) -> int {\n"                                                  \
    "        return a * 10 + i;\n"                                             \
    "    }\n"                                                                  \
    "    Small :: () -> int { return Wide(1, 2, 3, 4, 5, 6, 7, 8, 9); }\n"
/* Whether a Probe compiled with NATIVE_CALLS_LIBRARY reaches the VM's states
 * on both backends with `manifest` bound, every call running natively. */
bool native_calls_agree(const execution_native_manifest *manifest);
#endif
