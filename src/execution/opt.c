#include "execution/definition.h"
#include "execution/scalar.h"
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
    apush(o->f->slots, i.op >= BF_LT && i.op <= BF_NE ? PIN_BOOL : i.type);
    apush(o->f->slot_kinds, BEHAVIOR_VALUE);
    apush(o->f->instructions, i);
    apush(o->constant, i.op == BF_CONSTANT);
    apush(o->values, i.value);
    return i.output;
}
static int constant(optimizer *o, pin_value v)
{
    size_t size = behavior_scalar_size(v.type);
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
    bool integer = i.type >= PIN_I8 && i.type <= PIN_U64;
    if (((i.op >= BF_ADD && i.op <= BF_DIV) || (integer && i.op == BF_MOD)) &&
        o->constant[i.left] && o->constant[i.right]) {
        pin_value a = o->values[i.left], b = o->values[i.right],
                  v = { .type = i.type };
        if (i.type >= PIN_I8 && i.type <= PIN_U64) {
            behavior_scalar_integer_op(i.op, a, b, &v, NULL);
        } else if (i.type == PIN_F32) {
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
    if ((i.op >= BF_ADD && i.op <= BF_DIV) || i.op == BF_NEG || i.op == BF_ABS ||
        i.op == BF_MIN || i.op == BF_MAX || (i.op >= BF_LT && i.op <= BF_NE)) {
        /* Identical typed operands produce the same value. Keep their order:
         * reassociation and commutation can change floating-point results. */
        for (int pc = 0; pc < acount(o->f->instructions); pc++) {
            const behavior_function_instruction *prior = &o->f->instructions[pc];
            if (prior->op == i.op && prior->type == i.type && prior->left == i.left &&
                (!behavior_function_right_slot(i.op) || prior->right == i.right)) {
                return prior->output;
            }
        }
    }
    return define(o, i);
}
static void prune(behavior_function *f)
{
    bool *live = xcalloc((size_t)(acount(f->slots) + 1), sizeof *live);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE && !live[i->output]) {
            continue;
        }
        if (behavior_function_left_slot(i->op)) {
            live[i->left] = true;
        }
        if (behavior_function_right_slot(i->op)) {
            live[i->right] = true;
        }
    }
    int *map = xcalloc((size_t)(acount(f->slots) + 1), sizeof *map);
    int count = 0;
    for (int i = 0; i < acount(f->slots); i++) {
        if (live[i]) {
            f->slots[count] = f->slots[i];
            if (f->slot_kinds) {
                f->slot_kinds[count] = f->slot_kinds[i];
            }
            map[i] = count++;
        }
    }
    asetlen(f->slots, count);
    if (f->slot_kinds) {
        asetlen(f->slot_kinds, count);
    }
    count = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction i = f->instructions[pc];
        if (i.op < BF_STORE) {
            if (!live[i.output]) {
                continue;
            }
            i.output = map[i.output];
        }
        if (behavior_function_left_slot(i.op)) {
            i.left = map[i.left];
        }
        if (behavior_function_right_slot(i.op)) {
            i.right = map[i.right];
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
        if (f->slot_kinds && f->slot_kinds[i] != BEHAVIOR_VALUE) {
            return;
        }
        if (f->slots[i] != PIN_F32 && f->slots[i] != PIN_F64 &&
            f->slots[i] != PIN_BOOL && !behavior_scalar_integer(f->slots[i])) {
            return;
        }
    }
    bool names = false;
    for (int i = 0; i < acount(f->instructions); i++) {
        names |= f->instructions[i].op == BF_TYPE_NAME ||
                 f->instructions[i].op == BF_RESULT_TYPE_NAME;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *op = &f->instructions[i];
        if (behavior_function_math(op->op) || op->op == BF_RANDOM ||
            op->op == BF_RANDOM_RANGE || op->op == BF_STATE_SELECT ||
            op->op == BF_RETRIGGER || op->indirect || op->op == BF_OWNER ||
            op->op == BF_WORLD || op->op == BF_CONTEXT ||
            (op->op >= BF_ENTITY && op->op <= BF_LOAD) || op->op == BF_ASSERT ||
            op->op == BF_WRITE || op->op == BF_COMPONENT_WRITE ||
            op->op == BF_PRESENT || op->op == BF_REQUIRE || op->op == BF_SELECT ||
            op->op == BF_RESULT_PRESENT || op->op == BF_STORAGE_NAME ||
            op->op == BF_OWN_NAME || op->op == BF_COPY_STRING ||
            op->op == BF_COPY_ARRAY || op->op == BF_INTERN_STRING ||
            op->op == BF_STRING_EQUAL || op->op == BF_STORE_VALUE ||
            op->op == BF_STORE_HEADER || op->op == BF_STORE_TYPE_NAME ||
            op->op == BF_ARRAY || op->op == BF_QUERY_VARIABLE ||
            op->op == BF_QUERY || op->op == BF_READ || op->op == BF_HOST_SLOT ||
            op->op == BF_HOST_SERVICE || op->op == BF_HOST_CALL ||
            op->op == BF_COMMAND_SERVICE || op->op == BF_COMMAND_ARGUMENT ||
            op->op == BF_COMMAND || op->op == BF_COMMAND_OUTPUT ||
            op->op == BF_COMMAND_RESULT || op->op == BF_COMMAND_BEGIN ||
            op->op == BF_SUSPEND || op->op == BF_DELAY || op->op == BF_POINT ||
            op->op == BF_SOURCE_POINT || op->op == BF_SOURCE_WATCH ||
            op->op == BF_INVOKE) {
            return;
        }
        /* Only borrowed name metadata may occupy pointer slots. Arithmetic
         * and value storage keep the original typed execution path. */
        if (((op->op == BF_DIV || op->op == BF_MOD) && op->target >= 0) ||
            (op->op == BF_TYPE_NAME && op->target != BF_CONSTANT &&
             op->target != BF_ARGUMENT) ||
            (names && op->type == BF_POINTER_TYPE && op->op != BF_TYPE_NAME &&
             op->op != BF_CONSTANT && op->op != BF_LOCAL && op->op != BF_SAVE &&
             op->op != BF_RESULT_TYPE_NAME) ||
            (names && op->op >= BF_ADD && op->op < BF_STORE &&
             (f->slots[op->left] == BF_POINTER_TYPE ||
              (behavior_function_right_slot(op->op) &&
               f->slots[op->right] == BF_POINTER_TYPE)))) {
            return;
        }
    }
    optimizer o = { .f = xcalloc(1, sizeof *o.f) };
    o.f->artifact = execution_artifact_new();
    apush(o.f->frames, f->frames[0]);
    if (acount(f->sources)) {
        apush(o.f->sources, f->sources[0]);
    }
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
                if (!o.constant[slots[i.left]]) {
                    goto done;
                }
                if (o.values[slots[i.left]].b) {
                    pc = i.target - 1;
                }
                continue;
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
            case BF_RESULT_TYPE_NAME:
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
            case BF_DELTA:
            case BF_TYPE_NAME: {
                /* This guarded path has no host calls or suspension: inputs
                 * and Delta Time cannot change between inlined frames. */
                int prior = -1;
                for (int at = 0; at < acount(o.f->instructions); at++) {
                    const behavior_function_instruction *source = &o.f->instructions[at];
                    if (source->op == i.op && source->type == i.type &&
                        source->left == i.left && source->target == i.target &&
                        source->value.type == i.value.type &&
                        !memcmp(&source->value.d, &i.value.d, behavior_scalar_size(i.type))) {
                        prior = source->output;
                        break;
                    }
                }
                slots[output] = prior >= 0 ? prior : define(&o, i);
                continue;
            }
            default:
                if (i.op >= BF_ADD && i.op < BF_STORE) {
                    i.left = slots[i.left];
                    if (behavior_function_right_slot(i.op)) {
                        i.right = slots[i.right];
                    }
                    slots[output] = arithmetic(&o, i);
                    if (slots[output] < 0) {
                        goto done;
                    }
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
static bool discardable(const behavior_function_instruction *i)
{
    return i->op <= BF_ARRAY || i->op == BF_LOAD || i->op == BF_READ ||
           i->op == BF_SELECT || i->op == BF_CONVERT || i->op == BF_CAST;
}
void behavior_function_runtime_code(behavior_function *f)
{
    int count = acount(f->instructions), slots = acount(f->slots);
    bool *live = xcalloc((size_t)(slots + 1), sizeof *live);
    bool *keep = xcalloc((size_t)count, sizeof *keep);
    /* Inspection may be enabled at an actual wait. Preserve every saved value
     * needed after that continuation, including values read only by watches. */
    for (int j = 0; j < acount(f->suspensions); j++) {
        const behavior_function_suspension *s = &f->suspensions[j];
        if (f->instructions[s->pc].op == BF_POINT ||
            f->instructions[s->pc].op == BF_SOURCE_POINT) {
            continue;
        }
        for (int k = 0; k < s->value_count; k++) {
            if (s->values[k] < slots) {
                live[s->values[k]] = true;
            }
        }
    }
    bool *saved = xmalloc((size_t)(slots + 1) * sizeof *saved);
    memcpy(saved, live, (size_t)(slots + 1) * sizeof *saved);
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            behavior_function_reuse_reads(f, keep);
            behavior_function_scalarize_unions(f, keep);
            behavior_function_optimize_ranges(f, keep);
            behavior_function_borrow_snapshots(f, keep);
            memcpy(live, saved, (size_t)(slots + 1) * sizeof *live);
        }
        const behavior_function_instruction *code = pass ? f->runtime_body
                                                         : f->instructions;
        for (int pc = count - 1; pc >= 0; pc--) {
            const behavior_function_instruction *i = &code[pc];
            if ((pass && !keep[pc]) || behavior_function_observer(i->op) ||
                i->op == BF_POINT || i->op == BF_SOURCE_POINT ||
                i->op == BF_SOURCE_WATCH || (discardable(i) && !live[i->output])) {
                keep[pc] = false;
                continue;
            }
            keep[pc] = true;
            if (behavior_function_left_slot(i->op)) {
                live[i->left] = true;
            }
            if (behavior_function_right_slot(i->op)) {
                live[i->right] = true;
            }
            if (behavior_function_third_slot(i->op)) {
                live[i->target] = true;
            }
        }
    }
    free(saved);
    int kept = 0;
    for (int pc = 0; pc < count; pc++) {
        kept += keep[pc];
    }
    asetcap(f->runtime_instructions, kept);
    f->runtime_map = xcalloc((size_t)(count + 1), sizeof *f->runtime_map);
    for (int pc = 0; pc < count; pc++) {
        f->runtime_map[pc] = acount(f->runtime_instructions);
        if (keep[pc]) {
            apush(f->runtime_instructions, f->runtime_body[pc]);
        }
    }
    f->runtime_map[count] = acount(f->runtime_instructions);
    for (int pc = 0; pc < acount(f->runtime_instructions); pc++) {
        behavior_function_instruction *i = &f->runtime_instructions[pc];
        if (i->op == BF_JUMP || i->op == BF_BRANCH) {
            i->target = f->runtime_map[i->target];
        }
    }
    free(keep);
    free(live);
}
void behavior_function_prune_values(behavior_function *f)
{
    bool *live = xcalloc((size_t)(acount(f->slots) + 1), sizeof *live);
    bool *keep = xcalloc((size_t)acount(f->instructions), sizeof *keep);
    for (int pc = acount(f->instructions) - 1; pc >= 0; pc--) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (discardable(i) && !live[i->output]) {
            continue;
        }
        keep[pc] = true;
        if (behavior_function_left_slot(i->op)) {
            live[i->left] = true;
        }
        if (behavior_function_right_slot(i->op)) {
            live[i->right] = true;
        }
        if (behavior_function_third_slot(i->op)) {
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
            if (f->slot_kinds) {
                f->slot_kinds[used] = f->slot_kinds[i];
            }
            slots[i] = used++;
        }
    }
    asetlen(f->slots, used);
    if (f->slot_kinds) {
        asetlen(f->slot_kinds, used);
    }
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
            behavior_function_third_slot(i->op)) {
            i->target = slots[i->target];
        }
    }
    free(slots);
    free(pcs);
    free(keep);
    free(live);
}

/* Between suspensions these bodies cannot change world membership or layouts.
 * Reuse checked addresses along one path; joins, loop edges and yields start fresh.
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
        if (incoming[pc] != 1 ||
            (pc && (f->instructions[pc - 1].op == BF_BRANCH ||
                    f->instructions[pc - 1].op == BF_SUSPEND ||
                    f->instructions[pc - 1].op == BF_DELAY ||
                    f->instructions[pc - 1].op == BF_POINT ||
                    f->instructions[pc - 1].op == BF_SOURCE_POINT ||
                    f->instructions[pc - 1].op == BF_INVOKE ||
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
        if (((i.op == BF_DIV || i.op == BF_MOD) && i.target >= 0) ||
            behavior_function_third_slot(i.op)) {
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
        constant[i] = !f->locals[i].array_default;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            definitions[i->output] = i;
        }
        if (i->op == BF_SAVE) {
            const behavior_function_instruction *value = definitions[i->left];
            pin_value initial = f->locals[i->target].initial;
            if (f->locals[i->target].offset == SIZE_MAX - 1) {
                constant[i->target] &= value && value->op == BF_TYPE_NAME &&
                                       value->target == BF_CONSTANT &&
                                       ((initial.str == NULL && value->value.str == NULL) ||
                                        (initial.str && value->value.str &&
                                         !strcmp(initial.str, value->value.str)));
                continue;
            }
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
            bool name = f->locals[i->left].offset == SIZE_MAX - 1;
            i->value = f->locals[i->left].initial;
            i->op = name ? BF_TYPE_NAME : BF_CONSTANT;
            i->target = BF_CONSTANT;
        }
        if (i->op == BF_OWN_NAME) {
            const behavior_function_instruction *source = definitions[i->left];
            const char *name = source ? source->value.str : NULL;
            if (source && source->op == BF_TYPE_NAME && source->target == BF_CONSTANT &&
                behavior_function_static_name(&name)) {
                i->op = BF_TYPE_NAME;
                i->target = BF_CONSTANT;
                i->value = (pin_value){ .str = name };
            }
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

/* A single inclusive counted loop has one equality exit and a fixed number of
 * checks on each path. Prove its counter setup from IR rather than inferring
 * it from node names; any extra branch, frame or observable call rejects it. */
static bool loop_path_steps(
    const behavior_function *f,
    int start,
    int stop,
    unsigned *steps,
    unsigned char *regions,
    unsigned region
)
{
    int count = acount(f->instructions);
    bool valid = false;
    *steps = 0;
    for (int pc = start; pc >= 0 && pc < count && !(regions[pc] & region);) {
        if (pc == stop) {
            valid = true;
            break;
        }
        regions[pc] |= region;
        const behavior_function_instruction *i = &f->instructions[pc];
        if (!behavior_function_runtime_keeps(f, pc)) {
            pc++;
            continue;
        }
        if (i->op == BF_END || i->op == BF_RETURN) {
            valid = stop < 0;
            break;
        }
        if (i->op == BF_BRANCH) {
            break;
        }
        *steps += i->op == BF_CHECK;
        pc = i->op == BF_JUMP ? i->target : pc + 1;
    }
    return valid;
}
bool behavior_function_loop_budget(const behavior_function *f, behavior_loop_budget *proof)
{
    if (acount(f->frames) != 1 || acount(f->suspensions)) {
        return false;
    }
    int branch = -1;
    unsigned char *regions = xcalloc((size_t)acount(f->instructions), sizeof *regions);
    int *definitions = xmalloc((size_t)(acount(f->slots) + 1) * sizeof *definitions);
    for (int slot = 0; slot <= acount(f->slots); slot++) {
        definitions[slot] = -1;
    }
    bool valid = false;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (!behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        const behavior_function_instruction *i = &f->instructions[pc];
        switch (i->op) {
            case BF_CONSTANT:
            case BF_ARGUMENT:
            case BF_VARIABLE:
            case BF_LOCAL:
            case BF_SAVE:
            case BF_STORE:
            case BF_RESULT:
            case BF_RESULT_TYPE_NAME:
            case BF_CHECK:
            case BF_JUMP:
            case BF_RETURN:
            case BF_END:
            case BF_ADD:
            case BF_SUB:
            case BF_MUL:
            case BF_NEG:
            case BF_ABS:
            case BF_LT:
            case BF_LE:
            case BF_GT:
            case BF_GE:
            case BF_EQ:
            case BF_NE:
            case BF_SELECT: break;
            case BF_TYPE_NAME:
                if (i->target != BF_CONSTANT) {
                    goto done;
                }
                break;
            case BF_BRANCH:
                if (branch >= 0) {
                    goto done;
                }
                branch = pc;
                break;
            default: goto done;
        }
        if (i->op < BF_STORE) {
            definitions[i->output] = pc;
        }
    }
    if (branch < 0 || definitions[f->instructions[branch].left] < 0) {
        goto done;
    }
    const behavior_function_instruction
        *compare = &f->instructions[definitions[f->instructions[branch].left]];
    if (compare->op != BF_EQ || definitions[compare->left] < 0 ||
        definitions[compare->right] < 0) {
        goto done;
    }
    const behavior_function_instruction
        *current = &f->instructions[definitions[compare->left]],
        *last = &f->instructions[definitions[compare->right]];
    if (current->op != BF_LOCAL || last->op != BF_LOCAL ||
        current->type != PIN_I32 || last->type != PIN_I32) {
        goto done;
    }
    int first_pc = -1, last_pc = -1, increment_pc = -1, step_pc = -1;
    int first_save = -1, last_save = -1, step_save = -1;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_SAVE ||
            (i->target != current->left && i->target != last->left)) {
            continue;
        }
        int source = definitions[i->left];
        if (source < 0) {
            goto done;
        }
        const behavior_function_instruction *value = &f->instructions[source];
        if (i->target == last->left) {
            if (last_pc >= 0) {
                goto done;
            }
            last_pc = source;
            last_save = pc;
            continue;
        }
        if (value->op == BF_ARGUMENT || value->op == BF_CONSTANT) {
            if (first_pc >= 0) {
                goto done;
            }
            first_pc = source;
            first_save = pc;
            continue;
        }
        if (increment_pc >= 0 || value->op != BF_ADD ||
            value->left != compare->left || definitions[value->right] < 0) {
            goto done;
        }
        const behavior_function_instruction
            *step = &f->instructions[definitions[value->right]];
        if (step->op != BF_LOCAL || step->type != PIN_I32) {
            goto done;
        }
        increment_pc = pc;
        for (int at = 0; at < acount(f->instructions); at++) {
            const behavior_function_instruction *save = &f->instructions[at];
            if (save->op == BF_SAVE && save->target == step->left) {
                if (step_pc >= 0) {
                    goto done;
                }
                step_pc = definitions[save->left];
                step_save = at;
            }
        }
    }
    if (first_pc < 0 || last_pc < 0 || increment_pc < 0 || step_pc < 0) {
        goto done;
    }
    const behavior_function_instruction *first_input = &f->instructions[first_pc],
                                        *last_input = &f->instructions[last_pc],
                                        *select = &f->instructions[step_pc];
    if (first_input->type != PIN_I32 || last_input->type != PIN_I32 ||
        (last_input->op != BF_ARGUMENT && last_input->op != BF_CONSTANT) ||
        select->op != BF_SELECT || definitions[select->left] < 0 ||
        definitions[select->right] < 0 || definitions[select->target] < 0) {
        goto done;
    }
    const behavior_function_instruction
        *direction = &f->instructions[definitions[select->left]],
        *one = &f->instructions[definitions[select->right]],
        *minus_one = &f->instructions[definitions[select->target]];
    if (direction->op != BF_LT || direction->left != first_input->output ||
        direction->right != last_input->output || one->op != BF_CONSTANT ||
        one->type != PIN_I32 || one->value.i != 1 || minus_one->op != BF_CONSTANT ||
        minus_one->type != PIN_I32 || minus_one->value.i != -1) {
        goto done;
    }
    /* The guard reads arguments before entry. No effect may precede their
     * captures, even when a caller aliases argument and variable storage. */
    for (int pc = 0; pc <= first_pc || pc <= last_pc; pc++) {
        if (f->instructions[pc].op == BF_STORE) {
            goto done;
        }
    }
    unsigned entry, iteration, tail;
    if (!loop_path_steps(f, 0, branch, &entry, regions, 1) ||
        !loop_path_steps(f, branch + 1, branch, &iteration, regions, 2) ||
        !loop_path_steps(f, f->instructions[branch].target, -1, &tail, regions, 4) ||
        !iteration) {
        goto done;
    }
    if (regions[first_save] != 1 || regions[last_save] != 1 ||
        regions[step_save] != 1 || regions[increment_pc] != 2) {
        goto done;
    }
    *proof = (behavior_loop_budget){ first_pc, last_pc, entry + tail, iteration };
    valid = true;
done:
    free(regions);
    free(definitions);
    return valid;
}

/* Runtime loops can borrow a variable only when its owned storage cannot change
 * anywhere in this synchronous invocation. Inspection keeps its deep snapshot:
 * paused code permits callers to replace variables before continuation. Raw
 * pointer writes also block borrowing until their destination is proven disjoint. */
static void borrow_arrays(behavior_function_instruction *code)
{
    int count = acount(code);
    for (int pc = 0; pc < count; pc++) {
        behavior_function_op op = code[pc].op;
        if (op == BF_SUSPEND || op == BF_DELAY || op == BF_INVOKE ||
            op == BF_HOST_CALL || op == BF_COMMAND || op == BF_COMMAND_BEGIN ||
            op == BF_WRITE || op == BF_COMPONENT_WRITE) {
            return;
        }
    }
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction *copy = &code[pc];
        if (copy->op != BF_COPY_ARRAY || !copy->target) {
            continue;
        }
        const behavior_function_instruction *source = NULL;
        for (int at = pc - 1; at >= 0; at--) {
            if (code[at].op < BF_STORE && code[at].output == copy->left) {
                source = &code[at];
                break;
            }
        }
        if (!source || source->op != BF_ARRAY || source->target != BF_VARIABLE) {
            continue;
        }
        bool written = false;
        for (int at = 0; at < count; at++) {
            behavior_function_op op = code[at].op;
            if ((op == BF_STORE || op == BF_STORE_VALUE ||
                 op == BF_STORE_HEADER || op == BF_STORE_TYPE_NAME) &&
                code[at].target == source->left) {
                written = true;
                break;
            }
        }
        if (written) {
            continue;
        }
        /* Reloading the same immutable variable defines the existing output
         * slot without introducing an alias opcode or changing jump targets. */
        copy->op = BF_ARRAY;
        copy->left = source->left;
        copy->target = source->target;
        copy->value = source->value;
        copy->offset = source->offset;
        copy->left_slot = false;
        copy->right_slot = false;
    }
}
void behavior_function_borrow_arrays(behavior_function *f)
{
    if (!f->runtime_body || !f->runtime_instructions) {
        return;
    }
    borrow_arrays(f->runtime_body);
    borrow_arrays(f->runtime_instructions);
}
