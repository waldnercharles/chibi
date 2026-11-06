#include "execution/jit_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>
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
    int until[SLJIT_NUMBER_OF_SCRATCH_FLOAT_REGISTERS];
    int saved_until[SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS + 1];
    for (int r = 0; r < SLJIT_NUMBER_OF_SCRATCH_FLOAT_REGISTERS; r++) {
        until[r] = -1;
    }
    for (int r = 0; r < SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS; r++) {
        saved_until[r] = -1;
    }
    int *next_call = xmalloc((size_t)(acount(f->instructions) + 1) * sizeof *next_call);
    next_call[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        if (!inspected && !behavior_function_runtime_keeps(f, pc)) {
            next_call[pc] = next_call[pc + 1];
            continue;
        }
        next_call[pc] = behavior_jit_pointer_comparison(f, &f->instructions[pc]) ||
                                behavior_jit_call(f->instructions[pc].op) ||
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
        if (i->op >= BF_STORE) {
            continue;
        }
        pin_type type = f->slots[i->output];
        if (type != PIN_F32 && type != PIN_F64) {
            continue;
        }
        if (next_call[pc + 1] < last[i->output]) {
            /* Values crossing a C ABI call must survive scratch clobbers.
             * SysV x86 has no saved FP registers, so those values spill. */
            for (int r = 0; r < SLJIT_NUMBER_OF_SAVED_FLOAT_REGISTERS; r++) {
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
        for (int r = 0; r < SLJIT_NUMBER_OF_SCRATCH_FLOAT_REGISTERS - temporaries; r++) {
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
        next_integer_clobber[pc] = behavior_jit_pointer_comparison(f, &f->instructions[pc]) ||
                                           behavior_jit_integer_clobber(
                                               f->instructions[pc].op
                                           ) ||
                                           (inspected && behavior_function_inspection_call(
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
        if (i->op >= BF_STORE) {
            continue;
        }
        pin_type type = f->slots[i->output];
        if (registers[i->output] || invariants[i->output] ||
            !(behavior_scalar_integer(type) || type == PIN_BOOL ||
              (type == PIN_HANDLE || type == PIN_TASK || type == PIN_UNION ||
               type == PIN_POINTER) ||
              behavior_scalar_string(type)) ||
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
                                 (type == PIN_HANDLE || type == PIN_TASK ||
                                  type == PIN_UNION || type == PIN_POINTER) ||
                                 behavior_scalar_string(type))) {
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
                 (type == PIN_HANDLE || type == PIN_TASK || type == PIN_UNION ||
                  type == PIN_POINTER) ||
                 behavior_scalar_string(type)) &&
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
/* Only the fully guarded, synchronous path can defer scalar publication.
 * Keep variables in otherwise unused saved registers; the checked path keeps
 * its eager memory updates and therefore exact partial effects. */
int *behavior_jit_variable_registers(const behavior_function *f, int base, int scratch, int *count)
{
    int variables = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        int variable = i->op == BF_VARIABLE ? i->left
                     : i->op == BF_STORE    ? i->target
                                            : -1;
        if (variable >= variables) {
            variables = variable + 1;
        }
    }
    int *registers = xcalloc((size_t)(variables + 1), sizeof *registers);
    pin_type *types = xcalloc((size_t)(variables + 1), sizeof *types);
    bool *written = xcalloc((size_t)(variables + 1), sizeof *written);
    bool *valid = xmalloc((size_t)(variables + 1) * sizeof *valid);
    memset(valid, true, (size_t)(variables + 1) * sizeof *valid);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        int variable = i->op == BF_VARIABLE ? i->left
                     : i->op == BF_STORE    ? i->target
                                            : -1;
        if (variable < 0) {
            continue;
        }
        /* Arithmetic already normalizes these exact 32/64-bit stored widths.
         * Narrow integers and bool require a truncating STORE, so retain their
         * ordinary memory publication rather than aliasing arithmetic outputs. */
        valid[variable] &= (i->type == PIN_I32 || i->type == PIN_U32 ||
                            i->type == PIN_I64 || i->type == PIN_U64 ||
                            i->type == PIN_ENUM) &&
                           (!types[variable] || types[variable] == i->type);
        types[variable] = i->type;
        written[variable] |= i->op == BF_STORE;
    }
    *count = 0;
    for (int pc = 0; pc < acount(f->instructions) && *count < 2 &&
                     base + *count < SLJIT_NUMBER_OF_SAVED_REGISTERS &&
                     base + *count + scratch < SLJIT_NUMBER_OF_REGISTERS;
         pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_VARIABLE && !registers[i->left] && valid[i->left] &&
            written[i->left]) {
            registers[i->left] = SLJIT_S(base + (*count)++);
        }
    }
    free(types);
    free(written);
    free(valid);
    return registers;
}
void behavior_jit_alias_variables(const behavior_function *f, int *registers, const int *variables)
{
    int *last = lifetimes(f, false, NULL);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_VARIABLE || !variables[i->left]) {
            continue;
        }
        bool unchanged = true;
        for (int at = pc + 1; at <= last[i->output]; at++) {
            const behavior_function_instruction *next = &f->instructions[at];
            if (next->op == BF_STORE && next->target == i->left) {
                unchanged = false;
                break;
            }
        }
        if (unchanged) {
            registers[i->output] = variables[i->left];
        }
    }
    free(last);
    for (int pc = 0; pc + 1 < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc], *next = i + 1;
        if ((i->op == BF_ADD || i->op == BF_SUB || i->op == BF_MUL) &&
            next->op == BF_STORE && next->left == i->output && i->type == next->type &&
            variables[next->target] && single_use(f, i->output, pc + 1)) {
            registers[i->output] = variables[next->target];
        }
    }
}
/* Comparisons may feed a Boolean zero test before the branch. Consume only
 * private values with no control entry into the skipped instructions. */
static int predicate_branch(const behavior_function *f, int pc, bool *invert)
{
    const behavior_function_instruction *compare = &f->instructions[pc];
    if (compare->op < BF_LT || compare->op > BF_NE) {
        return -1;
    }
    int next = pc + 1, constant = -1;
    *invert = false;
    if (next < acount(f->instructions) && f->instructions[next].op == BF_CONSTANT &&
        f->instructions[next].type == PIN_BOOL) {
        constant = next++;
    }
    int consumer = next, result = compare->output;
    if (next < acount(f->instructions) &&
        (f->instructions[next].op == BF_EQ || f->instructions[next].op == BF_NE) &&
        f->slots[f->instructions[next].left] == PIN_BOOL) {
        const behavior_function_instruction *test = &f->instructions[next];
        int literal = test->left == compare->output  ? test->right
                    : test->right == compare->output ? test->left
                                                     : -1;
        /* Restrict inversion literals to this straight instruction span.
         * A constant defined earlier may not dominate an external entry into
         * the comparison, even when its textual definition precedes it. */
        if (constant < 0 || f->instructions[constant].output != literal ||
            !single_use(f, test->output, next + 1) || !single_use(f, literal, next)) {
            return -1;
        }
        *invert = (test->op == BF_EQ) != f->instructions[constant].value.b;
        result = test->output;
        next++;
    } else if (constant >= 0) {
        return -1;
    }
    if (next >= acount(f->instructions) ||
        (f->instructions[next].op != BF_BRANCH && f->instructions[next].op != BF_ASSERT) ||
        f->instructions[next].left != result ||
        !single_use(f, compare->output, consumer)) {
        return -1;
    }
    for (int at = 0; at < acount(f->instructions); at++) {
        const behavior_function_instruction *i = &f->instructions[at];
        if ((i->op == BF_BRANCH || i->op == BF_JUMP) && i->target > pc &&
            i->target <= next) {
            return -1;
        }
    }
    return next;
}

static int comparison_condition(behavior_function_op op, pin_type type)
{
    if (type == PIN_F32 || type == PIN_F64) {
        return (int[]){ SLJIT_ORDERED_LESS,    SLJIT_ORDERED_LESS_EQUAL,
                        SLJIT_ORDERED_GREATER, SLJIT_ORDERED_GREATER_EQUAL,
                        SLJIT_ORDERED_EQUAL,   SLJIT_UNORDERED_OR_NOT_EQUAL }[op - BF_LT];
    }
    return (int[]){ SLJIT_SIG_LESS,    SLJIT_SIG_LESS_EQUAL,
                    SLJIT_SIG_GREATER, SLJIT_SIG_GREATER_EQUAL,
                    SLJIT_EQUAL,       SLJIT_NOT_EQUAL }[op - BF_LT];
}

int behavior_jit_fused_branch(const behavior_machine *m, int pc, struct sljit_jump **jump)
{
    const behavior_function *f = m->function;
    const behavior_function_instruction *i = &f->instructions[pc];
    bool invert;
    int last = predicate_branch(f, pc, &invert);
    if (last < 0) {
        return 0;
    }
    pin_type type = f->slots[i->left];
    bool real = type == PIN_F32 || type == PIN_F64;
    bool equality = i->op == BF_EQ || i->op == BF_NE;
    if ((!real && type != PIN_I32 && type != PIN_I64 &&
         !(equality && (type == PIN_BOOL || type == PIN_U32 || type == PIN_U64 ||
                        (type == PIN_HANDLE || type == PIN_TASK || type == PIN_UNION)))) ||
        f->slots[i->right] != type) {
        return 0;
    }
    int condition = comparison_condition(i->op, type) ^ invert;
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
        return last - pc + 1;
    }
    /* Boolean stack slots contain one byte; a word-sized memory compare would
     * observe unrelated upper bytes retained from a previous loop iteration. */
    if (type == PIN_BOOL) {
        sljit_emit_op1(
            m->compiler,
            SLJIT_MOV_U8,
            SLJIT_R0,
            0,
            left ? left : SLJIT_MEM1(SLJIT_SP),
            left ? 0 : i->left * sizeof(double)
        );
        sljit_emit_op1(
            m->compiler,
            SLJIT_MOV_U8,
            SLJIT_R1,
            0,
            right ? right : SLJIT_MEM1(SLJIT_SP),
            right ? 0 : i->right * sizeof(double)
        );
        left = SLJIT_R0;
        right = SLJIT_R1;
    }
    *jump = sljit_emit_cmp(
        m->compiler,
        condition | (behavior_scalar_size(type) <= 4 ? SLJIT_32 : 0),
        left ? left : SLJIT_MEM1(SLJIT_SP),
        left ? 0 : i->left * sizeof(double),
        right ? right : SLJIT_MEM1(SLJIT_SP),
        right ? 0 : i->right * sizeof(double)
    );
    return last - pc + 1;
}
/* A field used only by a branch can be compared directly in memory. Keep
 * literals immediate instead of materializing and reloading stack slots. */
int behavior_jit_field_branch(const behavior_machine *m, int pc, struct sljit_jump **jump)
{
    const behavior_function *f = m->function;
    const behavior_function_instruction *load = &f->instructions[pc];
    pin_type type = load->type;
    if (load->op != BF_LOAD ||
        (type != PIN_I32 && type != PIN_U32 && type != PIN_I64 && type != PIN_U64 &&
         type != PIN_HANDLE && type != PIN_TASK && type != PIN_UNION) ||
        pc + 2 >= acount(f->instructions)) {
        return 0;
    }
    const behavior_function_instruction *literal = &f->instructions[pc + 1];
    /* SLJIT negates compare immediates, which INT64_MIN cannot survive. */
    bool immediate = literal->op == BF_CONSTANT && literal->type == type &&
                     behavior_scalar_bits(literal->value) != (uint64_t)INT64_MIN;
    int comparison = pc + 1 + immediate;
    bool invert;
    int last = predicate_branch(f, comparison, &invert);
    if (last < 0 || last >= acount(f->instructions)) {
        return 0;
    }
    const behavior_function_instruction *compare = &f->instructions[comparison],
                                        *branch = &f->instructions[last];
    if (compare->op < BF_LT || compare->op > BF_NE || compare->left != load->output ||
        branch->op != BF_BRANCH || !single_use(f, load->output, comparison) ||
        f->slots[compare->right] != type ||
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
    int condition = (sign ? (int[]){ SLJIT_SIG_LESS,
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
                                     SLJIT_NOT_EQUAL })[compare->op - BF_LT] ^
                    invert;
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
