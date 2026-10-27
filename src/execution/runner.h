#ifndef CHIBI_EXECUTION_RUNNER_H
#define CHIBI_EXECUTION_RUNNER_H

#include "execution/function.h"

typedef enum
{
    EXECUTION_RUN_BYTECODE,
    EXECUTION_RUN_SLJIT,
    EXECUTION_RUN_INSPECTED_SLJIT,
    EXECUTION_RUN_WASM,
    EXECUTION_RUN_INSPECTED_WASM,
    EXECUTION_RUN_INSPECTION_BYTECODE_FALLBACK,
    EXECUTION_RUN_SIZE_BYTECODE_FALLBACK,
    EXECUTION_RUN_NATIVE,
} execution_run_event;
/* Private recording hook at actual backend entry sites only. */
void behavior_run_record(execution_run_event event);

/* Shared invocation lifecycle for interpreted and compiled execution. */
int behavior_function_run_once(
    const behavior_function *function,
    behavior_function_context *context,
    bool compiled
);
int behavior_function_run_calls(
    const behavior_function *function,
    behavior_function_context *context,
    bool compiled
);
/* Executes the bytecode loop inside the shared invocation lifecycle. */
int behavior_function_run_interpreted(const behavior_function *function, behavior_function_context *context);

#endif
