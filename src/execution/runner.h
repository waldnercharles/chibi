#ifndef CHIBI_EXECUTION_RUNNER_H
#define CHIBI_EXECUTION_RUNNER_H

#include "execution/function.h"

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
