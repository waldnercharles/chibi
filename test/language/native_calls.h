#ifndef CHIBI_TEST_LANGUAGE_NATIVE_CALLS_H
#define CHIBI_TEST_LANGUAGE_NATIVE_CALLS_H
#include "execution/native.h"

/* A library of pure functions, at libraries/Native.chibi, that a Probe
 * behavior calls on each Tick. */
extern const char NATIVE_CALLS_LIBRARY[];
/* Whether a Probe compiled with NATIVE_CALLS_LIBRARY reaches the VM's states
 * on both backends with `manifest` bound, every call running natively. */
bool native_calls_agree(const execution_native_manifest *manifest);
#endif
