#ifndef CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#include "graph/compiler/behavior_function_internal.h"
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

typedef struct
{
    struct sljit_compiler *compiler;
    const behavior_function *function;
    const int *registers;
    int context, steps, variables, arguments, argument_count;
    int locals, parents;
} behavior_machine;
void behavior_jit_failure(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    behavior_function_failure reason
);
bool behavior_jit_integer(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int destination,
    sljit_sw offset
);
int behavior_jit_integer_move(pin_type type);
int behavior_jit_result_fields(const behavior_machine *machine, int pc);
bool behavior_jit_memory(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
);
#endif
#endif
