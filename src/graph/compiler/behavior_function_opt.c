#include "graph/compiler/behavior_function_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

/* A straight path through several logical frames has a known budget. Once an
 * entry guard proves those checks succeed, values can be forwarded and stores
 * delayed until exit. The original program remains the precise failure path. */
typedef struct
{
    behavior_function *f;
    bool *constant;
    pin_value *values;
} optimizer;

static int define(optimizer *o, behavior_function_instruction i)
{
    i.output = acount(o->f->slots);
    apush(o->f->slots, i.type);
    apush(o->f->instructions, i);
    apush(o->constant, i.op == BF_CONSTANT);
    apush(o->values, i.value);
    return i.output;
}
static int constant(optimizer *o, pin_value v)
{
    size_t size = v.type == PIN_BOOL ? sizeof(bool)
                : v.type == PIN_F32  ? sizeof(float)
                                     : sizeof(double);
    for (int i = 0; i < acount(o->f->slots); i++) {
        if (o->constant[i] && o->f->slots[i] == v.type &&
            !memcmp(&v.d, &o->values[i].d, size)) {
            return i;
        }
    }
    return define(o, (behavior_function_instruction){ .op = BF_CONSTANT, .type = v.type, .value = v });
}
static int arithmetic(optimizer *o, behavior_function_instruction i)
{
    if (i.op >= BF_ADD && i.op <= BF_DIV && o->constant[i.left] &&
        o->constant[i.right]) {
        pin_value a = o->values[i.left], b = o->values[i.right],
                  v = { .type = i.type };
        if (i.type == PIN_F32) {
            v.f = i.op == BF_ADD ? a.f + b.f
                : i.op == BF_SUB ? a.f - b.f
                : i.op == BF_MUL ? a.f * b.f
                                 : a.f / b.f;
        } else {
            v.d = i.op == BF_ADD ? a.d + b.d
                : i.op == BF_SUB ? a.d - b.d
                : i.op == BF_MUL ? a.d * b.d
                                 : a.d / b.d;
        }
        return constant(o, v);
    }
    return define(o, i);
}
static void prune(behavior_function *f)
{
    bool *live = xcalloc((size_t)(acount(f->slots) + 1), sizeof *live);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_STORE || i->op == BF_RESULT) {
            live[i->left] = true;
        }
        if (i->op >= BF_ADD && i->op < BF_STORE && live[i->output]) {
            live[i->left] = live[i->right] = true;
        }
    }
    int *map = xcalloc((size_t)(acount(f->slots) + 1), sizeof *map);
    int count = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        if (live[i]) {
            f->slots[count] = f->slots[i];
            map[i] = count++;
        }
    }
    asetlen(f->slots, count);
    count = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction i = f->instructions[pc];
        if (i.op < BF_STORE) {
            if (!live[i.output]) {
                continue;
            }
            i.output = map[i.output];
        }
        if (i.op >= BF_ADD && i.op <= BF_RESULT) {
            i.left = map[i.left];
            if (i.op < BF_STORE) {
                i.right = map[i.right];
            }
        }
        f->instructions[count++] = i;
    }
    asetlen(f->instructions, count);
    free(live);
    free(map);
}
void behavior_function_optimize(behavior_function *f)
{
    if (acount(f->frames) < 2) {
        return;
    }
    for (int i = 0; i < acount(f->slots); i++) {
        if (f->slots[i] != PIN_F32 && f->slots[i] != PIN_F64 && f->slots[i] != PIN_BOOL) {
            return;
        }
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *op = &f->instructions[i];
        if (behavior_function_math(op->op) || op->indirect || op->op == BF_OWNER ||
            op->op == BF_WORLD || (op->op >= BF_ENTITY && op->op <= BF_LOAD) ||
            op->op == BF_ASSERT || op->op == BF_WRITE || op->op == BF_PRESENT ||
            op->op == BF_TYPE_NAME || op->op == BF_RESULT_TYPE_NAME ||
            op->op == BF_REQUIRE || op->op == BF_SELECT || op->op == BF_RESULT_PRESENT ||
            op->op == BF_STORAGE_NAME || op->op == BF_OWN_NAME ||
            op->op == BF_STORE_HEADER || op->op == BF_ARRAY || op->op == BF_READ) {
            return;
        }
    }
    optimizer o = { .f = xcalloc(1, sizeof *o.f) };
    apush(o.f->frames, f->frames[0]);
    apush(o.f->sources, f->sources[0]);
    int variables = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        int index = i->op == BF_VARIABLE ? i->left
                  : i->op == BF_STORE    ? i->target
                                         : -1;
        if (index >= variables) {
            variables = index + 1;
        }
    }
    int *vars = xmalloc((size_t)(variables + 1) * sizeof *vars);
    bool *written = xcalloc((size_t)(variables + 1), sizeof *written);
    int *locals = xcalloc((size_t)(acount(f->locals) + 1), sizeof *locals);
    int *slots = xcalloc((size_t)(acount(f->slots) + 1), sizeof *slots);
    unsigned *steps = xcalloc((size_t)acount(f->frames), sizeof *steps);
    bool *visited = xcalloc((size_t)acount(f->instructions), sizeof *visited);
    for (int i = 0; i < variables; i++) {
        vars[i] = -1;
    }
    for (int i = 0; i < acount(f->locals); i++) {
        locals[i] = constant(&o, f->locals[i].initial);
    }
    bool valid = false;
    for (int pc = 0; !visited[pc]; pc++) {
        visited[pc] = true;
        behavior_function_instruction i = f->instructions[pc];
        int output = i.output;
        switch (i.op) {
            case BF_JUMP: pc = i.target - 1; continue;
            case BF_BRANCH:
            case BF_FAIL: goto done;
            case BF_CHECK:
                steps[i.frame]++;
                if (i.frame && steps[i.frame] > o.f->child_budget) {
                    o.f->child_budget = steps[i.frame];
                }
                continue;
            case BF_CALL_CHECK: {
                int limit = i.left - f->frames[i.frame].depth;
                if (limit <= 0) {
                    goto done;
                }
                if (!o.f->depth_limit || (unsigned)limit < o.f->depth_limit) {
                    o.f->depth_limit = (unsigned)limit;
                }
                continue;
            }
            case BF_ENTER:
                steps[i.target] = 0;
                for (int j = 0; j < acount(f->locals); j++) {
                    if (f->locals[j].frame == i.target) {
                        locals[j] = constant(&o, f->locals[j].initial);
                    }
                }
                continue;
            case BF_LEAVE: continue;
            case BF_LOCAL: slots[output] = locals[i.left]; continue;
            case BF_SAVE: locals[i.target] = slots[i.left]; continue;
            case BF_VARIABLE:
                if (vars[i.left] < 0) {
                    vars[i.left] = define(&o, i);
                }
                slots[output] = vars[i.left];
                continue;
            case BF_STORE:
                vars[i.target] = slots[i.left];
                written[i.target] = true;
                continue;
            case BF_RESULT:
                i.left = slots[i.left];
                apush(o.f->instructions, i);
                continue;
            case BF_END:
            case BF_RETURN:
                for (int j = 0; j < variables; j++) {
                    if (written[j]) {
                        apush(
                            o.f->instructions,
                            ((behavior_function_instruction){
                                .op = BF_STORE,
                                .left = vars[j],
                                .target = j,
                                .type = o.f->slots[vars[j]] })
                        );
                    }
                }
                apush(o.f->instructions, i);
                o.f->root_steps = steps[0];
                prune(o.f);
                valid = true;
                goto done;
            case BF_CONSTANT: slots[output] = constant(&o, i.value); continue;
            case BF_ARGUMENT:
            case BF_DELTA: slots[output] = define(&o, i); continue;
            default:
                if (i.op >= BF_ADD && i.op < BF_STORE) {
                    i.left = slots[i.left];
                    i.right = slots[i.right];
                    slots[output] = arithmetic(&o, i);
                } else {
                    goto done;
                }
        }
    }
done:
    if (valid) {
        f->fast = o.f;
    } else {
        behavior_function_free(o.f);
    }
    afree(o.constant);
    afree(o.values);
    free(vars);
    free(written);
    free(locals);
    free(slots);
    free(steps);
    free(visited);
}

/* Scalarized snapshots often expose only one field. Drop unused loads and
 * copies after local compaction, while retaining checks and all effects. */
static bool discardable(behavior_function_op op)
{
    return op <= BF_ARRAY || op == BF_LOAD || op == BF_READ || op == BF_SELECT;
}
void behavior_function_prune_values(behavior_function *f)
{
    bool *live = xcalloc((size_t)(acount(f->slots) + 1), sizeof *live);
    bool *keep = xcalloc((size_t)acount(f->instructions), sizeof *keep);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (discardable(i->op) && !live[i->output]) {
            continue;
        }
        keep[pc] = true;
        if (behavior_function_left_slot(i->op)) {
            live[i->left] = true;
        }
        if (behavior_function_right_slot(i->op)) {
            live[i->right] = true;
        }
        if (i->op == BF_SELECT) {
            live[i->target] = true;
        }
    }
    int *pcs = xcalloc((size_t)acount(f->instructions), sizeof *pcs);
    int count = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        pcs[pc] = count;
        if (keep[pc]) {
            f->instructions[count++] = f->instructions[pc];
        }
    }
    asetlen(f->instructions, count);
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_JUMP || i->op == BF_BRANCH) {
            i->target = pcs[i->target];
        }
    }
    memset(live, 0, (size_t)(acount(f->slots) + 1) * sizeof *live);
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            live[i->output] = true;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
            live[i->target] = true;
        }
    }
    int *slots = xcalloc((size_t)(acount(f->slots) + 1), sizeof *slots);
    int used = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        if (live[i]) {
            f->slots[used] = f->slots[i];
            slots[i] = used++;
        }
    }
    asetlen(f->slots, used);
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            i->output = slots[i->output];
        }
        if (behavior_function_left_slot(i->op)) {
            i->left = slots[i->left];
        }
        if (behavior_function_right_slot(i->op)) {
            i->right = slots[i->right];
        }
        if (((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) ||
            i->op == BF_SELECT) {
            i->target = slots[i->target];
        }
    }
    free(slots);
    free(pcs);
    free(keep);
    free(live);
}

/* These bodies cannot change world membership or layouts. Reuse checked
 * addresses along a single control path; joins and loop edges start fresh.
 * Row contents remain mutable, so every field read still observes prior writes. */
void behavior_function_reuse_accesses(behavior_function *f)
{
    int count = acount(f->instructions);
    int *incoming = xcalloc((size_t)count, sizeof *incoming);
    int *slots = xmalloc((size_t)(acount(f->slots) + 1) * sizeof *slots);
    int *pcs = xcalloc((size_t)count, sizeof *pcs);
    int *locals = xmalloc((size_t)(acount(f->locals) + 1) * sizeof *locals);
    for (int i = 0; i < acount(f->locals); i++) {
        locals[i] = -1;
    }
    behavior_function_instruction *out = NULL;
    for (int i = 0; i < acount(f->slots); i++) {
        slots[i] = i;
    }
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction i = f->instructions[pc];
        if (i.op == BF_JUMP || i.op == BF_BRANCH) {
            incoming[i.target]++;
        }
        if (pc + 1 < count && i.op != BF_JUMP && i.op != BF_RETURN &&
            i.op != BF_END && i.op != BF_FAIL) {
            incoming[pc + 1]++;
        }
    }
    int start = 0;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction i = f->instructions[pc];
        pcs[pc] = acount(out);
        if (incoming[pc] != 1 || (pc && (f->instructions[pc - 1].op == BF_BRANCH ||
                                         (f->instructions[pc - 1].op == BF_JUMP &&
                                          f->instructions[pc - 1].target != pc)))) {
            start = acount(out);
            for (int j = 0; j < acount(f->locals); j++) {
                locals[j] = -1;
            }
        }
        if (behavior_function_left_slot(i.op)) {
            i.left = slots[i.left];
        }
        if (behavior_function_right_slot(i.op)) {
            i.right = slots[i.right];
        }
        if (((i.op == BF_DIV || i.op == BF_MOD) && i.target >= 0) || i.op == BF_SELECT) {
            i.target = slots[i.target];
        }
        if (i.op == BF_ENTER) {
            for (int j = 0; j < acount(f->locals); j++) {
                if (f->locals[j].frame == i.target) {
                    locals[j] = -1;
                }
            }
        }
        if (i.op == BF_LOCAL && locals[i.left] >= 0) {
            slots[i.output] = locals[i.left];
            continue;
        }
        if (i.op == BF_SAVE) {
            locals[i.target] = i.left;
        }
        bool reusable = i.op == BF_OWNER || i.op == BF_WORLD || i.op == BF_ENTITY ||
                        i.op == BF_COMPONENT || i.op == BF_ASSERT;
        bool found = false;
        for (int j = start; reusable && j < acount(out); j++) {
            const behavior_function_instruction *previous = &out[j];
            if (previous->op != i.op || previous->type != i.type ||
                previous->left != i.left || previous->right != i.right ||
                (i.op == BF_WORLD && previous->target != i.target) ||
                previous->offset != i.offset || previous->value.u64 != i.value.u64 ||
                previous->layout != i.layout) {
                continue;
            }
            if (i.op != BF_ASSERT) {
                slots[i.output] = previous->output;
            }
            found = true;
            break;
        }
        if (!found) {
            apush(out, i);
        }
    }
    for (int pc = 0; pc < acount(out); pc++) {
        if (out[pc].op == BF_JUMP || out[pc].op == BF_BRANCH) {
            out[pc].target = pcs[out[pc].target];
        }
    }
    afree(f->instructions);
    f->instructions = out;
    free(pcs);
    free(slots);
    free(incoming);
    free(locals);
}

/* Fold locals whose only writes repeat their initial scalar value, including
 * loop break flags when the body has no Break. No control-flow assumption is
 * needed: entry, re-entry and every store all write the same bits. */
void behavior_function_constant_locals(behavior_function *f)
{
    bool *constant = xcalloc((size_t)(acount(f->locals) + 1), sizeof *constant);
    const behavior_function_instruction *
        *definitions = xcalloc((size_t)(acount(f->slots) + 1), sizeof *definitions);
    for (int i = 0; i < acount(f->locals); i++) {
        constant[i] = f->locals[i].offset != SIZE_MAX - 1;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            definitions[i->output] = i;
        }
        if (i->op == BF_SAVE) {
            const behavior_function_instruction *value = definitions[i->left];
            pin_value initial = f->locals[i->target].initial;
            constant[i->target] &= value && value->op == BF_CONSTANT &&
                                   value->type == initial.type &&
                                   !memcmp(
                                       &value->value.d,
                                       &initial.d,
                                       behavior_scalar_size(initial.type)
                                   );
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_LOCAL && constant[i->left]) {
            i->value = f->locals[i->left].initial;
            i->op = BF_CONSTANT;
        }
        if (i->op == BF_BRANCH) {
            const behavior_function_instruction *condition = definitions[i->left];
            if (condition && condition->op == BF_CONSTANT) {
                i->op = BF_JUMP;
                if (!condition->value.b) {
                    i->target = pc + 1;
                }
            }
        }
    }
    free(definitions);
    free(constant);
}
