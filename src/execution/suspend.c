#include "execution/definition.h"
#include "support/arena.h"
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
    if (behavior_function_third_slot(i->op)) {
        mark(bits, i->target, true);
    }
    if (i->op == BF_LOCAL) {
        mark(bits, slots + i->left, true);
    }
}
static bool live_at(const uint64_t *bits, int value)
{
    return bits[value / 64] & (UINT64_C(1) << (value % 64));
}
static int root_value(const behavior_function *f, behavior_value_root root)
{
    return root.index + (root.bank == BEHAVIOR_BANK_LOCAL ? acount(f->slots) : 0);
}
/* Count before allocating so retained storage is exactly the live sets. */
static bool save_live(
    behavior_function *f,
    behavior_function_suspension *s,
    const uint64_t *bits,
    int values,
    size_t budget
)
{
    for (int v = 0; v < values; v++) {
        s->value_count += live_at(bits, v);
    }
    for (int i = 0; i < acount(f->value_roots); i++) {
        s->root_count += live_at(bits, root_value(f, f->value_roots[i]));
    }
    if (s->value_count) {
        s->values = arena_alloc_bounded(
            &f->owned,
            (size_t)s->value_count * sizeof *s->values,
            budget
        );
        if (!s->values) {
            return false;
        }
    }
    if (s->root_count) {
        s->roots = arena_alloc_bounded(
            &f->owned,
            (size_t)s->root_count * sizeof *s->roots,
            budget
        );
        if (!s->roots) {
            return false;
        }
    }
    int saved = 0;
    for (int v = 0; v < values; v++) {
        if (live_at(bits, v)) {
            s->values[saved++] = v;
        }
    }
    saved = 0;
    for (int i = 0; i < acount(f->value_roots); i++) {
        behavior_value_root root = f->value_roots[i];
        root.index = root_value(f, root);
        if (live_at(bits, root.index)) {
            s->roots[saved++] = root;
        }
    }
    return true;
}
static bool suspensions(behavior_function *f, size_t budget)
{
    assert(!f->suspensions);
    int count = acount(f->instructions), values = acount(f->slots) + acount(f->locals);
    for (int pc = 0; pc < count; pc++) {
        if (f->instructions[pc].op == BF_SUSPEND ||
            f->instructions[pc].op == BF_DELAY || f->instructions[pc].op == BF_POINT ||
            f->instructions[pc].op == BF_SOURCE_POINT ||
            f->instructions[pc].op == BF_INVOKE) {
            assert(pc + 1 < count && !f->fast && !f->root_steps);
            f->instructions[pc].target = acount(f->suspensions);
            apush(f->suspensions, ((behavior_function_suspension){ .pc = pc }));
        }
    }
    if (!acount(f->suspensions)) {
        return true;
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
    bool saved = true;
    for (int j = 0; j < acount(f->suspensions) && saved; j++) {
        behavior_function_suspension *s = &f->suspensions[j];
        saved = save_live(f, s, live + (size_t)(s->pc + 1) * words, values, budget);
    }
    free(next);
    free(live);
    return saved;
}
static void operands(behavior_function *f)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        i->left_slot = behavior_function_left_slot(i->op);
        i->right_slot = behavior_function_right_slot(i->op);
    }
}
bool behavior_function_finalize(behavior_function *f, size_t arena_budget)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        f->uses_storage |= i->op == BF_COPY_ARRAY || i->op == BF_COPY_STRING ||
                           (i->op == BF_INTERN_STRING && i->type == PIN_STR) ||
                           i->op == BF_INVOKE ||
                           (i->op == BF_HOST_CALL &&
                            f->host_calls[i->target].call->value);
    }
    operands(f);
    if (f->fast) {
        operands(f->fast);
    }
    if (!suspensions(f, arena_budget)) {
        return false;
    }
    behavior_function_runtime_code(f);
    behavior_function_borrow_arrays(f);
    return true;
}
void behavior_function_activation_init(const behavior_function *f, behavior_function_activation *a)
{
    assert(acount(f->suspensions) && !a->values);
    int count = acount(f->slots) + acount(f->locals) + acount(f->frames);
    *a = (behavior_function_activation){
        .values = xcalloc((size_t)count, sizeof *a->values),
        .wake_times = f->waits ? xcalloc((size_t)acount(f->frames), sizeof *a->wake_times) : NULL
    };
}
void behavior_function_activation_free(behavior_function_activation *a)
{
    behavior_function_calls_free(a->calls);
    if (a->vm) {
        free(a->vm->values);
        free(a->vm->parents);
        free(a->vm);
    }
    free(a->wake_times);
    free(a->values);
    *a = (behavior_function_activation){ 0 };
}
static size_t vm_values(const behavior_function *f)
{
    size_t count = (size_t)acount(f->slots) + acount(f->locals) + 1;
    if (f->fast) {
        size_t fast = (size_t)acount(f->fast->slots) + acount(f->fast->locals) + 1;
        if (fast > count) {
            count = fast;
        }
    }
    return count;
}
static size_t vm_frames(const behavior_function *f)
{
    int count = acount(f->frames);
    return (size_t)(f->fast && acount(f->fast->frames) > count ? acount(f->fast->frames)
                                                               : count);
}
size_t behavior_function_activation_storage_bytes(
    const behavior_function *f,
    const behavior_function_activation *a
)
{
    size_t bytes = behavior_function_calls_storage_bytes(a->calls);
    if (a->values) {
        bytes += ((size_t)acount(f->slots) + acount(f->locals) + acount(f->frames)) *
                 sizeof *a->values;
    }
    if (a->wake_times) {
        bytes += (size_t)acount(f->frames) * sizeof *a->wake_times;
    }
    if (a->vm) {
        bytes += sizeof *a->vm;
        if (a->vm->values) {
            bytes += vm_values(f) * sizeof *a->vm->values;
        }
        if (a->vm->parents) {
            bytes += vm_frames(f) * sizeof *a->vm->parents;
        }
    }
    return bytes;
}
void behavior_function_reset_activation(const behavior_function *f, behavior_function_activation *a)
{
    behavior_function_calls_reset(a->calls);
    if (a->values) {
        memset(
            a->values,
            0,
            ((size_t)acount(f->slots) + acount(f->locals) + acount(f->frames)) *
                sizeof *a->values
        );
    }
    if (a->wake_times) {
        memset(a->wake_times, 0, (size_t)acount(f->frames) * sizeof *a->wake_times);
    }
    if (a->vm) {
        if (a->vm->values) {
            memset(a->vm->values, 0, vm_values(f) * sizeof *a->vm->values);
        }
        if (a->vm->parents) {
            memset(a->vm->parents, 0, vm_frames(f) * sizeof *a->vm->parents);
        }
    }
    *a = (behavior_function_activation){ .values = a->values,
                                         .wake_times = a->wake_times,
                                         .vm = a->vm,
                                         .calls = a->calls };
}
void behavior_function_copy_activation(
    const behavior_function *f,
    behavior_function_activation *target,
    const behavior_function_activation *source
)
{
    assert(target->values && source->values);
    assert(!target->calls && !source->calls);
    int count = acount(f->slots) + acount(f->locals) + acount(f->frames);
    memcpy(target->values, source->values, (size_t)count * sizeof *target->values);
    if (f->waits) {
        memcpy(
            target->wake_times,
            source->wake_times,
            (size_t)acount(f->frames) * sizeof *target->wake_times
        );
    }
    *target = (behavior_function_activation){ .values = target->values,
                                              .wake_times = target->wake_times,
                                              .vm = target->vm,
                                              .steps = source->steps,
                                              .pc = source->pc,
                                              .frame = source->frame,
                                              .source = source->source,
                                              .stack = source->stack,
                                              .node_id = source->node_id };
}

bool behavior_function_resumable(const behavior_function *f)
{
    return acount(f->suspensions) != 0;
}

int behavior_function_frame_count(const behavior_function *f)
{
    return acount(f->frames);
}
