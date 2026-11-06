#include "execution/definition.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Address provenance is deliberately conservative. Locals, casts and arbitrary
 * handles do not establish ownership. Unsupported operations remain serial. */
typedef struct
{
    bool owner, scratch;
    int component; /* one-based component table index */
    int host;      /* one-based host value bank index */
} origin;

static void mask_predecessor(int *parents, int target, int pc)
{
    parents[target] = parents[target] == -1 && pc < target ? pc : -2;
}

static unsigned constant_mask(
    const behavior_function_instruction *code,
    const int *parents,
    const int *definitions,
    int slot
)
{
    while (definitions[slot] >= 0) {
        int pc = definitions[slot];
        const behavior_function_instruction *value = &code[pc];
        if (value->op == BF_CONSTANT && value->type == PIN_U32) {
            return value->value.u;
        }
        if (value->op != BF_LOCAL || value->type != PIN_U32) {
            break;
        }
        /* Inlined function parameters are mutable locals. Follow only a
         * single forward path to their last assignment, never a join/loop. */
        bool found = false;
        for (pc = parents[pc]; pc >= 0; pc = parents[pc]) {
            const behavior_function_instruction *i = &code[pc];
            if (i->op == BF_SAVE && i->target == value->left) {
                slot = i->left;
                found = true;
                break;
            }
            if (i->op == BF_ENTER) {
                break;
            }
        }
        if (!found) {
            break;
        }
    }
    return UINT_MAX;
}

static bool same_host(const node_host_call *a, const node_host_call *b)
{
    return a->invoke == b->invoke && a->value == b->value &&
           a->context == b->context && a->action == b->action &&
           a->optional_service == b->optional_service &&
           a->variadic == b->variadic && a->reusable == b->reusable &&
           a->language_values == b->language_values &&
           a->owned_result == b->owned_result &&
           ((!a->service && !b->service) ||
            (a->service && b->service && !strcmp(a->service, b->service)));
}

unsigned behavior_function_host_mask(const behavior_function *f, const node_host_call *call, int input)
{
    assert(input >= 0);
    f = f->fast ? f->fast : f;
    if (acount(f->calls)) {
        return UINT_MAX;
    }
    if (!acount(f->host_calls)) {
        return 0;
    }
    const behavior_function_instruction *code = f->runtime_instructions
                                                  ? f->runtime_instructions
                                                  : f->instructions;
    int count = acount(code);
    int *parents = xmalloc((size_t)count * sizeof *parents);
    int *definitions = xmalloc((size_t)(acount(f->slots) + 1) * sizeof *definitions);
    memset(parents, 0xff, (size_t)count * sizeof *parents);
    memset(definitions, 0xff, (size_t)(acount(f->slots) + 1) * sizeof *definitions);
    parents[0] = -2;
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &code[pc];
        if (i->op < BF_STORE) {
            definitions[i->output] = pc;
        }
        if (i->op == BF_JUMP || i->op == BF_BRANCH) {
            mask_predecessor(parents, i->target, pc);
        }
        if (pc + 1 < count && i->op != BF_JUMP && i->op != BF_RETURN &&
            i->op != BF_END && i->op != BF_FAIL) {
            mask_predecessor(parents, pc + 1, pc);
        }
    }
    unsigned mask = 0;
    for (int pc = 0; pc < count; pc++) {
        if (code[pc].op != BF_HOST_CALL ||
            !same_host(f->host_calls[code[pc].target].call, call)) {
            continue;
        }
        const behavior_function_host_call *host = &f->host_calls[code[pc].target];
        assert(host->first + input < host->result_first);
        unsigned value = UINT_MAX;
        for (int before = parents[pc]; before >= 0; before = parents[before]) {
            const behavior_function_instruction *i = &code[before];
            if (i->op != BF_WRITE || i->offset != offsetof(pin_value, u) ||
                definitions[i->right] < 0) {
                continue;
            }
            const behavior_function_instruction *address = &code[definitions[i->right]];
            if (address->op == BF_HOST_SLOT && !address->target &&
                address->left == host->first + input) {
                value = i->type == PIN_U32
                          ? constant_mask(code, parents, definitions, i->left)
                          : UINT_MAX;
                break;
            }
        }
        mask |= value;
    }
    free(definitions);
    free(parents);
    return mask;
}

void behavior_function_parallel_proof(behavior_function *f)
{
    if (!f->batchable) {
        return;
    }
    origin *slots = xcalloc((size_t)acount(f->slots) + 1, sizeof *slots);
    bool *targets = xcalloc((size_t)acount(f->data_values) + 1, sizeof *targets);
    bool valid = true;
    for (int pc = 0; valid && pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        origin out = { 0 }, left = slots[i->left_slot ? i->left : 0],
               right = slots[i->right_slot ? i->right : 0];
        switch (i->op) {
            case BF_OWNER: out.owner = true; break;
            case BF_ENTITY: out.owner = right.owner; break;
            case BF_COMPONENT_READ:
            case BF_COMPONENT_WRITE:
                /* Preserve the generic getter's conservative scheduling proof. */
                valid = false;
                break;
            case BF_COMPONENT:
                out.owner = right.owner;
                out.component = i->component + 1;
                if (!right.owner) {
                    apush(f->parallel_reads, i->value.u64);
                }
                break;
            case BF_COLUMN:
            case BF_COLUMN_TRY:
                out.component = i->component + 1;
                apush(f->parallel_reads, i->value.u64);
                break;
            case BF_ROW:
                out.component = left.component;
                out.owner = right.owner;
                break;
            case BF_HOST_SLOT:
                out.scratch = true;
                out.host = i->target ? 0 : i->left + 1;
                break;
            case BF_INDEX: out.scratch = left.scratch; break;
            case BF_WRITE:
                valid = right.scratch || (right.component && right.owner);
                if (right.host && i->offset == offsetof(pin_value, u64)) {
                    targets[right.host - 1] = left.owner;
                }
                break;
            case BF_HOST_CALL: {
                const behavior_function_host_call *call = &f->host_calls[i->target];
                const node_access *a = call->access;
                if (!a || (a->flags & NODE_EFFECT_STRUCTURAL) ||
                    ((a->flags & NODE_EFFECT_HOST_SINK) &&
                     !(a->flags & NODE_EFFECT_PARALLEL))) {
                    valid = false;
                    break;
                }
                if (a->flags & NODE_EFFECT_PURE) {
                    break;
                }
                valid = (a->flags & NODE_EFFECT_PARALLEL) != 0;
                if (a->flags & NODE_EFFECT_ENTITY_SCOPED) {
                    valid &= a->target_input > 0 &&
                             call->first + a->target_input <= call->result_first &&
                             targets[call->first + a->target_input - 1];
                } else if (a->write_count) {
                    valid = false;
                }
                for (int j = 0; j < a->read_count; j++) {
                    apush(f->parallel_reads, a->reads[j]);
                }
                for (int j = 0; a->read_refs && a->read_refs[j]; j++) {
                    apush(f->parallel_reads, *a->read_refs[j]);
                }
                for (int j = 0; j < a->write_count; j++) {
                    apush(f->component_writes, a->writes[j]);
                }
                break;
            }
            case BF_RANDOM:
            case BF_RANDOM_RANGE:
            case BF_STATE_SELECT: /* Random selection can use the global RNG. */
            case BF_INTERN_STRING:
            case BF_RETRIGGER:
            case BF_QUERY:
            case BF_COMMAND:
            case BF_COMMAND_BEGIN:
            case BF_INVOKE:
            case BF_SUSPEND:
            case BF_DELAY: valid = false; break;
            case BF_JUMP:
            case BF_BRANCH:
            case BF_ENTER:
                memset(targets, 0, ((size_t)acount(f->data_values) + 1) * sizeof *targets);
                break;
            case BF_CONSTANT:
            case BF_VARIABLE:
            case BF_DELTA:
            case BF_ARGUMENT:
            case BF_LOCAL:
            case BF_PRESENT:
            case BF_TYPE_NAME:
            case BF_WORLD:
            case BF_CONTEXT:
            case BF_STORAGE_NAME:
            case BF_ARRAY:
            case BF_QUERY_VARIABLE:
            case BF_HOST_SERVICE:
            case BF_OWN_NAME:
            case BF_COPY_STRING:
            case BF_COPY_ARRAY:
            case BF_STRING_EQUAL:
            case BF_ADD:
            case BF_SUB:
            case BF_MUL:
            case BF_DIV:
            case BF_MOD:
            case BF_NEG:
            case BF_ABS:
            case BF_MIN:
            case BF_MAX:
            case BF_CONVERT:
            case BF_CAST:
            case BF_FLAGS:
            case BF_LT:
            case BF_LE:
            case BF_GT:
            case BF_GE:
            case BF_EQ:
            case BF_NE:
            case BF_SELECT:
            case BF_BIT_AND:
            case BF_BIT_OR:
            case BF_BIT_XOR:
            case BF_BIT_NOT:
            case BF_SHL:
            case BF_SHR:
            case BF_LOAD:
            case BF_READ:
            case BF_STORE:
            case BF_STORE_VALUE:
            case BF_SAVE:
            case BF_STORE_HEADER:
            case BF_STORE_TYPE_NAME:
            case BF_REQUIRE:
            case BF_ASSERT:
            case BF_POINT:
            case BF_CHECK:
            case BF_CALL_CHECK:
            case BF_LEAVE:
            case BF_FAIL:
            case BF_RETURN:
            case BF_END: break;
            default:
                valid = behavior_function_math(i->op) ||
                        behavior_function_observer(i->op);
                break;
        }
        if (i->op < BF_STORE) {
            slots[i->output] = out;
        }
    }
    free(targets);
    free(slots);
    f->parallel = valid;
}
bool behavior_function_parallel(const behavior_function *f)
{
    return f && f->parallel;
}
int behavior_function_parallel_work(const behavior_function *f)
{
    f = f->fast ? f->fast : f;
    const behavior_function_instruction *code = f->runtime_instructions
                                                  ? f->runtime_instructions
                                                  : f->instructions;
    int work = 0;
    for (int i = 0; i < acount(code); i++) {
        work += behavior_function_call(code[i].op) ? 64 : 1;
    }
    return work;
}
bool behavior_function_parallel_with(const behavior_function *a, const behavior_function *b)
{
    if (!behavior_function_parallel(a) || !behavior_function_parallel(b)) {
        return false;
    }
    for (int i = 0; i < acount(a->parallel_reads); i++) {
        for (int j = 0; j < acount(b->component_writes); j++) {
            if (a->parallel_reads[i] == b->component_writes[j]) {
                return false;
            }
        }
    }
    for (int i = 0; i < acount(b->parallel_reads); i++) {
        for (int j = 0; j < acount(a->component_writes); j++) {
            if (b->parallel_reads[i] == a->component_writes[j]) {
                return false;
            }
        }
    }
    return true;
}
