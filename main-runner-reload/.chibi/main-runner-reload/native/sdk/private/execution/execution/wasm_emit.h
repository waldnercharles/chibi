#ifndef CHIBI_EXECUTION_WASM_EMIT_H
#define CHIBI_EXECUTION_WASM_EMIT_H

#include "execution/function.h"

/* The emitter runs in the engine's address space: embedded pointer constants
 * and structure offsets belong to that engine and its shared linear memory.
 * The caller owns the returned malloc allocation. */
bool execution_wasm_emit(
    const behavior_function *function,
    bool inspected,
    unsigned char **bytes,
    size_t *size,
    char *error,
    size_t error_size
);

#endif
