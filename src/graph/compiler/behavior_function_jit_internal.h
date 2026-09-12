#ifndef CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#include "graph/compiler/behavior_function_internal.h"
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

typedef struct
{
    struct sljit_compiler *compiler;
    const behavior_function *function;
    const int *registers, *local_registers;
    int context, steps, variables, arguments, argument_count;
    int locals, parents;
    bool countdown;
} behavior_machine;
void behavior_jit_suspend(const behavior_machine *m, int pc);
void behavior_jit_resume(const behavior_machine *m, struct sljit_jump **jumps, struct sljit_label **labels);
int *behavior_jit_registers(const behavior_function *f, bool integers, int *saved_count);
int *behavior_jit_local_registers(
    const behavior_function *f,
    int base,
    int scratch,
    int *count,
    int *float_count
);
void behavior_jit_alias_locals(const behavior_function *f, int *registers, const int *locals);
void behavior_jit_coalesce_saves(const behavior_function *f, int *registers, const int *locals);
bool behavior_jit_fused_branch(const behavior_machine *m, int pc, struct sljit_jump **jump);
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
bool behavior_jit_component(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
);
bool behavior_jit_memory(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
);
#endif
#endif
