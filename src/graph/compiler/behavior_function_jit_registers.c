#include "graph/compiler/behavior_function_jit_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#if CHIBI_HAVE_SJLIT
/* Slots are defined before their uses within one lowered expression. Allocate
 * scratch registers by live interval; excess live values spill to the
 * activation stack. FR0/FR1 are temporaries, plus FR2 for ternary math. */
static int *lifetimes(const behavior_function *f, bool inspected, bool *invariants)
{
    int count = acount(f->slots);
    int *last = xcalloc((size_t)(count + 1), sizeof *last);
    int *defined = xmalloc((size_t)(count + 1) * sizeof *defined);
    for (int i = 0; i <= count; i++) {
        defined[i] = -1;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            defined[i->output] = pc;
        }
        if (behavior_function_left_slot(i->op)) {
            last[i->left] = pc;
        }
        if (behavior_function_third_slot(i->op)) {
            last[i->target] = pc;
        }
        if (behavior_function_right_slot(i->op)) {
            last[i->right] = pc;
        }
    }
    /* An invariant used in a loop must survive the complete iteration, even
     * when its last textual use precedes scratch clobbers near the back edge. */
    bool changed;
    do {
        changed = false;
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if ((i->op != BF_JUMP && i->op != BF_BRANCH) || i->target > pc) {
                continue;
            }
            for (int slot = 0; slot < count; slot++) {
                if (defined[slot] >= 0 && defined[slot] < i->target &&
                    last[slot] >= i->target) {
                    if (invariants) {
                        invariants[slot] = true;
                    }
                    if (last[slot] < pc) {
                        last[slot] = pc;
                        changed = true;
                    }
                }
            }
        }
    } while (changed);
    free(defined);
    return last;
}
int *behavior_jit_registers(const behavior_function *f, bool inspected, int *saved_count, bool *invariants)
{
    int *last = lifetimes(f, inspected, invariants);
    int *registers = xcalloc((size_t)(acount(f->slots) + 1), sizeof *registers);
    int temporaries = 2;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if ((inspected || behavior_function_runtime_keeps(f, pc)) &&
            f->instructions[pc].op == BF_FMA) {
            temporaries = 3;
            break;
        }
    }
    int until[4] = { -1, -1, -1, -1 }, saved_until[4] = { -1, -1, -1, -1 };
    int *next_call = xmalloc((size_t)(acount(f->instructions) + 1) * sizeof *next_call);
    next_call[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            next_call[pc] = next_call[pc + 1];
            continue;
        }
        next_call[pc] = behavior_jit_call(f->instructions[pc].op) ||
                                (inspected && behavior_function_inspection_call(
                                                  f->instructions[pc].op
                                              ))
                          ? pc
                          : next_call[pc + 1];
    }
    *saved_count = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op >= BF_STORE || (i->type != PIN_F32 && i->type != PIN_F64)) {
            continue;
        }
        if (next_call[pc + 1] < last[i->output]) {
            /* Values crossing a C ABI call must survive scratch clobbers.
             * SysV x86 has no saved FP registers, so those values spill. */
            for (int r = 0; r < 4 && r < SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS; r++) {
                if (saved_until[r] <= pc) {
                    registers[i->output] = SLJIT_FS(r);
                    saved_until[r] = last[i->output];
                    if (r + 1 > *saved_count) {
                        *saved_count = r + 1;
                    }
                    break;
                }
            }
            continue;
        }
        for (int r = 0; r < 6 - temporaries; r++) {
            if (until[r] <= pc) {
                registers[i->output] = SLJIT_FR0 + temporaries + r;
                until[r] = last[i->output];
                break;
            }
        }
    }
    free(next_call);
    free(last);
    return registers;
}

/* Allocate scratch after aliases and invariant registers have been assigned.
 * A local already held in a saved register must not reserve a scratch interval. */
void behavior_jit_integer_registers(
    const behavior_function *f,
    bool inspected,
    int *registers,
    const bool *invariants
)
{
    int *last = lifetimes(f, inspected, NULL);
    int *next_integer_clobber = xmalloc(
        (size_t)(acount(f->instructions) + 1) * sizeof *next_integer_clobber
    );
    next_integer_clobber[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            next_integer_clobber[pc] = next_integer_clobber[pc + 1];
            continue;
        }
        next_integer_clobber[pc] = behavior_jit_integer_clobber(
                                       f->instructions[pc].op
                                   ) || (inspected &&
                                         behavior_function_inspection_call(
                                             f->instructions[pc].op
                                         ))
                                     ? pc
                                     : next_integer_clobber[pc + 1];
    }
    int until[2] = { -1, -1 };
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op >= BF_STORE || registers[i->output] || invariants[i->output] ||
            !(behavior_scalar_integer(i->type) || i->type == PIN_BOOL ||
              i->type == PIN_HANDLE || behavior_scalar_string(i->type)) ||
            next_integer_clobber[pc + 1] < last[i->output]) {
            continue;
        }
        for (int r = 0; r < 2; r++) {
            if (until[r] <= pc) {
                registers[i->output] = r ? SLJIT_R4 : SLJIT_R2;
                until[r] = last[i->output];
                break;
            }
        }
    }
    free(next_integer_clobber);
    free(last);
}

/* Keep long-lived integer inputs out of the two expression scratch registers.
 * Otherwise a count and array pointer can force every loop temporary to spill.
 * Saved registers retain these values across loop bodies and native calls. */
int behavior_jit_invariant_registers(
    const behavior_function *f,
    int *registers,
    const bool *invariants,
    int base,
    int scratch
)
{
    int used = 0;
    for (int slot = 0; slot < acount(f->slots) && used < 4 &&
                       base + used < SLJIT_NUMBER_OF_SAVED_REGISTERS &&
                       base + used + scratch < SLJIT_NUMBER_OF_REGISTERS;
         slot++) {
        pin_type type = f->slots[slot];
        if (invariants[slot] && (behavior_scalar_integer(type) || type == PIN_BOOL ||
                                 type == PIN_HANDLE || behavior_scalar_string(type))) {
            registers[slot] = SLJIT_S(base + used++);
        }
    }
    return used;
}

int *behavior_jit_local_registers(const behavior_function *f, int base, int scratch, int *count, int *float_count)
{
    int *registers = xcalloc((size_t)(acount(f->locals) + 1), sizeof *registers);
    int *uses = xcalloc((size_t)(acount(f->locals) + 1), sizeof *uses);
    bool loop = false;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        loop |= (i->op == BF_JUMP || i->op == BF_BRANCH) && i->target <= pc;
        if (i->op == BF_LOCAL) {
            uses[i->left]++;
        }
    }
    *count = 0;
    int available = SLJIT_NUMBER_OF_REGISTERS - scratch - base;
    for (int r = 0; loop && r < 4 && r < available &&
                    r + base < SLJIT_NUMBER_OF_SAVED_REGISTERS;
         r++) {
        int best = -1;
        for (int i = 0; i < acount(f->locals); i++) {
            pin_type type = f->locals[i].initial.type;
            if (!registers[i] && uses[i] &&
                (behavior_scalar_integer(type) || type == PIN_BOOL ||
                 type == PIN_HANDLE || behavior_scalar_string(type)) &&
                (best < 0 || uses[i] > uses[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        registers[best] = SLJIT_S(base + r);
        (*count)++;
    }
    /* Loop-carried values survive block boundaries and C calls. Targets
     * without saved FP registers keep the existing stack representation. */
    for (int r = 0; loop && r < 4 && *float_count < SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS;
         r++) {
        int best = -1;
        for (int i = 0; i < acount(f->locals); i++) {
            pin_type type = f->locals[i].initial.type;
            if (!registers[i] && uses[i] && (type == PIN_F32 || type == PIN_F64) &&
                (best < 0 || uses[i] > uses[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        registers[best] = SLJIT_FS((*float_count)++);
    }
    free(uses);
    return registers;
}

/* Preserve a separate snapshot if a later save or frame entry can overwrite
 * the local before this expression has finished reading it. */
void behavior_jit_alias_locals(
    const behavior_function *f,
    int *registers,
    const int *locals,
    const bool *invariants
)
{
    int *last = xcalloc((size_t)(acount(f->slots) + 1), sizeof *last);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (behavior_function_left_slot(i->op)) {
            last[i->left] = pc;
        }
        if (behavior_function_right_slot(i->op)) {
            last[i->right] = pc;
        }
        if (behavior_function_third_slot(i->op)) {
            last[i->target] = pc;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
            last[i->target] = pc;
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_LOCAL || !locals[i->left] || invariants[i->output]) {
            continue;
        }
        bool unchanged = true;
        for (int at = pc + 1; at <= last[i->output]; at++) {
            const behavior_function_instruction *next = &f->instructions[at];
            if ((next->op == BF_SAVE && next->target == i->left) ||
                (next->op == BF_ENTER && next->target == f->locals[i->left].frame)) {
                unchanged = false;
                break;
            }
        }
        if (unchanged) {
            registers[i->output] = locals[i->left];
        }
    }
    free(last);
}

static bool single_use(const behavior_function *f, int slot, int expected)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if ((i->op == BF_BRANCH || i->op == BF_JUMP) && i->target == expected) {
            return false;
        }
        if (pc == expected) {
            continue;
        }
        if ((behavior_function_left_slot(i->op) && i->left == slot) ||
            (behavior_function_right_slot(i->op) && i->right == slot) ||
            (behavior_function_third_slot(i->op) && i->target == slot)) {
            return false;
        }
    }
    return true;
}
void behavior_jit_coalesce_saves(const behavior_function *f, int *registers, const int *locals)
{
    for (int pc = 0; pc + 1 < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc], *next = i + 1;
        if ((i->op == BF_ADD || i->op == BF_SUB || i->op == BF_MUL) &&
            next->op == BF_SAVE && next->left == i->output &&
            locals[next->target] && single_use(f, i->output, pc + 1)) {
            registers[i->output] = locals[next->target];
        }
    }
}
bool behavior_jit_fused_branch(const behavior_machine *m, int pc, struct sljit_jump **jump)
{
    const behavior_function *f = m->function;
    const behavior_function_instruction *i = &f->instructions[pc];
    if (i->op < BF_LT || i->op > BF_NE || pc + 1 >= acount(f->instructions)) {
        return false;
    }
    const behavior_function_instruction *next = i + 1;
    pin_type type = f->slots[i->left];
    bool real = type == PIN_F32 || type == PIN_F64;
    if ((!real && type != PIN_I32 && type != PIN_I64) ||
        (next->op != BF_BRANCH && next->op != BF_ASSERT) ||
        next->left != i->output || !single_use(f, i->output, pc + 1)) {
        return false;
    }
    int condition = real ? (int[]){ SLJIT_ORDERED_LESS,
                                    SLJIT_ORDERED_LESS_EQUAL,
                                    SLJIT_ORDERED_GREATER,
                                    SLJIT_ORDERED_GREATER_EQUAL,
                                    SLJIT_ORDERED_EQUAL,
                                    SLJIT_UNORDERED_OR_NOT_EQUAL }[i->op - BF_LT]
                         : (int[]){ SLJIT_SIG_LESS,    SLJIT_SIG_LESS_EQUAL,
                                    SLJIT_SIG_GREATER, SLJIT_SIG_GREATER_EQUAL,
                                    SLJIT_EQUAL,       SLJIT_NOT_EQUAL }[i->op - BF_LT];
    int left = m->registers[i->left], right = m->registers[i->right];
    if (real) {
        *jump = sljit_emit_fcmp(
            m->compiler,
            condition | (type == PIN_F32 ? SLJIT_32 : 0),
            left ? left : SLJIT_MEM1(SLJIT_SP),
            left ? 0 : i->left * sizeof(double),
            right ? right : SLJIT_MEM1(SLJIT_SP),
            right ? 0 : i->right * sizeof(double)
        );
        return true;
    }
    *jump = sljit_emit_cmp(
        m->compiler,
        condition | (type == PIN_I32 ? SLJIT_32 : 0),
        left ? left : SLJIT_MEM1(SLJIT_SP),
        left ? 0 : i->left * sizeof(double),
        right ? right : SLJIT_MEM1(SLJIT_SP),
        right ? 0 : i->right * sizeof(double)
    );
    return true;
}
/* A field used only by a branch can be compared directly in memory. Keep
 * literals immediate instead of materializing and reloading stack slots. */
int behavior_jit_field_branch(const behavior_machine *m, int pc, struct sljit_jump **jump)
{
    const behavior_function *f = m->function;
    const behavior_function_instruction *load = &f->instructions[pc];
    pin_type type = load->type;
    if (load->op != BF_LOAD ||
        (type != PIN_I32 && type != PIN_U32 && type != PIN_I64 &&
         type != PIN_U64 && type != PIN_HANDLE) ||
        pc + 2 >= acount(f->instructions)) {
        return 0;
    }
    const behavior_function_instruction *literal = &f->instructions[pc + 1];
    bool immediate = literal->op == BF_CONSTANT && literal->type == type;
    int comparison = pc + 1 + immediate, last = comparison + 1;
    if (last >= acount(f->instructions)) {
        return 0;
    }
    const behavior_function_instruction *compare = &f->instructions[comparison],
                                        *branch = &f->instructions[last];
    if (compare->op < BF_LT || compare->op > BF_NE || compare->left != load->output ||
        branch->op != BF_BRANCH || branch->left != compare->output ||
        !single_use(f, load->output, comparison) ||
        !single_use(f, compare->output, last) || f->slots[compare->right] != type ||
        (immediate && (compare->right != literal->output ||
                       !single_use(f, literal->output, comparison)))) {
        return 0;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *op = &f->instructions[i];
        if ((op->op == BF_BRANCH || op->op == BF_JUMP) && op->target > pc &&
            op->target <= last) {
            return 0;
        }
    }
    bool sign = type == PIN_I32 || type == PIN_I64;
    int condition =
        (sign ? (int[]){ SLJIT_SIG_LESS,
                         SLJIT_SIG_LESS_EQUAL,
                         SLJIT_SIG_GREATER,
                         SLJIT_SIG_GREATER_EQUAL,
                         SLJIT_EQUAL,
                         SLJIT_NOT_EQUAL }
              : (int[]){ SLJIT_LESS,
                         SLJIT_LESS_EQUAL,
                         SLJIT_GREATER,
                         SLJIT_GREATER_EQUAL,
                         SLJIT_EQUAL,
                         SLJIT_NOT_EQUAL })[compare->op - BF_LT];
    int pointer = m->registers[load->left];
    if (!pointer) {
        sljit_emit_op1(m->compiler, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_SP), load->left * sizeof(double));
        pointer = SLJIT_R3;
    }
    int right = m->registers[compare->right];
    *jump = sljit_emit_cmp(
        m->compiler,
        condition | (behavior_scalar_size(type) == 4 ? SLJIT_32 : 0),
        SLJIT_MEM1(pointer),
        load->offset,
        immediate ? SLJIT_IMM
        : right   ? right
                  : SLJIT_MEM1(SLJIT_SP),
        immediate ? (sljit_sw)behavior_scalar_bits(literal->value)
        : right   ? 0
                  : compare->right * sizeof(double)
    );
    return last - pc + 1;
}
#endif
