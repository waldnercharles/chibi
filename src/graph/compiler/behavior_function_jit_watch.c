#include "graph/compiler/behavior_function_jit_internal.h"

#if CHIBI_HAVE_SJLIT
static void bind(struct sljit_compiler *c, struct sljit_jump *jump)
{
    sljit_set_label(jump, sljit_emit_label(c));
}
static int boolean(const behavior_machine *m, int slot)
{
    int reg = m->registers[slot];
    if (reg) {
        return reg;
    }
    sljit_emit_op1(m->compiler, SLJIT_MOV_U8, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_SP), slot * sizeof(double));
    return SLJIT_R3;
}
bool behavior_jit_watch(const behavior_machine *m, const behavior_function_instruction *i)
{
    if (!behavior_function_observer(i->op)) {
        return false;
    }
    if (!m->inspected) {
        return true;
    }
    struct sljit_compiler *c = m->compiler;
    size_t callback = i->op == BF_TRACE
                        ? offsetof(behavior_function_context, trace)
                        : offsetof(behavior_function_context, watch);
    struct sljit_jump *skip = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_MEM1(m->context), callback, SLJIT_IMM, 0);
    if (i->op == BF_WATCH_BEGIN) {
        const pin_value *initial = &m->function->watches[i->target].initial;
        for (size_t offset = 0; offset < sizeof *initial; offset += sizeof(sljit_sw)) {
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_MEM0(), (sljit_sw)((const char *)initial + offset));
            sljit_emit_op1(
                c,
                SLJIT_MOV,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, watched) + offset,
                SLJIT_R3,
                0
            );
        }
    } else if (i->op == BF_WATCH_VALUE) {
        int reg = m->registers[i->left], source = reg ? reg : SLJIT_MEM1(SLJIT_SP);
        sljit_sw offset = reg ? 0 : i->left * sizeof(double);
        int base = m->context;
        sljit_sw target = offsetof(behavior_function_context, watched) + i->offset;
        if (i->right) {
            sljit_emit_op1(
                c,
                SLJIT_MOV_P,
                SLJIT_R3,
                0,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, watch_storage)
            );
            base = SLJIT_R3;
            target = i->offset;
        }
        if (i->type == PIN_F32 || i->type == PIN_F64) {
            sljit_emit_fop1(
                c,
                i->type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
                SLJIT_MEM1(base),
                target,
                source,
                offset
            );
        } else {
            sljit_emit_op1(c, behavior_jit_integer_move(i->type), SLJIT_MEM1(base), target, source, offset);
        }
    } else if (i->op == BF_WATCH_PRESENT) {
        int reg = boolean(m, i->left);
        struct sljit_jump *absent = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, reg, 0, SLJIT_IMM, 0);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R3,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, watch_storage)
        );
        struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
        bind(c, absent);
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_IMM, 0);
        bind(c, done);
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, watched) + offsetof(pin_value, ptr),
            SLJIT_R3,
            0
        );
    } else {
        struct sljit_jump *absent = NULL;
        if (i->op == BF_WATCH_END) {
            int reg = boolean(m, i->left);
            absent = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, reg, 0, SLJIT_IMM, 0);
        }
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(m->context),
            offsetof(behavior_function_context, inspection_user)
        );
        if (i->op == BF_WATCH_END || i->op == BF_WATCH_RESET) {
            sljit_emit_op2(
                c,
                SLJIT_ADD,
                SLJIT_R1,
                0,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, watches),
                SLJIT_IMM,
                i->target * sizeof(behavior_function_watch)
            );
            if (i->op == BF_WATCH_RESET) {
                sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_IMM, 0);
            } else {
                sljit_emit_op2(
                    c,
                    SLJIT_ADD,
                    SLJIT_R2,
                    0,
                    m->context,
                    0,
                    SLJIT_IMM,
                    offsetof(behavior_function_context, watched)
                );
            }
            sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3V(P, P, P), SLJIT_MEM1(m->context), callback);
        } else {
            sljit_emit_op2(
                c,
                SLJIT_ADD,
                SLJIT_R1,
                0,
                SLJIT_MEM1(m->context),
                offsetof(behavior_function_context, edges),
                SLJIT_IMM,
                i->target * sizeof(behavior_function_edge)
            );
            if (m->countdown) {
                sljit_emit_op2(
                    c,
                    SLJIT_SUB,
                    SLJIT_R2,
                    0,
                    SLJIT_MEM1(m->context),
                    offsetof(behavior_function_context, budget),
                    m->steps,
                    0
                );
            } else {
                sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, m->steps, 0);
            }
            sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3V(P, P, W), SLJIT_MEM1(m->context), callback);
        }
        if (absent) {
            bind(c, absent);
        }
    }
    bind(c, skip);
    return true;
}
#endif
