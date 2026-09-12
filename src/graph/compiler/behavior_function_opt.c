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
        if (behavior_function_math(f->instructions[i].op)) {
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
