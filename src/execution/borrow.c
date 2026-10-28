#include "execution/borrow.h"

#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/program_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

enum
{
    BORROW_DEPTH = 64,
    BORROW_FUNCTIONS = 256,
    BORROW_INSTRUCTIONS = 200000
};

/* Bytecode and Wasm also overwrite their retained scratch slots. Generated C
 * uses C locals instead; only the public activation banks apply there. */
static bool vm_disjoint(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const pin_array *array
)
{
    if (!array || !ctx->activation || !ctx->activation->vm) {
        return true;
    }
    const behavior_function_vm_storage *vm = ctx->activation->vm;
    uint64_t values = (uint64_t)acount(f->slots) + (uint64_t)acount(f->locals) + 1;
    size_t frames = (size_t)acount(f->frames);
    if (f->fast) {
        uint64_t fast = (uint64_t)acount(f->fast->slots) +
                        (uint64_t)acount(f->fast->locals) + 1;
        if (fast > values) {
            values = fast;
        }
        if ((size_t)acount(f->fast->frames) > frames) {
            frames = (size_t)acount(f->fast->frames);
        }
    }
    if (values > SIZE_MAX) {
        return false;
    }
    /* The shared guard already checked count, shape, and byte arithmetic. */
    const void *ranges[] = { array, array->stride ? array->data : (void *)array->items };
    const size_t sizes[] = { sizeof *array, pin_array_storage_bytes(array) };
    for (int j = 0; j < 2; j++) {
        if (behavior_borrow_overlap(ranges[j], sizes[j], vm, 1, sizeof *vm) ||
            behavior_borrow_overlap(
                ranges[j],
                sizes[j],
                vm->values,
                (size_t)values,
                sizeof *vm->values
            ) ||
            behavior_borrow_overlap(
                ranges[j],
                sizes[j],
                vm->parents,
                frames,
                sizeof *vm->parents
            )) {
            return false;
        }
    }
    return true;
}

/* Binding/committing even a read-only imported helper synchronizes its header
 * banks. Check those future banks before the caller starts borrowing, without
 * running binding or changing the source value before a fallback snapshot. */
static bool calls_disjoint(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const pin_array *array
)
{
    if (!array || !ctx->bind_instance) {
        return true;
    }
    const behavior_function *functions[BORROW_FUNCTIONS] = { f };
    int count = 1, edges = 0;
    for (int at = 0; at < count; at++) {
        const behavior_function *current = functions[at];
        if (!program_view_disjoint(current, ctx, array, sizeof *array) ||
            !program_view_disjoint(
                current,
                ctx,
                array->stride ? array->data : (void *)array->items,
                pin_array_storage_bytes(array)
            )) {
            return false;
        }
        for (int i = 0; i < acount(current->calls); i++) {
            const behavior_function *callee = current->calls[i].function;
            if (++edges > BORROW_INSTRUCTIONS) {
                return false;
            }
            bool seen = false;
            for (int j = 0; j < count; j++) {
                seen |= functions[j] == callee;
            }
            if (seen) {
                continue;
            }
            if (!callee || count == BORROW_FUNCTIONS) {
                return false;
            }
            functions[count++] = callee;
        }
    }
    return true;
}

bool behavior_function_borrow_argument(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const pin_value *input,
    pin_value *output
)
{
    const behavior_borrow_layout layout = {
        f->variable_count, f->result_count,   acount(f->data_values),
        acount(f->slots),  acount(f->locals), acount(f->frames)
    };
    pin_value borrowed;
    if (!program_call_views_allowed(ctx) ||
        !behavior_borrow_array(ctx, layout, input, &borrowed) ||
        !vm_disjoint(f, ctx, input->ptr) || !calls_disjoint(f, ctx, input->ptr)) {
        return false;
    }
    const pin_array *array = input->ptr;
    for (const behavior_function_context *parent = ctx->parent; parent;
         parent = parent->parent) {
        const behavior_function *owner = parent->source_function;
        if (!owner || !program_call_views_allowed(parent)) {
            return false;
        }
        const behavior_borrow_layout banks = {
            owner->variable_count,      owner->result_count,
            acount(owner->data_values), acount(owner->slots),
            acount(owner->locals),      acount(owner->frames)
        };
        if (array && (!behavior_borrow_disjoint(parent, array, sizeof *array, banks) ||
                      !behavior_borrow_disjoint(
                          parent,
                          array->stride ? array->data : (void *)array->items,
                          pin_array_storage_bytes(array),
                          banks
                      ) ||
                      !vm_disjoint(owner, parent, array) ||
                      !program_view_disjoint(owner, parent, array, sizeof *array) ||
                      !program_view_disjoint(
                          owner,
                          parent,
                          array->stride ? array->data : (void *)array->items,
                          pin_array_storage_bytes(array)
                      ))) {
            return false;
        }
    }
    *output = borrowed;
    return true;
}

const char *behavior_function_borrow_snapshot(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    const behavior_function_host_call *h = &f->host_calls[host];
    const pin_value *input = ctx->data->values + h->first;
    pin_value *output = ctx->data->values + h->result_first;
    assert(h->result_first == h->first + 1 && h->result_count == 1);
    if (behavior_function_borrow_argument(f, ctx, input, output)) {
        return NULL;
    }
    return EXECUTION_snapshot_call.value(ctx->value_storage, input, 1, output);
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

typedef struct
{
    const behavior_function *active[BORROW_DEPTH];
    int depth, functions, instructions;
    size_t nested; /* Callee fact bytes live now. */
} borrow_proof;

static bool readonly(const behavior_function *, const bool *, borrow_proof *);

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
    const bool *locals,
    borrow_proof *proof
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
            } else if (
                array(a) &&
                (i->offset == offsetof(pin_array, items) ||
                 i->offset == offsetof(pin_array, data)) &&
                i->type == BF_POINTER_TYPE
            ) {
                out.kind = ITEMS;
            } else if (array(a) && i->offset == offsetof(pin_array, stride) && i->type == BF_POINTER_TYPE) {
                out.kind = VALUE;
            } else if (
                a.kind != ITEM ||
                (i->offset != offsetof(pin_value, d) && i->offset != 0) ||
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
        case BF_ADD:
            if ((a.kind == ITEMS || a.kind == ITEM) && b.kind == VALUE) {
                out.kind = ITEM;
            } else if (a.kind != VALUE || b.kind != VALUE) {
                return false;
            }
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
        case BF_INVOKE: {
            const behavior_function_invocation *call = &f->calls[i->left];
            const behavior_function *callee = call->function;
            if (call->task || !callee || !callee->frozen ||
                call->count != callee->argument_count ||
                call->result_count != callee->result_count) {
                return false;
            }
            return readonly(callee, NULL, proof);
        }
        case BF_STORE:
        case BF_RESULT:
            /* A caller's snapshot can overlap a future imported state bank,
             * which is not bound yet. Nested callees may write private scalar
             * results, but only the guarded root may publish scalar state. */
            return (i->op != BF_STORE || proof->depth == 1) && a.kind == VALUE &&
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
            } else if (b.kind == ITEMS && other.kind == ITEMS) {
                out.kind = ITEMS;
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

static bool readonly(const behavior_function *f, const bool *keep, borrow_proof *proof)
{
    int count = acount(f->instructions);
    if (!f->runtime_body || proof->depth == BORROW_DEPTH ||
        ++proof->functions > BORROW_FUNCTIONS ||
        count > BORROW_INSTRUCTIONS - proof->instructions ||
        acount(f->slots) > BORROW_INSTRUCTIONS || acount(f->locals) > BORROW_INSTRUCTIONS ||
        (proof->depth && behavior_function_borrow_bytes(f) >
                             BEHAVIOR_BORROW_NESTED_BYTES - proof->nested)) {
        return false;
    }
    for (int j = 0; j < proof->depth; j++) {
        if (proof->active[j] == f) {
            return false;
        }
    }
    for (int j = 0; j < f->argument_count; j++) {
        if (f->argument_kinds[j] != BEHAVIOR_VALUE ||
            (!array_type(f->argument_defaults[j]) &&
             !behavior_borrow_plain(f->argument_defaults[j]))) {
            return false;
        }
    }
    for (int j = 0; j < f->result_count; j++) {
        if (f->result_kinds[j] != BEHAVIOR_VALUE ||
            !behavior_borrow_plain(f->result_defaults[j])) {
            return false;
        }
    }
    size_t frame = proof->depth ? behavior_function_borrow_bytes(f) : 0;
    proof->nested += frame;
    proof->instructions += count;
    proof->active[proof->depth++] = f;
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
        if (keep ? keep[pc] : behavior_function_runtime_keeps(f, pc)) {
            safe = instruction(f, &f->runtime_body[pc], slots, locals, proof);
        }
    }
    proof->depth--;
    proof->nested -= frame;
    free(slots);
    free(locals);
    return safe;
}

size_t behavior_function_borrow_bytes(const behavior_function *f)
{
    return ((size_t)acount(f->slots) + 1) * sizeof(borrow_value) +
           ((size_t)acount(f->locals) + 1) * sizeof(bool);
}

void behavior_function_borrow_snapshots(behavior_function *f, const bool *keep)
{
    borrow_proof proof = { 0 };
    f->readonly_collections = readonly(f, keep, &proof);
    if (!f->readonly_collections) {
        return;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (keep[pc] && snapshot(f, &f->runtime_body[pc])) {
            f->runtime_body[pc].right = BF_HOST_BORROW_ARRAY;
        }
    }
}

/* Finalization order is not call order. Publication retries the proof after all
 * callees have been verified, before compiling any owned definition. Only the
 * derived guard flags change; canonical instructions and resume maps stay put. */
void behavior_function_prepare_borrows(behavior_function *f)
{
    if (!f || !f->frozen) {
        return;
    }
    execution_prepare_argument_sharing(f);
    execution_prepare_task_dependencies(f);
    /* With every callee's argument sharing known, a raw address to a built
     * value may reach a callee that changes no argument. */
    behavior_function_note_derived(f);
    borrow_proof proof = { 0 };
    f->readonly_collections = readonly(f, NULL, &proof);
    if (!f->readonly_collections) {
        return;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (behavior_function_runtime_keeps(f, pc) &&
            snapshot(f, &f->runtime_body[pc])) {
            f->runtime_body[pc].right = BF_HOST_BORROW_ARRAY;
            f->runtime_instructions[f->runtime_map[pc]].right = BF_HOST_BORROW_ARRAY;
        }
    }
}
