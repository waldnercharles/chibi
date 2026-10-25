#ifndef CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_JIT_INTERNAL_H
#include "execution/definition.h"
#include "support/array.h"

/* Checked publication takes pointers to both scalar operands. Register-backed
 * operands therefore also need stack cells, before locals and parent frames. */
static inline int behavior_jit_stack_slots(const behavior_function *f, const int *registers)
{
    int count = 0;
    for (int slot = 0; slot < acount(f->slots); slot++) {
        if (!registers[slot]) {
            count = slot + 1;
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_COMPONENT_WRITE) {
            if (i->left >= count) {
                count = i->left + 1;
            }
            if (i->right >= count) {
                count = i->right + 1;
            }
        }
    }
    return count;
}
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

static inline bool behavior_jit_hardware_math(behavior_function_op op)
{
#if defined(SLJIT_CONFIG_ARM_64) && SLJIT_CONFIG_ARM_64
    return (op >= BF_SQRT && op <= BF_ROUND) || op == BF_FMA;
#else
    (void)op;
    return false;
#endif
}
static inline bool behavior_jit_pointer_comparison(
    const behavior_function *function,
    const behavior_function_instruction *instruction
)
{
    return (instruction->op == BF_EQ || instruction->op == BF_NE) &&
           function->slots[instruction->left] == PIN_POINTER;
}
static inline bool behavior_jit_call(behavior_function_op op)
{
    return behavior_function_call(op) && !behavior_jit_hardware_math(op);
}
static inline bool behavior_jit_integer_clobber(behavior_function_op op)
{
    return behavior_jit_call(op) || op == BF_ENTITY || op == BF_COMPONENT ||
           op == BF_COMPONENT_READ || op == BF_COLUMN || op == BF_COLUMN_TRY ||
           op == BF_ROW;
}

typedef struct
{
    struct sljit_compiler *compiler;
    const behavior_function *function;
    const int *registers, *local_registers, *variable_registers;
    int context, steps, variables, arguments, argument_count;
    int locals, parents;
    int system_stack;
    const behavior_function *system_root;
    struct sljit_label *system_loop;
    struct sljit_jump **system_exits;
    bool countdown, inspected, scoped, system_registers;
} behavior_machine;
void behavior_jit_system_enter(behavior_machine *machine);
void behavior_jit_system_begin(behavior_machine *machine);
void behavior_jit_system_next(behavior_machine *machine);
void behavior_jit_return(const behavior_machine *machine, int source, sljit_sw offset);
bool behavior_jit_watch(const behavior_machine *machine, const behavior_function_instruction *instruction);
bool behavior_jit_host_call(
    const behavior_machine *machine,
    const behavior_function_instruction *instruction,
    int destination,
    sljit_sw offset
);
void behavior_jit_wake(const behavior_machine *m, int frame, bool clear);
void behavior_jit_parameter(const behavior_machine *m, const behavior_function_instruction *i);
void behavior_jit_frame_steps(const behavior_machine *m, int frame);
void behavior_jit_suspend(const behavior_machine *m, int pc);
void behavior_jit_resume(const behavior_machine *m, struct sljit_jump **jumps, struct sljit_label **labels);
int *behavior_jit_registers(const behavior_function *f, bool inspected, int *saved_count, bool *invariants);
int behavior_jit_invariant_registers(
    const behavior_function *f,
    int *registers,
    const bool *invariants,
    int base,
    int scratch
);
int *behavior_jit_local_registers(
    const behavior_function *f,
    int base,
    int scratch,
    int *count,
    int *float_count
);
void behavior_jit_alias_locals(
    const behavior_function *f,
    int *registers,
    const int *locals,
    const bool *invariants
);
void behavior_jit_integer_registers(
    const behavior_function *f,
    bool inspected,
    int *registers,
    const bool *invariants
);
int *behavior_jit_variable_registers(const behavior_function *f, int base, int scratch, int *count);
void behavior_jit_alias_variables(const behavior_function *f, int *registers, const int *variables);
void behavior_jit_load_variables(const behavior_machine *m);
void behavior_jit_flush_variables(const behavior_machine *m);
void behavior_jit_coalesce_saves(const behavior_function *f, int *registers, const int *locals);
int behavior_jit_fused_branch(const behavior_machine *m, int pc, struct sljit_jump **jump);
int behavior_jit_field_branch(const behavior_machine *m, int pc, struct sljit_jump **jump);
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
