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
static bool suspensions(behavior_function *f, size_t *remaining)
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
    for (int j = 0; j < acount(f->suspensions); j++) {
        behavior_function_suspension *s = &f->suspensions[j];
        const uint64_t *bits = live + (size_t)(s->pc + 1) * words;
        int count = 0;
        for (int v = 0; v < values; v++) {
            count += (bits[v / 64] & (UINT64_C(1) << (v % 64))) != 0;
        }
        if (!count) {
            continue;
        }
        size_t capacity = count < 16 ? 16 : (size_t)count;
        size_t bytes = sizeof(dyna_hdr) + capacity * sizeof *s->values;
        if (remaining && bytes > *remaining) {
            free(next);
            free(live);
            return false;
        }
        if (remaining) {
            *remaining -= bytes;
        }
        asetlen(s->values, count);
        int at = 0;
        for (int v = 0; v < values; v++) {
            if (bits[v / 64] & (UINT64_C(1) << (v % 64))) {
                s->values[at++] = v;
            }
        }
    }
    free(next);
    free(live);
    return true;
}
static void operands(behavior_function *f)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        i->left_slot = behavior_function_left_slot(i->op);
        i->right_slot = behavior_function_right_slot(i->op);
    }
}
bool behavior_function_finalize(behavior_function *f, size_t *remaining)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        f->uses_storage |= i->op == BF_COPY_ARRAY || i->op == BF_COPY_STRING ||
                           i->op == BF_INVOKE ||
                           (i->op == BF_HOST_CALL &&
                            f->host_calls[i->target].call->value);
    }
    operands(f);
    if (f->fast) {
        operands(f->fast);
    }
    if (!suspensions(f, remaining)) {
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
bool behavior_function_breakpoints(const behavior_function *f)
{
    /* Asset generations are immutable while an instance owns them. */
    return f->breakpoints;
}
const behavior_function_point *behavior_function_pause_point(
    const behavior_function *f,
    const behavior_function_activation *a
)
{
    if (!a->pc || f->instructions[a->pc - 1].op != BF_POINT) {
        return NULL;
    }
    return &f->points[f->instructions[a->pc - 1].left];
}

int behavior_function_frame_count(const behavior_function *f)
{
    return acount(f->frames);
}
uintptr_t behavior_function_frame_steps(
    const behavior_function *f,
    const behavior_function_activation *a,
    int frame
)
{
    return a->values[acount(f->slots) + acount(f->locals) + frame];
}
int behavior_function_frame_parent(const behavior_function *f, int frame)
{
    return f->frames[frame].source.parent;
}
const behavior_function_source *behavior_function_frame_source(const behavior_function *f, int frame)
{
    return &f->frames[frame].source;
}
int behavior_function_frame_stack(const behavior_function *f, int frame)
{
    return f->frames[frame].stack;
}
