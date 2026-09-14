#include "ecs/execution.h"
#include "graph/compiler/behavior_function_jit_internal.h"
#if CHIBI_HAVE_SJLIT
#include <stddef.h>

static void bind(struct sljit_compiler *c, struct sljit_jump *jump)
{
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(jump, label);
    }
}
static void slot(const behavior_machine *m, int index, int reg)
{
    int source = m->registers[index];
    sljit_emit_op1(
        m->compiler,
        behavior_jit_integer_move(m->function->slots[index]),
        reg,
        0,
        source ? source : SLJIT_MEM1(SLJIT_SP),
        source ? 0 : index * sizeof(double)
    );
}
static void entity(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    slot(m, i->left, SLJIT_R0);
    slot(m, i->right, SLJIT_R1);
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, 32);
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R1, 0, SLJIT_R1, 0);
    struct sljit_jump *invalid[5];
    invalid[0] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    invalid[1] = sljit_emit_cmp(
        c,
        SLJIT_GREATER_EQUAL | SLJIT_32,
        SLJIT_R1,
        0,
        SLJIT_MEM1(SLJIT_R0),
        offsetof(ecs_execution_view, slot_count)
    );
    invalid[2] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R0), offsetof(ecs_execution_view, occupied));
    sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R3, 0, SLJIT_MEM2(SLJIT_R3, SLJIT_R1), 0);
    invalid[3] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R0), offsetof(ecs_execution_view, generations));
    invalid[4] = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_MEM2(SLJIT_R3, SLJIT_R1), 2);
    sljit_emit_op1(c, SLJIT_MOV_U32, dest, offset, SLJIT_R1, 0);
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    for (int j = 0; j < 5; j++) {
        bind(c, invalid[j]);
    }
    sljit_emit_op1(c, SLJIT_MOV_U32, dest, offset, SLJIT_IMM, 0);
    bind(c, done);
}
/* R0..R4 are scratch for checked ECS lookup. The allocator spills integer
 * values that survive this instruction; floating point values stay live. */
static void component(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    slot(m, i->left, SLJIT_R0);
    bool metadata = i->op != BF_COMPONENT;
    struct sljit_jump *empty[6] = { 0 };
    if (!metadata) {
        slot(m, i->right, SLJIT_R1);
        empty[0] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    }
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(m->context), offsetof(behavior_function_context, columns));
    struct sljit_jump *unbound = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R2), i->component * sizeof(void *));
    empty[4] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
    struct sljit_jump *bound = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, unbound);
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R4, 0, SLJIT_MEM1(SLJIT_R0), offsetof(ecs_execution_view, capacity));
    empty[1] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R4, 0, SLJIT_IMM, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R4, 0, SLJIT_R4, 0, SLJIT_IMM, 1);
    uint64_t hash = i->value.u64 * UINT64_C(11400714819323198485);
    sljit_emit_op2(c, SLJIT_AND, SLJIT_R3, 0, SLJIT_R4, 0, SLJIT_IMM, (sljit_sw)(hash ^ (hash >> 32)));
    struct sljit_label *probe = sljit_emit_label(c);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), offsetof(ecs_execution_view, lookup));
    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM2(SLJIT_R2, SLJIT_R3), 2);
    empty[2] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
    sljit_emit_op2(c, SLJIT_SUB, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1);
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, sizeof(ecs_execution_column));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), offsetof(ecs_execution_view, columns));
    struct sljit_jump *found = sljit_emit_cmp(
        c,
        SLJIT_EQUAL,
        SLJIT_MEM1(SLJIT_R2),
        offsetof(ecs_execution_column, id),
        SLJIT_IMM,
        (sljit_sw)i->value.u64
    );
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, 1);
    sljit_emit_op2(c, SLJIT_AND, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_R4, 0);
    struct sljit_jump *again = sljit_emit_jump(c, SLJIT_JUMP);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(again, probe);
    }
    bind(c, found);
    bind(c, bound);
    if (!metadata) {
        sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), offsetof(ecs_execution_column, occupancy));
        sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R3, 0, SLJIT_R1, 0, SLJIT_IMM, 6);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_R0, SLJIT_R3), 3);
        sljit_emit_op2(c, SLJIT_AND, SLJIT_R3, 0, SLJIT_R1, 0, SLJIT_IMM, 63);
        sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R3, 0);
        sljit_emit_op2(c, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        empty[3] = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    }
    if (i->type == PIN_BOOL) {
        sljit_emit_op1(c, SLJIT_MOV_U8, dest, offset, SLJIT_IMM, 1);
    } else {
        struct sljit_jump *wrong = sljit_emit_cmp(
            c,
            SLJIT_NOT_EQUAL,
            SLJIT_MEM1(SLJIT_R2),
            offsetof(ecs_execution_column, layout),
            SLJIT_IMM,
            (sljit_sw)i->layout
        );
        struct sljit_jump *valid = sljit_emit_cmp(
            c,
            SLJIT_EQUAL,
            SLJIT_MEM1(SLJIT_R2),
            offsetof(ecs_execution_column, stride),
            SLJIT_IMM,
            (sljit_sw)i->offset
        );
        bind(c, wrong);
        if (i->op == BF_COLUMN_TRY) {
            empty[5] = sljit_emit_jump(c, SLJIT_JUMP);
        } else {
            behavior_jit_failure(m, i, i->target);
        }
        bind(c, valid);
        if (metadata) {
            sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_R2, 0);
        } else {
            sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), offsetof(ecs_execution_column, data));
            sljit_emit_op2(
                c,
                SLJIT_MUL,
                SLJIT_R1,
                0,
                SLJIT_R1,
                0,
                SLJIT_IMM,
                (sljit_sw)i->offset
            );
            sljit_emit_op2(c, SLJIT_ADD, dest, offset, SLJIT_R0, 0, SLJIT_R1, 0);
        }
    }
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    for (int j = 0; j < 6; j++) {
        if (empty[j]) {
            bind(c, empty[j]);
        }
    }
    sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, SLJIT_IMM, 0);
    bind(c, done);
}
static void row(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    slot(m, i->left, SLJIT_R0);
    slot(m, i->right, SLJIT_R1);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_R0, 0);
    struct sljit_jump *invalid = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), offsetof(ecs_execution_column, occupancy));
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R3, 0, SLJIT_R1, 0, SLJIT_IMM, 6);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_R0, SLJIT_R3), 3);
    sljit_emit_op2(c, SLJIT_AND, SLJIT_R3, 0, SLJIT_R1, 0, SLJIT_IMM, 63);
    sljit_emit_op2(c, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R3, 0);
    sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
    struct sljit_jump *absent = sljit_emit_jump(c, SLJIT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R2), offsetof(ecs_execution_column, data));
    sljit_emit_op2(
        c,
        SLJIT_MUL,
        SLJIT_R1,
        0,
        SLJIT_R1,
        0,
        SLJIT_IMM,
        (sljit_sw)i->offset
    );
    sljit_emit_op2(c, SLJIT_ADD, dest, offset, SLJIT_R0, 0, SLJIT_R1, 0);
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, invalid);
    bind(c, absent);
    sljit_emit_op1(c, SLJIT_MOV_P, dest, offset, SLJIT_IMM, 0);
    bind(c, done);
}
bool behavior_jit_component(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    switch (i->op) {
        case BF_CONTEXT:
            if (i->type == PIN_F32 || i->type == PIN_F64) {
                sljit_emit_fop1(
                    c,
                    i->type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
                    dest,
                    offset,
                    SLJIT_MEM1(m->context),
                    i->offset
                );
            } else {
                sljit_emit_op1(
                    c,
                    behavior_jit_integer_move(i->type),
                    dest,
                    offset,
                    SLJIT_MEM1(m->context),
                    i->offset
                );
            }
            return true;
        case BF_OWNER:
        case BF_WORLD:
            sljit_emit_op1(
                c,
                SLJIT_MOV,
                dest,
                offset,
                SLJIT_MEM1(m->context),
                i->op == BF_OWNER ? offsetof(behavior_function_context, owner)
                : i->target ? offsetof(behavior_function_context, binding_world)
                            : offsetof(behavior_function_context, world)
            );
            return true;
        case BF_ENTITY: entity(m, i, dest, offset); return true;
        case BF_ROW: row(m, i, dest, offset); return true;
        case BF_COLUMN:
        case BF_COLUMN_TRY:
        case BF_COMPONENT: component(m, i, dest, offset); return true;
        case BF_ASSERT: {
            slot(m, i->left, SLJIT_R3);
            struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0);
            behavior_jit_failure(m, i, i->target);
            bind(c, ok);
            return true;
        }
        case BF_LOAD:
        case BF_WRITE: {
            bool write = i->op == BF_WRITE;
            slot(m, write ? i->right : i->left, SLJIT_R3);
            int source = write ? m->registers[i->left] : SLJIT_MEM1(SLJIT_R3);
            sljit_sw address = write ? (source ? 0 : i->left * sizeof(double))
                                     : (sljit_sw)i->offset;
            if (write && !source) {
                source = SLJIT_MEM1(SLJIT_SP);
            }
            if (write) {
                dest = SLJIT_MEM1(SLJIT_R3);
                offset = (sljit_sw)i->offset;
            }
            if (i->type == PIN_F32 || i->type == PIN_F64) {
                sljit_emit_fop1(
                    c,
                    i->type == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
                    dest,
                    offset,
                    source,
                    address
                );
            } else {
                sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, source, address);
            }
            return true;
        }
        default: return false;
    }
}
#endif
