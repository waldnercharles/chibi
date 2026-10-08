#include "execution/definition.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

typedef struct
{
    behavior_function *f;
    behavior_function_instruction *out;
    int *slots, *definitions;
    int cursor, accumulator, shared_begin;
} fusion;

static int emit(fusion *b, behavior_function_instruction i)
{
    apush(b->out, i);
    return acount(b->out) - 1;
}
static int typed_value(fusion *b, behavior_function_instruction i, behavior_value_kind kind)
{
    i.output = acount(b->f->slots);
    if (acount(b->f->slot_kinds) == i.output) {
        apush(b->f->slot_kinds, kind);
    }
    apush(b->f->slots, i.type);
    emit(b, i);
    return i.output;
}
static int value(fusion *b, behavior_function_instruction i)
{
    behavior_value_kind kind = i.op == BF_ROW || i.op == BF_COLUMN ||
                                       i.op == BF_COLUMN_TRY || i.op == BF_INDEX
                                 ? BEHAVIOR_ADDRESS
                                 : BEHAVIOR_VALUE;
    return typed_value(b, i, kind);
}
static int integer(fusion *b, int n)
{
    return value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = n } }
    );
}
static int local(fusion *b, int index)
{
    return value(b, (behavior_function_instruction){ .op = BF_LOCAL, .type = PIN_I32, .left = index });
}
static void save(fusion *b, int index, int slot)
{
    emit(b, (behavior_function_instruction){ .op = BF_SAVE, .type = PIN_I32, .left = slot, .target = index });
}
static int scratch(fusion *b, int frame)
{
    int index = acount(b->f->locals);
    apush(b->f->locals, ((behavior_function_local){ .initial = { .type = PIN_I32 }, .frame = frame }));
    return index;
}
static int branch(fusion *b, int slot, bool zero)
{
    int z = value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = b->f->slots[slot],
                                         .value = { .type = b->f->slots[slot] } }
    );
    int test = value(
        b,
        (
            behavior_function_instruction
        ){ .op = zero ? BF_EQ : BF_NE, .type = PIN_BOOL, .left = slot, .right = z }
    );
    return emit(b, (behavior_function_instruction){ .op = BF_BRANCH, .left = test });
}
static void bind(fusion *b, int *branches)
{
    for (int i = 0; i < acount(branches); i++) {
        b->out[branches[i]].target = acount(b->out);
    }
}
static int operand(fusion *b, int slot)
{
    if (b->slots[slot] >= 0) {
        return b->slots[slot];
    }
    assert(b->definitions[slot] >= 0);
    behavior_function_instruction i = b->f->instructions[b->definitions[slot]];
    assert(i.op == BF_CONSTANT);
    behavior_value_kind kind = b->f->slot_kinds ? b->f->slot_kinds[slot] : BEHAVIOR_VALUE;
    return b->slots[slot] = typed_value(b, i, kind);
}
static int copy(fusion *b, const behavior_reduction *r, int pc)
{
    behavior_function_instruction i = b->f->instructions[pc];
    int output = i.output;
    if (behavior_function_left_slot(i.op) && i.op != BF_LOCAL) {
        i.left = operand(b, i.left);
    }
    if (behavior_function_right_slot(i.op)) {
        i.right = operand(b, i.right);
    }
    if (i.op == BF_LOCAL) {
        assert(i.left == r->index_local || i.left == r->count_local);
        i.left = i.left == r->index_local ? b->cursor : b->accumulator;
    }
    if (i.op == BF_SAVE) {
        assert(i.target == r->index_local || i.target == r->count_local);
        i.target = i.target == r->index_local ? b->cursor : b->accumulator;
    }
    if (i.op == BF_COLUMN) {
        i.op = BF_COLUMN_TRY;
    }
    if (i.op == BF_ASSERT) {
        return branch(b, i.left, true);
    }
    /* The speculative prelude has no effects and all columns are checked
     * before the row loop. Share layout lookups and snapshot metadata there;
     * source-site assertions remain in place for the ordinary fallback. */
    if (b->shared_begin >= 0 &&
        (i.op == BF_WORLD || i.op == BF_COLUMN_TRY || i.op == BF_READ)) {
        for (int pc = b->shared_begin; pc < acount(b->out); pc++) {
            const behavior_function_instruction *prior = &b->out[pc];
            if (prior->op == i.op && prior->type == i.type &&
                (i.op == BF_WORLD ||
                 (prior->left == i.left && prior->offset == i.offset &&
                  (i.op == BF_READ ||
                   (prior->value.u64 == i.value.u64 && prior->layout == i.layout &&
                    prior->component == i.component))))) {
                b->slots[output] = prior->output;
                return pc;
            }
        }
    }
    if (i.op < BF_STORE) {
        behavior_value_kind kind = b->f->slot_kinds ? b->f->slot_kinds[output]
                                                    : BEHAVIOR_VALUE;
        b->slots[output] = typed_value(b, i, kind);
        return acount(b->out) - 1;
    }
    return emit(b, i);
}

/* Only move work across reads, value bookkeeping and debugger checkpoints.
 * Writes, host calls, waits, randomness and control joins split the group. */
static bool transparent(behavior_function_op op)
{
    if (behavior_function_observer(op) || op <= BF_ARRAY) {
        return true;
    }
    switch (op) {
        case BF_CONSTANT:
        case BF_VARIABLE:
        case BF_LOCAL:
        case BF_PRESENT:
        case BF_TYPE_NAME:
        case BF_READ:
        case BF_LOAD:
        case BF_WORLD:
        case BF_ADD:
        case BF_SUB:
        case BF_MUL:
        case BF_CONVERT:
        case BF_CAST:
        case BF_LT:
        case BF_LE:
        case BF_GT:
        case BF_GE:
        case BF_EQ:
        case BF_NE:
        case BF_SAVE:
        case BF_REQUIRE:
        case BF_ASSERT:
        case BF_RESULT:
        case BF_RESULT_TYPE_NAME:
        case BF_RESULT_PRESENT:
        case BF_CHECK:
        case BF_POINT: return true;
        default: return false;
    }
}
static const char *barrier(const behavior_function *f, int first, int last)
{
    const behavior_reduction *a = &f->reductions[first], *b = &f->reductions[last];
    if (a->pointer != b->pointer) {
        return "Separate scan: the compiler cannot prove these counts share "
               "a snapshot. Pass one snapshot value to related counts when "
               "they should describe the same population.";
    }
    if (a->graph_id != b->graph_id ||
        f->instructions[a->begin].frame != f->instructions[b->begin].frame) {
        return "Separate scan: counts cross a function boundary. Keep related "
               "reductions in one function to allow fusion.";
    }
    for (int pc = f->reductions[last - 1].end; pc < b->begin; pc++) {
        if (!transparent(f->instructions[pc].op)) {
            behavior_function_op op = f->instructions[pc].op;
            return op == BF_WRITE || op == BF_STORE || op == BF_STORE_VALUE ? "Separate scan: an intervening write can change query results. Group reads before writes only when that preserves behavior."
                 : behavior_function_call(op)
                     ? "Separate scan: an intervening host call may change "
                       "query inputs. Group related reads before the call when "
                       "that preserves behavior."
                     : "Separate scan: a control or execution boundary "
                       "prevents fusion. Keep related reads in one "
                       "uninterrupted expression when possible.";
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if ((i->op == BF_BRANCH || i->op == BF_JUMP) && i->target > a->begin &&
            i->target <= b->end) {
            bool internal = false;
            for (int j = first; j <= last; j++) {
                const behavior_reduction *r = &f->reductions[j];
                internal |= pc >= r->loop && pc < r->end &&
                            i->target >= r->loop && i->target <= r->end;
            }
            if (!internal) {
                return "Separate scan: control flow can enter between these "
                       "counts. Fusion would evaluate queries on a path that "
                       "may not execute them.";
            }
        }
    }
    return NULL;
}

/* A row lookup only tests occupancy and computes an address in a checked
 * column. Repeated lookups may run before predicate short-circuits, but field
 * reads stay behind their original presence guards. This makes the shared row
 * dominate every consumer without speculating a null dereference. */
static void shared_rows(fusion *b, int first, int last)
{
    int *rows = NULL;
    const behavior_reduction *a = &b->f->reductions[first];
    for (int j = first; j <= last; j++) {
        const behavior_reduction *r = &b->f->reductions[j];
        b->slots[r->index] = b->slots[a->index];
        b->slots[r->entity] = b->slots[a->entity];
        for (int pc = r->body; pc < r->next; pc++) {
            const behavior_function_instruction *i = &b->f->instructions[pc];
            if (i->op == BF_ROW) {
                apush(rows, pc);
            }
        }
    }
    for (int k = 0; k < acount(rows); k++) {
        const behavior_function_instruction *i = &b->f->instructions[rows[k]];
        if (b->slots[i->output] >= 0) {
            continue;
        }
        int uses = 0;
        for (int n = k; n < acount(rows); n++) {
            const behavior_function_instruction *other = &b->f->instructions[rows[n]];
            uses += b->slots[i->left] == b->slots[other->left] &&
                    b->slots[i->right] == b->slots[other->right] &&
                    i->offset == other->offset;
        }
        if (uses < 2) {
            continue;
        }
        behavior_function_instruction shared = *i;
        shared.left = operand(b, i->left);
        shared.right = operand(b, i->right);
        int row = value(b, shared);
        for (int n = k; n < acount(rows); n++) {
            const behavior_function_instruction *other = &b->f->instructions[rows[n]];
            if (b->slots[i->left] == b->slots[other->left] &&
                b->slots[i->right] == b->slots[other->right] &&
                i->offset == other->offset) {
                b->slots[other->output] = row;
            }
        }
    }
    afree(rows);
}

static void fused_loop(fusion *b, int first, int last, int flag, const int *counts)
{
    const behavior_reduction *a = &b->f->reductions[first];
    int *slow = NULL;
    save(b, flag, integer(b, 0));
    size_t observers[] = { offsetof(behavior_function_context, inspect),
                           offsetof(behavior_function_context, watch),
                           offsetof(behavior_function_context, trace) };
    for (int k = 0; k < 3; k++) {
        int observed = value(
            b,
            (behavior_function_instruction){ .op = BF_CONTEXT,
                                             .type = BF_POINTER_TYPE,
                                             .offset = observers[k] }
        );
        apush(slow, branch(b, observed, false));
    }
    /* Guards never fail or publish values. The original preludes still report
     * errors at their original sites if a service or layout is unavailable. */
    b->shared_begin = acount(b->out);
    for (int j = first; j <= last; j++) {
        const behavior_reduction *r = &b->f->reductions[j];
        b->accumulator = counts[j];
        b->slots[r->pointer] = r->pointer;
        for (int pc = r->begin; pc < r->loop; pc++) {
            int emitted = copy(b, r, pc);
            if (b->f->instructions[pc].op == BF_ASSERT) {
                apush(slow, emitted);
            }
        }
    }
    b->shared_begin = -1;
    b->accumulator = counts[first];
    int loop = acount(b->out), finished = -1, stale = -1;
    for (int pc = a->loop; pc < a->body; pc++) {
        int emitted = copy(b, a, pc);
        if (b->f->instructions[pc].op == BF_BRANCH) {
            if (b->f->instructions[pc].target == a->end) {
                finished = emitted;
            } else {
                stale = emitted;
            }
        }
    }
    assert(finished >= 0 && stale >= 0);
    shared_rows(b, first, last);
    for (int j = first; j <= last; j++) {
        const behavior_reduction *r = &b->f->reductions[j];
        b->accumulator = counts[j];
        b->slots[r->index] = b->slots[a->index];
        b->slots[r->entity] = b->slots[a->entity];
        int *next = NULL;
        for (int pc = r->body; pc < r->next; pc++) {
            const behavior_function_instruction *i = &b->f->instructions[pc];
            if (i->op == BF_ROW && b->slots[i->output] >= 0) {
                continue;
            }
            int emitted = copy(b, r, pc);
            if (b->f->instructions[pc].op == BF_BRANCH) {
                assert(b->f->instructions[pc].target == r->next);
                apush(next, emitted);
            }
        }
        bind(b, next);
        afree(next);
    }
    b->out[stale].target = acount(b->out);
    for (int pc = a->next; pc < a->end; pc++) {
        int emitted = copy(b, a, pc);
        if (b->f->instructions[pc].op == BF_JUMP) {
            b->out[emitted].target = loop;
        }
    }
    b->out[finished].target = acount(b->out);
    save(b, flag, integer(b, 1));
    emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = -a->end - 1 });
    bind(b, slow);
    afree(slow);
}

const compile_diagnostic *behavior_function_query_diagnostics(const behavior_function *f, int *count)
{
    *count = f ? acount(f->query_diagnostics) : 0;
    return f ? f->query_diagnostics : NULL;
}
void behavior_function_fuse_queries(behavior_function *f)
{
    int count = acount(f->reductions);
    if (count < 2) {
        afree(f->reductions);
        return;
    }
    fusion b = { .f = f, .shared_begin = -1 };
    int slots = acount(f->slots), size = acount(f->instructions);
    b.slots = xmalloc((size_t)slots * sizeof *b.slots);
    b.definitions = xmalloc((size_t)slots * sizeof *b.definitions);
    for (int i = 0; i < slots; i++) {
        b.slots[i] = b.definitions[i] = -1;
    }
    for (int pc = 0; pc < size; pc++) {
        if (f->instructions[pc].op < BF_STORE) {
            b.definitions[f->instructions[pc].output] = pc;
        }
    }
    int *ends = xmalloc((size_t)count * sizeof *ends);
    int *flags = xmalloc((size_t)count * sizeof *flags);
    int *counts = xmalloc((size_t)count * sizeof *counts);
    for (int first = 0; first < count;) {
        int last = first;
        const char *reason = NULL;
        while (last + 1 < count && !(reason = barrier(f, first, last + 1))) {
            last++;
        }
        int frame = f->instructions[f->reductions[first].begin].frame;
        int flag = last > first ? scratch(&b, frame) : -1;
        for (int i = first; i <= last; i++) {
            ends[i] = i == first ? last : -1;
            flags[i] = flag;
            counts[i] = flag >= 0 ? scratch(&b, frame) : -1;
            behavior_reduction *r = &f->reductions[i];
            compile_diagnostic d = { .severity = flag >= 0 ? CMPL_NOTE : CMPL_WARNING,
                                     .rule = flag >= 0 ? CMPL_RULE_QUERY_FUSION
                                                       : CMPL_RULE_QUERY_SCAN,
                                     .graph_id = r->graph_id,
                                     .node_id = r->node_id,
                                     .pin_id = r->pin_id };
            if (flag >= 0) {
                snprintf(
                    d.message,
                    sizeof d.message,
                    "%d compatible counts share one entity traversal when "
                    "inspection is inactive.",
                    last - first + 1
                );
            } else {
                if (!reason && first) {
                    reason = barrier(f, first - 1, first);
                }
                snprintf(d.message, sizeof d.message, "%s", reason ? reason : "Separate scan: no compatible adjacent reduction.");
            }
            apush(f->query_diagnostics, d);
        }
        first = last + 1;
    }
    int *pcs = xmalloc((size_t)(size + 1) * sizeof *pcs);
    for (int pc = 0; pc < size; pc++) {
        for (int j = 0; j < count; j++) {
            const behavior_reduction *r = &f->reductions[j];
            if (flags[j] < 0 || pc != r->loop) {
                continue;
            }
            if (ends[j] > j) {
                for (int k = 0; k < slots; k++) {
                    b.slots[k] = -1;
                }
                b.cursor = scratch(&b, f->instructions[r->begin].frame);
                fused_loop(&b, j, ends[j], flags[j], counts);
            } else {
                int test = branch(&b, local(&b, flags[j]), false);
                b.out[test].target = -r->end - 1;
            }
        }
        pcs[pc] = acount(b.out);
        for (int j = 0; j < count; j++) {
            const behavior_reduction *r = &f->reductions[j];
            if (flags[j] >= 0 && pc == r->end) {
                int skip = branch(&b, local(&b, flags[j]), true);
                save(&b, r->count_local, local(&b, counts[j]));
                b.out[skip].target = acount(b.out);
            }
        }
        behavior_function_instruction i = f->instructions[pc];
        if (i.op == BF_BRANCH || i.op == BF_JUMP) {
            i.target = -i.target - 1;
        }
        emit(&b, i);
    }
    pcs[size] = acount(b.out);
    for (int pc = 0; pc < acount(b.out); pc++) {
        behavior_function_instruction *i = &b.out[pc];
        if ((i->op == BF_BRANCH || i->op == BF_JUMP) && i->target < 0) {
            i->target = pcs[-i->target - 1];
        }
    }
    afree(f->instructions);
    f->instructions = b.out;
    free(pcs);
    free(ends);
    free(flags);
    free(counts);
    free(b.slots);
    free(b.definitions);
    afree(f->reductions);
}
