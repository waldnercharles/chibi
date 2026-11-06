#ifndef CHIBI_EXECUTION_MACHINE_H
#define CHIBI_EXECUTION_MACHINE_H

#include "execution/function.h"
#include "execution/instruction.h"

#include <limits.h>

enum
{
    EXECUTION_CONTINUE = INT_MAX
};

typedef struct
{
    const behavior_function *function;
    behavior_function_context *context;
    pin_value *values;
    uintptr_t *parents;
    uintptr_t steps;
    int pc;
    const behavior_function_instruction *instructions;
} execution_machine;

/* Canonical bodies retain source PCs. Only the VM requests compact runtime code
 * and the guarded fast body; its branch PCs belong to that selected body. */
void execution_machine_begin(
    execution_machine *,
    const behavior_function *,
    behavior_function_context *,
    bool compact
);
/* Executes one instruction, publishes the next PC and returns either
 * EXECUTION_CONTINUE or a terminal FLOW status. */
int execution_machine_step(execution_machine *, int pc);
/* Publish a pre-instruction scope failure through the canonical VM path. */
int execution_machine_fail(execution_machine *, const char *message);
void execution_machine_end(execution_machine *, int result);

#endif
