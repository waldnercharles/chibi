#include "graph/compiler/behavior_function_jit_internal.h"
#if CHIBI_HAVE_SJLIT
#include <stddef.h>

int behavior_jit_integer_move(pin_type type)
{
    switch (type) {
        case PIN_BOOL:
        case PIN_U8: return SLJIT_MOV_U8;
        case PIN_I8: return SLJIT_MOV_S8;
        case PIN_U16: return SLJIT_MOV_U16;
        case PIN_I16: return SLJIT_MOV_S16;
        case PIN_I32:
        case PIN_ENUM: return SLJIT_MOV32;
        case PIN_U32: return SLJIT_MOV_U32;
        case PIN_I64:
        case PIN_U64:
        case PIN_HANDLE: return SLJIT_MOV;
        default: assert(false); return 0;
    }
}
static void bind(struct sljit_compiler *c, struct sljit_jump *jump)
{
    struct sljit_label *label = sljit_emit_label(c);
    if (sljit_get_compiler_error(c) == SLJIT_SUCCESS) {
        sljit_set_label(jump, label);
    }
}
static void load(const behavior_machine *m, int slot, int reg)
{
    int source = m->registers[slot];
    sljit_emit_op1(
        m->compiler,
        behavior_jit_integer_move(m->function->slots[slot]),
        reg,
        0,
        source ? source : SLJIT_MEM1(SLJIT_SP),
        source ? 0 : slot * sizeof(double)
    );
}
static void argument(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    struct sljit_jump *missing = sljit_emit_cmp(
        c,
        SLJIT_SIG_LESS_EQUAL | SLJIT_32,
        m->argument_count,
        0,
        SLJIT_IMM,
        i->left
    );
    struct sljit_jump *wrong = sljit_emit_cmp(
        c,
        SLJIT_NOT_EQUAL | SLJIT_32,
        SLJIT_MEM1(m->arguments),
        i->left * sizeof(pin_value) + offsetof(pin_value, type),
        SLJIT_IMM,
        i->type
    );
    sljit_emit_op1(
        c,
        behavior_jit_integer_move(i->type),
        dest,
        offset,
        SLJIT_MEM1(m->arguments),
        i->left * sizeof(pin_value) + offsetof(pin_value, d)
    );
    struct sljit_jump *done = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, missing);
    bind(c, wrong);
    sljit_emit_op1(
        c,
        behavior_jit_integer_move(i->type),
        dest,
        offset,
        SLJIT_IMM,
        (sljit_sw)behavior_scalar_bits(i->value)
    );
    bind(c, done);
}
static void cast(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    pin_type from = m->function->slots[i->left];
    if (i->type == PIN_F64) {
        load(m, i->left, SLJIT_R0);
        sljit_emit_fop1(
            c,
            from == PIN_I64 ? SLJIT_CONV_F64_FROM_SW : SLJIT_CONV_F64_FROM_S32,
            SLJIT_FR0,
            0,
            SLJIT_R0,
            0
        );
        sljit_emit_fop1(c, SLJIT_MOV_F64, dest, offset, SLJIT_FR0, 0);
        return;
    }
    if (from == PIN_I32) {
        load(m, i->left, SLJIT_R0);
        sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R0, 0, SLJIT_R0, 0);
        sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, SLJIT_R0, 0);
        return;
    }
    int source = m->registers[i->left];
    sljit_emit_fop1(
        c,
        from == PIN_F32 ? SLJIT_CONV_F64_FROM_F32 : SLJIT_MOV_F64,
        SLJIT_FR0,
        0,
        source ? source : SLJIT_MEM1(SLJIT_SP),
        source ? 0 : i->left * sizeof(double)
    );
    static const double bounds[][2] = { { 0, UINT8_MAX },
                                        { INT32_MIN, INT32_MAX },
                                        { -0x1p63, 0x1p63 } };
    int which = i->type == PIN_U8 ? 0 : i->type == PIN_I32 ? 1 : 2;
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
    struct sljit_jump *nan = sljit_emit_fcmp(c, SLJIT_UNORDERED, SLJIT_FR0, 0, SLJIT_FR0, 0);
    sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&bounds[which][0]);
    struct sljit_jump *low = sljit_emit_fcmp(c, SLJIT_ORDERED_LESS_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
    sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&bounds[which][1]);
    struct sljit_jump *high = sljit_emit_fcmp(c, SLJIT_ORDERED_GREATER_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
    sljit_emit_fop1(
        c,
        i->type == PIN_I64 ? SLJIT_CONV_SW_FROM_F64 : SLJIT_CONV_S32_FROM_F64,
        SLJIT_R0,
        0,
        SLJIT_FR0,
        0
    );
    struct sljit_jump *normal = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, low);
    sljit_emit_op1(
        c,
        SLJIT_MOV,
        SLJIT_R0,
        0,
        SLJIT_IMM,
        i->type == PIN_U8    ? 0
        : i->type == PIN_I32 ? INT32_MIN
                             : (sljit_sw)INT64_MIN
    );
    struct sljit_jump *bounded = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, high);
    sljit_emit_op1(
        c,
        SLJIT_MOV,
        SLJIT_R0,
        0,
        SLJIT_IMM,
        i->type == PIN_U8    ? UINT8_MAX
        : i->type == PIN_I32 ? INT32_MAX
                             : (sljit_sw)INT64_MAX
    );
    bind(c, nan);
    bind(c, normal);
    bind(c, bounded);
    sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, SLJIT_R0, 0);
}
static void arithmetic(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    pin_type type = m->function->slots[i->left];
    int flags = behavior_scalar_size(type) == 8 ? 0 : SLJIT_32;
    if (i->op == BF_ADD || i->op == BF_SUB || i->op == BF_MUL) {
        int left = m->registers[i->left], right = m->registers[i->right];
        sljit_emit_op2(
            c,
            (i->op == BF_ADD   ? SLJIT_ADD
             : i->op == BF_SUB ? SLJIT_SUB
                               : SLJIT_MUL) |
                flags,
            dest,
            offset,
            left ? left : SLJIT_MEM1(SLJIT_SP),
            left ? 0 : i->left * sizeof(double),
            right ? right : SLJIT_MEM1(SLJIT_SP),
            right ? 0 : i->right * sizeof(double)
        );
        return;
    }
    load(m, i->left, SLJIT_R0);
    if (i->op != BF_NEG && i->op != BF_ABS) {
        load(m, i->right, SLJIT_R1);
    }
    if (i->op == BF_DIV || i->op == BF_MOD) {
        struct sljit_jump *zero = sljit_emit_cmp(c, SLJIT_EQUAL | flags, SLJIT_R1, 0, SLJIT_IMM, 0);
        struct sljit_jump *
            valid = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | flags, SLJIT_R1, 0, SLJIT_IMM, -1);
        struct sljit_jump *not_min = sljit_emit_cmp(
            c,
            SLJIT_NOT_EQUAL | flags,
            SLJIT_R0,
            0,
            SLJIT_IMM,
            type == PIN_I32 ? INT32_MIN : (sljit_sw)INT64_MIN
        );
        bind(c, zero);
        behavior_jit_failure(m, i, BEHAVIOR_FUNCTION_DATA);
        bind(c, valid);
        bind(c, not_min);
        sljit_emit_op0(c, (i->op == BF_MOD || i->target >= 0 ? SLJIT_DIVMOD_SW : SLJIT_DIV_SW) | flags);
        if (i->target >= 0) {
            /* The secondary definition currently spills; the GP interval
             * allocator assigns the primary result independently. */
            sljit_emit_op1(
                c,
                behavior_jit_integer_move(type),
                SLJIT_MEM1(SLJIT_SP),
                i->target * sizeof(double),
                i->op == BF_MOD ? SLJIT_R0 : SLJIT_R1,
                0
            );
        }
        sljit_emit_op1(c, behavior_jit_integer_move(type), dest, offset, i->op == BF_MOD ? SLJIT_R1 : SLJIT_R0, 0);
        return;
    }
    if (i->op == BF_FLAGS || (i->op >= BF_LT && i->op <= BF_NE)) {
        int condition = i->op == BF_FLAGS
                          ? SLJIT_NOT_ZERO
                          : (int[]){ SLJIT_SIG_LESS,    SLJIT_SIG_LESS_EQUAL,
                                     SLJIT_SIG_GREATER, SLJIT_SIG_GREATER_EQUAL,
                                     SLJIT_EQUAL,       SLJIT_NOT_EQUAL }[i->op - BF_LT];
        int set = condition == SLJIT_EQUAL || condition == SLJIT_NOT_EQUAL
                    ? SLJIT_SET_Z
                    : SLJIT_SET(condition & ~1);
        sljit_emit_op2u(c, (i->op == BF_FLAGS ? SLJIT_AND : SLJIT_SUB) | flags | set, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op_flags(c, SLJIT_MOV32, dest, offset, condition);
        return;
    }
    if (i->op == BF_NEG || i->op == BF_ABS) {
        struct sljit_jump *positive = i->op == BF_ABS
                                        ? sljit_emit_cmp(
                                              c,
                                              SLJIT_SIG_GREATER_EQUAL | flags,
                                              SLJIT_R0,
                                              0,
                                              SLJIT_IMM,
                                              0
                                          )
                                        : NULL;
        sljit_emit_op2(c, SLJIT_SUB | flags, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0);
        if (positive) {
            bind(c, positive);
        }
    } else {
        assert(i->op == BF_MIN || i->op == BF_MAX);
        struct sljit_jump *left = sljit_emit_cmp(
            c,
            (i->op == BF_MIN ? SLJIT_SIG_LESS : SLJIT_SIG_GREATER) | flags,
            SLJIT_R0,
            0,
            SLJIT_R1,
            0
        );
        sljit_emit_op1(c, behavior_jit_integer_move(type), SLJIT_R0, 0, SLJIT_R1, 0);
        bind(c, left);
    }
    sljit_emit_op1(c, behavior_jit_integer_move(type), dest, offset, SLJIT_R0, 0);
}
bool behavior_jit_integer(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int dest,
    sljit_sw offset
)
{
    struct sljit_compiler *c = m->compiler;
    bool integer = behavior_scalar_integer(i->type) || i->type == PIN_BOOL ||
                   i->type == PIN_HANDLE;
    if (i->op == BF_CAST) {
        cast(m, i, dest, offset);
        return true;
    }
    if (i->op >= BF_ADD && i->op <= BF_NE &&
        behavior_scalar_integer(m->function->slots[i->left])) {
        arithmetic(m, i, dest, offset);
        return true;
    }
    if (!integer) {
        return false;
    }
    switch (i->op) {
        case BF_CONSTANT:
            sljit_emit_op1(
                c,
                behavior_jit_integer_move(i->type),
                dest,
                offset,
                SLJIT_IMM,
                (sljit_sw)behavior_scalar_bits(i->value)
            );
            return true;
        case BF_LOCAL:
        case BF_VARIABLE:
            if (i->op == BF_LOCAL && m->local_registers[i->left]) {
                if (dest == m->local_registers[i->left]) {
                    return true;
                }
                sljit_emit_op1(
                    c,
                    behavior_jit_integer_move(i->type),
                    dest,
                    offset,
                    m->local_registers[i->left],
                    0
                );
                return true;
            }
            sljit_emit_op1(
                c,
                behavior_jit_integer_move(i->type),
                dest,
                offset,
                SLJIT_MEM1(i->op == BF_LOCAL ? SLJIT_SP : m->variables),
                i->op == BF_LOCAL
                    ? m->locals + i->left * sizeof(double)
                    : i->left * sizeof(pin_value) + offsetof(pin_value, d)
            );
            return true;
        case BF_ARGUMENT: argument(m, i, dest, offset); return true;
        case BF_SAVE:
        case BF_STORE:
        case BF_RESULT: {
            if (i->op == BF_SAVE && m->local_registers[i->target]) {
                if (m->registers[i->left] == m->local_registers[i->target]) {
                    return true;
                }
                load(m, i->left, m->local_registers[i->target]);
                return true;
            }
            int base = m->variables;
            sljit_sw address = i->target * sizeof(pin_value) + offsetof(pin_value, d);
            if (i->op == BF_SAVE) {
                base = SLJIT_SP;
                address = m->locals + i->target * sizeof(double);
            }
            if (i->op == BF_RESULT) {
                base = SLJIT_R3;
                sljit_emit_op1(
                    c,
                    SLJIT_MOV_P,
                    base,
                    0,
                    SLJIT_MEM1(m->context),
                    offsetof(behavior_function_context, results)
                );
            }
            int source = m->registers[i->left];
            sljit_emit_op1(
                c,
                behavior_jit_integer_move(i->type),
                SLJIT_MEM1(base),
                address,
                source ? source : SLJIT_MEM1(SLJIT_SP),
                source ? 0 : i->left * sizeof(double)
            );
            return true;
        }
        default: return false;
    }
}
#endif
