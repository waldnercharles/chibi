#include "execution/module_effects.h"

#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define EFFECT_DEPTH 64
#define EFFECT_RECORDS 65536
#define EFFECT_STEPS 200000
#define SCALAR_HELPERS 64

typedef struct
{
    bool owner, context, world, constant, private, readable;
    int bank;
    uint64_t bits, component;
    size_t offset, extent;
} origin;
typedef struct
{
    execution_effect_summary *summary;
    uint64_t *reads, *writes, *reduces;
    size_t capacity, records;
    bool counting, valid, predicate, reducing;
    const behavior_function *scalar_root, *scalar_functions[SCALAR_HELPERS];
    int scalar_count, reduction_modes;
    int operation, instruction;
    uint64_t capability, function;
} effects;
static uint64_t intrinsic(const behavior_function_host_call *host)
{
    const node_host_call *actual = host->call;
    const node_host_call *canonical = execution_intrinsic_resolve(host->capability_id);
    if (!canonical ||
        host->capability_abi != execution_intrinsic_abi(host->capability_id) ||
        actual->service || actual->missing_service ||
        actual->invoke != canonical->invoke || actual->value != canonical->value ||
        actual->context != canonical->context || actual->action != canonical->action ||
        actual->optional_service != canonical->optional_service ||
        actual->variadic != canonical->variadic ||
        actual->reusable != canonical->reusable ||
        actual->language_values != canonical->language_values ||
        actual->owned_result != canonical->owned_result) {
        return 0;
    }
    int count = host->result_first - host->first;
    int expected = -1;
    switch (execution_intrinsic_kind(host->capability_id)) {
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_UNION_MAKE:
            if (count < 1 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_UNION_KEEP:
            if (count < 2 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET: {
            int minimum = execution_intrinsic_kind(host->capability_id) ==
                                  EXECUTION_INTRINSIC_UNION_TEST
                            ? 2
                            : 4;
            if (count < minimum || host->result_count != 1) {
                return 0;
            }
            break;
        }
        case EXECUTION_INTRINSIC_POINTER_MAKE: expected = 3; break;
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_WRITE: expected = 2; break;
        case EXECUTION_INTRINSIC_POINTER_FIELD: expected = 5; break;
        case EXECUTION_INTRINSIC_POINTER_REFERENCE: expected = 1; break;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: expected = 9; break;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_UNION: expected = 4; break;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_VALUE_COPY:
        case EXECUTION_INTRINSIC_VALUE_DROP: expected = 1; break;
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT: expected = 2; break;
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE: expected = 2; break;
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE: expected = 3; break;
        case EXECUTION_INTRINSIC_QUERY_SOURCE: expected = 5; break;
        case EXECUTION_INTRINSIC_QUERY_TARGET: expected = 4; break;
        case EXECUTION_INTRINSIC_COMPONENT_HAS: expected = 3; break;
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW: expected = 4; break;
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH: expected = 1; break;
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW: expected = 2; break;
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            if (count < 2 || count > 4 || host->result_count != 1) {
                return 0;
            }
            break;
        default: break;
    }
    if (expected >= 0 && (count != expected || host->result_count != 1)) {
        return 0;
    }
    return host->capability_id;
}

bool behavior_function_hosts_pure(const behavior_function *root)
{
    /* Keep discovery order separate from the pointer index. Repeated edges
     * search logarithmically; inserting a new pointer charges every move. */
    const behavior_function **functions = NULL, **sorted = NULL;
    apush(functions, root);
    apush(sorted, root);
    int steps = EFFECT_STEPS;
    bool valid = true;
    for (int at = 0; valid && at < acount(functions); at++) {
        const behavior_function *f = functions[at];
        if (!f || !f->frozen || acount(functions) > EFFECT_RECORDS ||
            acount(f->queries) || acount(f->commands)) {
            valid = false;
            break;
        }
        for (int i = 0; valid && i < acount(f->host_calls); i++) {
            const behavior_function_host_call *host = &f->host_calls[i];
            if (--steps < 0) {
                valid = false;
                break;
            }
            if (intrinsic(host)) {
                continue;
            }
            const node_access *access = host->access;
            valid = access && (access->flags & NODE_EFFECT_PURE) &&
                    !(access->flags & ~(NODE_EFFECT_PURE | NODE_EFFECT_PARALLEL)) &&
                    !access->read_count && !access->write_count &&
                    !access->read_refs && !host->call->service &&
                    !host->call->missing_service && !host->call->action;
        }
        for (int i = 0; valid && i < acount(f->calls); i++) {
            const behavior_function_invocation *call = &f->calls[i];
            if (call->task) {
                valid = false;
                break;
            }
            bool seen = false;
            int low = 0, high = acount(sorted);
            while (low < high) {
                if (--steps < 0) {
                    valid = false;
                    break;
                }
                int middle = low + (high - low) / 2;
                if (sorted[middle] == call->function) {
                    seen = true;
                    break;
                }
                if ((uintptr_t)sorted[middle] < (uintptr_t)call->function) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            if (valid && !seen) {
                int count = acount(sorted), moves = count - low;
                if (count == EFFECT_RECORDS || steps < moves) {
                    valid = false;
                    break;
                }
                steps -= moves;
                apush(functions, call->function);
                apush(sorted, call->function);
                memmove(sorted + low + 1, sorted + low, (size_t)moves * sizeof *sorted);
                sorted[low] = call->function;
            }
        }
    }
    afree(sorted);
    afree(functions);
    return valid;
}
static bool plain(pin_value v)
{
    if (v.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (v.type >= PIN_BOOL && v.type <= PIN_F64) {
        return true;
    }
    const type_desc *type = v.type == PIN_STRUCT ? pin_value_type(&v) : NULL;
    return type && type_is_plain(type);
}
static origin slot_origin(const behavior_function *, int, int, int, int *);
static bool control_write(const behavior_function_instruction *, origin);
static origin bank_origin(const behavior_function *f, int pc, int bank, int depth, int *steps)
{
    origin none = { 0 };
    if (bank < 0 || bank >= acount(f->data_values) || depth > EFFECT_DEPTH ||
        --*steps < 0) {
        return none;
    }
    bool crossing = false;
    for (int j = pc - 1; j >= 0; j--) {
        const behavior_function_instruction *i = &f->instructions[j];
        if (--*steps < 0) {
            return none;
        }
        crossing |= i->op == BF_BRANCH || i->op == BF_JUMP || i->op == BF_ENTER;
        if (i->op == BF_WRITE) {
            origin address = slot_origin(f, j, i->right, depth + 1, steps);
            if (!address.bank) {
                if (control_write(i, address)) {
                    continue;
                }
                return none;
            }
            if (address.bank == bank + 1) {
                if (crossing || address.offset > offsetof(pin_value, d) ||
                    i->offset != offsetof(pin_value, d) - address.offset) {
                    return none;
                }
                return slot_origin(f, j, i->left, depth + 1, steps);
            }
        }
        if (i->op != BF_HOST_CALL) {
            continue;
        }
        const behavior_function_host_call *call = &f->host_calls[i->target];
        if (bank < call->result_first || bank >= call->result_first + call->result_count) {
            continue;
        }
        if (crossing) {
            return none;
        }
        uint64_t id = intrinsic(call);
        if (id == execution_intrinsic_id(&EXECUTION_reference_make_call)) {
            origin kind = bank_origin(f, j, call->first + 2, depth + 1, steps);
            origin entity = bank_origin(f, j, call->first + 5, depth + 1, steps);
            origin component = bank_origin(f, j, call->first + 6, depth + 1, steps);
            origin context = bank_origin(f, j, call->first, depth + 1, steps);
            if (kind.constant && kind.bits == LANGUAGE_REFERENCE_COMPONENT &&
                component.constant && component.bits && context.context &&
                plain(f->data_values[call->first + 1].initial)) {
                return (origin){ .owner = entity.owner,
                                 .component = component.bits,
                                 .readable = true };
            }
            if (kind.constant && kind.bits == LANGUAGE_REFERENCE_VALUE &&
                context.context) {
                origin cell = bank_origin(f, j, call->first + 4, depth + 1, steps);
                if (cell.private && cell.bank) {
                    return cell;
                }
            }
            return none;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_field_call) ||
            id == execution_intrinsic_id(&EXECUTION_reference_union_call) ||
            id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
            origin source = bank_origin(f, j, call->first, depth + 1, steps);
            pin_value value = f->data_values[bank].initial;
            if (id == execution_intrinsic_id(&EXECUTION_snapshot_call) &&
                (value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE)) {
                source.readable = true;
            }
            return source;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_read_call)) {
            origin target = bank_origin(f, j, call->first, depth + 1, steps);
            pin_value value = f->data_values[bank].initial;
            const type_desc *type = value.type == PIN_STRUCT &&
                                            value.container == PIN_CONTAINER_SINGLE
                                      ? pin_value_type(&value)
                                      : NULL;
            if (target.component && type && type_is_plain(type)) {
                /* The snapshot retains its source access, with its own bounds. */
                return (origin){ .owner = target.owner,
                                 .component = target.component,
                                 .readable = true,
                                 .extent = type->size };
            }
            if (value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE) {
                return (origin){ .readable = true };
            }
        }
        pin_value value = f->data_values[bank].initial;
        if (value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE) {
            return (origin){ .readable = true };
        }
        return none;
    }
    const pin_value *v = &f->data_values[bank].initial;
    if (v->type == PIN_STRUCT || v->container != PIN_CONTAINER_SINGLE) {
        return (origin){ .readable = true };
    }
    if (v->container == PIN_CONTAINER_SINGLE && v->type >= PIN_BOOL &&
        v->type <= PIN_HANDLE && v->type != PIN_STR && v->type != PIN_NAME) {
        return (origin){ .constant = true, .bits = v->u64 };
    }
    return none;
}
static origin slot_origin(const behavior_function *f, int pc, int slot, int depth, int *steps)
{
    origin none = { 0 };
    if (slot < 0 || slot >= acount(f->slots) || depth > EFFECT_DEPTH || --*steps < 0) {
        return none;
    }
    for (int j = pc - 1; j >= 0; j--) {
        const behavior_function_instruction *i = &f->instructions[j];
        if (--*steps < 0) {
            return none;
        }
        if (i->op == BF_BRANCH || i->op == BF_JUMP || i->op == BF_ENTER) {
            return none;
        }
        if (i->op >= BF_STORE || i->output != slot) {
            continue;
        }
        switch (i->op) {
            case BF_OWNER: return (origin){ .owner = true };
            case BF_CONTEXT:
                return (
                    origin
                ){ .context = i->target == 1,
                   .private = !i->target &&
                              i->offset == offsetof(behavior_function_context, results),
                   .readable = !i->target &&
                               i->offset == offsetof(behavior_function_context, arguments),
                   .extent = !i->target && i->offset ==
                                               offsetof(behavior_function_context, results)
                               ? (size_t)f->result_count * sizeof(pin_value)
                               : 0 };
            case BF_ARGUMENT: {
                pin_value value = f->argument_defaults[i->left];
                return (origin){ .readable = value.type == PIN_STRUCT ||
                                             value.container != PIN_CONTAINER_SINGLE };
            }
            case BF_ARRAY:
                return (origin){ .readable = i->target == BF_ARGUMENT ||
                                             i->target == BF_CONSTANT };
            case BF_SELECT: {
                origin yes = slot_origin(f, j, i->right, depth + 1, steps);
                origin no = slot_origin(f, j, i->target, depth + 1, steps);
                /* Packed and boxed arrays select between two payloads. Both
                 * alternatives must independently prove read-only access. */
                if (yes.readable && no.readable && !yes.context && !no.context &&
                    !yes.private && !no.private && !yes.bank && !no.bank &&
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) ==
                        BEHAVIOR_ADDRESS) {
                    return (origin){ .readable = true };
                }
                return none;
            }
            case BF_LOCAL:
                for (int at = j - 1; at >= 0; at--) {
                    const behavior_function_instruction *save = &f->instructions[at];
                    if (--*steps < 0 || save->op == BF_BRANCH || save->op == BF_JUMP ||
                        save->op == BF_ENTER || save->op == BF_LEAVE) {
                        return none;
                    }
                    if (save->op == BF_SAVE && save->target == i->left) {
                        return slot_origin(f, at, save->left, depth + 1, steps);
                    }
                }
                return none;
            case BF_WORLD: return (origin){ .world = true };
            case BF_ENTITY: {
                origin world = slot_origin(f, j, i->left, depth + 1, steps);
                origin entity = slot_origin(f, j, i->right, depth + 1, steps);
                return (origin){ .owner = world.world && entity.owner };
            }
            case BF_COMPONENT: {
                origin world = slot_origin(f, j, i->left, depth + 1, steps);
                origin entity = slot_origin(f, j, i->right, depth + 1, steps);
                return (origin){ .owner = world.world && entity.owner,
                                 .readable = world.world,
                                 .component = i->value.u64,
                                 .extent = i->type == PIN_BOOL ? 0 : i->offset };
            }
            case BF_ADD: {
                origin address = slot_origin(f, j, i->left, depth + 1, steps);
                origin offset = slot_origin(f, j, i->right, depth + 1, steps);
                if (address.readable && !address.private && !address.extent) {
                    return (origin){ .readable = true };
                }
                if (!offset.constant || !address.extent ||
                    offset.bits > address.extent - address.offset ||
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) !=
                        BEHAVIOR_ADDRESS) {
                    return none;
                }
                address.offset += (size_t)offset.bits;
                return address;
            }
            case BF_INDEX: {
                origin address = slot_origin(f, j, i->left, depth + 1, steps);
                origin index = slot_origin(f, j, i->right, depth + 1, steps);
                if (address.readable && !address.private) {
                    return (origin){ .readable = true };
                }
                if (!address.private || !index.constant || !i->offset ||
                    index.bits > (address.extent - address.offset) / i->offset) {
                    return none;
                }
                address.offset += (size_t)index.bits * i->offset;
                return address;
            }
            case BF_CONSTANT:
                return (origin){ .constant = true, .bits = i->value.u64 };
            case BF_HOST_SLOT:
                return (origin){ .bank = i->target == 0 ? i->left + 1 : 0,
                                 .private = i->target == 0,
                                 .readable = true,
                                 .extent = sizeof(pin_value) };
            case BF_LOAD: {
                origin address = slot_origin(f, j, i->left, depth + 1, steps);
                if (address.bank && address.offset <= offsetof(pin_value, d) &&
                    i->offset == offsetof(pin_value, d) - address.offset) {
                    return bank_origin(f, j, address.bank - 1, depth + 1, steps);
                }
                if ((address.private || address.readable) &&
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) ==
                        BEHAVIOR_ADDRESS) {
                    return (origin){ .readable = true };
                }
                return none;
            }
            default: return none;
        }
    }
    return none;
}
static bool context_bank(const behavior_function *f, int bank)
{
    bool found = false;
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *host = &f->host_calls[i];
        if (bank < host->first || bank >= host->result_first + host->result_count) {
            continue;
        }
        uint64_t id = intrinsic(host);
        if (bank != host->first ||
            (id != execution_intrinsic_id(&EXECUTION_reference_make_call) &&
             id != execution_intrinsic_id(&EXECUTION_query_source_call) &&
             id != execution_intrinsic_id(&EXECUTION_query_target_call) &&
             id != execution_intrinsic_id(&EXECUTION_component_has_call) &&
             id != execution_intrinsic_id(&EXECUTION_local_release_call))) {
            return false;
        }
        found = true;
    }
    for (int i = 0; i < acount(f->calls); i++) {
        const behavior_function_invocation *call = &f->calls[i];
        if (bank >= call->first && bank < call->result_first + call->result_count) {
            return false;
        }
    }
    return found;
}
static bool control_write(const behavior_function_instruction *i, origin address)
{
    return address.context &&
           ((i->type == PIN_BOOL &&
             i->offset == offsetof(behavior_function_context, exit_requested)) ||
            (i->type == PIN_U64 &&
             i->offset == offsetof(behavior_function_context, returned_outcome)));
}
static bool context_uses(const behavior_function *f, int slot, int *steps)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (--*steps < 0) {
            return false;
        }
        bool left = i->left_slot && i->left == slot;
        bool right = i->right_slot && i->right == slot;
        bool third = behavior_function_third_slot(i->op) && i->target == slot;
        if (!left && !right && !third) {
            continue;
        }
        if (i->op == BF_WRITE && right && !left && !third &&
            control_write(i, (origin){ .context = true })) {
            continue;
        }
        if (i->op != BF_WRITE || !left || right || third) {
            return false;
        }
        origin address = slot_origin(f, pc, i->right, 0, steps);
        if (!address.bank || address.offset > offsetof(pin_value, d) ||
            i->offset != offsetof(pin_value, d) - address.offset ||
            !context_bank(f, address.bank - 1)) {
            return false;
        }
    }
    return true;
}
static bool share_local(const behavior_function *, bool built, bool *returned);
static bool predicate_host(const behavior_function *f, int pc, bool local_arrays)
{
    const behavior_function_host_call *host = &f->host_calls[f->instructions[pc].target];
    uint64_t id = intrinsic(host);
    if (!id) {
        const node_access *access = host->access;
        unsigned allowed = NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ |
                           NODE_EFFECT_PARALLEL;
        return access && !access->write_count &&
               (access->flags & (NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ)) &&
               !(access->flags & ~allowed) && !host->call->action;
    }
    int steps = EFFECT_STEPS;
    switch (execution_intrinsic_kind(id)) {
        /* Managed cells can be shared with persistent state or other calls.
         * Query predicates cannot observe or mutate that heap. */
        case EXECUTION_INTRINSIC_POINTER_MAKE:
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_WRITE:
        case EXECUTION_INTRINSIC_POINTER_FIELD:
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW: return false;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: {
            origin kind = bank_origin(f, pc, host->first + 2, 0, &steps);
            return kind.constant && kind.bits != LANGUAGE_REFERENCE_STATE;
        }
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
        case EXECUTION_INTRINSIC_ARRAY_STORE:
        case EXECUTION_INTRINSIC_ARRAY_EXTEND: {
            /* A private value header may still hold a caller's array object. */
            origin target = bank_origin(f, pc, host->first, 0, &steps);
            return local_arrays && target.private && target.bank;
        }
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE: return local_arrays;
        case EXECUTION_INTRINSIC_MAP_STORE:
        case EXECUTION_INTRINSIC_MAP_DELETE: {
            /* In-place updates write only the function's own local collection. */
            origin target = bank_origin(f, pc, host->first, 0, &steps);
            return target.private && target.bank;
        }
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT:
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_UNION:
        case EXECUTION_INTRINSIC_VALUE_COPY:
        case EXECUTION_INTRINSIC_VALUE_DROP:
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
        case EXECUTION_INTRINSIC_UNION_MAKE:
        case EXECUTION_INTRINSIC_UNION_KEEP:
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET:
        case EXECUTION_INTRINSIC_LOCAL_RELEASE: return true;
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH:
            return f->data_values[host->first].initial.container != PIN_CONTAINER_VIEW;
        default: return false;
    }
}
bool execution_query_predicate_valid(const behavior_function *root, unsigned bound)
{
    const behavior_function **functions = NULL;
    apush(functions, root);
    int steps = EFFECT_STEPS;
    bool valid = true;
    for (int at = 0; at < acount(functions) && valid; at++) {
        const behavior_function *f = functions[at];
        if (!f || !f->frozen || acount(functions) > EFFECT_RECORDS) {
            valid = false;
            break;
        }
        bool local_arrays = share_local(f, false, NULL);
        for (int i = 0; i < f->argument_count; i++) {
            pin_value type = f->argument_defaults[i];
            if (behavior_function_kind(f, BEHAVIOR_BANK_ARGUMENT, i) == BEHAVIOR_ADDRESS &&
                type.type != PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
                valid = false;
            }
        }
        for (int pc = 0; pc < acount(f->instructions) && valid; pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if (--steps < 0) {
                valid = false;
                break;
            }
            switch (i->op) {
                case BF_VARIABLE:
                case BF_STORAGE_NAME:
                case BF_QUERY:
                case BF_OWN_NAME:
                case BF_INTERN_STRING:
                case BF_STATE_SELECT:
                case BF_RANDOM:
                case BF_RANDOM_RANGE:
                case BF_RETRIGGER:
                case BF_STORE:
                case BF_STORE_VALUE:
                case BF_STORE_HEADER:
                case BF_STORE_TYPE_NAME:
                case BF_COMMAND_SERVICE:
                case BF_COMMAND_RESULT:
                case BF_COMMAND_ARGUMENT:
                case BF_COMMAND_OUTPUT:
                case BF_COMMAND:
                case BF_COMMAND_BEGIN:
                case BF_COMPONENT_WRITE:
                case BF_SUSPEND:
                case BF_DELAY: valid = false; break;
                case BF_ARRAY:
                    valid = i->target == BF_ARGUMENT || i->target == BF_CONSTANT;
                    break;
                case BF_PRESENT: valid = i->target == BF_ARGUMENT; break;
                case BF_QUERY_VARIABLE:
                    valid = i->target >= 1 && i->target <= ECS_QUERY_VARS &&
                            (bound & (1u << i->target));
                    break;
                case BF_TYPE_NAME: valid = i->target != BF_VARIABLE; break;
                case BF_CONTEXT:
                    if (i->target && !context_uses(f, i->output, &steps)) {
                        valid = false;
                    }
                    if (!i->target &&
                        i->offset != offsetof(behavior_function_context, now) &&
                        i->offset != offsetof(behavior_function_context, delta_seconds) &&
                        i->offset != offsetof(behavior_function_context, results) &&
                        i->offset != offsetof(behavior_function_context, arguments)) {
                        valid = false;
                    }
                    break;
                case BF_LOAD:
                case BF_READ: {
                    int origin_steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->left, 0, &origin_steps);
                    /* A context handle belongs only in checked intrinsic calls;
                     * raw loads could otherwise bypass the state-read rule. */
                    valid = !address.context &&
                            (address.bank || address.private ||
                             address.readable || address.component);
                    if (address.bank && context_bank(f, address.bank - 1)) {
                        valid = false;
                    }
                    break;
                }
                case BF_WRITE: {
                    int origin_steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->right, 0, &origin_steps);
                    size_t width = behavior_scalar_size(i->type);
                    valid = address.private && address.offset <= address.extent &&
                            i->offset <= address.extent - address.offset && width &&
                            width <= address.extent - address.offset - i->offset;
                    valid |= control_write(i, address);
                    break;
                }
                case BF_HOST_CALL:
                    valid = predicate_host(f, pc, local_arrays);
                    break;
                case BF_INVOKE: {
                    const behavior_function_invocation *call = &f->calls[i->left];
                    if (call->task) {
                        valid = false;
                        break;
                    }
                    for (int j = 0; j < call->count; j++) {
                        int origin_steps = EFFECT_STEPS;
                        if (bank_origin(f, pc, call->first + j, 0, &origin_steps)
                                .context) {
                            valid = false;
                        }
                    }
                    bool seen = false;
                    for (int j = 0; j < acount(functions); j++) {
                        seen |= functions[j] == call->function;
                    }
                    if (!seen) {
                        apush(functions, call->function);
                    }
                    break;
                }
                default: break;
            }
        }
    }
    afree(functions);
    return valid;
}
static void add(effects *e, bool write, uint64_t id)
{
    if (e->counting) {
        if (e->capacity == EFFECT_RECORDS) {
            e->valid = false;
            return;
        }
        e->capacity++;
        return;
    }
    execution_effect_summary *s = e->summary;
    if (write) {
        e->writes[s->write_count++] = id;
    } else {
        e->reads[s->read_count++] = id;
    }
}
static void add_reduction(effects *e, uint64_t id)
{
    if (e->counting) {
        add(e, false, id);
        return;
    }
    for (int i = 0; i < e->summary->reduce_count; i++) {
        if (e->reduces[i] == id) {
            return;
        }
    }
    e->reduces[e->summary->reduce_count++] = id;
}
static bool function_reduces(const behavior_function *f, int depth, int *steps)
{
    if (!f || depth > EFFECT_DEPTH || --*steps < 0) {
        return false;
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        if (--*steps < 0) {
            return false;
        }
        if (intrinsic(&f->host_calls[i]) ==
            execution_intrinsic_id(&EXECUTION_reference_reduce_call)) {
            return true;
        }
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        if (function_reduces(f->definitions[i], depth + 1, steps)) {
            return true;
        }
    }
    return false;
}
static bool plan_reduces(const execution_module_plan *p)
{
    int steps = EFFECT_RECORDS;
    for (int i = 0; i < p->function_count; i++) {
        if (function_reduces(p->functions[i].body, 0, &steps)) {
            return true;
        }
    }
    return false;
}
typedef struct
{
    const behavior_function *seen[SCALAR_HELPERS], *stack[EFFECT_DEPTH];
    int count, steps;
} scalar_helpers;
static bool scalar_value(pin_value value)
{
    return value.container == PIN_CONTAINER_SINGLE && value.type >= PIN_BOOL &&
           value.type <= PIN_F64;
}
static bool scalar_helper(scalar_helpers *proof, const behavior_function *f, int depth)
{
    if (!f || depth == EFFECT_DEPTH || --proof->steps < 0 || f->variable_count ||
        acount(f->components) || acount(f->queries) || acount(f->commands)) {
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (proof->stack[i] == f) {
            return false;
        }
    }
    for (int i = 0; i < proof->count; i++) {
        if (proof->seen[i] == f) {
            return true;
        }
    }
    if (proof->count == SCALAR_HELPERS) {
        return false;
    }
    /* Lowered writable scalar locals reserve private banks through snapshot
     * descriptors. Executing a host call is still excluded below. */
    for (int i = 0; i < acount(f->host_calls); i++) {
        if (--proof->steps < 0 || intrinsic(&f->host_calls[i]) !=
                                      execution_intrinsic_id(&EXECUTION_snapshot_call)) {
            return false;
        }
    }
    for (int i = 0; i < f->argument_count; i++) {
        if (!scalar_value(f->argument_defaults[i]) ||
            behavior_function_kind(f, BEHAVIOR_BANK_ARGUMENT, i) != BEHAVIOR_VALUE) {
            return false;
        }
    }
    for (int i = 0; i < f->result_count; i++) {
        if (!scalar_value(f->result_defaults[i]) ||
            behavior_function_kind(f, BEHAVIOR_BANK_RESULT, i) != BEHAVIOR_VALUE) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->locals); i++) {
        if (!scalar_value(f->locals[i].initial) || f->locals[i].kind != BEHAVIOR_VALUE) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        if (!scalar_value(f->data_values[i].initial) ||
            f->data_values[i].kind != BEHAVIOR_VALUE) {
            return false;
        }
    }
    proof->stack[depth] = f;
    for (int i = 0; i < acount(f->calls); i++) {
        if (f->calls[i].task ||
            !scalar_helper(proof, f->calls[i].function, depth + 1)) {
            return false;
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (--proof->steps < 0 || i->indirect) {
            return false;
        }
        if (i->op >= BF_ADD && i->op <= BF_FMA &&
            behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) == BEHAVIOR_VALUE) {
            continue;
        }
        switch (i->op) {
            case BF_CONSTANT:
                if (!scalar_value(i->value) ||
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) !=
                        BEHAVIOR_VALUE) {
                    return false;
                }
                break;
            case BF_ARGUMENT:
            case BF_LOCAL:
            case BF_SAVE:
            case BF_RESULT:
            case BF_BRANCH:
            case BF_JUMP:
            case BF_REQUIRE:
            case BF_ASSERT:
            case BF_FAIL:
            case BF_CHECK:
            case BF_CALL_CHECK:
            case BF_RETURN:
            case BF_END:
            case BF_SOURCE_POINT:
            case BF_SOURCE_WATCH: break;
            case BF_HOST_SLOT:
                if (i->target) {
                    return false;
                }
                break;
            case BF_CONTEXT:
                if (i->target != 1 || !context_uses(f, i->output, &proof->steps)) {
                    return false;
                }
                break;
            case BF_LOAD:
            case BF_WRITE: {
                origin address = slot_origin(
                    f,
                    pc,
                    i->op == BF_LOAD ? i->left : i->right,
                    0,
                    &proof->steps
                );
                if (i->op == BF_WRITE && control_write(i, address)) {
                    break;
                }
                if (!address.bank || address.offset ||
                    i->offset != offsetof(pin_value, d) || i->type < PIN_BOOL ||
                    i->type > PIN_F64 ||
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->op == BF_LOAD ? i->output : i->left) !=
                        BEHAVIOR_VALUE) {
                    return false;
                }
                break;
            }
            case BF_INVOKE:
                if (i->left < 0 || i->left >= acount(f->calls)) {
                    return false;
                }
                break;
            default: return false;
        }
    }
    if (proof->count == SCALAR_HELPERS) {
        return false;
    }
    proof->seen[proof->count++] = f;
    return true;
}
static const behavior_function *scalar_helper_root(const execution_module_plan *p, scalar_helpers *proof)
{
    if (p->state_count || p->binding_count || p->action_count || p->import_count) {
        return NULL;
    }
    const behavior_function *root = NULL;
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *function = &p->functions[i];
        if (function->subscription ||
            (function->kind != EXECUTION_FUNCTION_SYSTEM &&
             function->kind != EXECUTION_FUNCTION_AUTHORED)) {
            return NULL;
        }
        if (function->kind == EXECUTION_FUNCTION_SYSTEM) {
            if (root) {
                return NULL;
            }
            root = function->body;
        }
    }
    if (!root) {
        return NULL;
    }
    for (int i = 0; i < acount(root->calls); i++) {
        if (root->calls[i].task || !scalar_helper(proof, root->calls[i].function, 0)) {
            return NULL;
        }
    }
    for (int i = 0; i < p->function_count; i++) {
        if (p->functions[i].body == root) {
            continue;
        }
        bool reachable = false;
        for (int j = 0; j < proof->count; j++) {
            reachable |= proof->seen[j] == p->functions[i].body;
        }
        if (!reachable) {
            return NULL;
        }
    }
    return root;
}
static void scalar_plan(effects *e, const execution_module_plan *p)
{
    if (!e->reducing) {
        return;
    }
    scalar_helpers proof = { .steps = EFFECT_STEPS };
    e->scalar_root = scalar_helper_root(p, &proof);
    if (e->scalar_root) {
        for (int i = 0; i < proof.count; i++) {
            e->scalar_functions[e->scalar_count++] = proof.seen[i];
        }
    }
}
static bool scalar_function(const effects *e, const behavior_function *f)
{
    for (int i = 0; i < e->scalar_count; i++) {
        if (e->scalar_functions[i] == f) {
            return true;
        }
    }
    return false;
}
static void unknown(effects *e)
{
    if (e->counting) {
        return;
    }
    if (e->summary->unknown_operation < 0) {
        e->summary->unknown_operation = e->operation;
        e->summary->unknown_instruction = e->instruction;
        e->summary->unknown_capability = e->capability;
        e->summary->unknown_function = e->function;
    }
    e->summary->declared = e->summary->parallel_safe = false;
    e->summary->structural = e->summary->host_sink = e->summary->world_read = true;
    e->summary->deterministic = false;
}
static void add_access(effects *e, const node_access *a)
{
    if (!a) {
        unknown(e);
        return;
    }
    if (a->read_count < 0 || a->write_count < 0 ||
        a->read_count > EFFECT_RECORDS || a->write_count > EFFECT_RECORDS ||
        (a->read_count && !a->reads) || (a->write_count && !a->writes)) {
        e->valid = false;
        return;
    }
    for (int j = 0; j < a->read_count; j++) {
        add(e, false, a->reads[j]);
    }
    for (int j = 0; a->read_refs && a->read_refs[j]; j++) {
        if (j == EFFECT_RECORDS) {
            e->valid = false;
            return;
        }
        add(e, false, *a->read_refs[j]);
    }
    for (int j = 0; j < a->write_count; j++) {
        add(e, true, a->writes[j]);
    }
    if (e->counting) {
        return;
    }
    execution_effect_summary *s = e->summary;
    s->structural |= (a->flags & NODE_EFFECT_STRUCTURAL) != 0;
    s->host_sink |= (a->flags & NODE_EFFECT_HOST_SINK) != 0;
    s->world_read |= (a->flags & NODE_EFFECT_WORLD_READ) != 0;
    s->deterministic &= (a->flags & NODE_EFFECT_RANDOM) == 0;
    if (a->write_count || !(a->flags & NODE_EFFECT_PURE) ||
        (a->flags & (NODE_EFFECT_STRUCTURAL | NODE_EFFECT_HOST_SINK | NODE_EFFECT_RANDOM)) ||
        ((a->flags & NODE_EFFECT_WORLD_READ) && !(a->flags & NODE_EFFECT_PARALLEL))) {
        s->parallel_safe = false;
    }
}
static void payload_read(effects *e, const behavior_function *f, int pc, int bank)
{
    if (!e->reducing) {
        return;
    }
    int steps = EFFECT_STEPS;
    origin source = bank_origin(f, pc, bank, 0, &steps);
    pin_value value = f->data_values[bank].initial;
    if (source.component) {
        add(e, false, source.component);
    } else if (
        behavior_function_kind(f, BEHAVIOR_BANK_DATA, bank) != BEHAVIOR_VALUE ||
        value.type == PIN_STRUCT || value.type == PIN_UNION ||
        value.container != PIN_CONTAINER_SINGLE
    ) {
        /* A payload can alias component storage without a scalar load. */
        unknown(e);
    }
}
static void function(effects *e, const behavior_function *f, int depth)
{
    if (!f || !e->valid) {
        return;
    }
    if (!e->predicate && scalar_function(e, f)) {
        return; /* Reachable scalar helpers have no world effects. */
    }
    if (depth > EFFECT_DEPTH || ++e->records > EFFECT_RECORDS) {
        e->valid = false;
        return;
    }
    for (int j = 0; !e->reducing && j < acount(f->components); j++) {
        add(e, false, f->components[j].id);
    }
    for (int j = 0; j < acount(f->definitions); j++) {
        function(e, f->definitions[j], depth + 1);
    }
    int local_places = -1;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        e->operation = i->op;
        e->instruction = pc;
        e->capability = 0;
        switch (i->op) {
            case BF_HOST_CALL: {
                if (i->target < 0 || i->target >= acount(f->host_calls)) {
                    e->valid = false;
                    return;
                }
                const behavior_function_host_call *call = &f->host_calls[i->target];
                e->capability = call->capability_id;
                uint64_t id = intrinsic(call);
                if (id == execution_intrinsic_id(&EXECUTION_reference_make_call) ||
                    id == execution_intrinsic_id(&EXECUTION_query_source_call) ||
                    id == execution_intrinsic_id(&EXECUTION_component_has_call)) {
                    int steps = EFFECT_STEPS;
                    int ordinal = id == execution_intrinsic_id(&EXECUTION_reference_make_call)
                                    ? 6
                                    : 2;
                    origin component = bank_origin(f, pc, call->first + ordinal, 0, &steps);
                    if (component.constant && component.bits) {
                        if (!e->reducing ||
                            id != execution_intrinsic_id(&EXECUTION_reference_make_call)) {
                            add(e, false, component.bits);
                        }
                    } else {
                        unknown(e);
                    }
                }
                if (id == execution_intrinsic_id(&EXECUTION_query_source_call) ||
                    id == execution_intrinsic_id(&EXECUTION_query_target_call) ||
                    id == execution_intrinsic_id(&EXECUTION_component_has_call)) {
                    int steps = EFFECT_STEPS;
                    int ordinal = id == execution_intrinsic_id(&EXECUTION_query_source_call)
                                    ? 3
                                    : 2;
                    origin relationship = bank_origin(f, pc, call->first + ordinal, 0, &steps);
                    if (id != execution_intrinsic_id(&EXECUTION_component_has_call) &&
                        relationship.constant && relationship.bits) {
                        add(e, false, relationship.bits);
                    }
                    if (!e->counting) {
                        e->summary->world_read = true;
                        e->summary->parallel_safe = false;
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_reference_make_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_field_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_union_call)) {
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_union_test_call) ||
                    id == execution_intrinsic_id(&EXECUTION_union_get_call)) {
                    payload_read(e, f, pc, call->first);
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_reference_reduce_call)) {
                    int steps = EFFECT_STEPS;
                    origin target = bank_origin(f, pc, call->first, 0, &steps);
                    origin op = bank_origin(f, pc, call->first + 2, 0, &steps);
                    pin_value value = f->data_values[call->first + 1].initial;
                    if (!target.component || value.container != PIN_CONTAINER_SINGLE ||
                        value.type < PIN_I8 || value.type > PIN_F64 || !op.constant ||
                        op.bits > EXECUTION_REDUCTION_ADD_UNORDERED) {
                        unknown(e);
                    }
                    add_reduction(e, target.component);
                    if (op.constant) {
                        e->reduction_modes |= op.bits == EXECUTION_REDUCTION_ADD_UNORDERED
                                                ? 2
                                                : 1;
                    }
                    break;
                }
                execution_intrinsic kind = execution_intrinsic_kind(id);
                bool place_write = kind == EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE ||
                                   kind == EXECUTION_INTRINSIC_MAP_DELETE_PLACE;
                if (id == execution_intrinsic_id(&EXECUTION_reference_read_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_write_call) ||
                    place_write) {
                    if (e->counting) {
                        add(e,
                            place_write ||
                                id == execution_intrinsic_id(&EXECUTION_reference_write_call),
                            0);
                        if (place_write) {
                            add(e, false, 0);
                        }
                        break;
                    }
                    int steps = EFFECT_STEPS;
                    origin target = bank_origin(f, pc, call->first, 0, &steps);
                    bool write = place_write ||
                                 id == execution_intrinsic_id(&EXECUTION_reference_write_call);
                    if (place_write && target.private && target.bank) {
                        if (local_places < 0) {
                            local_places = share_local(f, false, NULL);
                        }
                        if (local_places) {
                            break;
                        }
                    }
                    const pin_value *value = &f->data_values[write ? call->first + 1 : call->result_first]
                                                  .initial;
                    if (!target.component || (!place_write && !plain(*value))) {
                        unknown(e);
                    }
                    if (!target.owner && (!e->reducing || write)) {
                        e->summary->parallel_safe = false;
                    }
                    add(e, write, target.component);
                    if (place_write) {
                        add(e, false, target.component);
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
                    payload_read(e, f, pc, call->first);
                    if (!e->counting &&
                        !plain(f->data_values[call->result_first].initial)) {
                        unknown(e);
                    }
                    break;
                }
                if (execution_intrinsic_kind(id) >= EXECUTION_INTRINSIC_POINTER_MAKE &&
                    execution_intrinsic_kind(id) <= EXECUTION_INTRINSIC_POINTER_REFERENCE) {
                    /* The owner heap is mutable across invocations. Its access
                     * cannot be attributed to one ECS column or worker. */
                    unknown(e);
                    break;
                }
                if (kind == EXECUTION_INTRINSIC_VALUE_COPY) {
                    payload_read(e, f, pc, call->first);
                    add_access(e, call->access);
                    break;
                }
                if (kind == EXECUTION_INTRINSIC_VALUE_DROP) {
                    /* Retires only this context's owned value shells. */
                    break;
                }
                if (id && kind >= EXECUTION_INTRINSIC_SEQUENCE_SLICE &&
                    kind <= EXECUTION_INTRINSIC_SEQUENCE_LENGTH) {
                    for (int bank = call->first; bank < call->result_first; bank++) {
                        payload_read(e, f, pc, bank);
                    }
                    pin_value source = f->data_values[call->first].initial;
                    bool text = source.container == PIN_CONTAINER_SINGLE &&
                                source.type == PIN_STR;
                    if (!text) {
                        add_access(e, call->access);
                    }
                    break;
                }
                if (id) {
                    unknown(e);
                    break;
                }
                for (int bank = call->first; bank < call->result_first; bank++) {
                    payload_read(e, f, pc, bank);
                }
                add_access(e, call->access);
                break;
            }
            case BF_WRITE:
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->right, 0, &steps);
                    size_t width = behavior_scalar_size(i->type);
                    bool private = address.private &&
                                   address.offset <= address.extent &&
                                   i->offset <= address.extent - address.offset &&
                                   width &&
                                   width <= address.extent - address.offset - i->offset;
                    if (!private && !control_write(i, address)) {
                        unknown(e);
                    }
                }
                break;
            case BF_READ:
                if (!e->predicate) {
                    unknown(e);
                }
                /* fall through */
            case BF_LOAD:
                if (e->reducing) {
                    int steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->left, 0, &steps);
                    if (e->counting || address.component) {
                        add(e, false, address.component);
                    }
                }
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->left, 0, &steps);
                    bool scalar = (i->type >= PIN_BOOL && i->type <= PIN_F64) ||
                                  (e->reducing && i->type == PIN_HANDLE);
                    size_t width = scalar ? behavior_scalar_size(i->type) : 0;
                    bool row = address.component &&
                               behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) ==
                                   BEHAVIOR_VALUE &&
                               scalar && address.offset <= address.extent &&
                               i->offset <= address.extent - address.offset &&
                               width <= address.extent - address.offset - i->offset;
                    if (!address.bank && !row &&
                        !(e->predicate && address.readable && !e->reducing)) {
                        unknown(e);
                    }
                    if (row && !address.owner && !e->reducing) {
                        e->summary->parallel_safe = false;
                    }
                }
                break;
            case BF_CONTEXT:
                if (!i->target &&
                    i->offset != offsetof(behavior_function_context, delta_seconds) &&
                    i->offset != offsetof(behavior_function_context, now) &&
                    !(e->predicate &&
                      (i->offset == offsetof(behavior_function_context, results) ||
                       i->offset == offsetof(behavior_function_context, arguments)))) {
                    unknown(e);
                }
                break;
            case BF_COMPONENT_WRITE:
                add(e, true, i->value.u64);
                if (!e->counting) {
                    e->summary->parallel_safe = false;
                }
                break;
            case BF_COMPONENT_READ:
                if (e->reducing) {
                    add(e, false, i->value.u64);
                }
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    if (!slot_origin(f, pc, i->left, 0, &steps).owner && !e->reducing) {
                        e->summary->parallel_safe = false;
                    }
                }
                break;
            case BF_COMPONENT:
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin world = slot_origin(f, pc, i->left, 0, &steps);
                    if (!world.world) {
                        unknown(e);
                    }
                    if (!slot_origin(f, pc, i->right, 0, &steps).owner && !e->reducing) {
                        e->summary->parallel_safe = false;
                    }
                }
                break;
            case BF_WORLD:
            case BF_QUERY_VARIABLE:
            case BF_ENTITY: break;
            case BF_INVOKE:
                if (i->left < 0 || i->left >= acount(f->calls)) {
                    e->valid = false;
                    return;
                }
                if (!e->predicate && !scalar_function(e, f->calls[i->left].function)) {
                    unknown(e);
                }
                break;
            case BF_ARGUMENT:
            case BF_INDEX:
            case BF_COLUMN:
            case BF_COLUMN_TRY:
            case BF_ROW:
            case BF_HOST_SERVICE:
            case BF_ARRAY:
                if (!e->predicate) {
                    unknown(e);
                }
                break;
            case BF_LOCAL:
            case BF_SAVE:
                if (!e->predicate && !e->reducing) {
                    unknown(e);
                }
                break;
            case BF_RANDOM:
            case BF_RANDOM_RANGE:
            case BF_STATE_SELECT:
            case BF_RETRIGGER:
            case BF_QUERY:
            case BF_COMMAND:
            case BF_COMMAND_BEGIN:
            case BF_SUSPEND:
            case BF_DELAY:
            case BF_STORE:
            case BF_STORE_VALUE:
            case BF_VARIABLE:
            case BF_STORE_HEADER:
            case BF_STORE_TYPE_NAME:
            case BF_INTERN_STRING: unknown(e); break;
            default: break;
        }
    }
}
static void predicate_effects(effects *e, const behavior_function *root)
{
    if (!root) {
        return;
    }
    const behavior_function **functions = NULL;
    apush(functions, root);
    e->predicate = true;
    for (int at = 0; at < acount(functions) && e->valid; at++) {
        const behavior_function *f = functions[at];
        function(e, f, 0);
        for (int j = 0; j < acount(f->calls); j++) {
            const behavior_function *callee = f->calls[j].function;
            bool seen = false;
            for (int k = 0; k < acount(functions); k++) {
                seen |= functions[k] == callee;
            }
            if (!seen) {
                if (acount(functions) == EFFECT_RECORDS) {
                    e->valid = false;
                    break;
                }
                apush(functions, callee);
            }
        }
    }
    e->predicate = false;
    afree(functions);
}
static void plan(effects *e, const execution_module_plan *p)
{
    for (int i = 0; i < p->query_count; i++) {
        if (p->queries[i].component &&
            (!e->reducing || p->queries[i].kind == EXECUTION_QUERY_CHANGED)) {
            add(e, false, p->queries[i].component);
        }
        if (p->queries[i].source_relationship) {
            add(e, false, p->queries[i].source_relationship);
        }
        e->function = 0;
        predicate_effects(e, p->queries[i].predicate);
        if (!e->counting && !e->reducing &&
            p->queries[i].kind == EXECUTION_QUERY_GENERATOR) {
            /* Different rows can share the same owner or joined target. */
            e->summary->parallel_safe = false;
        }
    }
    if (!e->counting && (p->state_count || p->binding_count ||
                         p->action_count || p->import_count)) {
        e->summary->parallel_safe = false;
    }
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *f = &p->functions[i];
        e->function = f->id;
        if (!e->counting && ((!e->scalar_root && f->kind != EXECUTION_FUNCTION_SYSTEM) ||
                             f->subscription)) {
            e->summary->parallel_safe = false;
        }
        function(e, f->body, 0);
        function(e, f->subscription, 0);
    }
    for (int i = 0; i < p->binding_count; i++) {
        e->function = 0;
        function(e, p->bindings[i].body, 0);
        function(e, p->bindings[i].dependencies, 0);
    }
    for (int i = 0; i < p->import_count; i++) {
        execution_effect_summary s = execution_module_effects(p->imports[i]);
        for (int j = 0; j < s.read_count; j++) {
            add(e, false, s.reads[j]);
        }
        for (int j = 0; j < s.write_count; j++) {
            add(e, true, s.writes[j]);
        }
        for (int j = 0; j < s.reduce_count; j++) {
            add_reduction(e, s.reduces[j]);
        }
        if (e->counting) {
            continue;
        }
        e->summary->declared &= s.declared;
        e->summary->structural |= s.structural;
        e->summary->host_sink |= s.host_sink;
        e->summary->world_read |= s.world_read;
        e->summary->deterministic &= s.deterministic;
    }
}
size_t execution_module_effects_bytes(const execution_module_plan *p)
{
    effects e = { .counting = true, .valid = true, .reducing = plan_reduces(p) };
    scalar_plan(&e, p);
    plan(&e, p);
    return !e.valid || e.capacity > EFFECT_RECORDS ||
                   e.capacity > SIZE_MAX / (3 * sizeof(uint64_t))
             ? SIZE_MAX
             : e.capacity * 3 * sizeof(uint64_t);
}
bool execution_module_build_effects(execution_module *m)
{
    size_t bytes = execution_module_effects_bytes(&m->plan);
    if (bytes == SIZE_MAX) {
        return false;
    }
    m->effect_ids = bytes ? xcalloc(1, bytes) : NULL;
    m->effect_bytes = bytes;
    size_t capacity = bytes / (3 * sizeof(uint64_t));
    m->effects = (execution_effect_summary){
        .reads = m->effect_ids,
        .writes = capacity ? m->effect_ids + capacity : NULL,
        .reduces = capacity ? m->effect_ids + 2 * capacity : NULL,
        .declared = true,
        .deterministic = true,
        .unknown_operation = -1,
        .unknown_instruction = -1,
        .parallel_safe = m->plan.function_count > 0
    };
    effects e = { .summary = &m->effects,
                  .reads = m->effect_ids,
                  .writes = capacity ? m->effect_ids + capacity : NULL,
                  .reduces = capacity ? m->effect_ids + 2 * capacity : NULL,
                  .valid = true,
                  .reducing = plan_reduces(&m->plan) };
    scalar_plan(&e, &m->plan);
    plan(&e, &m->plan);
    if (e.reducing) {
        /* Rows may alias through generators or arbitrary target handles. Only
         * private contributions, never ordinary writes, permit this proof. */
        m->effects.parallel_safe &= m->effects.reduce_count > 0 &&
                                    !m->effects.write_count &&
                                    e.reduction_modes != 3;
        /* Mixed ordered/unordered public IR stays serial so the exact failing
         * contribution retains its row and source diagnostics. */
        for (int i = 0; i < m->effects.reduce_count; i++) {
            for (int j = 0; j < m->effects.read_count; j++) {
                if (m->effects.reduces[i] == m->effects.reads[j]) {
                    m->effects.parallel_safe = false;
                }
            }
        }
    }
    return e.valid;
}
execution_effect_summary execution_module_effects(const execution_module *m)
{
    assert(m);
    return m->effects;
}

#define COST_FUNCTIONS 1024
#define COST_ALL                                                               \
    (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE)
typedef struct
{
    execution_cost_summary summary;
    const behavior_function **visited;
    const execution_module **modules;
    int steps;
} costs;

static bool pointer_value(pin_value v, behavior_value_kind kind);
static void cost_add(
    costs *c,
    const behavior_function *f,
    int pc,
    uint64_t capability,
    unsigned proven,
    unsigned possible,
    const char *reason
)
{
    execution_cost_site site = { f, pc, capability, reason };
    for (int bit = 0; bit < EXECUTION_COST_KINDS; bit++) {
        unsigned mask = 1u << bit;
        if ((proven & mask) && !(c->summary.proven & mask)) {
            c->summary.proven_sites[bit] = site;
        }
        if ((possible & mask) && !(c->summary.possible & mask)) {
            c->summary.possible_sites[bit] = site;
        }
    }
    c->summary.proven |= proven;
    c->summary.possible |= possible;
}

/* A snapshot copies type-name text even when the payload itself is scalar. */
static bool snapshot_storage(pin_value value)
{
    return value.type_name || value.container != PIN_CONTAINER_SINGLE ||
           value.type == PIN_STR || value.type == PIN_NAME ||
           value.type == PIN_STRUCT || value.type == PIN_UNION;
}

static bool managed_value(pin_value value)
{
    return value.container != PIN_CONTAINER_SINGLE ||
           value.type == PIN_STRUCT || value.type == PIN_STR ||
           value.type == PIN_NAME || value.type == PIN_UNION ||
           value.type == PIN_TASK || value.type == PIN_POINTER;
}

static void cost_host(costs *c, const behavior_function *f, int pc)
{
    const behavior_function_host_call *call = &f->host_calls[f->instructions[pc].target];
    uint64_t id = intrinsic(call);
    unsigned allocate_copy = EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY;
    if (!id) {
        cost_add(c, f, pc, call->capability_id, 0, COST_ALL, "Native call has no verified allocation, copy, or escape contract");
        return;
    }
    switch (execution_intrinsic_kind(id)) {
        case EXECUTION_INTRINSIC_POINTER_MAKE:
            cost_add(c, f, pc, id, allocate_copy | EXECUTION_COST_ESCAPE, 0, "Managed cell construction retains an initializer in the owner heap");
            return;
        case EXECUTION_INTRINSIC_POINTER_FIELD:
            cost_add(c, f, pc, id, allocate_copy, 0, "Managed field projection retains a location and its type metadata");
            return;
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
            cost_add(c, f, pc, id, EXECUTION_COST_ALLOCATE, 0, "Managed pointee reference allocates an invocation descriptor");
            return;
        case EXECUTION_INTRINSIC_POINTER_READ:
            cost_add(c, f, pc, id, 0, allocate_copy, "Managed pointee read snapshots its value and type metadata");
            return;
        case EXECUTION_INTRINSIC_POINTER_WRITE:
            cost_add(c, f, pc, id, EXECUTION_COST_ESCAPE, allocate_copy, "Managed pointee write retains a snapshot in the owner heap");
            return;
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_ITEM:
            cost_add(c, f, pc, id, allocate_copy, 0, "String iteration snapshots its cursor or one Unicode scalar");
            return;
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_STRING_NEXT: return;
        case EXECUTION_INTRINSIC_UNION_KEEP:
        case EXECUTION_INTRINSIC_UNION_MAKE: {
            bool keep = execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_UNION_KEEP;
            int first = call->first + (keep ? 2 : 1);
            if (first < call->result_first || keep) {
                unsigned possible = 0;
                for (int i = first; i < call->result_first; i++) {
                    const behavior_function_data_value *payload = &f->data_values[i];
                    if (pointer_value(payload->initial, payload->kind) ||
                        snapshot_storage(payload->initial)) {
                        possible |= EXECUTION_COST_COPY;
                    }
                }
                cost_add(c, f, pc, id, EXECUTION_COST_ALLOCATE, possible, "Union construction allocates a payload; VALUE copies can share collection backing or import managed payloads");
                return;
            }
            origin tag = bank_origin(f, pc, call->first, 0, &c->steps);
            if (tag.constant && tag.bits == PIN_UNION_CANCELLED_TAG) {
                return;
            }
            cost_add(
                c,
                f,
                pc,
                id,
                tag.constant ? EXECUTION_COST_ALLOCATE : 0,
                tag.constant ? 0 : EXECUTION_COST_ALLOCATE,
                "Union construction allocates a payload unless it selects the "
                "cancelled singleton"
            );
            return;
        }
        case EXECUTION_INTRINSIC_CONSTRUCT: {
            bool fields = call->result_first - call->first > 1;
            pin_value header = f->data_values[call->first].initial;
            cost_add(
                c,
                f,
                pc,
                id,
                EXECUTION_COST_ALLOCATE |
                    (fields || (header.container != PIN_CONTAINER_SINGLE &&
                                header.type_name)
                         ? EXECUTION_COST_COPY
                         : 0),
                !fields && header.type == PIN_STRUCT ? EXECUTION_COST_COPY : 0,
                "Value construction allocates storage and copies its fields or "
                "elements"
            );
            return;
        }
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
            cost_add(c, f, pc, id, EXECUTION_COST_ALLOCATE, 0, "Logical reference construction allocates reference storage");
            return;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_UNION:
            cost_add(c, f, pc, id, allocate_copy, 0, "Field reference construction allocates and copies a reference path");
            return;
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_ACTION_READ:
            for (int j = 0; j < call->result_count; j++) {
                if (snapshot_storage(f->data_values[call->result_first + j].initial)) {
                    cost_add(c, f, pc, id, 0, allocate_copy, "Value snapshot can allocate and copy managed payload or type metadata");
                }
            }
            return;
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            cost_add(c, f, pc, id, 0, COST_ALL, "Logical place write can detach ancestors, grow storage, and copy nested value shells");
            return;
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
        case EXECUTION_INTRINSIC_ARRAY_STORE:
        case EXECUTION_INTRINSIC_ARRAY_EXTEND:
            cost_add(c, f, pc, id, 0, allocate_copy, "In-place array update can grow storage and copy the item");
            return;
        case EXECUTION_INTRINSIC_VALUE_COPY:
            cost_add(c, f, pc, id, 0, allocate_copy, "Value copy can allocate a shell, import storage, or copy nested value metadata");
            return;
        case EXECUTION_INTRINSIC_VALUE_DROP:
            cost_add(c, f, pc, id, 0, allocate_copy, "Value retirement can allocate traversal scratch and release nested backing owners");
            return;
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
            cost_add(c, f, pc, id, 0, allocate_copy, "Sequence copy allocates an outer buffer and forks nested value shells");
            return;
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
            cost_add(c, f, pc, id, 0, allocate_copy, "Sequence view retains its pointer cell and allocates a fixed-window descriptor");
            return;
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
            if (snapshot_storage(f->data_values[call->result_first].initial)) {
                cost_add(c, f, pc, id, 0, allocate_copy, "Element access may retain ownership metadata or copy a value payload");
            }
            return;
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH: return;
        case EXECUTION_INTRINSIC_MAP_STORE:
        case EXECUTION_INTRINSIC_MAP_DELETE:
            cost_add(c, f, pc, id, 0, allocate_copy, "In-place map update can grow storage, copy the entry and rebuild the index");
            return;
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
            cost_add(c, f, pc, id, EXECUTION_COST_ESCAPE, EXECUTION_COST_ALLOCATE, "Reduction retains a scalar contribution until system publication");
            return;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET:
        case EXECUTION_INTRINSIC_BREAK_TAKE: return;
        default:
            /* These delegate to runtime callbacks, which may publish events,
             * retain results, or grow state. Pure/effect flags do not prove costs. */
            cost_add(c, f, pc, id, 0, COST_ALL, "Runtime callback can allocate, copy, or retain values");
            return;
    }
}

static void cost_function(costs *c, const behavior_function *f)
{
    if (!f || !c->summary.complete) {
        return;
    }
    for (int j = 0; j < acount(c->visited); j++) {
        if (c->visited[j] == f) {
            return;
        }
    }
    if (acount(c->visited) == COST_FUNCTIONS) {
        c->summary.complete = false;
        cost_add(c, f, -1, 0, 0, COST_ALL, "Cost analysis reached its function budget");
        return;
    }
    apush(c->visited, f);
    const behavior_function_instruction *code = f->runtime_body ? f->runtime_body
                                                                : f->instructions;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (--c->steps < 0) {
            c->summary.complete = false;
            cost_add(c, f, pc, 0, 0, COST_ALL, "Cost analysis reached its instruction budget");
            return;
        }
        if (!behavior_function_runtime_keeps(f, pc)) {
            continue;
        }
        const behavior_function_instruction *i = &code[pc];
        switch (i->op) {
            case BF_HOST_CALL: cost_host(c, f, pc); break;
            case BF_INVOKE: {
                const behavior_function_invocation *call = &f->calls[i->left];
                cost_add(
                    c,
                    f,
                    pc,
                    0,
                    EXECUTION_COST_ALLOCATE,
                    call->task ? EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE : 0,
                    call->task ? "Task invocation allocates storage and can "
                                 "retain captured values"
                               : "Outlined function invocation allocates "
                                 "argument and result banks"
                );
                for (int j = 0; j < call->count; j++) {
                    if (snapshot_storage(f->data_values[call->first + j].initial)) {
                        cost_add(c, f, pc, 0, 0, EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE, "Invocation snapshots managed arguments beyond the producing expression");
                    }
                }
                cost_function(c, call->function);
                break;
            }
            case BF_COPY_STRING:
                cost_add(c, f, pc, 0, 0, EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY, "Text retention allocates a descriptor and copies borrowed bytes when needed");
                break;
            case BF_COPY_ARRAY:
                cost_add(c, f, pc, 0, 0, EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY, "Array value copy allocates a shell and can import or detach collection backing");
                break;
            case BF_INTERN_STRING:
                cost_add(c, f, pc, 0, EXECUTION_COST_ESCAPE, EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY, "World string interning retains text and allocates/copies on a cache miss");
                break;
            case BF_OWN_NAME:
                cost_add(c, f, pc, 0, 0, COST_ALL, "Owner name interning can retain, allocate, and copy new text");
                break;
            case BF_STORE_VALUE:
                cost_add(c, f, pc, 0, 0, COST_ALL, "Persistent value storage can retain and snapshot a managed value");
                break;
            case BF_STORE:
                if (managed_value(f->variable_defaults[i->target])) {
                    cost_add(c, f, pc, 0, EXECUTION_COST_ESCAPE, 0, "Managed value is retained in persistent state");
                }
                break;
            case BF_RESULT_ARRAY:
                cost_add(c, f, pc, 0, EXECUTION_COST_ESCAPE, 0, "Managed result crosses the producing function boundary");
                break;
            case BF_RETURN:
            case BF_END:
                for (int j = 0; j < f->result_count; j++) {
                    if (managed_value(f->result_defaults[j]) ||
                        f->result_kinds[j] == BEHAVIOR_REFERENCE) {
                        cost_add(c, f, pc, 0, EXECUTION_COST_ESCAPE, 0, "Managed result crosses the producing function boundary");
                        break;
                    }
                }
                break;
            case BF_QUERY:
            case BF_COMMAND:
            case BF_COMMAND_BEGIN:
            case BF_STATE_SELECT:
            case BF_RETRIGGER:
            case BF_COMPONENT_WRITE:
                cost_add(c, f, pc, 0, 0, COST_ALL, "Runtime service has input-dependent allocation, copy, or retention costs");
                break;
            default: break;
        }
    }
}

execution_cost_summary behavior_function_costs(const behavior_function *f)
{
    assert(f);
    costs c = { .summary.complete = true, .steps = EFFECT_STEPS };
    cost_function(&c, f);
    afree(c.visited);
    return c.summary;
}

static void cost_module(costs *c, const execution_module *m, int depth)
{
    if (!c->summary.complete) {
        return;
    }
    for (int i = 0; i < acount(c->modules); i++) {
        if (c->modules[i] == m) {
            return;
        }
    }
    if (depth > EFFECT_DEPTH || acount(c->modules) == COST_FUNCTIONS) {
        c->summary.complete = false;
        cost_add(c, NULL, -1, 0, 0, COST_ALL, "Cost analysis reached its import depth budget");
        return;
    }
    apush(c->modules, m);
    for (int i = 0; i < m->plan.function_count; i++) {
        cost_function(c, m->plan.functions[i].body);
        cost_function(c, m->plan.functions[i].subscription);
    }
    for (int i = 0; i < m->plan.binding_count; i++) {
        cost_function(c, m->plan.bindings[i].body);
        cost_function(c, m->plan.bindings[i].dependencies);
    }
    for (int i = 0; i < m->plan.query_count; i++) {
        cost_function(c, m->plan.queries[i].predicate);
    }
    for (int i = 0; i < m->plan.import_count; i++) {
        cost_module(c, m->plan.imports[i], depth + 1);
    }
}

execution_cost_summary execution_module_costs(const execution_module *m)
{
    assert(m);
    costs c = { .summary.complete = true, .steps = EFFECT_STEPS };
    cost_module(&c, m, 0);
    afree(c.visited);
    afree(c.modules);
    return c.summary;
}

/* Argument sharing. A synchronous call may pass the caller's argument values
 * without copying them when the callee, and every function it can reach, leaves
 * storage it did not allocate untouched. Taint marks each pointer that may
 * reach such storage: arguments, state, context, results and anything loaded
 * through or derived from them. Nothing may write through a tainted address,
 * update a tainted local in place, or write through a reference other than a
 * value reference to an own, untainted cell. Writes through addresses the
 * analysis cannot place forget what every cell holds. Values copied by
 * snapshots, constructors, reference reads and authored returns can retain
 * mutable array roots even when their outer value storage is fresh. Plain
 * values and immutable text remain independent. The verifier rejects direct
 * writes to a cell's type and container, a return checks its results'
 * headers, a reference read checks its result's header and its root's type,
 * a reference write checks the written value's type, and `retyped` covers the
 * writes none of them can place. */
enum
{
    SHARE_NONE = -2,
    SHARE_UNKNOWN = -1,
    SHARE_RESULTS = -3,
    SHARE_CONTEXT = -4,
    SHARE_PASSES = 64
};
/* Text is immutable, so storing the caller's text in an own cell or record
 * leaves that storage the definition's own: `texts` marks such cells and
 * `text_memory` storage the analysis cannot place. Only reading those bytes
 * back as an address could reach the caller, so such a read is tainted.
 * `referenced` marks the cells a value reference may write. `headers` marks
 * slots that may hold a cell's header address. `retyped` says a write may have
 * changed a stored value's type or container, after which a copy may keep the
 * bits of the pointer it copied. */
typedef struct
{
    const behavior_function *f;
    bool *slots, *cells, *locals, *texts, *referenced, *headers;
    /* Per slot: the cell or context it addresses; per cell: the address it
     * holds, or for references the root cell they write. */
    int *addresses, *contents;
    /* Small nonnegative integer constants a slot or cell holds. */
    int *literals, *cell_literals;
    bool memory, results, text_memory, retyped, unsafe, changed;
    /* Whether a result may be written from a value that reaches the caller's. */
    bool returned;
    /* Whether values built in the caller's storage count as the caller's:
     * then only updates that run there may change them. */
    bool built;
} share_flow;

static bool pointer_value(pin_value v, behavior_value_kind kind)
{
    return kind != BEHAVIOR_VALUE || v.container != PIN_CONTAINER_SINGLE ||
           v.type == BF_POINTER_TYPE || v.type == PIN_STRUCT || v.type == PIN_STR ||
           v.type == PIN_NAME || v.type == PIN_UNION || v.type == PIN_POINTER;
}
static bool text_type(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}
static bool pointer_slot(const behavior_function *f, int slot)
{
    return pointer_value((pin_value){ .type = f->slots[slot] }, f->slot_kinds[slot]);
}
/* VALUE copies separate collection owners. Explicit pointers and views still
 * reach shared targets; opaque union payloads may contain either. */
static bool shared_targets(pin_value value, int depth)
{
    if (value.container == PIN_CONTAINER_VIEW || value.type == PIN_POINTER ||
        value.type == PIN_UNION) {
        return true;
    }
    value.container = PIN_CONTAINER_SINGLE;
    if (value.type != PIN_STRUCT) {
        return false;
    }
    const type_desc *type = pin_value_type(&value);
    if (!type || depth >= TYPE_MAX_NESTING) {
        return true;
    }
    if (type_alias_layout(type)) {
        const type_desc *target = type_alias_type(type, (type_resolver){ 0 });
        return !target || shared_targets(
                              (pin_value){ .type = PIN_STRUCT,
                                           .type_name = target->name,
                                           .record = target->carrier },
                              depth + 1
                          );
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const type_desc *child = field->type == PIN_STRUCT
                                   ? type_field_type(field, (type_resolver){ 0 })
                                   : NULL;
        if (shared_targets(
                (pin_value){ .type = field->type,
                             .type_name = child ? child->name : field->type_name,
                             .record = child ? child->carrier : NULL },
                depth + 1
            )) {
            return true;
        }
    }
    return false;
}
static int share_merge(int old, int value)
{
    return old == SHARE_NONE || old == value ? value : SHARE_UNKNOWN;
}
static void share_taint(share_flow *s, bool *bit, bool tainted)
{
    if (tainted && !*bit) {
        *bit = true;
        s->changed = true;
    }
}
static void share_set(share_flow *s, int *field, int value)
{
    int merged = share_merge(*field, value);
    if (merged != *field) {
        *field = merged;
        s->changed = true;
    }
}
static void share_define(share_flow *s, int slot, bool tainted, int address, int literal)
{
    if (slot < 0 || slot >= acount(s->f->slots)) {
        return;
    }
    share_taint(s, &s->slots[slot], tainted && pointer_slot(s->f, slot));
    share_set(s, &s->addresses[slot], address);
    share_set(s, &s->literals[slot], literal);
}
static bool share_cell(const share_flow *s, int cell)
{
    return cell >= 0 && cell < acount(s->f->data_values) && s->cells[cell];
}
static bool share_any_cell(const share_flow *s)
{
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (s->cells[cell]) {
            return true;
        }
    }
    return false;
}
/* Taint of the value an address reads. */
static bool share_load(const share_flow *s, int address_slot)
{
    int address = s->addresses[address_slot];
    if (s->slots[address_slot] || address == SHARE_CONTEXT) {
        return true;
    }
    if (address >= 0) {
        return s->cells[address];
    }
    if (address == SHARE_RESULTS) {
        return s->results;
    }
    return s->memory || share_any_cell(s);
}
/* Whether an address may read the caller's text as a pointer. */
static bool share_text(const share_flow *s, int address_slot)
{
    int address = s->addresses[address_slot];
    if (address >= 0) {
        return s->texts[address];
    }
    if (address == SHARE_CONTEXT || address == SHARE_RESULTS) {
        return false;
    }
    if (s->text_memory) {
        return true;
    }
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (s->texts[cell]) {
            return true;
        }
    }
    return false;
}
/* A write through a value reference may change any cell such a reference
 * roots, so none of them keeps a known address or literal. */
static void share_forget_referenced(share_flow *s, int written)
{
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (cell == written || (written < 0 && s->referenced[cell])) {
            share_set(s, &s->contents[cell], SHARE_UNKNOWN);
            share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
        }
    }
}
/* An address derived from `base` reaches what the base can reach. */
static bool share_derived(const share_flow *s, int base)
{
    int address = s->addresses[base];
    return s->slots[base] || address == SHARE_CONTEXT ||
           address == SHARE_RESULTS || (address >= 0 && s->cells[address]);
}
/* A write through an address the analysis cannot place may change any cell,
 * its header included. */
static void share_scatter(share_flow *s, bool tainted)
{
    share_taint(s, &s->memory, tainted);
    share_taint(s, &s->returned, tainted);
    share_taint(s, &s->retyped, true);
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        share_taint(s, &s->cells[cell], tainted);
        share_set(s, &s->contents[cell], SHARE_UNKNOWN);
        share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
    }
}
/* Header fields a write may change while the value keeps the type and
 * container its copies follow. */
static bool value_field(size_t offset)
{
    return offset == offsetof(pin_value, d) ||
           offset == offsetof(pin_value, type_name) ||
           offset == offsetof(pin_value, record);
}
/* The own, untainted cell whose address host argument `cell` holds, or -1. */
static int share_target(const share_flow *s, int cell)
{
    if (cell < 0 || cell >= acount(s->f->data_values)) {
        return -1;
    }
    int target = s->contents[cell];
    return target >= 0 && !s->cells[target] ? target : -1;
}
static void share_host(share_flow *s, int pc)
{
    const behavior_function *f = s->f;
    const behavior_function_host_call *host = &f->host_calls[f->instructions[pc].target];
    bool inputs = false, text = false;
    for (int cell = host->first; cell < host->result_first; cell++) {
        inputs |= share_cell(s, cell);
        text |= s->texts[cell];
    }
    share_taint(s, &s->text_memory, text);
    bool output = inputs;
    int root = SHARE_UNKNOWN;
    uint64_t id = intrinsic(host);
    if (s->built && id && execution_intrinsic_in_result(id)) {
        /* What it builds is the caller's, the array an update of a local
         * leaves included; such an update may change it again. */
        output = execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_CONSTRUCT ||
                 execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_UNION_KEEP;
        if (!output) {
            int updated = s->contents[host->first];
            if (updated < 0) {
                share_scatter(s, true);
            } else {
                share_taint(s, &s->cells[updated], true);
                share_set(s, &s->contents[updated], SHARE_UNKNOWN);
                share_set(s, &s->cell_literals[updated], SHARE_UNKNOWN);
            }
        }
        for (int k = 0; k < host->result_count; k++) {
            share_taint(s, &s->cells[host->result_first + k], output);
            share_set(s, &s->contents[host->result_first + k], SHARE_UNKNOWN);
            share_set(s, &s->cell_literals[host->result_first + k], SHARE_UNKNOWN);
        }
        return;
    }
    if (!id) {
        const node_access *access = host->access;
        unsigned allowed = NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ |
                           NODE_EFFECT_PARALLEL;
        s->unsafe |= !access || access->write_count ||
                     !(access->flags & (NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ)) ||
                     (access->flags & ~allowed) || host->call->action;
        /* A native's results own fresh collections: it publishes them, or the
         * call copies them. Only pointers and views inside still reach. */
        output = s->retyped && inputs;
        for (int k = 0; k < host->result_count; k++) {
            output |= inputs &&
                      shared_targets(f->data_values[host->result_first + k].initial, 0);
        }
    } else {
        switch (execution_intrinsic_kind(id)) {
            /* A copy reaches only what its source reaches. */
            case EXECUTION_INTRINSIC_VALUE_COPY:
                output = inputs &&
                         (s->retyped ||
                          shared_targets(f->data_values[host->result_first].initial, 0));
                break;
            /* Copies follow the copied headers; a rewritten one may keep the
             * bits of a pointer instead of copying what it points to. */
            case EXECUTION_INTRINSIC_SNAPSHOT:
            case EXECUTION_INTRINSIC_CONSTRUCT:
            case EXECUTION_INTRINSIC_REFERENCE_READ:
                output = s->retyped && inputs;
                for (int k = 0; k < host->result_count; k++) {
                    output |= inputs &&
                              shared_targets(
                                  f->data_values[host->result_first + k].initial,
                                  0
                              );
                }
                break;
            case EXECUTION_INTRINSIC_UNION_MAKE:
            case EXECUTION_INTRINSIC_UNION_KEEP: {
                /* Both constructors fork ordinary VALUE owners, while payload
                 * references can still reach shared mutable targets. */
                int first = host->first + (execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_UNION_KEEP
                                               ? 2
                                               : 1);
                output = s->retyped && inputs;
                for (int cell = first; cell < host->result_first; cell++) {
                    output |= share_cell(s, cell) &&
                              shared_targets(f->data_values[cell].initial, 0);
                }
                break;
            }
            case EXECUTION_INTRINSIC_POINTER_MAKE:
            case EXECUTION_INTRINSIC_POINTER_READ:
            case EXECUTION_INTRINSIC_POINTER_FIELD:
                /* A pointer is an alias, including when nested in a copied
                 * record. Its pointee never becomes private call storage. */
                output = true;
                break;
            case EXECUTION_INTRINSIC_POINTER_REFERENCE:
                /* A reference through a pointer reaches only its pointee in
                 * the managed heap, never a lent argument; see the write. */
                output = s->built;
                break;
            case EXECUTION_INTRINSIC_POINTER_WRITE:
                /* A pointee lives in the managed heap, never in a lent
                 * argument, and the write copies its value there: it is safe
                 * when what it writes shares nothing. A result built in place
                 * may still update a payload the pointee shares. */
                s->unsafe |= s->built;
                for (int cell = host->first + 1; cell < host->result_first; cell++) {
                    s->unsafe |= share_cell(s, cell) &&
                                 shared_targets(f->data_values[cell].initial, 0);
                }
                output = false;
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
                /* Array views retain the pointer cell's logical pointee. */
                output = f->data_values[host->first].initial.type != PIN_STR ||
                         f->data_values[host->first].initial.container !=
                             PIN_CONTAINER_SINGLE;
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
            case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
                /* Nested values fork their owners. Pointers inside elements
                 * retain shared target provenance after that fork. */
                {
                    pin_value element = f->data_values[host->first].initial;
                    element.container = PIN_CONTAINER_SINGLE;
                    output = s->retyped || shared_targets(element, 0);
                }
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_GET:
            case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
                output = s->retyped ||
                         shared_targets(f->data_values[host->result_first].initial, 0);
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_LENGTH: output = false; break;
            case EXECUTION_INTRINSIC_UNION_TEST:
            case EXECUTION_INTRINSIC_VALUE_EQUAL:
            case EXECUTION_INTRINSIC_VALUE_DROP:
            case EXECUTION_INTRINSIC_LOCAL_RELEASE: output = false; break;
            case EXECUTION_INTRINSIC_UNION_GET: break;
            case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            case EXECUTION_INTRINSIC_REFERENCE_UNION:
                output = share_cell(s, host->first);
                root = s->contents[host->first];
                break;
            case EXECUTION_INTRINSIC_REFERENCE_MAKE:
                /* A value reference made in this frame to an own, untainted
                 * cell writes only that cell. */
                root = share_target(s, host->first + 4);
                output = root < 0 || s->contents[host->first] != SHARE_CONTEXT ||
                         s->cell_literals[host->first + 2] != LANGUAGE_REFERENCE_VALUE;
                if (root >= 0) {
                    share_taint(s, &s->referenced[root], true);
                }
                break;
            case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
            case EXECUTION_INTRINSIC_REFERENCE_WRITE: {
                /* Text the value cell holds marked `texts`, not its taint, and
                 * the text input above records where it went. */
                int written = s->contents[host->first];
                execution_intrinsic kind = execution_intrinsic_kind(id);
                bool value = false;
                int first = kind == EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE
                              ? host->first + 2
                              : host->first + 1;
                for (int cell = first; cell < host->result_first; cell++) {
                    value |= share_cell(s, cell) &&
                             shared_targets(f->data_values[cell].initial, 0);
                }
                if (kind == EXECUTION_INTRINSIC_MAP_DELETE_PLACE) {
                    value = false;
                }
                /* A write through a reference that reaches no tracked cell
                 * is safe when what it writes shares nothing. */
                s->unsafe |= share_cell(s, host->first) || (written < 0 && value);
                if (written >= 0) {
                    share_taint(s, &s->cells[written], value);
                }
                share_forget_referenced(s, written);
                output = false;
                break;
            }
            case EXECUTION_INTRINSIC_ARRAY_PUSH:
            case EXECUTION_INTRINSIC_ARRAY_STORE:
            case EXECUTION_INTRINSIC_ARRAY_EXTEND: {
                int target = share_target(s, host->first);
                s->unsafe |= target < 0;
                if (target >= 0) {
                    share_set(s, &s->contents[target], SHARE_UNKNOWN);
                    share_set(s, &s->cell_literals[target], SHARE_UNKNOWN);
                }
                for (int cell = host->first + 1; cell < host->result_first; cell++) {
                    if (share_cell(s, cell) &&
                        shared_targets(f->data_values[cell].initial, 0)) {
                        share_scatter(s, true);
                    }
                }
                output = false;
                break;
            }
            case EXECUTION_INTRINSIC_MAP_STORE:
            case EXECUTION_INTRINSIC_MAP_DELETE: {
                int target = share_target(s, host->first);
                s->unsafe |= target < 0;
                if (target >= 0) {
                    share_set(s, &s->contents[target], SHARE_UNKNOWN);
                    share_set(s, &s->cell_literals[target], SHARE_UNKNOWN);
                }
                output = false;
                break;
            }
            default: s->unsafe = true; break;
        }
    }
    for (int k = 0; k < host->result_count; k++) {
        int cell = host->result_first + k;
        share_taint(s, &s->cells[cell], output);
        /* A text result may be the caller's text; others hold fresh storage
         * the text entered, which `text_memory` covers, unless a rewritten
         * header copied the text's address. */
        share_taint(
            s,
            &s->texts[cell],
            text && (s->retyped || text_type(f->data_values[cell].initial.type))
        );
        share_set(s, &s->contents[cell], root);
        share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
    }
}
static void share_instruction(share_flow *s, int pc)
{
    const behavior_function *f = s->f;
    const behavior_function_instruction *i = &f->instructions[pc];
    int out = i->output;
    switch (i->op) {
        case BF_CONSTANT: {
            int literal = i->value.type == PIN_I32 && i->value.i >= 0
                            ? i->value.i
                            : SHARE_UNKNOWN;
            share_define(s, out, false, SHARE_NONE, literal);
            break;
        }
        case BF_COPY_ARRAY:
            /* This raw opcode has no element schema proving pointer-free
             * payloads, so preserve potential shared-target provenance. */
            share_define(s, out, s->slots[i->left], s->addresses[i->left], SHARE_UNKNOWN);
            break;
        case BF_COPY_STRING:
            share_define(s, out, false, SHARE_NONE, SHARE_UNKNOWN);
            break;
        case BF_ARGUMENT:
            /* A scoped reference argument is the caller's location, which it
             * lends for writing: the caller lends no other argument that
             * shares it, so writes through it reach no lent argument. */
            share_define(s, out, f->argument_kinds[i->left] != BEHAVIOR_REFERENCE, SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_VARIABLE:
        case BF_ARRAY:
        case BF_TYPE_NAME:
        case BF_STORAGE_NAME:
        case BF_OWN_NAME:
        case BF_INTERN_STRING:
        case BF_WORLD:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_COMMAND_RESULT:
        case BF_QUERY:
        case BF_QUERY_VARIABLE:
        case BF_ENTITY:
        case BF_COMPONENT:
        case BF_COMPONENT_READ:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
            share_define(s, out, true, SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_CONTEXT:
            if (i->target == 1) {
                share_define(s, out, false, SHARE_CONTEXT, SHARE_UNKNOWN);
            } else if (i->offset == offsetof(behavior_function_context, results)) {
                share_define(s, out, false, SHARE_RESULTS, SHARE_UNKNOWN);
            } else {
                share_define(s, out, true, SHARE_UNKNOWN, SHARE_UNKNOWN);
            }
            break;
        case BF_LOCAL:
            share_define(s, out, s->locals[i->left], SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_SAVE:
            share_taint(s, &s->locals[i->target], s->slots[i->left]);
            break;
        case BF_HOST_SLOT:
            share_define(s, out, false, i->left, SHARE_UNKNOWN);
            share_taint(s, &s->headers[out], !i->target);
            break;
        case BF_ADD:
        case BF_INDEX:
        case BF_ROW:
            if (f->slot_kinds[i->left] != BEHAVIOR_ADDRESS) {
                share_define(s, out, s->slots[i->left] || s->slots[i->right], SHARE_NONE, SHARE_UNKNOWN);
            } else if (
                i->op == BF_INDEX && s->addresses[i->left] == SHARE_RESULTS &&
                s->literals[i->right] >= 0 && s->literals[i->right] < f->result_count
            ) {
                /* A result row at a constant ordinal. */
                share_define(s, out, false, SHARE_RESULTS, SHARE_UNKNOWN);
            } else {
                bool tainted = share_derived(s, i->left) || s->slots[i->right];
                share_define(s, out, tainted, SHARE_UNKNOWN, SHARE_UNKNOWN);
            }
            break;
        case BF_SELECT: {
            int address = share_merge(s->addresses[i->right], s->addresses[i->target]);
            share_define(s, out, s->slots[i->right] || s->slots[i->target], address, SHARE_UNKNOWN);
            share_taint(s, &s->headers[out], s->headers[i->right] || s->headers[i->target]);
            break;
        }
        case BF_LOAD:
        case BF_READ:
            share_define(s, out, share_load(s, i->left) || share_text(s, i->left), SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_WRITE: {
            int address = s->addresses[i->right];
            /* Through a cell's header, only the value and names stay in it. */
            if (address >= 0 && s->headers[i->right] && !value_field(i->offset)) {
                address = SHARE_UNKNOWN;
            }
            bool text = text_type(i->type) && s->slots[i->left];
            bool value = s->slots[i->left] && !text;
            s->unsafe |= s->slots[i->right];
            if (address == SHARE_CONTEXT) {
                /* Only the scalar exit and outcome fields, as a return writes. */
                s->unsafe |= !control_write(i, (origin){ .context = true });
            } else if (address >= 0) {
                share_taint(s, &s->cells[address], value);
                share_taint(s, &s->texts[address], text);
                share_set(s, &s->contents[address], s->addresses[i->left]);
                /* Only a whole I32 keeps its literal; other writes may
                 * change part of a wider value. */
                share_set(
                    s,
                    &s->cell_literals[address],
                    f->data_values[address].initial.type == PIN_I32
                        ? s->literals[i->left]
                        : SHARE_UNKNOWN
                );
            } else if (address == SHARE_RESULTS) {
                share_taint(s, &s->results, value || text);
                share_taint(s, &s->returned, value || text);
            } else {
                share_scatter(s, value);
                share_taint(s, &s->text_memory, text);
            }
            break;
        }
        case BF_RESULT:
            s->unsafe |= i->indirect;
            share_taint(s, &s->results, s->slots[i->left]);
            share_taint(s, &s->returned, s->slots[i->left]);
            break;
        case BF_RESULT_PRESENT:
        case BF_RESULT_TYPE_NAME:
        case BF_RESULT_ARRAY:
            share_taint(s, &s->results, s->slots[i->left]);
            share_taint(s, &s->returned, s->slots[i->left]);
            break;
        case BF_HOST_CALL: share_host(s, pc); break;
        case BF_INVOKE: {
            const behavior_function_invocation *call = &f->calls[i->left];
            const behavior_function *callee = call->function;
            s->unsafe |= call->task;
            /* A value argument reaches the callee as a copy, or shared with
             * one that changes none; a raw address to what this definition
             * builds would let any other callee write through it. */
            for (int k = 0; s->built && k < call->count; k++) {
                const behavior_function_data_value *argument = &f->data_values[call->first + k];
                s->unsafe |= share_cell(s, call->first + k) &&
                             (argument->kind != BEHAVIOR_VALUE ||
                              argument->initial.type == BF_POINTER_TYPE) &&
                             callee->argument_sharing != BEHAVIOR_SHARING_SAFE;
            }
            /* A callee's result that reaches shared targets may alias its
             * arguments or state unless its result-building proof or its fresh
             * results establish independence. */
            for (int k = 0; k < call->result_count; k++) {
                int cell = call->result_first + k;
                bool raw = k >= callee->result_count ||
                           callee->result_kinds[k] != BEHAVIOR_VALUE ||
                           callee->result_defaults[k].type == BF_POINTER_TYPE;
                bool retained = k < callee->result_count &&
                                !callee->builds_results && !callee->fresh_results &&
                                shared_targets(callee->result_defaults[k], 0);
                share_taint(s, &s->cells[cell], raw || retained);
                share_set(s, &s->contents[cell], SHARE_UNKNOWN);
                share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
            }
            break;
        }
#define SHARE_MATH(op, name, fn) case BF_##op:
            BEHAVIOR_MATH_UNARY(SHARE_MATH)
            BEHAVIOR_MATH_BINARY(SHARE_MATH)
#undef SHARE_MATH
        case BF_DELTA:
        case BF_PRESENT:
        case BF_OWNER:
        case BF_RANDOM:
        case BF_RANDOM_RANGE:
        case BF_STRING_EQUAL:
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
        case BF_BIT_AND:
        case BF_BIT_OR:
        case BF_BIT_XOR:
        case BF_BIT_NOT:
        case BF_SHL:
        case BF_SHR:
        case BF_MOD_F32:
        case BF_MOD_F64:
        case BF_HYPOT:
        case BF_FMA:
            share_define(s, out, false, SHARE_NONE, SHARE_UNKNOWN);
            break;
        case BF_REQUIRE:
        case BF_ASSERT:
        case BF_POINT:
        case BF_WATCH_BEGIN:
        case BF_WATCH_RESET:
        case BF_WATCH_VALUE:
        case BF_WATCH_PRESENT:
        case BF_WATCH_END:
        case BF_TRACE:
        case BF_CHECK:
        case BF_CALL_CHECK:
        case BF_ENTER:
        case BF_LEAVE:
        case BF_FAIL:
        case BF_JUMP:
        case BF_BRANCH:
        case BF_RETURN:
        case BF_SOURCE_POINT:
        case BF_SOURCE_WATCH:
        case BF_END: break;
        default: s->unsafe = true; break;
    }
}
static int *share_column(int count)
{
    int *column = xmalloc(((size_t)count + 1) * sizeof *column);
    for (int i = 0; i <= count; i++) {
        column[i] = SHARE_NONE;
    }
    return column;
}
/* Whether a result-storage update may run after union_keep has kept a value,
 * which the union would then share with the local it updates. */
static bool updates_kept(const share_flow *s)
{
    const behavior_function *f = s->f;
    int count = acount(f->instructions), *pending = NULL;
    bool *seen = xcalloc((size_t)count + 1, sizeof *seen), updates = false;
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_HOST_CALL ||
            execution_intrinsic_kind(intrinsic(&f->host_calls[i->target])) !=
                EXECUTION_INTRINSIC_UNION_KEEP) {
            continue;
        }
        if (s->cell_literals[f->host_calls[i->target].first + 1] != 0) {
            apush(pending, pc);
        }
    }
    while (acount(pending) && !updates) {
        int pc = pending[acount(pending) - 1], targets[2];
        asetlen(pending, acount(pending) - 1);
        for (int k = 0, n = behavior_function_successors(f, pc, targets); k < n; k++) {
            if (seen[targets[k]]) {
                continue;
            }
            seen[targets[k]] = true;
            const behavior_function_instruction *next = &f->instructions[targets[k]];
            if (next->op == BF_HOST_CALL) {
                uint64_t id = intrinsic(&f->host_calls[next->target]);
                execution_intrinsic kind = execution_intrinsic_kind(id);
                updates |= execution_intrinsic_in_result(id) &&
                           (kind == EXECUTION_INTRINSIC_ARRAY_PUSH ||
                            kind == EXECUTION_INTRINSIC_ARRAY_STORE ||
                            kind == EXECUTION_INTRINSIC_ARRAY_EXTEND);
            }
            apush(pending, targets[k]);
        }
    }
    afree(pending);
    free(seen);
    return updates;
}
/* One definition's own rules, ignoring what its callees do. Results start
 * tainted: they hold the caller's result cells until written. With `built`,
 * what the definition builds in its caller's storage is tainted too, and no
 * result-storage update may follow a union_keep that may keep a value.
 * `returned`, when given, says whether a result may reach the caller's. */
static bool share_local(const behavior_function *f, bool built, bool *returned)
{
    int slots = acount(f->slots), cells = acount(f->data_values),
        locals = acount(f->locals);
    share_flow s = { .f = f, .results = true, .built = built };
    s.slots = xcalloc((size_t)slots + 1, sizeof *s.slots);
    s.cells = xcalloc((size_t)cells + 1, sizeof *s.cells);
    s.locals = xcalloc((size_t)locals + 1, sizeof *s.locals);
    s.texts = xcalloc((size_t)cells + 1, sizeof *s.texts);
    s.referenced = xcalloc((size_t)cells + 1, sizeof *s.referenced);
    s.headers = xcalloc((size_t)slots + 1, sizeof *s.headers);
    s.addresses = share_column(slots);
    s.literals = share_column(slots);
    s.contents = share_column(cells);
    s.cell_literals = share_column(cells);
    /* A cell no instruction writes keeps its initial value, as reference
     * kinds do; writes merge with it. */
    for (int cell = 0; cell < cells; cell++) {
        pin_value initial = f->data_values[cell].initial;
        if (initial.type == PIN_I32 &&
            initial.container == PIN_CONTAINER_SINGLE && initial.i >= 0) {
            s.cell_literals[cell] = initial.i;
        }
        if (initial.type == PIN_U64 &&
            initial.container == PIN_CONTAINER_SINGLE && initial.u64 <= INT_MAX) {
            s.cell_literals[cell] = (int)initial.u64;
        }
    }
    int passes = 0;
    do {
        s.changed = false;
        for (int pc = 0; pc < acount(f->instructions) && !s.unsafe; pc++) {
            share_instruction(&s, pc);
        }
    } while (s.changed && !s.unsafe && ++passes < SHARE_PASSES);
    bool safe = !s.unsafe && !s.changed && !(built && updates_kept(&s));
    if (returned) {
        *returned = s.returned;
    }
    free(s.slots);
    free(s.cells);
    free(s.locals);
    free(s.texts);
    free(s.referenced);
    free(s.headers);
    free(s.addresses);
    free(s.literals);
    free(s.contents);
    free(s.cell_literals);
    return safe;
}
static int by_definition(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)(*(behavior_function *const *)a);
    uintptr_t y = (uintptr_t)(*(behavior_function *const *)b);
    return (x > y) - (x < y);
}
/* A definition with one result builds it in its caller's storage when only
 * the updates that run there change what it builds, none after union_keep
 * keeps a value, and no result may be the caller's own data. Without `built`,
 * what the definition builds is its own, so a result that reaches the
 * caller's came from its arguments or other data: moving it would let the
 * caller's result share that data. Two results could share what they reach. */
bool execution_builds_safely(const behavior_function *f)
{
    bool returned = true;
    return f->result_count <= 1 && share_local(f, true, NULL) &&
           share_local(f, false, &returned) && !returned;
}
/* Each pending definition's own verdict, and whether its results are fresh: no
 * result reaches what its arguments or state reach. A verdict takes pending
 * callees' results as fresh until their own rules show otherwise, and then
 * their callers decide again; verdicts only ever weaken. `callers` lists, for
 * each definition in `sorted` order, the pending indices of its callers. */
static void decide_locally(
    behavior_function **pending,
    behavior_function **sorted,
    const int *starts,
    const int *callers
)
{
    int count = acount(pending), *queue = NULL;
    bool *queued = xmalloc((size_t)count * sizeof *queued);
    for (int at = count - 1; at >= 0; at--) {
        apush(queue, at);
        queued[at] = true;
    }
    while (acount(queue)) {
        int at = apop(queue);
        behavior_function *f = pending[at];
        queued[at] = false;
        bool returned = true;
        bool safe = f->frozen && share_local(f, false, &returned);
        if (!safe) {
            f->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
        }
        if (!f->fresh_results || (safe && !returned)) {
            continue;
        }
        f->fresh_results = false;
        behavior_function **found = bsearch(&f, sorted, (size_t)count, sizeof *sorted, by_definition);
        int callee = (int)(found - sorted);
        for (int c = starts[callee]; c < starts[callee + 1]; c++) {
            if (!queued[callers[c]]) {
                queued[callers[c]] = true;
                apush(queue, callers[c]);
            }
        }
    }
    afree(queue);
    free(queued);
}
void execution_prepare_argument_sharing(behavior_function *root)
{
    if (!root || root->argument_sharing != BEHAVIOR_SHARING_UNKNOWN) {
        return;
    }
    /* Every reachable definition still unknown takes a pending verdict from its
     * own rules; known callees are final. A copying verdict then flows back to
     * every caller over reverse call edges. Definitions are immutable; this
     * derived verdict is their publication cache, as the borrow proof's is. */
    behavior_function **pending = NULL, **copying = NULL;
    root->argument_sharing = BEHAVIOR_SHARING_PENDING_SAFE;
    apush(pending, root);
    for (int at = 0; at < acount(pending); at++) {
        behavior_function *f = pending[at];
        /* Fresh until decide_locally shows otherwise. */
        f->fresh_results = true;
        for (int i = 0; i < acount(f->calls); i++) {
            behavior_function *callee = (behavior_function *)f->calls[i].function;
            if (callee->argument_sharing == BEHAVIOR_SHARING_UNKNOWN) {
                callee->argument_sharing = BEHAVIOR_SHARING_PENDING_SAFE;
                apush(pending, callee);
            }
        }
    }
    int count = acount(pending);
    behavior_function **sorted = xmalloc((size_t)count * sizeof *sorted);
    memcpy(sorted, pending, (size_t)count * sizeof *sorted);
    qsort(sorted, (size_t)count, sizeof *sorted, by_definition);
    /* Callers of each pending definition, grouped by callee. */
    int *starts = xcalloc((size_t)count + 1, sizeof *starts), *callers = NULL;
    int *edges = NULL;
    for (int at = 0; at < count; at++) {
        const behavior_function *f = pending[at];
        for (int i = 0; i < acount(f->calls); i++) {
            behavior_function *callee = (behavior_function *)f->calls[i].function;
            behavior_function **found = bsearch(&callee, sorted, (size_t)count, sizeof *sorted, by_definition);
            if (!found) {
                if (callee->argument_sharing != BEHAVIOR_SHARING_SAFE) {
                    pending[at]->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
                }
                continue;
            }
            apush(edges, (int)(found - sorted));
            apush(edges, at);
            starts[found - sorted + 1]++;
        }
    }
    for (int at = 0; at < count; at++) {
        starts[at + 1] += starts[at];
    }
    asetlen(callers, starts[count]);
    int *fill = xmalloc(((size_t)count + 1) * sizeof *fill);
    memcpy(fill, starts, ((size_t)count + 1) * sizeof *fill);
    for (int e = 0; e + 1 < acount(edges); e += 2) {
        callers[fill[edges[e]]++] = edges[e + 1];
    }
    decide_locally(pending, sorted, starts, callers);
    for (int at = 0; at < count; at++) {
        if (pending[at]->argument_sharing == BEHAVIOR_SHARING_PENDING_COPY) {
            apush(copying, pending[at]);
        }
    }
    while (acount(copying)) {
        behavior_function *f = apop(copying);
        behavior_function **found = bsearch(&f, sorted, (size_t)count, sizeof *sorted, by_definition);
        int callee = (int)(found - sorted);
        for (int c = starts[callee]; c < starts[callee + 1]; c++) {
            behavior_function *caller = pending[callers[c]];
            if (caller->argument_sharing == BEHAVIOR_SHARING_PENDING_SAFE) {
                caller->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
                apush(copying, caller);
            }
        }
    }
    for (int at = 0; at < count; at++) {
        pending[at]->argument_sharing = pending[at]->argument_sharing == BEHAVIOR_SHARING_PENDING_SAFE
                                          ? BEHAVIOR_SHARING_SAFE
                                          : BEHAVIOR_SHARING_COPY;
    }
    afree(pending);
    afree(copying);
    afree(edges);
    afree(callers);
    free(sorted);
    free(starts);
    free(fill);
}
