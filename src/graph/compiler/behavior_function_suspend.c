#include "graph/compiler/behavior_function_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void mark(uint64_t *bits, int value, bool live)
{
    uint64_t mask = UINT64_C(1) << (value % 64);
    if (live) {
        bits[value / 64] |= mask;
    } else {
        bits[value / 64] &= ~mask;
    }
}
static void transfer(const behavior_function *f, const behavior_function_instruction *i, uint64_t *bits)
{
    int slots = acount(f->slots);
    if (i->op < BF_STORE) {
        mark(bits, i->output, false);
    }
    if ((i->op == BF_DIV || i->op == BF_MOD) &&
        behavior_scalar_integer(i->type) && i->target >= 0) {
        mark(bits, i->target, false);
    }
    if (i->op == BF_SAVE) {
        mark(bits, slots + i->target, false);
    }
    if (i->op == BF_ENTER) {
        for (int j = 0; j < acount(f->locals); j++) {
            if (f->locals[j].frame == i->target) {
                mark(bits, slots + j, false);
            }
        }
    }
    if (behavior_function_left_slot(i->op)) {
        mark(bits, i->left, true);
    }
    if (behavior_function_right_slot(i->op)) {
        mark(bits, i->right, true);
    }
    if (i->op == BF_SELECT) {
        mark(bits, i->target, true);
    }
    if (i->op == BF_LOCAL) {
        mark(bits, slots + i->left, true);
    }
}
void behavior_function_suspensions(behavior_function *f)
{
    assert(!f->suspensions);
    int count = acount(f->instructions), values = acount(f->slots) + acount(f->locals);
    for (int pc = 0; pc < count; pc++) {
        if (f->instructions[pc].op == BF_SUSPEND) {
            assert(pc + 1 < count && !f->fast && !f->root_steps);
            f->instructions[pc].target = acount(f->suspensions);
            apush(f->suspensions, ((behavior_function_suspension){ .pc = pc }));
        }
    }
    if (!acount(f->suspensions)) {
        return;
    }
    /* Backward fixed point includes both branch successors and loop back edges.
     * Local definitions/entries kill old values just like SSA definitions. */
    size_t words = (size_t)(values + 63) / 64 + 1;
    uint64_t *live = xcalloc((size_t)count * words, sizeof *live);
    uint64_t *next = xcalloc(words, sizeof *next);
    bool changed;
    do {
        changed = false;
        for (int pc = count - 1; pc >= 0; pc--) {
            const behavior_function_instruction *i = &f->instructions[pc];
            memset(next, 0, words * sizeof *next);
            if (pc + 1 < count && i->op != BF_JUMP && i->op != BF_RETURN &&
                i->op != BF_END && i->op != BF_FAIL) {
                memcpy(next, live + (size_t)(pc + 1) * words, words * sizeof *next);
            }
            if (i->op == BF_JUMP || i->op == BF_BRANCH) {
                for (size_t w = 0; w < words; w++) {
                    next[w] |= live[(size_t)i->target * words + w];
                }
            }
            transfer(f, i, next);
            uint64_t *at = live + (size_t)pc * words;
            if (memcmp(at, next, words * sizeof *next)) {
                memcpy(at, next, words * sizeof *next);
                changed = true;
            }
        }
    } while (changed);
    for (int j = 0; j < acount(f->suspensions); j++) {
        behavior_function_suspension *s = &f->suspensions[j];
        const uint64_t *bits = live + (size_t)(s->pc + 1) * words;
        for (int v = 0; v < values; v++) {
            if (bits[v / 64] & (UINT64_C(1) << (v % 64))) {
                apush(s->values, v);
            }
        }
    }
    free(next);
    free(live);
}
void behavior_function_activation_init(const behavior_function *f, behavior_function_activation *a)
{
    assert(acount(f->suspensions) && !a->values);
    int count = acount(f->slots) + acount(f->locals) + acount(f->frames);
    *a = (behavior_function_activation){
        .values = xcalloc((size_t)count, sizeof *a->values)
    };
}
void behavior_function_activation_free(behavior_function_activation *a)
{
    if (a->vm) {
        free(a->vm->values);
        free(a->vm->parents);
        free(a->vm);
    }
    free(a->values);
    *a = (behavior_function_activation){ 0 };
}
