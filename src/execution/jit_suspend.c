#include "execution/jit_internal.h"
#include "execution/status.h"
#include "support/array.h"

#include <assert.h>
#include <stddef.h>
#if CHIBI_HAVE_SJLIT
/* R3 and FR0/FR1 are emitter temporaries, never allocated live values. */
static void activation(const behavior_machine *m)
{
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, activation)
    );
}
void behavior_jit_parameter(const behavior_machine *m, const behavior_function_instruction *i)
{
    struct sljit_compiler *c = m->compiler;
    activation(m);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_function_activation, values));
    int reg = m->registers[i->left], source = reg ? reg : SLJIT_MEM1(SLJIT_SP);
    sljit_sw offset = reg ? 0 : i->left * sizeof(double);
    sljit_sw target = (acount(m->function->slots) + i->target) * sizeof(uint64_t);
    if (i->type == PIN_F32 || i->type == PIN_F64) {
        sljit_emit_fop1(
            c,
            i->type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
            SLJIT_MEM1(SLJIT_R3),
            target,
            source,
            offset
        );
        return;
    }
    sljit_emit_op1(c, behavior_jit_integer_move(i->type), SLJIT_MEM1(SLJIT_R3), target, source, offset);
}
void behavior_jit_frame_steps(const behavior_machine *m, int frame)
{
    struct sljit_compiler *c = m->compiler;
    activation(m);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R3), offsetof(behavior_function_activation, values));
    sljit_sw offset = (acount(m->function->slots) + acount(m->function->locals) + frame) *
                      sizeof(uint64_t);
    if (m->countdown) {
        sljit_emit_op2(
            c,
            SLJIT_SUB,
            SLJIT_MEM1(SLJIT_R3),
            offset,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, budget),
            m->steps,
            0
        );
        return;
    }
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_R3), offset, m->steps, 0);
}
void behavior_jit_wake(const behavior_machine *m, int frame, bool clear)
{
    activation(m);
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(SLJIT_R3),
        offsetof(behavior_function_activation, wake_times)
    );
    if (clear) {
        sljit_emit_op1(m->compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_R3), frame * sizeof(double), SLJIT_IMM, 0);
        if (sizeof(sljit_sw) == 4) {
            sljit_emit_op1(
                m->compiler,
                SLJIT_MOV,
                SLJIT_MEM1(SLJIT_R3),
                frame * sizeof(double) + 4,
                SLJIT_IMM,
                0
            );
        }
        return;
    }
    sljit_emit_fop1(
        m->compiler,
        SLJIT_MOV_F64,
        SLJIT_MEM1(SLJIT_R3),
        frame * sizeof(double),
        SLJIT_MEM1(m->context),
        offsetof(behavior_function_context, wake_time)
    );
}
static void scalar(const behavior_machine *m, int value, bool restore)
{
    const behavior_function *f = m->function;
    int slots = acount(f->slots), local = value - slots;
    pin_type type = local < 0 ? f->slots[value] : f->locals[local].initial.type;
    int reg = local < 0 ? m->registers[value] : m->local_registers[local];
    int place = reg ? reg : SLJIT_MEM1(SLJIT_SP);
    sljit_sw offset = reg       ? 0
                    : local < 0 ? value * sizeof(double)
                                : m->locals + local * sizeof(double);
    int op = type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64;
    if (type == PIN_F32 || type == PIN_F64) {
        sljit_emit_fop1(
            m->compiler,
            op,
            restore ? place : SLJIT_MEM1(SLJIT_R3),
            restore ? offset : value * sizeof(uint64_t),
            restore ? SLJIT_MEM1(SLJIT_R3) : place,
            restore ? value * sizeof(uint64_t) : offset
        );
        return;
    }
    sljit_emit_op1(
        m->compiler,
        behavior_jit_integer_move(type),
        restore ? place : SLJIT_MEM1(SLJIT_R3),
        restore ? offset : value * sizeof(uint64_t),
        restore ? SLJIT_MEM1(SLJIT_R3) : place,
        restore ? value * sizeof(uint64_t) : offset
    );
}
static void values(const behavior_machine *m, const behavior_function_suspension *s, bool restore)
{
    const behavior_function *f = m->function;
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV_P,
        SLJIT_R3,
        0,
        SLJIT_MEM1(SLJIT_R3),
        offsetof(behavior_function_activation, values)
    );
    for (int j = 0; j < s->value_count; j++) {
        scalar(m, s->values[j], restore);
    }
    int base = acount(f->slots) + acount(f->locals);
    int frame = f->frames[f->instructions[s->pc].frame].source.parent;
    for (; frame >= 0; frame = f->frames[frame].source.parent) {
        sljit_sw saved = (base + frame) * sizeof(uint64_t);
        sljit_sw stack = m->parents + frame * sizeof(sljit_sw);
        int dest = restore ? SLJIT_MEM1(SLJIT_SP) : SLJIT_MEM1(SLJIT_R3);
        int source = restore ? SLJIT_MEM1(SLJIT_R3) : SLJIT_MEM1(SLJIT_SP);
        if (m->countdown) {
            sljit_emit_op2(
                m->compiler,
                SLJIT_SUB,
                dest,
                restore ? stack : saved,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, budget),
                source,
                restore ? saved : stack
            );
        } else {
            sljit_emit_op1(m->compiler, SLJIT_MOV, dest, restore ? stack : saved, source, restore ? saved : stack);
        }
    }
}
static void active_steps(const behavior_machine *m, bool restore)
{
    int dest = restore ? m->steps : SLJIT_MEM1(SLJIT_R3);
    int source = restore ? SLJIT_MEM1(SLJIT_R3) : m->steps;
    sljit_sw offset = offsetof(behavior_function_activation, steps);
    if (m->countdown) {
        sljit_emit_op2(
            m->compiler,
            SLJIT_SUB,
            dest,
            restore ? 0 : offset,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, budget),
            source,
            restore ? offset : 0
        );
    } else {
        sljit_emit_op1(m->compiler, SLJIT_MOV, dest, restore ? 0 : offset, source, restore ? offset : 0);
    }
}
void behavior_jit_suspend(const behavior_machine *m, int pc)
{
    const behavior_function *f = m->function;
    const behavior_function_instruction *i = &f->instructions[pc];
    const behavior_function_suspension *s = NULL;
    for (int j = 0; j < acount(f->suspensions); j++) {
        if (f->suspensions[j].pc == pc) {
            s = &f->suspensions[j];
            break;
        }
    }
    assert(s);
    activation(m);
    active_steps(m, false);
#define FIELD(name, value, op)                                                 \
    sljit_emit_op1(                                                            \
        m->compiler,                                                           \
        op,                                                                    \
        SLJIT_MEM1(SLJIT_R3),                                                  \
        offsetof(behavior_function_activation, name),                          \
        SLJIT_IMM,                                                             \
        value                                                                  \
    )
    FIELD(pc, pc + 1, SLJIT_MOV32);
    FIELD(frame, i->frame, SLJIT_MOV32);
    FIELD(source, i->source, SLJIT_MOV32);
    FIELD(stack, i->stack, SLJIT_MOV32);
    FIELD(node_id, i->node_id, SLJIT_MOV);
#undef FIELD
    values(m, s, false);
    if (i->frame) {
        sljit_emit_op1(m->compiler, SLJIT_MOV, m->steps, 0, SLJIT_MEM1(SLJIT_SP), m->parents);
    }
    if (m->countdown) {
        sljit_emit_op2(
            m->compiler,
            SLJIT_SUB,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, steps),
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, budget),
            m->steps,
            0
        );
    } else {
        sljit_emit_op1(
            m->compiler,
            SLJIT_MOV,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, steps),
            m->steps,
            0
        );
    }
    sljit_emit_return(
        m->compiler,
        SLJIT_MOV32,
        SLJIT_IMM,
        (i->op == BF_POINT || i->op == BF_SOURCE_POINT) ? FLOW_PAUSED_RESULT
        : i->op == BF_INVOKE ? BEHAVIOR_FUNCTION_CALL_RESULT
                             : FLOW_SUSPENDED_RESULT
    );
}
void behavior_jit_resume(const behavior_machine *m, struct sljit_jump **jumps, struct sljit_label **labels)
{
    const behavior_function *f = m->function;
    if (!acount(f->suspensions)) {
        return;
    }
    activation(m);
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV32,
        SLJIT_R4,
        0,
        SLJIT_MEM1(SLJIT_R3),
        offsetof(behavior_function_activation, pc)
    );
    struct sljit_jump *
        fresh = sljit_emit_cmp(m->compiler, SLJIT_EQUAL | SLJIT_32, SLJIT_R4, 0, SLJIT_IMM, 0);
    sljit_emit_op1(
        m->compiler,
        SLJIT_MOV_P,
        SLJIT_R0,
        0,
        SLJIT_IMM,
        (sljit_sw)(m->inspected ? f->artifact->inspection_addresses
                                : f->artifact->resume_addresses)
    );
    sljit_emit_ijump(
        m->compiler,
        SLJIT_JUMP,
        SLJIT_MEM2(SLJIT_R0, SLJIT_R4),
        sizeof(uintptr_t) == 8 ? 3 : 2
    );
    for (int j = 0; j < acount(f->suspensions); j++) {
        const behavior_function_suspension *s = &f->suspensions[j];
        labels[j] = sljit_emit_label(m->compiler);
        active_steps(m, true);
        sljit_emit_op1(
            m->compiler,
            SLJIT_MOV32,
            SLJIT_MEM1(SLJIT_R3),
            offsetof(behavior_function_activation, pc),
            SLJIT_IMM,
            0
        );
        values(m, s, true);
        jumps[s->pc] = sljit_emit_jump(m->compiler, SLJIT_JUMP);
    }
    sljit_set_label(fresh, sljit_emit_label(m->compiler));
}
#endif
