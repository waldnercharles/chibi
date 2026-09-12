#include "graph/compiler/behavior_function_jit_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#if CHIBI_HAVE_SJLIT
/* Slots are defined before their uses within one lowered expression. Allocate
 * four scratch registers by live interval; excess live values spill to the
 * activation stack. FR0/FR1 remain emitter temporaries. */
int *behavior_jit_registers(const behavior_function *f, bool integers, int *saved_count)
{
    int count = acount(f->slots);
    int *registers = xcalloc((size_t)(count + 1), sizeof *registers);
    int *last = xcalloc((size_t)(count + 1), sizeof *last);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (behavior_function_left_slot(i->op)) {
            last[i->left] = pc;
        }
        if (i->op == BF_SELECT) {
            last[i->target] = pc;
        }
        if (behavior_function_right_slot(i->op)) {
            last[i->right] = pc;
        }
    }
    int until[4] = { -1, -1, -1, -1 }, saved_until[4] = { -1, -1, -1, -1 };
    int *next_call = xmalloc((size_t)(acount(f->instructions) + 1) * sizeof *next_call);
    next_call[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        next_call[pc] = behavior_function_call(f->instructions[pc].op)
                          ? pc
                          : next_call[pc + 1];
    }
    int *next_integer_clobber = xmalloc(
        (size_t)(acount(f->instructions) + 1) * sizeof *next_integer_clobber
    );
    next_integer_clobber[acount(f->instructions)] = acount(f->instructions);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        next_integer_clobber[pc] = behavior_function_integer_clobber(
                                       f->instructions[pc].op
                                   )
                                     ? pc
                                     : next_integer_clobber[pc + 1];
    }
    *saved_count = 0;
    int integer_until[2] = { -1, -1 };
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE && integers &&
            (behavior_scalar_integer(i->type) || i->type == PIN_BOOL ||
             i->type == PIN_HANDLE)) {
            if (next_integer_clobber[pc + 1] >= last[i->output]) {
                for (int r = 0; r < 2; r++) {
                    if (integer_until[r] <= pc) {
                        registers[i->output] = r ? SLJIT_R4 : SLJIT_R2;
                        integer_until[r] = last[i->output];
                        break;
                    }
                }
            }
            continue;
        }
        if (i->op >= BF_STORE || i->type == PIN_BOOL) {
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
        for (int r = 0; r < 4; r++) {
            if (until[r] <= pc) {
                registers[i->output] = SLJIT_FR2 + r;
                until[r] = last[i->output];
                break;
            }
        }
    }
    free(next_call);
    free(next_integer_clobber);
    free(last);
    return registers;
}

int *behavior_jit_local_registers(const behavior_function *f, int base, int scratch, int *count)
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
                (behavior_scalar_integer(type) || type == PIN_BOOL || type == PIN_HANDLE) &&
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
    free(uses);
    return registers;
}

/* Preserve a separate snapshot if a later save or frame entry can overwrite
 * the local before this expression has finished reading it. */
void behavior_jit_alias_locals(const behavior_function *f, int *registers, const int *locals)
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
        if (i->op == BF_SELECT) {
            last[i->target] = pc;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
            last[i->target] = pc;
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_LOCAL || !locals[i->left]) {
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
            (i->op == BF_SELECT && i->target == slot)) {
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
    if ((type != PIN_I32 && type != PIN_I64) || next->op != BF_BRANCH ||
        next->left != i->output || !single_use(f, i->output, pc + 1)) {
        return false;
    }
    int condition = (int[]){ SLJIT_SIG_LESS,    SLJIT_SIG_LESS_EQUAL,
                             SLJIT_SIG_GREATER, SLJIT_SIG_GREATER_EQUAL,
                             SLJIT_EQUAL,       SLJIT_NOT_EQUAL }[i->op - BF_LT];
    int left = m->registers[i->left], right = m->registers[i->right];
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
#endif
