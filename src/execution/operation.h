#ifndef CHIBI_EXECUTION_OPERATION_H
#define CHIBI_EXECUTION_OPERATION_H

#include "capabilities/catalog.h"
#include "execution/instruction.h"

/* The same mapping generates native bindings and verifies lowerer inputs.
 * Zero is not an operation; unknown capabilities use their generic binding. */
static inline behavior_function_op behavior_operation_instruction(script_operation operation)
{
    switch (operation) {
#define INSTRUCTION(NAME, OP, VERSION, EFFECTS, GUARANTEES)                    \
    case SCRIPT_OPERATION_##NAME: return BF_##OP;
        SCRIPT_ENGINE_OPERATIONS(INSTRUCTION)
#undef INSTRUCTION
        default: return 0;
    }
}
#endif
