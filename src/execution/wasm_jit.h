#ifndef CHIBI_EXECUTION_WASM_JIT_H
#define CHIBI_EXECUTION_WASM_JIT_H

#include "execution/function.h"

#include <stdint.h>

typedef struct execution_wasm_jit execution_wasm_jit;
typedef struct
{
    /* Successful per-thread installations and actual generated entry calls. */
    uint64_t compilations, executions, compile_failures;
    /* Table registrations retained across all threads. */
    uint64_t live_entries;
} execution_wasm_jit_stats;

bool behavior_function_compile_wasm(behavior_function *);
int behavior_function_run_wasm(const behavior_function *, behavior_function_context *);
void execution_wasm_jit_free(execution_wasm_jit *);
void execution_wasm_jit_get_stats(execution_wasm_jit_stats *);
/* Last loader failure on this thread; overwritten by the next loader attempt. */
const char *execution_wasm_jit_error();

#endif
