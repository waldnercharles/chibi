#include "execution/integer_report.h"
#include "execution/jit_internal.h"
#if CHIBI_HAVE_SJLIT
#include "support/array.h"
#include "support/xalloc.h"
#include "types/pointer.h"

#include <stddef.h>
#include <stdlib.h>

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
        case PIN_UNION:
        case PIN_TASK:
        case PIN_HANDLE: return SLJIT_MOV;
        case PIN_POINTER:
        case PIN_NAME:
        case PIN_STR: return SLJIT_MOV_P;
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
/* A development build marks the event's source site; values are unchanged. */
static bool reports(const behavior_machine *m, const behavior_function_instruction *i)
{
    return behavior_integer_site(m->function, i, 0) &&
           behavior_integer_reportable(m->function, i);
}
static void mark(const behavior_machine *m, const behavior_function_instruction *i, int kind)
{
    atomic_uchar *site = behavior_integer_site(m->function, i, kind);
    sljit_emit_op1(m->compiler, SLJIT_MOV_U8, SLJIT_MEM0(), (sljit_sw)site, SLJIT_IMM, 1);
}
/* Marks kind unless the jump that skips it was taken. */
static void mark_unless(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    int kind,
    struct sljit_jump *fits
)
{
    mark(m, i, kind);
    bind(m->compiler, fits);
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
static void variables(const behavior_machine *m, bool store)
{
    if (!m->variable_registers) {
        return;
    }
    int count = 0;
    for (int pc = 0; pc < acount(m->function->instructions); pc++) {
        const behavior_function_instruction *i = &m->function->instructions[pc];
        if (i->op == BF_VARIABLE && i->left >= count) {
            count = i->left + 1;
        }
    }
    bool *seen = xcalloc((size_t)(count + 1), sizeof *seen);
    for (int pc = 0; pc < acount(m->function->instructions); pc++) {
        const behavior_function_instruction *i = &m->function->instructions[pc];
        if (i->op != BF_VARIABLE || seen[i->left] || !m->variable_registers[i->left]) {
            continue;
        }
        seen[i->left] = true;
        int reg = m->variable_registers[i->left];
        sljit_sw offset = i->left * sizeof(pin_value) + offsetof(pin_value, d);
        sljit_emit_op1(
            m->compiler,
            behavior_jit_integer_move(i->type),
            store ? SLJIT_MEM1(m->variables) : reg,
            store ? offset : 0,
            store ? reg : SLJIT_MEM1(m->variables),
            store ? 0 : offset
        );
    }
    free(seen);
}
void behavior_jit_load_variables(const behavior_machine *m)
{
    variables(m, false);
}
void behavior_jit_flush_variables(const behavior_machine *m)
{
    variables(m, true);
}
/* Sign- or zero-extends a slot to the full word. */
static int widening_move(pin_type type)
{
    switch (type) {
        case PIN_I8: return SLJIT_MOV_S8;
        case PIN_I16: return SLJIT_MOV_S16;
        case PIN_I32: return SLJIT_MOV_S32;
        case PIN_I64:
        case PIN_U64: return SLJIT_MOV;
        default: return behavior_jit_integer_move(type);
    }
}
static void load_word(const behavior_machine *m, int slot, int reg)
{
    int source = m->registers[slot];
    sljit_emit_op1(
        m->compiler,
        widening_move(m->function->slots[slot]),
        reg,
        0,
        source ? source : SLJIT_MEM1(SLJIT_SP),
        source ? 0 : slot * sizeof(double)
    );
}
/* Wrapping keeps the low bits; the other modes clamp a value outside the
 * destination range. */
static void integer_cast(const behavior_machine *m, const behavior_function_instruction *i)
{
    struct sljit_compiler *c = m->compiler;
    pin_type from = m->function->slots[i->left];
    bool sign = behavior_scalar_signed(from);
    int64_t low = behavior_scalar_integer_low(i->type);
    uint64_t high = behavior_scalar_integer_high(i->type);
    load_word(m, i->left, SLJIT_R0);
    if (i->target == BEHAVIOR_CAST_WRAP) {
        return;
    }
    struct sljit_jump *below = NULL, *above = NULL;
    if (sign && low > behavior_scalar_integer_low(from)) {
        below = sljit_emit_cmp(c, SLJIT_SIG_LESS, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)low);
    }
    if (high < behavior_scalar_integer_high(from)) {
        above = sljit_emit_cmp(
            c,
            sign ? SLJIT_SIG_GREATER : SLJIT_GREATER,
            SLJIT_R0,
            0,
            SLJIT_IMM,
            (sljit_sw)high
        );
    }
    if (!below && !above) {
        return;
    }
    struct sljit_jump *fits = sljit_emit_jump(c, SLJIT_JUMP), *clamped = NULL;
    bool check = reports(m, i);
    if (below) {
        bind(c, below);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)low);
        if (check) {
            mark(m, i, EXECUTION_INTEGER_CLAMP);
        }
        clamped = above ? sljit_emit_jump(c, SLJIT_JUMP) : NULL;
    }
    if (above) {
        bind(c, above);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)high);
        if (check) {
            mark(m, i, EXECUTION_INTEGER_CLAMP);
        }
    }
    if (clamped) {
        bind(c, clamped);
    }
    bind(c, fits);
}
/* Rounds toward zero, saturates and maps NaN to zero, like the VM. */
static void float_cast(const behavior_machine *m, const behavior_function_instruction *i)
{
    struct sljit_compiler *c = m->compiler;
    pin_type from = m->function->slots[i->left];
    static const double bounds[PIN_U64 + 1][2] = {
        [PIN_I8] = { INT8_MIN, INT8_MAX },    [PIN_U8] = { 0, UINT8_MAX },
        [PIN_I16] = { INT16_MIN, INT16_MAX }, [PIN_U16] = { 0, UINT16_MAX },
        [PIN_I32] = { INT32_MIN, INT32_MAX }, [PIN_U32] = { 0, UINT32_MAX },
        [PIN_I64] = { -0x1p63, 0x1p63 },      [PIN_U64] = { 0, 0x1p64 },
    };
    static const double sign_bit = 0x1p63;
    int source = m->registers[i->left];
    sljit_emit_fop1(
        c,
        from == PIN_F32 ? SLJIT_CONV_F64_FROM_F32 : SLJIT_MOV_F64,
        SLJIT_FR0,
        0,
        source ? source : SLJIT_MEM1(SLJIT_SP),
        source ? 0 : i->left * sizeof(double)
    );
    if (reports(m, i)) {
        const double *fit = behavior_integer_cast_bounds(i->type);
        struct sljit_jump *unordered = sljit_emit_fcmp(c, SLJIT_UNORDERED, SLJIT_FR0, 0, SLJIT_FR0, 0);
        sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&fit[0]);
        struct sljit_jump
            *under = sljit_emit_fcmp(c, SLJIT_ORDERED_LESS_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
        sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&fit[1]);
        struct sljit_jump *
            over = sljit_emit_fcmp(c, SLJIT_ORDERED_GREATER_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
        struct sljit_jump *fits = sljit_emit_jump(c, SLJIT_JUMP);
        bind(c, unordered);
        bind(c, under);
        bind(c, over);
        mark_unless(m, i, EXECUTION_INTEGER_CLAMP, fits);
    }
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
    struct sljit_jump *nan = sljit_emit_fcmp(c, SLJIT_UNORDERED, SLJIT_FR0, 0, SLJIT_FR0, 0);
    sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&bounds[i->type][0]);
    struct sljit_jump *low = sljit_emit_fcmp(c, SLJIT_ORDERED_LESS_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
    sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&bounds[i->type][1]);
    struct sljit_jump *high = sljit_emit_fcmp(c, SLJIT_ORDERED_GREATER_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
    struct sljit_jump *upper = NULL;
    if (i->type == PIN_U64) {
        /* Word conversion is signed; shift the upper half below 2^63 first. */
        sljit_emit_fop1(c, SLJIT_MOV_F64, SLJIT_FR1, 0, SLJIT_MEM0(), (sljit_sw)&sign_bit);
        upper = sljit_emit_fcmp(c, SLJIT_ORDERED_GREATER_EQUAL, SLJIT_FR0, 0, SLJIT_FR1, 0);
    }
    sljit_emit_fop1(c, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_FR0, 0);
    struct sljit_jump *normal = sljit_emit_jump(c, SLJIT_JUMP), *shifted = NULL;
    if (upper) {
        bind(c, upper);
        sljit_emit_fop2(c, SLJIT_SUB_F64, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        sljit_emit_fop1(c, SLJIT_CONV_SW_FROM_F64, SLJIT_R0, 0, SLJIT_FR0, 0);
        sljit_emit_op2(c, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)INT64_MIN);
        shifted = sljit_emit_jump(c, SLJIT_JUMP);
    }
    bind(c, low);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)behavior_scalar_integer_low(i->type));
    struct sljit_jump *bounded = sljit_emit_jump(c, SLJIT_JUMP);
    bind(c, high);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)behavior_scalar_integer_high(i->type));
    bind(c, nan);
    bind(c, normal);
    bind(c, bounded);
    if (shifted) {
        bind(c, shifted);
    }
}
static void cast(const behavior_machine *m, const behavior_function_instruction *i, int dest, sljit_sw offset)
{
    struct sljit_compiler *c = m->compiler;
    pin_type from = m->function->slots[i->left];
    if (from == i->type && (from == PIN_F32 || from == PIN_F64)) {
        int source = m->registers[i->left];
        sljit_emit_fop1(
            c,
            from == PIN_F32 ? SLJIT_MOV_F32 : SLJIT_MOV_F64,
            dest,
            offset,
            source ? source : SLJIT_MEM1(SLJIT_SP),
            source ? 0 : i->left * sizeof(double)
        );
        return;
    }
    if (i->type == PIN_F64) {
        load_word(m, i->left, SLJIT_R0);
        sljit_emit_fop1(
            c,
            from == PIN_U64 ? SLJIT_CONV_F64_FROM_UW : SLJIT_CONV_F64_FROM_SW,
            SLJIT_FR0,
            0,
            SLJIT_R0,
            0
        );
        sljit_emit_fop1(c, SLJIT_MOV_F64, dest, offset, SLJIT_FR0, 0);
        return;
    }
    if (from == PIN_F32 || from == PIN_F64) {
        float_cast(m, i);
    } else {
        integer_cast(m, i);
    }
    sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, SLJIT_R0, 0);
}
/* Negating the signed minimum in R0 wraps to itself. */
static void minimum_overflows(const behavior_machine *m, const behavior_function_instruction *i, pin_type type)
{
    int flags = behavior_scalar_size(type) == 8 ? 0 : SLJIT_32;
    sljit_sw low = (sljit_sw)behavior_scalar_integer_low(type);
    mark_unless(
        m,
        i,
        EXECUTION_INTEGER_OVERFLOW,
        sljit_emit_cmp(m->compiler, SLJIT_NOT_EQUAL | flags, SLJIT_R0, 0, SLJIT_IMM, low)
    );
}
static int operation(behavior_function_op op)
{
    return op == BF_ADD ? SLJIT_ADD : op == BF_SUB ? SLJIT_SUB : SLJIT_MUL;
}
/* Computes R0 op R1 into R0 and marks a result that wrapped. Narrow operands
 * are extended, so their 32-bit result is exact; it wraps when truncating
 * changes it. Wide operations test the hardware flags, and an unsigned
 * multiply its high bits. */
static void checked_arithmetic(
    const behavior_machine *m,
    const behavior_function_instruction *i,
    pin_type type
)
{
    struct sljit_compiler *c = m->compiler;
    size_t size = behavior_scalar_size(type);
    bool sign = behavior_scalar_signed(type);
    int flags = size == 8 ? 0 : SLJIT_32;
    struct sljit_jump *fits;
    if (size < 4) {
        sljit_emit_op2(c, operation(i->op) | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(c, behavior_jit_integer_move(type), SLJIT_R1, 0, SLJIT_R0, 0);
        fits = sljit_emit_cmp(c, SLJIT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_R1, 0);
    } else if (i->op == BF_MUL && !sign && size == 4) {
        /* Both operands are zero-extended words. */
        sljit_emit_op2(c, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        fits = sljit_emit_cmp(c, SLJIT_LESS_EQUAL, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)UINT32_MAX);
    } else if (i->op == BF_MUL && !sign) {
        sljit_emit_op0(c, SLJIT_LMUL_UW);
        fits = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    } else {
        sljit_emit_op2(
            c,
            operation(i->op) | flags | (sign ? SLJIT_SET_OVERFLOW : SLJIT_SET_CARRY),
            SLJIT_R0,
            0,
            SLJIT_R0,
            0,
            SLJIT_R1,
            0
        );
        fits = sljit_emit_jump(c, sign ? SLJIT_NOT_OVERFLOW : SLJIT_NOT_CARRY);
    }
    mark_unless(m, i, EXECUTION_INTEGER_OVERFLOW, fits);
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
    bool signed_type = behavior_scalar_signed(type);
    bool check = reports(m, i);
    if (i->op == BF_ADD || i->op == BF_SUB || i->op == BF_MUL) {
        load(m, i->left, SLJIT_R0);
        load(m, i->right, SLJIT_R1);
        if (check) {
            checked_arithmetic(m, i, type);
        } else {
            sljit_emit_op2(c, operation(i->op) | flags, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        }
        sljit_emit_op1(c, behavior_jit_integer_move(type), dest, offset, SLJIT_R0, 0);
        return;
    }
    load(m, i->left, SLJIT_R0);
    if (i->op != BF_NEG && i->op != BF_ABS) {
        load(m, i->right, SLJIT_R1);
    }
    if (i->op == BF_DIV || i->op == BF_MOD) {
        /* x / 0 is 0 with remainder x, and x / -1 negates with remainder 0,
         * so the signed minimum wraps to itself. Neither reaches the hardware
         * divide, which would fault on both. */
        struct sljit_jump *zero = sljit_emit_cmp(c, SLJIT_EQUAL | flags, SLJIT_R1, 0, SLJIT_IMM, 0);
        struct sljit_jump *minus = signed_type
                                     ? sljit_emit_cmp(c, SLJIT_EQUAL | flags, SLJIT_R1, 0, SLJIT_IMM, -1)
                                     : NULL;
        bool modulo = i->op == BF_MOD || i->target >= 0;
        sljit_emit_op0(
            c,
            (signed_type ? (modulo ? SLJIT_DIVMOD_SW : SLJIT_DIV_SW)
                         : (modulo ? SLJIT_DIVMOD_UW : SLJIT_DIV_UW)) |
                flags
        );
        struct sljit_jump *divided = sljit_emit_jump(c, SLJIT_JUMP), *negated = NULL;
        bind(c, zero);
        if (check) {
            mark(m, i, EXECUTION_INTEGER_ZERO_DIVISOR);
        }
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_R0, 0);
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
        if (minus) {
            negated = sljit_emit_jump(c, SLJIT_JUMP);
            bind(c, minus);
            if (check && (i->op == BF_DIV || i->target >= 0)) {
                minimum_overflows(m, i, type);
            }
            sljit_emit_op2(c, SLJIT_SUB | flags, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0);
            sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, 0);
            bind(c, negated);
        }
        bind(c, divided);
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
        int condition = i->op == BF_FLAGS ? SLJIT_NOT_ZERO
                      : signed_type
                          ? (int[]){ SLJIT_SIG_LESS,    SLJIT_SIG_LESS_EQUAL,
                                     SLJIT_SIG_GREATER, SLJIT_SIG_GREATER_EQUAL,
                                     SLJIT_EQUAL,       SLJIT_NOT_EQUAL }[i->op - BF_LT]
                          : (int[]){ SLJIT_LESS,    SLJIT_LESS_EQUAL,
                                     SLJIT_GREATER, SLJIT_GREATER_EQUAL,
                                     SLJIT_EQUAL,   SLJIT_NOT_EQUAL }[i->op - BF_LT];
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
                                              (signed_type ? SLJIT_SIG_GREATER_EQUAL
                                                           : SLJIT_GREATER_EQUAL) |
                                                  flags,
                                              SLJIT_R0,
                                              0,
                                              SLJIT_IMM,
                                              0
                                          )
                                        : NULL;
        if (check && signed_type) {
            minimum_overflows(m, i, type);
        } else if (check && i->op == BF_NEG) {
            /* Negating any unsigned value but zero wraps. */
            mark_unless(
                m,
                i,
                EXECUTION_INTEGER_OVERFLOW,
                sljit_emit_cmp(c, SLJIT_EQUAL | flags, SLJIT_R0, 0, SLJIT_IMM, 0)
            );
        }
        sljit_emit_op2(c, SLJIT_SUB | flags, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0);
        if (positive) {
            bind(c, positive);
        }
    } else {
        assert(i->op == BF_MIN || i->op == BF_MAX);
        struct sljit_jump *left = sljit_emit_cmp(
            c,
            (signed_type ? (i->op == BF_MIN ? SLJIT_SIG_LESS : SLJIT_SIG_GREATER)
                         : (i->op == BF_MIN ? SLJIT_LESS : SLJIT_GREATER)) |
                flags,
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
    if (behavior_jit_pointer_comparison(m->function, i)) {
        load(m, i->left, SLJIT_R0);
        load(m, i->right, SLJIT_R1);
        sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS2(32, P, P), SLJIT_IMM, (sljit_sw)pin_pointer_equal);
        if (i->op == BF_NE) {
            sljit_emit_op2(c, SLJIT_XOR | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        }
        /* A bool return defines only its low byte. */
        sljit_emit_op1(c, SLJIT_MOV_U8, dest, offset, SLJIT_R0, 0);
        return true;
    }
    if (behavior_function_bitwise(i->op)) {
        load(m, i->left, SLJIT_R0);
        if (i->op != BF_BIT_NOT) {
            load(m, i->right, SLJIT_R1);
        }
        int op = i->op == BF_BIT_AND                        ? SLJIT_AND
               : i->op == BF_BIT_OR                         ? SLJIT_OR
               : i->op == BF_BIT_XOR || i->op == BF_BIT_NOT ? SLJIT_XOR
               : i->op == BF_SHL                            ? SLJIT_SHL
               : behavior_scalar_signed(i->type)            ? SLJIT_ASHR
                                                            : SLJIT_LSHR;
        if (i->op == BF_SHL || i->op == BF_SHR) {
            /* A count at or past the width shifts every bit out. SLJIT leaves
             * such counts undefined, so an arithmetic shift takes width - 1
             * and the others shift zero by zero. */
            sljit_sw width = (sljit_sw)behavior_scalar_size(i->type) * CHAR_BIT;
            struct sljit_jump *within = sljit_emit_cmp(
                c,
                SLJIT_LESS | (width == 64 ? 0 : SLJIT_32),
                SLJIT_R1,
                0,
                SLJIT_IMM,
                width
            );
            if (op == SLJIT_ASHR) {
                sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, width - 1);
            } else {
                sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
                sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, 0);
            }
            bind(c, within);
        }
        sljit_emit_op2(
            c,
            op | (behavior_scalar_size(i->type) == 8 ? 0 : SLJIT_32),
            SLJIT_R0,
            0,
            SLJIT_R0,
            0,
            i->op == BF_BIT_NOT ? SLJIT_IMM : SLJIT_R1,
            i->op == BF_BIT_NOT ? -1 : 0
        );
        sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, SLJIT_R0, 0);
        return true;
    }
    bool integer = behavior_scalar_integer(i->type) || i->type == PIN_BOOL ||
                   (i->type == PIN_HANDLE || i->type == PIN_TASK ||
                    i->type == PIN_UNION || i->type == PIN_POINTER) ||
                   behavior_scalar_string(i->type);
    if (i->op == BF_CAST) {
        cast(m, i, dest, offset);
        return true;
    }
    pin_type left_type = i->op >= BF_ADD && i->op <= BF_NE
                           ? m->function->slots[i->left]
                           : PIN_FLOW;
    if ((i->op >= BF_ADD && i->op <= BF_NE && behavior_scalar_integer(left_type)) ||
        ((i->op == BF_EQ || i->op == BF_NE) &&
         (left_type == PIN_BOOL || (left_type == PIN_HANDLE || left_type == PIN_TASK ||
                                    left_type == PIN_UNION)))) {
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
            if (i->op == BF_VARIABLE && m->variable_registers &&
                m->variable_registers[i->left]) {
                int variable = m->variable_registers[i->left];
                if (dest != variable) {
                    sljit_emit_op1(c, behavior_jit_integer_move(i->type), dest, offset, variable, 0);
                }
                return true;
            }
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
            if (i->op == BF_STORE && m->variable_registers &&
                m->variable_registers[i->target]) {
                int variable = m->variable_registers[i->target];
                if (m->registers[i->left] != variable) {
                    load(m, i->left, variable);
                }
                return true;
            }
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
