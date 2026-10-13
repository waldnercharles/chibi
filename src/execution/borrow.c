#include "execution/borrow.h"

#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

const char *behavior_function_borrow_snapshot(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    const behavior_function_host_call *h = &f->host_calls[host];
    const pin_value *input = ctx->data->values + h->first;
    pin_value *output = ctx->data->values + h->result_first;
    assert(h->result_first == h->first + 1 && h->result_count == 1);
    return behavior_borrow_array(ctx, f->variable_count, f->result_count, acount(f->data_values), input, output)
             ? NULL
             : EXECUTION_snapshot_call.value(ctx->value_storage, input, 1, output);
}

typedef enum
{
    UNKNOWN,
    VALUE,
    EMPTY,
    ARRAY,
    ITEMS,
    ITEM,
    BANK,
    CONTEXT
} borrow_kind;

typedef struct
{
    borrow_kind kind;
    int bank;
} borrow_value;

static bool array_type(pin_value value)
{
    if (value.container != PIN_CONTAINER_ARRAY) {
        return false;
    }
    value.container = PIN_CONTAINER_SINGLE;
    return behavior_borrow_plain(value);
}

static bool array(borrow_value value)
{
    return value.kind == ARRAY || value.kind == EMPTY;
}

static bool snapshot(const behavior_function *f, const behavior_function_instruction *i)
{
    if (i->op != BF_HOST_CALL || i->left != BEHAVIOR_FUNCTION_HOST_DATA) {
        return false;
    }
    const behavior_function_host_call *h = &f->host_calls[i->target];
    const node_host_call *call = h->call, *canonical = &EXECUTION_snapshot_call;
    if (h->capability_id != execution_intrinsic_id(canonical) ||
        h->capability_abi != execution_intrinsic_abi(h->capability_id) ||
        call->service || call->missing_service || call->invoke ||
        call->context || call->action || call->value != canonical->value ||
        call->optional_service || call->variadic || call->reusable ||
        h->result_first != h->first + 1 || h->result_count != 1) {
        return false;
    }
    pin_value input = f->data_values[h->first].initial,
              output = f->data_values[h->result_first].initial;
    return array_type(input) && array_type(output) && input.type == output.type &&
           behavior_borrow_name(input.type_name, output.type_name);
}

/* The whole synchronous body must leave every collection owner unchanged.
 * Locals may alias arrays, but addresses can only reach count/items/element
 * reads or private snapshot payload banks. In particular, capacity, pointer
 * identity, arbitrary writes, managed results and opaque effects are excluded. */
static bool instruction(
    const behavior_function *f,
    const behavior_function_instruction *i,
    borrow_value *slots,
    const bool *locals
)
{
    if (i->indirect) {
        return false;
    }
    borrow_value a = i->left_slot ? slots[i->left] : (borrow_value){ 0 },
                 b = i->right_slot ? slots[i->right] : (borrow_value){ 0 },
                 out = { .kind = VALUE };
    switch (i->op) {
        case BF_CONSTANT:
            if (f->slot_kinds[i->output] == BEHAVIOR_ADDRESS) {
                if (i->value.u64) {
                    return false;
                }
                out.kind = EMPTY;
            } else if (!behavior_borrow_plain(i->value)) {
                return false;
            }
            break;
        case BF_VARIABLE:
        case BF_ARGUMENT: {
            pin_value value = (i->op == BF_VARIABLE ? f->variable_defaults : f->argument_defaults)[i->left];
            if (i->indirect || !behavior_borrow_plain(value)) {
                return false;
            }
            break;
        }
        case BF_DELTA:
        case BF_OWNER: break;
        case BF_CONTEXT:
            if (i->target == 1) {
                out.kind = CONTEXT;
            } else if (!((i->type == PIN_BOOL &&
                          i->offset == offsetof(behavior_function_context, exit_requested)) ||
                         (i->type == PIN_U64 &&
                          i->offset ==
                              offsetof(behavior_function_context, returned_outcome)))) {
                return false;
            }
            break;
        case BF_ARRAY:
            if (!array_type(i->value)) {
                return false;
            }
            out.kind = ARRAY;
            break;
        case BF_LOCAL:
            if (f->locals[i->left].kind == BEHAVIOR_ADDRESS) {
                if (!locals[i->left]) {
                    return false;
                }
                out.kind = ARRAY;
            } else if (!behavior_borrow_plain(f->locals[i->left].initial)) {
                return false;
            }
            break;
        case BF_SAVE:
            return a.kind == VALUE ? f->locals[i->target].kind == BEHAVIOR_VALUE
                                   : array(a) && locals[i->target];
        case BF_HOST_SLOT:
            if (i->target ||
                (!array_type(f->data_values[i->left].initial) &&
                 !behavior_borrow_plain(f->data_values[i->left].initial)) ||
                f->data_values[i->left].kind != BEHAVIOR_VALUE) {
                return false;
            }
            out = (borrow_value){ .kind = BANK, .bank = i->left };
            break;
        case BF_LOAD:
        case BF_READ:
            if (a.kind == BANK && i->offset == offsetof(pin_value, d)) {
                pin_value header = f->data_values[a.bank].initial;
                bool aggregate = array_type(header);
                if (i->type != (aggregate ? BF_POINTER_TYPE : header.type)) {
                    return false;
                }
                out.kind = aggregate ? ARRAY : VALUE;
            } else if (array(a) && i->offset == offsetof(pin_array, count) && i->type == PIN_I32) {
                out.kind = VALUE;
            } else if (array(a) && i->offset == offsetof(pin_array, items) && i->type == BF_POINTER_TYPE) {
                out.kind = ITEMS;
            } else if (
                a.kind != ITEM || i->offset != offsetof(pin_value, d) ||
                f->slot_kinds[i->output] != BEHAVIOR_VALUE ||
                !behavior_borrow_plain((pin_value){ .type = i->type })
            ) {
                return false;
            }
            break;
        case BF_INDEX:
            if (a.kind != ITEMS || b.kind != VALUE || i->offset != sizeof(pin_value)) {
                return false;
            }
            out.kind = ITEM;
            break;
        case BF_WRITE:
            if (b.kind == CONTEXT) {
                return a.kind == VALUE &&
                       ((i->type == PIN_BOOL &&
                         i->offset == offsetof(behavior_function_context, exit_requested)) ||
                        (i->type == PIN_U64 &&
                         i->offset ==
                             offsetof(behavior_function_context, returned_outcome)));
            }
            if (b.kind != BANK || i->offset != offsetof(pin_value, d)) {
                return false;
            }
            pin_value header = f->data_values[b.bank].initial;
            return array_type(header) ? array(a) && i->type == BF_POINTER_TYPE
                                      : a.kind == VALUE && i->type == header.type;
        case BF_HOST_CALL: return snapshot(f, i);
        case BF_STORE:
        case BF_RESULT:
            return a.kind == VALUE &&
                   behavior_borrow_plain((
                       i->op == BF_STORE ? f->variable_defaults : f->result_defaults
                   )[i->target]);
        case BF_ASSERT: return a.kind == VALUE || array(a);
        case BF_EQ:
        case BF_NE:
            if (a.kind != VALUE || b.kind != VALUE) {
                if (!array(a) || !array(b) || (a.kind != EMPTY && b.kind != EMPTY)) {
                    return false;
                }
            }
            break;
        case BF_SELECT: {
            borrow_value other = slots[i->target];
            if (a.kind != VALUE) {
                return false;
            }
            if (array(b) && array(other)) {
                out.kind = ARRAY;
            } else if (b.kind != VALUE || other.kind != VALUE) {
                return false;
            }
            break;
        }
        case BF_BRANCH: return a.kind == VALUE;
        case BF_CHECK:
        case BF_CALL_CHECK:
        case BF_JUMP:
        case BF_FAIL:
        case BF_RETURN:
        case BF_END: return true;
        default:
            if (i->op < BF_ADD || i->op > BF_RANDOM_RANGE ||
                (i->left_slot && a.kind != VALUE) ||
                (i->right_slot && b.kind != VALUE) ||
                (behavior_function_third_slot(i->op) && slots[i->target].kind != VALUE)) {
                return false;
            }
            break;
    }
    if (i->op < BF_STORE) {
        if (slots[i->output].kind != UNKNOWN) {
            return false;
        }
        slots[i->output] = out;
        if ((i->op == BF_DIV || i->op == BF_MOD) &&
            behavior_scalar_integer(i->type) && i->target >= 0) {
            if (slots[i->target].kind != UNKNOWN) {
                return false;
            }
            slots[i->target].kind = VALUE;
        }
    }
    return true;
}

void behavior_function_borrow_snapshots(behavior_function *f, const bool *keep)
{
    int count = acount(f->instructions);
    bool candidate = false;
    for (int pc = 0; pc < count; pc++) {
        candidate |= keep[pc] && snapshot(f, &f->runtime_body[pc]);
    }
    if (!candidate) {
        return;
    }
    bool *locals = xcalloc((size_t)acount(f->locals) + 1, sizeof *locals);
    for (int j = 0; j < acount(f->value_roots); j++) {
        const behavior_value_root *root = &f->value_roots[j];
        if (root->bank == BEHAVIOR_BANK_LOCAL && array_type(root->shape)) {
            locals[root->index] = true;
        }
    }
    borrow_value *slots = xcalloc((size_t)acount(f->slots) + 1, sizeof *slots);
    bool safe = true;
    for (int pc = 0; pc < count && safe; pc++) {
        if (keep[pc]) {
            safe = instruction(f, &f->runtime_body[pc], slots, locals);
        }
    }
    if (safe) {
        for (int pc = 0; pc < count; pc++) {
            if (keep[pc] && f->runtime_body[pc].op == BF_HOST_CALL) {
                f->runtime_body[pc].right = BF_HOST_BORROW_ARRAY;
            }
        }
    }
    free(slots);
    free(locals);
}
