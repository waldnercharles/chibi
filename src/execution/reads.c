#include "execution/definition.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static void predecessor(int *parents, int target, int pc)
{
    parents[target] = parents[target] == -1 && pc < target ? pc : -2;
}
static bool reusable_call(const behavior_function *f, const behavior_function_instruction *i)
{
    return i->op == BF_HOST_CALL && i->left == BEHAVIOR_FUNCTION_HOST_DATA &&
           f->host_calls[i->target].call->reusable &&
           !f->host_calls[i->target].call->value &&
           !f->host_calls[i->target].call->action;
}
static bool barrier(const behavior_function *f, const behavior_function_instruction *i)
{
    switch (i->op) {
        case BF_SOURCE_POINT:
        case BF_SUSPEND:
        case BF_DELAY:
        case BF_INVOKE:
        case BF_COMMAND:
        case BF_COMMAND_BEGIN: return true;
        case BF_HOST_CALL: return !reusable_call(f, i);
        default: return false;
    }
}
static bool same_access(const behavior_function_instruction *a, const behavior_function_instruction *b)
{
    return a->op == b->op && a->type == b->type && a->left == b->left &&
           a->right == b->right && a->offset == b->offset &&
           a->value.u64 == b->value.u64 && a->layout == b->layout &&
           ((a->op != BF_WORLD && a->op != BF_HOST_SLOT) || a->target == b->target);
}

/* A single forward predecessor proves that its values dominate this instruction.
 * Forks inherit those values; joins and back edges stop the search. Nothing moves
 * ahead of a check, and live continuation slots retain their original definitions.
 * Only the unobserved body changes: paused inspection can mutate the world. */
void behavior_function_reuse_reads(behavior_function *f, bool *keep)
{
    int count = acount(f->instructions), values = acount(f->slots);
    asetlen(f->runtime_body, count);
    memcpy(f->runtime_body, f->instructions, (size_t)count * sizeof *f->instructions);
    behavior_function_instruction *code = f->runtime_body;
    int *parents = xmalloc((size_t)count * sizeof *parents);
    int *slots = xmalloc((size_t)(values + 1) * sizeof *slots);
    int *banks = xmalloc((size_t)(acount(f->data_values) + 1) * sizeof *banks);
    bool *unused = xcalloc((size_t)(acount(f->data_values) + 1), sizeof *unused);
    bool *pinned = xcalloc((size_t)(values + 1), sizeof *pinned);
    int *scratch = xcalloc((size_t)(values + 1), sizeof *scratch);
    bool *writes = xcalloc((size_t)count, sizeof *writes);
    for (int pc = 0; pc < count; pc++) {
        parents[pc] = -1;
    }
    parents[0] = -2;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_op op = code[pc].op;
        if (op == BF_JUMP || op == BF_BRANCH) {
            predecessor(parents, code[pc].target, pc);
        }
        if (pc + 1 < count && op != BF_JUMP && op != BF_RETURN &&
            op != BF_END && op != BF_FAIL) {
            predecessor(parents, pc + 1, pc);
        }
    }
    for (int j = 0; j < acount(f->suspensions); j++) {
        const behavior_function_suspension *s = &f->suspensions[j];
        if (code[s->pc].op == BF_POINT) {
            continue;
        }
        for (int k = 0; k < s->value_count; k++) {
            if (s->values[k] < values) {
                pinned[s->values[k]] = true;
            }
        }
    }
    for (int i = 0; i < values; i++) {
        slots[i] = i;
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        banks[i] = i;
    }
    for (int pc = 0; pc < count; pc++) {
        if (!keep[pc]) {
            continue;
        }
        behavior_function_instruction *i = &code[pc];
        if (i->left_slot) {
            i->left = slots[i->left];
        }
        if (i->right_slot) {
            i->right = slots[i->right];
        }
        if (behavior_function_third_slot(i->op)) {
            i->target = slots[i->target];
        }
        if (i->op == BF_HOST_SLOT) {
            i->left = banks[i->left];
            scratch[i->output] = i->left + 1;
        }
        writes[pc] = (i->op == BF_WRITE && !scratch[i->right]) ||
                     i->op == BF_COMPONENT_WRITE || i->op == BF_STORE ||
                     i->op == BF_STORE_VALUE || i->op == BF_STORE_HEADER ||
                     i->op == BF_STORE_TYPE_NAME || i->op == BF_SAVE ||
                     i->op == BF_ENTER || i->op == BF_LEAVE ||
                     i->op == BF_RANDOM || i->op == BF_RANDOM_RANGE ||
                     i->op == BF_STATE_SELECT || i->op == BF_RETRIGGER;
        bool call = reusable_call(f, i);
        bool access = i->op == BF_OWNER || i->op == BF_WORLD ||
                      i->op == BF_ENTITY || i->op == BF_COMPONENT ||
                      i->op == BF_ASSERT || i->op == BF_HOST_SLOT ||
                      (i->op == BF_LOAD && scratch[i->left]);
        if (!call && (!access || (i->op != BF_ASSERT && pinned[i->output]))) {
            continue;
        }
        for (int at = parents[pc]; at >= 0; at = parents[at]) {
            if (!keep[at]) {
                continue;
            }
            const behavior_function_instruction *previous = &code[at];
            if (barrier(f, previous) || (call && writes[at])) {
                break;
            }
            if (i->op == BF_LOAD &&
                (previous->op == BF_HOST_CALL ||
                 (previous->op == BF_WRITE && previous->right == i->left))) {
                break;
            }
            if (call) {
                if (previous->op != BF_HOST_CALL || previous->node_id != i->node_id ||
                    previous->source != i->source || previous->frame != i->frame ||
                    f->host_calls[previous->target].call !=
                        f->host_calls[i->target].call) {
                    continue;
                }
                const behavior_function_host_call *from = &f->host_calls[i->target],
                                                  *to = &f->host_calls[previous->target];
                for (int k = from->first; k < from->result_first; k++) {
                    unused[k] = true;
                }
                for (int k = 0; k < from->result_count; k++) {
                    banks[from->result_first + k] = banks[to->result_first + k];
                }
            } else {
                if (!same_access(i, previous)) {
                    continue;
                }
                if (i->op != BF_ASSERT) {
                    slots[i->output] = previous->output;
                }
            }
            keep[pc] = false;
            break;
        }
    }
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &code[pc];
        if (i->op == BF_WRITE && scratch[i->right] && unused[scratch[i->right] - 1]) {
            keep[pc] = false;
        }
    }
    free(writes);
    free(scratch);
    free(pinned);
    free(unused);
    free(banks);
    free(slots);
    free(parents);
}
