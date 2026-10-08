#include "execution/jit_internal.h"
#include "execution/update_jit.h"
#include "support/array.h"

#include <assert.h>
#include <stddef.h>

#if CHIBI_HAVE_SJLIT
/* Simple bodies reserve saved registers for the row loop. Bodies with calls
 * use four stack slots, leaving those registers available to the body. */
static int slot(const behavior_machine *m, int index)
{
    return m->system_stack + index * sizeof(sljit_sw);
}
void behavior_jit_system_enter(behavior_machine *m)
{
    struct sljit_compiler *c = m->compiler;
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_R3), 0, SLJIT_IMM, 0);
    for (int i = 0; i < 4; i++) {
        sljit_emit_op1(
            c,
            SLJIT_MOV_P,
            m->system_registers ? SLJIT_S(i) : SLJIT_MEM1(SLJIT_SP),
            m->system_registers ? 0 : slot(m, i),
            SLJIT_R(i),
            0
        );
    }
}
void behavior_jit_system_begin(behavior_machine *m)
{
    struct sljit_compiler *c = m->compiler;
    m->system_loop = sljit_emit_label(c);
    if (!m->system_registers) {
        for (int i = 0; i < 4; i++) {
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S(i), 0, SLJIT_MEM1(SLJIT_SP), slot(m, i));
        }
    }
    struct sljit_jump *stop[20];
    int count = behavior_update_emit_guard(c, stop);
    stop[count++] = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL,
        SLJIT_MEM1(SLJIT_S5),
        offsetof(execution_update_binding, function),
        SLJIT_IMM,
        (sljit_sw)m->system_root
    );
    const behavior_function *f = m->function;
    if (f->root_steps) {
        unsigned required = f->root_steps > f->child_budget ? f->root_steps
                                                            : f->child_budget;
        stop[count++] = sljit_emit_cmp(
            c,
            SLJIT_LESS,
            SLJIT_MEM1(SLJIT_S5),
            offsetof(behavior_function_context, budget),
            SLJIT_IMM,
            required
        );
    }
    if (f->depth_limit) {
        stop[count++] = sljit_emit_cmp(
            c,
            SLJIT_GREATER_EQUAL | SLJIT_32,
            SLJIT_MEM1(SLJIT_S5),
            offsetof(behavior_function_context, call_depth),
            SLJIT_IMM,
            f->depth_limit
        );
    }
    struct sljit_jump *ready = sljit_emit_jump(c, SLJIT_JUMP);
    struct sljit_label *done = sljit_emit_label(c);
    sljit_emit_return(c, SLJIT_MOV_P, SLJIT_S0, 0);
    assert(count <= (int)(sizeof stop / sizeof *stop));
    for (int i = 0; i < count; i++) {
        sljit_set_label(stop[i], done);
    }
    sljit_set_label(ready, sljit_emit_label(c));
    behavior_update_emit_bind(c);
}
void behavior_jit_system_next(behavior_machine *m)
{
    struct sljit_compiler *c = m->compiler;
    int row = m->system_registers ? SLJIT_S0 : SLJIT_MEM1(SLJIT_SP);
    int offset = m->system_registers ? 0 : slot(m, 0);
    sljit_emit_op2(c, SLJIT_ADD, row, offset, row, offset, SLJIT_IMM, sizeof(execution_update_row));
    apush(m->system_exits, sljit_emit_jump(c, SLJIT_JUMP));
}
void behavior_jit_return(const behavior_machine *m, int source, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    if (!m->system_root) {
        sljit_emit_return(c, SLJIT_MOV32, source, offset);
        return;
    }
    if (m->system_registers) {
        sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S3), 0, source, offset);
        sljit_emit_return(c, SLJIT_MOV_P, SLJIT_S0, 0);
        return;
    }
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R0, 0, source, offset);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_SP), slot(m, 3));
    sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_R1), 0, SLJIT_R0, 0);
    sljit_emit_return(c, SLJIT_MOV_P, SLJIT_MEM1(SLJIT_SP), slot(m, 0));
}
#endif
