#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
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
static void suspensions(behavior_function *f)
{
    assert(!f->suspensions);
    int count = acount(f->instructions), values = acount(f->slots) + acount(f->locals);
    for (int pc = 0; pc < count; pc++) {
        if (f->instructions[pc].op == BF_SUSPEND ||
            f->instructions[pc].op == BF_DELAY || f->instructions[pc].op == BF_POINT ||
            f->instructions[pc].op == BF_INVOKE) {
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
static void operands(behavior_function *f)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        i->left_slot = behavior_function_left_slot(i->op);
        i->right_slot = behavior_function_right_slot(i->op);
    }
}
void behavior_function_finalize(behavior_function *f)
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
    suspensions(f);
    behavior_function_runtime_code(f);
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

static void point(
    behavior_function *f,
    behavior_function_instruction **out,
    behavior_function_instruction i,
    behavior_point_kind kind,
    int frame,
    int stack
)
{
    graph *g = f->frames[frame].source.graph;
    node *n = i.node_id ? graph_node_by_id(g, i.node_id) : NULL;
    f->breakpoints |= n && n->breakpoint;
    int index = acount(f->points);
    apush(
        f->points,
        ((behavior_function_point){ .graph = g,
                                    .node = n,
                                    .frame = frame,
                                    .stack = stack,
                                    .depth = f->frames[frame].depth,
                                    .kind = kind })
    );
    i.op = BF_POINT;
    i.left = index;
    i.frame = frame;
    i.stack = stack;
    i.source = f->frames[frame].source_index;
    apush(*out, i);
}
void behavior_function_points(behavior_function *f)
{
    if (!behavior_function_inspectable(f)) {
        return;
    }
    int count = acount(f->instructions);
    int *map = xcalloc((size_t)count, sizeof *map);
    behavior_function_instruction *out = NULL;
    int call_done = -1, call_parent = -1;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction i = f->instructions[pc];
        map[pc] = acount(out);
        if (i.op == BF_CHECK) {
            point(f, &out, i, BEHAVIOR_POINT_NODE, i.frame, i.stack);
        }
        if (i.op == BF_LEAVE || i.op == BF_RETURN) {
            point(
                f,
                &out,
                i,
                BEHAVIOR_POINT_FRAME_DONE,
                i.frame,
                f->frames[i.frame].stack
            );
        }
        if (pc == call_done) {
            behavior_function_instruction completed = i;
            completed.node_id = 0;
            const behavior_function_instruction *next = &f->instructions[i.target];
            point(
                f,
                &out,
                completed,
                BEHAVIOR_POINT_CALL_DONE,
                call_parent,
                next->op == BF_END ? f->frames[call_parent].stack : next->stack
            );
        }
        apush(out, i);
        if (i.op == BF_LEAVE || i.op == BF_INVOKE) {
            call_parent = i.op == BF_INVOKE ? i.frame
                                            : f->frames[i.target].source.parent;
            call_done = -1;
            for (int next = pc + 1; next < count; next++) {
                behavior_function_op op = f->instructions[next].op;
                if (op == BF_FAIL || op == BF_CHECK || op == BF_ENTER) {
                    break;
                }
                if (op == BF_JUMP) {
                    call_done = next;
                    break;
                }
            }
        }
    }
    for (int pc = 0; pc < acount(out); pc++) {
        if (out[pc].op == BF_JUMP || out[pc].op == BF_BRANCH) {
            out[pc].target = map[out[pc].target];
        }
    }
    afree(f->instructions);
    f->instructions = out;
    free(map);
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
const pin_value *behavior_function_frame_inputs(
    const behavior_function *f,
    const behavior_function_activation *activation,
    int frame,
    arena *storage,
    int *count
)
{
    graph *g = f->frames[frame].source.graph;
    node *entry = graph_node_by_id(g, g->entry_id);
    int *pins = arena_alloc(storage, (size_t)(entry->output_count + 1) * sizeof *pins);
    *count = 0;
    for (int i = 0; i < entry->output_count; i++) {
        pins[i] = entry->output_pins[i].kind == PIN_DATA &&
                          !entry->output_pins[i].field_source
                    ? (*count)++
                    : -1;
    }
    pin_value *values = arena_alloc(storage, (size_t)(*count + 1) * sizeof *values);
    bool *present = arena_alloc(storage, (size_t)(*count + 1) * sizeof *present);
    for (int i = 0; i < entry->output_count; i++) {
        if (pins[i] < 0) {
            continue;
        }
        const pin_spec *spec = &entry->output_pins[i];
        pin_value *v = &values[pins[i]];
        *v = spec->default_value;
        present[pins[i]] = true;
        if (v->type == PIN_STRUCT && v->container == PIN_CONTAINER_SINGLE) {
            const type_desc *type = registry_type_by_name(spec->type_name);
            assert(type);
            v->ptr = arena_alloc(storage, type->size);
            memset(v->ptr, 0, type->size);
        }
    }
    for (int i = 0; i < acount(f->locals); i++) {
        const behavior_function_local *local = &f->locals[i];
        if (!local->parameter || local->frame != frame) {
            continue;
        }
        int index = pins[local->pin];
        assert(index >= 0);
        pin_value *v = &values[index];
        const uint64_t *bits = &activation->values[acount(f->slots) + i];
        if (local->offset == SIZE_MAX) {
            memcpy(&present[index], bits, sizeof *present);
        } else if (local->offset == SIZE_MAX - 1) {
            memcpy(&v->type_name, bits, sizeof v->type_name);
        } else {
            void *target = v->type == PIN_STRUCT && v->container == PIN_CONTAINER_SINGLE
                             ? (char *)v->ptr + local->offset
                             : (void *)&v->d;
            memcpy(target, bits, behavior_scalar_size(local->initial.type));
        }
    }
    for (int i = 0; i < *count; i++) {
        if (!present[i]) {
            values[i].ptr = NULL;
        }
    }
    return values;
}
