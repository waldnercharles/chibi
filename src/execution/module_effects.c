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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EFFECT_DEPTH 64
#define EFFECT_RECORDS 65536
#define EFFECT_STEPS 200000
#define SCALAR_HELPERS 64

typedef struct
{
    bool owner, context, world, constant, private, readable, row, owned_payload;
    int bank, row_field, local_reference;
    uint64_t bits, component;
    size_t offset, extent;
} origin;
typedef struct
{
    execution_effect_summary *summary;
    uint64_t *reads, *writes, *reduces;
    size_t capacity, records;
    bool counting, valid, predicate, reducing;
    const execution_module_plan *plan;
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
        actual->reusable != canonical->reusable) {
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
        case EXECUTION_INTRINSIC_QUERY_MAKE:
            if (count < 2 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_QUERY_NEXT:
            if (count != 2 || host->result_count != 2) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_QUERY_TEST:
        case EXECUTION_INTRINSIC_QUERY_BEGIN:
        case EXECUTION_INTRINSIC_QUERY_END:
        case EXECUTION_INTRINSIC_QUERY_ROW_GET: expected = 2; break;
        case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW: expected = 1; break;
        case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
        case EXECUTION_INTRINSIC_COMPONENT_POINTER: expected = 6; break;
        case EXECUTION_INTRINSIC_POINTER_MAKE: expected = 3; break;
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_WRITE: expected = 2; break;
        case EXECUTION_INTRINSIC_POINTER_FIELD: expected = 5; break;
        case EXECUTION_INTRINSIC_POINTER_REFERENCE: expected = 1; break;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: expected = 9; break;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD: expected = 4; break;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_SNAPSHOT: expected = 1; break;
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT: expected = 2; break;
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_REFERENCE_WRITE: expected = 2; break;
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE: expected = 3; break;
        case EXECUTION_INTRINSIC_QUERY_SOURCE: expected = 5; break;
        case EXECUTION_INTRINSIC_QUERY_TARGET: expected = 4; break;
        case EXECUTION_INTRINSIC_COMPONENT_HAS: expected = 3; break;
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
static bool collection_header_fixed(const behavior_function *, int bank, int depth, int *steps);
static bool same_origin(origin a, origin b)
{
    return a.owner == b.owner && a.context == b.context && a.world == b.world &&
           a.constant == b.constant && a.private == b.private &&
           a.readable == b.readable && a.row == b.row &&
           a.owned_payload == b.owned_payload && a.bank == b.bank &&
           a.row_field == b.row_field && a.local_reference == b.local_reference &&
           a.bits == b.bits && a.component == b.component &&
           a.offset == b.offset && a.extent == b.extent;
}
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
                if (control_write(i, address) || address.private) {
                    continue;
                }
                return none;
            }
            if (address.bank == bank + 1) {
                size_t width = behavior_scalar_size(i->type);
                if (i->offset > SIZE_MAX - address.offset) {
                    return none;
                }
                size_t offset = address.offset + i->offset;
                if (width && (offset >= offsetof(pin_value, d) + sizeof(double) ||
                              (offset < offsetof(pin_value, d) &&
                               width <= offsetof(pin_value, d) - offset))) {
                    continue;
                }
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
        if (id == execution_intrinsic_id(&EXECUTION_query_current_row_call)) {
            origin context = bank_origin(f, j, call->first, depth + 1, steps);
            return (origin){ .row = context.context };
        }
        if (id == execution_intrinsic_id(&EXECUTION_query_row_get_call)) {
            origin row = bank_origin(f, j, call->first, depth + 1, steps);
            origin field = bank_origin(f, j, call->first + 1, depth + 1, steps);
            if (!row.row || !field.constant || field.bits >= INT_MAX) {
                return none;
            }
            pin_value value = f->data_values[bank].initial;
            const type_desc *type = value.type == PIN_STRUCT ? pin_value_type(&value)
                                                             : NULL;
            return (origin){ .row_field = (int)field.bits + 1,
                             .readable = value.type == PIN_STRUCT ||
                                         value.container != PIN_CONTAINER_SINGLE,
                             .extent = type && type_is_plain(type) ? type->size : 0 };
        }
        if (id == execution_intrinsic_id(&EXECUTION_component_pointer_call) ||
            id == execution_intrinsic_id(&EXECUTION_component_snapshot_call)) {
            origin context = bank_origin(f, j, call->first, depth + 1, steps);
            origin entity = bank_origin(f, j, call->first + 1, depth + 1, steps);
            origin component = bank_origin(f, j, call->first + 2, depth + 1, steps);
            pin_value value = f->data_values[call->first + 3].initial;
            const type_desc *type = value.type == PIN_STRUCT ? pin_value_type(&value)
                                                             : NULL;
            if (context.context && component.constant && component.bits &&
                type && type_is_plain(type)) {
                return (origin){ .owner = entity.owner,
                                 .component = component.bits,
                                 .readable = true,
                                 .extent = type->size };
            }
            return none;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_make_call)) {
            origin kind = bank_origin(f, j, call->first + 2, depth + 1, steps);
            origin entity = bank_origin(f, j, call->first + 5, depth + 1, steps);
            origin component = bank_origin(f, j, call->first + 6, depth + 1, steps);
            origin context = bank_origin(f, j, call->first, depth + 1, steps);
            if (kind.constant && kind.bits == LANGUAGE_REFERENCE_VALUE &&
                context.context &&
                f->data_values[call->first + 1].initial.type == PIN_POINTER) {
                origin address = bank_origin(f, j, call->first + 4, depth + 1, steps);
                if (address.private && address.bank && !address.offset) {
                    return (origin){ .local_reference = address.bank };
                }
            }
            if (kind.constant && kind.bits == LANGUAGE_REFERENCE_COMPONENT &&
                component.constant && component.bits && context.context &&
                plain(f->data_values[call->first + 1].initial)) {
                return (origin){ .owner = entity.owner,
                                 .component = component.bits,
                                 .readable = true };
            }
            return none;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_field_call) ||
            id == execution_intrinsic_id(&EXECUTION_pointer_reference_call) ||
            id == execution_intrinsic_id(&EXECUTION_pointer_field_call) ||
            id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
            origin source = bank_origin(f, j, call->first, depth + 1, steps);
            pin_value value = f->data_values[bank].initial;
            if (id == execution_intrinsic_id(&EXECUTION_pointer_field_call)) {
                pin_value field = f->data_values[call->first + 1].initial;
                const type_desc *type = field.type == PIN_STRUCT
                                          ? pin_value_type(&field)
                                          : NULL;
                source.extent = type ? type->size : behavior_scalar_size(field.type);
                source.offset = 0;
            }
            if (id == execution_intrinsic_id(&EXECUTION_snapshot_call) &&
                (value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE)) {
                source.readable = true;
                source.owned_payload = value.container != PIN_CONTAINER_SINGLE &&
                                       i->right != BF_HOST_BORROW_ARRAY &&
                                       collection_header_fixed(f, call->first, depth + 1, steps);
            }
            return source;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_read_call) ||
            id == execution_intrinsic_id(&EXECUTION_pointer_read_call)) {
            origin target = bank_origin(f, j, call->first, depth + 1, steps);
            pin_value value = f->data_values[bank].initial;
            const type_desc *type = value.type == PIN_STRUCT &&
                                            value.container == PIN_CONTAINER_SINGLE
                                      ? pin_value_type(&value)
                                      : NULL;
            if ((target.component || target.row_field) && type && type_is_plain(type)) {
                /* The snapshot retains its source access, with its own bounds. */
                return (origin){ .owner = target.owner,
                                 .component = target.component,
                                 .row_field = target.row_field,
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
                        break;
                    }
                    if (save->op == BF_SAVE && save->target == i->left) {
                        return slot_origin(f, at, save->left, depth + 1, steps);
                    }
                }
                /* Guards can separate a saved component pointer from its
                 * use. A null initial value has no external effects; every
                 * assignment must prove the same source before crossing those
                 * guards. Other locals keep the bounded backward scan: this
                 * one reads the whole function. */
                pin_value initial = f->locals[i->left].initial;
                if (initial.type != PIN_POINTER ||
                    initial.container != PIN_CONTAINER_SINGLE || initial.ptr) {
                    return none;
                }
                {
                    origin source = { 0 };
                    bool found = false;
                    for (int at = 0; at < acount(f->instructions); at++) {
                        const behavior_function_instruction *save = &f->instructions[at];
                        if (--*steps < 0) {
                            return none;
                        }
                        if (save->op != BF_SAVE || save->target != i->left) {
                            continue;
                        }
                        origin value = slot_origin(f, at, save->left, depth + 1, steps);
                        if (found && !same_origin(source, value)) {
                            return none;
                        }
                        source = value;
                        found = true;
                    }
                    return found ? source : none;
                }
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
static origin effect_origin(const effects *e, origin value)
{
    if (!value.row_field || !e->plan || !e->plan->system_description) {
        return value;
    }
    const behavior_function
        *f = e->plan->descriptions[e->plan->system_description - 1].evaluate;
    int result = value.row_field;
    if (!f || result >= f->result_count || f->result_defaults[result].u64) {
        return (origin){ 0 };
    }
    origin source = { 0 };
    bool found = false;
    int steps = EFFECT_STEPS;
    size_t payload = (size_t)result * sizeof(pin_value) + offsetof(pin_value, d);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (--steps < 0) {
            return (origin){ 0 };
        }
        if ((i->op == BF_RESULT_ARRAY || i->op == BF_RESULT_PRESENT ||
             i->op == BF_RESULT_TYPE_NAME) &&
            i->target == result) {
            return (origin){ 0 };
        }
        if (i->op != BF_WRITE && i->op != BF_RESULT) {
            continue;
        }
        size_t width = behavior_scalar_size(i->type);
        if (i->op == BF_RESULT) {
            if (i->target != result) {
                continue;
            }
            if (i->indirect || width != sizeof(uint64_t)) {
                return (origin){ 0 };
            }
        } else {
            origin address = slot_origin(f, pc, i->right, 0, &steps);
            if (control_write(i, address) || address.bank) {
                continue;
            }
            if (!address.private || !width || i->offset > SIZE_MAX - address.offset) {
                return (origin){ 0 };
            }
            size_t offset = address.offset + i->offset;
            if (offset >= payload + sizeof(uint64_t) ||
                (offset < payload && width <= payload - offset)) {
                continue;
            }
            if (offset != payload || width != sizeof(uint64_t)) {
                return (origin){ 0 };
            }
        }
        origin candidate = slot_origin(f, pc, i->left, 0, &steps);
        if (candidate.row_field || (found && !same_origin(source, candidate))) {
            return (origin){ 0 };
        }
        source = candidate;
        found = true;
    }
    if (!found) {
        return (origin){ 0 };
    }
    value.owner = source.owner;
    value.component = source.component;
    value.row_field = 0;
    if (!value.extent) {
        value.extent = source.extent;
    }
    value.readable |= source.readable;
    return value;
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
             id != execution_intrinsic_id(&EXECUTION_component_pointer_call) &&
             id != execution_intrinsic_id(&EXECUTION_component_snapshot_call) &&
             id != execution_intrinsic_id(&EXECUTION_query_test_call) &&
             id != execution_intrinsic_id(&EXECUTION_query_current_row_call) &&
             id != execution_intrinsic_id(&EXECUTION_query_make_call))) {
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
/* Row snapshots remain readable after later guards. The general effect proof
 * stops at branches; here every possible local assignment must independently
 * resolve to a readable value before crossing that guard. */
static bool row_readable(const behavior_function *f, int pc, int slot, int depth, int *steps)
{
    if (depth == EFFECT_DEPTH || --*steps < 0) {
        return false;
    }
    origin address = slot_origin(f, pc, slot, 0, steps);
    if (address.context || (address.bank && context_bank(f, address.bank - 1))) {
        return false;
    }
    if (address.bank || address.private || address.readable || address.component) {
        return true;
    }
    for (int at = pc - 1; at >= 0; at--) {
        const behavior_function_instruction *instruction = &f->instructions[at];
        if (--*steps < 0) {
            return false;
        }
        if (instruction->op >= BF_STORE || instruction->output != slot) {
            continue;
        }
        if (instruction->op == BF_LOCAL) {
            bool found = false;
            for (int save = 0; save < acount(f->instructions); save++) {
                const behavior_function_instruction *value = &f->instructions[save];
                if (value->op != BF_SAVE || value->target != instruction->left) {
                    continue;
                }
                found = true;
                if (!row_readable(f, save, value->left, depth + 1, steps)) {
                    return false;
                }
            }
            return found;
        }
        if (instruction->op == BF_ADD || instruction->op == BF_INDEX) {
            origin offset = slot_origin(f, at, instruction->right, 0, steps);
            return offset.constant &&
                   row_readable(f, at, instruction->left, depth + 1, steps);
        }
        return false;
    }
    return false;
}
static bool owned_payload(const behavior_function *f, int pc, int slot, int depth, int *steps)
{
    if (depth == EFFECT_DEPTH || --*steps < 0) {
        return false;
    }
    origin value = slot_origin(f, pc, slot, depth, steps);
    if (value.owned_payload || (value.constant && !value.bits)) {
        return true;
    }
    for (int at = pc - 1; at >= 0; at--) {
        const behavior_function_instruction *i = &f->instructions[at];
        if (--*steps < 0) {
            return false;
        }
        if (i->op >= BF_STORE || i->output != slot) {
            continue;
        }
        if (i->op != BF_LOCAL || f->locals[i->left].initial.u64) {
            return false;
        }
        bool found = false;
        for (int save = 0; save < acount(f->instructions); save++) {
            const behavior_function_instruction *assignment = &f->instructions[save];
            if (assignment->op != BF_SAVE || assignment->target != i->left) {
                continue;
            }
            found = true;
            if (!owned_payload(f, save, assignment->left, depth + 1, steps)) {
                return false;
            }
        }
        return found;
    }
    return false;
}
static bool byte_overlap(size_t offset, size_t width, size_t field, size_t size)
{
    return offset < field + size && width > (offset < field ? field - offset : 0);
}
/* SNAPSHOT copies the runtime header. A forged alias of its input header must
 * not turn a collection copy into an identity copy of an opaque handle. */
static bool collection_header_fixed(const behavior_function *f, int bank, int depth, int *steps)
{
    if (depth == EFFECT_DEPTH || --*steps < 0 || bank < 0 ||
        bank >= acount(f->data_values) ||
        f->data_values[bank].initial.container == PIN_CONTAINER_SINGLE) {
        return false;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (--*steps < 0) {
            return false;
        }
        if (i->op == BF_HOST_CALL) {
            const behavior_function_host_call *host = &f->host_calls[i->target];
            if (bank >= host->result_first &&
                bank < host->result_first + host->result_count) {
                return false;
            }
        }
        if (i->op != BF_WRITE) {
            continue;
        }
        origin address = slot_origin(f, pc, i->right, depth + 1, steps);
        if (address.bank != bank + 1) {
            continue;
        }
        size_t width = behavior_scalar_size(i->type);
        if (!width || i->offset > SIZE_MAX - address.offset) {
            return false;
        }
        size_t offset = address.offset + i->offset;
        if (byte_overlap(offset, width, offsetof(pin_value, type), sizeof(pin_type)) ||
            byte_overlap(offset, width, offsetof(pin_value, container), sizeof(pin_container))) {
            return false;
        }
    }
    return true;
}
/* A private header may still point at a capture or module constant. Every
 * payload assignment must be a fresh snapshot or the empty value, including
 * assignments separated from this mutation by guards or back edges. */
static bool owned_collection(const behavior_function *f, origin target, int *steps)
{
    if (!target.private || !target.bank || target.offset) {
        return false;
    }
    int bank = target.bank - 1;
    pin_value initial = f->data_values[bank].initial;
    if (initial.ptr || (initial.container != PIN_CONTAINER_ARRAY &&
                        initial.container != PIN_CONTAINER_MAP)) {
        return false;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (--*steps < 0) {
            return false;
        }
        if (i->op == BF_HOST_CALL) {
            const behavior_function_host_call *host = &f->host_calls[i->target];
            if (bank >= host->result_first &&
                bank < host->result_first + host->result_count &&
                (intrinsic(host) != execution_intrinsic_id(&EXECUTION_snapshot_call) ||
                 i->right == BF_HOST_BORROW_ARRAY ||
                 !collection_header_fixed(f, host->first, 0, steps))) {
                return false;
            }
        }
        if (i->op != BF_WRITE) {
            continue;
        }
        origin address = slot_origin(f, pc, i->right, 0, steps);
        if (address.bank != target.bank) {
            continue;
        }
        size_t width = behavior_scalar_size(i->type);
        if (i->offset > SIZE_MAX - address.offset || !width) {
            return false;
        }
        size_t offset = address.offset + i->offset;
        if (byte_overlap(offset, width, offsetof(pin_value, d), sizeof(void *))) {
            if (offset != offsetof(pin_value, d) || width != sizeof(void *) ||
                !owned_payload(f, pc, i->left, 0, steps)) {
                return false;
            }
        }
        if (byte_overlap(offset, width, offsetof(pin_value, type), sizeof(pin_type)) ||
            byte_overlap(offset, width, offsetof(pin_value, container), sizeof(pin_container))) {
            return false;
        }
    }
    return true;
}
static bool predicate_host(const behavior_function *f, int pc, bool row)
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
        case EXECUTION_INTRINSIC_POINTER_WRITE:
        case EXECUTION_INTRINSIC_POINTER_REFERENCE: return false;
        case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
        case EXECUTION_INTRINSIC_COMPONENT_POINTER:
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_FIELD:
        case EXECUTION_INTRINSIC_QUERY_ROW_GET:
        case EXECUTION_INTRINSIC_QUERY_TEST:
        case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW: return row;
        case EXECUTION_INTRINSIC_QUERY_MAKE: return row;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: {
            origin kind = bank_origin(f, pc, host->first + 2, 0, &steps);
            return kind.constant && kind.bits != LANGUAGE_REFERENCE_STATE;
        }
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
        case EXECUTION_INTRINSIC_ARRAY_STORE:
        case EXECUTION_INTRINSIC_ARRAY_EXTEND:
        case EXECUTION_INTRINSIC_MAP_STORE:
        case EXECUTION_INTRINSIC_MAP_DELETE: {
            /* In-place updates write only the function's own local collection. */
            origin target = bank_origin(f, pc, host->first, 0, &steps);
            return owned_collection(f, target, &steps);
        }
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT:
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
        case EXECUTION_INTRINSIC_UNION_MAKE:
        case EXECUTION_INTRINSIC_UNION_KEEP:
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET: return true;
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
            return bank_origin(f, pc, host->first, 0, &steps).local_reference != 0;
        default: return false;
    }
}
static bool query_evaluator_valid(const behavior_function *root, unsigned bound, bool row)
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
                case BF_RESULT: valid = !i->indirect; break;
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
                    if (row && !valid) {
                        valid = row_readable(f, pc, i->left, 0, &origin_steps);
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
                case BF_HOST_CALL: valid = predicate_host(f, pc, row); break;
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
    origin source = effect_origin(e, bank_origin(f, pc, bank, 0, &steps));
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
                if (id == execution_intrinsic_id(&EXECUTION_component_snapshot_call) ||
                    id == execution_intrinsic_id(&EXECUTION_component_pointer_call)) {
                    int steps = EFFECT_STEPS;
                    origin source = bank_origin(f, pc + 1, call->result_first, 0, &steps);
                    if (!source.component) {
                        unknown(e);
                    } else if (id == execution_intrinsic_id(&EXECUTION_component_snapshot_call)) {
                        add(e, false, source.component);
                    }
                    /* Scheduled row evaluators run serially before workers.
                     * Creating component cells in a body mutates their shared
                     * managed heap and cannot run on row workers. */
                    if (!e->counting && !e->predicate &&
                        id == execution_intrinsic_id(&EXECUTION_component_pointer_call)) {
                        e->summary->parallel_safe = false;
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_query_current_row_call) ||
                    id == execution_intrinsic_id(&EXECUTION_query_row_get_call)) {
                    int steps = EFFECT_STEPS;
                    origin source = bank_origin(f, pc + 1, call->result_first, 0, &steps);
                    if (id == execution_intrinsic_id(&EXECUTION_query_current_row_call)) {
                        if (!source.row || !e->plan->system_description) {
                            unknown(e);
                        }
                    } else if (!source.row_field || !e->plan->system_description) {
                        unknown(e);
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_reference_make_call) ||
                    id == execution_intrinsic_id(&EXECUTION_query_source_call) ||
                    id == execution_intrinsic_id(&EXECUTION_component_has_call)) {
                    int steps = EFFECT_STEPS;
                    if (id == execution_intrinsic_id(&EXECUTION_reference_make_call) &&
                        bank_origin(f, pc + 1, call->result_first, 0, &steps).local_reference) {
                        break;
                    }
                    int ordinal = id == execution_intrinsic_id(&EXECUTION_reference_make_call)
                                    ? 6
                                    : 2;
                    origin component = bank_origin(f, pc, call->first + ordinal, 0, &steps);
                    if (component.constant && component.bits) {
                        if (!e->reducing &&
                            id == execution_intrinsic_id(&EXECUTION_reference_make_call)) {
                            add(e, false, component.bits);
                        }
                    } else {
                        unknown(e);
                    }
                }
                if (id == execution_intrinsic_id(&EXECUTION_local_release_call)) {
                    int steps = EFFECT_STEPS;
                    if (!bank_origin(f, pc, call->first, 0, &steps).local_reference) {
                        unknown(e);
                    }
                    break;
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
                    bool membership = e->predicate &&
                                      id == execution_intrinsic_id(&EXECUTION_component_has_call);
                    if (e->predicate &&
                        id == execution_intrinsic_id(&EXECUTION_query_source_call)) {
                        origin traversal = bank_origin(f, pc, call->first + 4, 0, &steps);
                        membership = relationship.constant && !relationship.bits &&
                                     traversal.constant && !traversal.bits;
                    }
                    if (id != execution_intrinsic_id(&EXECUTION_query_target_call) &&
                        (!e->reducing || !membership)) {
                        origin component = bank_origin(f, pc, call->first + 2, 0, &steps);
                        if (component.constant && component.bits) {
                            add(e, false, component.bits);
                        }
                    }
                    if (membership) {
                        /* Plain membership is already a declared read of its
                         * component; it does not inspect arbitrary world data. */
                        break;
                    }
                    if (!e->counting) {
                        e->summary->world_read = true;
                        e->summary->parallel_safe = false;
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_reference_make_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_field_call)) {
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_union_test_call) ||
                    id == execution_intrinsic_id(&EXECUTION_union_get_call)) {
                    payload_read(e, f, pc, call->first);
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_reference_reduce_call)) {
                    int steps = EFFECT_STEPS;
                    origin target = effect_origin(e, bank_origin(f, pc, call->first, 0, &steps));
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
                if (id == execution_intrinsic_id(&EXECUTION_reference_read_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_write_call)) {
                    if (e->counting) {
                        add(e,
                            id == execution_intrinsic_id(&EXECUTION_reference_write_call),
                            0);
                        break;
                    }
                    int steps = EFFECT_STEPS;
                    origin target = effect_origin(e, bank_origin(f, pc, call->first, 0, &steps));
                    bool write = id == execution_intrinsic_id(&EXECUTION_reference_write_call);
                    const pin_value *value = &f->data_values[write ? call->first + 1 : call->result_first]
                                                  .initial;
                    if (!target.component || !plain(*value)) {
                        unknown(e);
                    }
                    if (!target.owner && (!e->reducing || write)) {
                        e->summary->parallel_safe = false;
                    }
                    add(e, write, target.component);
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
                    pin_value value = f->data_values[call->result_first].initial;
                    if (value.container == PIN_CONTAINER_SINGLE &&
                        (value.type == PIN_POINTER || value.type == PIN_QUERY ||
                         value.type == PIN_HANDLE || value.type == PIN_ENUM)) {
                        /* Identity copies do not read the pointee or mutate its
                         * shared storage. Dereferences are checked separately. */
                        break;
                    }
                    payload_read(e, f, pc, call->first);
                    if (!e->counting && !plain(value)) {
                        unknown(e);
                    }
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_pointer_read_call) ||
                    id == execution_intrinsic_id(&EXECUTION_pointer_write_call) ||
                    id == execution_intrinsic_id(&EXECUTION_pointer_reference_call) ||
                    id == execution_intrinsic_id(&EXECUTION_pointer_field_call)) {
                    int steps = EFFECT_STEPS;
                    origin target = effect_origin(e, bank_origin(f, pc, call->first, 0, &steps));
                    bool read = id == execution_intrinsic_id(&EXECUTION_pointer_read_call);
                    bool write = id == execution_intrinsic_id(&EXECUTION_pointer_write_call);
                    pin_value value = f->data_values[write ? call->first + 1 : call->result_first]
                                          .initial;
                    if (!target.component || ((read || write) && !plain(value))) {
                        unknown(e);
                    }
                    if (read || write) {
                        add(e, write, target.component);
                    }
                    if (!e->counting &&
                        ((!target.owner && (!e->reducing || write)) ||
                         (!e->predicate &&
                          id == execution_intrinsic_id(&EXECUTION_pointer_field_call)))) {
                        e->summary->parallel_safe = false;
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
                    origin address = effect_origin(e, slot_origin(f, pc, i->left, 0, &steps));
                    if (e->counting || address.component) {
                        add(e, false, address.component);
                    }
                }
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin address = effect_origin(e, slot_origin(f, pc, i->left, 0, &steps));
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
                    int steps = EFFECT_STEPS;
                    origin value = i->op == BF_SAVE
                                     ? slot_origin(f, pc, i->left, 0, &steps)
                                     : slot_origin(f, pc + 1, i->output, 0, &steps);
                    pin_value type = f->locals[i->op == BF_SAVE ? i->target : i->left]
                                         .initial;
                    if (type.type != PIN_POINTER || !effect_origin(e, value).component) {
                        unknown(e);
                    }
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
    e->plan = p;
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
    for (int i = 0; i < p->description_count; i++) {
        predicate_effects(e, p->descriptions[i].evaluate);
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
    return value.container != PIN_CONTAINER_SINGLE || value.type == PIN_STRUCT ||
           value.type == PIN_STR || value.type == PIN_NAME ||
           value.type == PIN_UNION || value.type == PIN_TASK ||
           value.type == PIN_POINTER || value.type == PIN_QUERY;
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
        case EXECUTION_INTRINSIC_UNION_KEEP: {
            /* It copies each managed payload value its mask leaves out, and
             * one it keeps that its storage does not hold. */
            origin keep = bank_origin(f, pc, call->first + 1, 0, &c->steps);
            unsigned proven = EXECUTION_COST_ALLOCATE, possible = 0;
            for (int i = 0; i < call->result_first - call->first - 2; i++) {
                const behavior_function_data_value *payload = &f->data_values[call->first + 2 + i];
                if (!pointer_value(payload->initial, payload->kind)) {
                    continue;
                }
                bool kept = keep.constant && i < 64 && (keep.bits >> i & 1);
                if (keep.constant && !kept) {
                    proven |= EXECUTION_COST_COPY;
                } else {
                    possible |= EXECUTION_COST_COPY;
                }
            }
            cost_add(c, f, pc, id, proven, possible, "Union construction in the caller's storage copies the payload values it does not keep");
            return;
        }
        case EXECUTION_INTRINSIC_UNION_MAKE: {
            int count = call->result_first - call->first;
            if (count > 1) {
                cost_add(c, f, pc, id, allocate_copy, 0, "Union construction allocates a payload and snapshots supplied values");
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
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            cost_add(c, f, pc, id, 0, COST_ALL, "Reference write can retain and copy the referenced root value");
            return;
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
        case EXECUTION_INTRINSIC_ARRAY_STORE:
        case EXECUTION_INTRINSIC_ARRAY_EXTEND:
            cost_add(c, f, pc, id, 0, allocate_copy, "In-place array update can grow storage and copy the item");
            return;
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
            case BF_COPY_ARRAY:
                cost_add(c, f, pc, 0, 0, EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY, "Managed copy allocates and copies when its input is non-null");
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
    for (int i = 0; i < m->plan.description_count; i++) {
        cost_function(c, m->plan.descriptions[i].evaluate);
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
 * snapshots, constructors, reference reads and authored returns are fresh while
 * the copied headers keep their declared types: the verifier rejects direct
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
           v.type == BF_POINTER_TYPE || v.type == PIN_STRUCT ||
           v.type == PIN_STR || v.type == PIN_NAME || v.type == PIN_UNION ||
           v.type == PIN_POINTER || v.type == PIN_QUERY;
}
static bool text_type(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}
static bool pointer_slot(const behavior_function *f, int slot)
{
    return pointer_value((pin_value){ .type = f->slots[slot] }, f->slot_kinds[slot]);
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
           offset == offsetof(pin_value, record) ||
           offset == offsetof(pin_value, schema);
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
/* Snapshotting containers or records copies their ordinary payloads, but
 * managed pointer fields retain identity. Unknown union payloads may carry
 * such pointers too. */
static bool snapshot_aliases(pin_value value, int depth)
{
    if (value.type == PIN_POINTER || value.type == PIN_QUERY ||
        value.type == PIN_UNION || depth >= EFFECT_DEPTH) {
        return true;
    }
    if (value.type != PIN_STRUCT) {
        return false;
    }
    value.container = PIN_CONTAINER_SINGLE;
    const type_desc *type = pin_value_type(&value);
    if (!type) {
        return true;
    }
    if (type->alias_of) {
        return snapshot_aliases(
            (pin_value){ .type = PIN_STRUCT, .type_name = type->alias_of },
            depth + 1
        );
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (snapshot_aliases(
                (pin_value){ .type = field->type, .type_name = field->type_name },
                depth + 1
            )) {
            return true;
        }
    }
    return false;
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
    } else {
        switch (execution_intrinsic_kind(id)) {
            /* Copies follow the copied headers; a rewritten one may keep the
             * bits of a pointer instead of copying what it points to. */
            case EXECUTION_INTRINSIC_SNAPSHOT:
            case EXECUTION_INTRINSIC_CONSTRUCT:
            case EXECUTION_INTRINSIC_REFERENCE_READ:
                output = s->retyped && inputs;
                for (int k = 0; k < host->result_count; k++) {
                    output |= inputs &&
                              snapshot_aliases(
                                  f->data_values[host->result_first + k].initial,
                                  0
                              );
                }
                break;
            case EXECUTION_INTRINSIC_UNION_MAKE:
                /* The constructor exposes every payload header, unlike an
                 * arbitrary union snapshot. Ordinary payload copies sever
                 * aliases; only pointer-bearing payloads keep them. */
                output = s->retyped && inputs;
                for (int cell = host->first + 1; cell < host->result_first; cell++) {
                    output |= share_cell(s, cell) &&
                              snapshot_aliases(f->data_values[cell].initial, 0);
                }
                break;
            case EXECUTION_INTRINSIC_POINTER_MAKE:
            case EXECUTION_INTRINSIC_POINTER_READ:
            case EXECUTION_INTRINSIC_POINTER_FIELD:
            case EXECUTION_INTRINSIC_POINTER_REFERENCE:
                /* A pointer is an alias, including when nested in a copied
                 * record. Its pointee never becomes private call storage. */
                output = true;
                break;
            case EXECUTION_INTRINSIC_POINTER_WRITE:
                s->unsafe = true;
                output = false;
                break;
            case EXECUTION_INTRINSIC_UNION_TEST:
            case EXECUTION_INTRINSIC_VALUE_EQUAL:
            case EXECUTION_INTRINSIC_LOCAL_RELEASE: output = false; break;
            case EXECUTION_INTRINSIC_UNION_GET: break;
            case EXECUTION_INTRINSIC_UNION_KEEP: {
                /* A payload value it keeps keeps what it reaches; it copies
                 * those its mask leaves out, as a snapshot does. */
                int keep = s->cell_literals[host->first + 1];
                output = s->retyped && inputs;
                for (int k = 0; k < host->result_first - host->first - 2; k++) {
                    bool kept = keep < 0 || (k < 31 && (keep >> k & 1));
                    output |= (kept || snapshot_aliases(
                                           f->data_values[host->first + 2 + k].initial,
                                           0
                                       )) &&
                              share_cell(s, host->first + 2 + k);
                }
                break;
            }
            case EXECUTION_INTRINSIC_REFERENCE_FIELD:
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
            case EXECUTION_INTRINSIC_REFERENCE_WRITE: {
                /* Text the value cell holds marked `texts`, not its taint, and
                 * the text input above records where it went. */
                int written = s->contents[host->first];
                bool value = share_cell(s, host->first + 1);
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
            case EXECUTION_INTRINSIC_ARRAY_EXTEND:
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
        case BF_COPY_STRING:
        case BF_COPY_ARRAY:
            share_define(s, out, false, SHARE_NONE, SHARE_UNKNOWN);
            break;
        case BF_ARGUMENT:
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
            /* Authored returns copy managed values; raw pointers keep their bits. */
            for (int k = 0; k < call->result_count; k++) {
                int cell = call->result_first + k;
                bool raw = k >= callee->result_count ||
                           callee->result_kinds[k] != BEHAVIOR_VALUE ||
                           callee->result_defaults[k].type == BF_POINTER_TYPE;
                share_taint(s, &s->cells[cell], raw);
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
        f->argument_sharing = f->frozen && share_local(f, false, NULL)
                                ? BEHAVIOR_SHARING_PENDING_SAFE
                                : BEHAVIOR_SHARING_PENDING_COPY;
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

/* Task dependencies follow argument values through one definition. Weak
 * masks hold what a slot, cell or local may carry at any point; within one
 * straight-line block, a value defined there replaces them, since banks and
 * slots are reused. A dereference on any path counts, so removal cancels a
 * task before a continuation that would have skipped it. A value stored where
 * the flow cannot place it may come back from any read it cannot place. */
enum
{
    DEPEND_NO_CELL = -1,
    DEPEND_ANY_CELL = -2
};
typedef struct
{
    uint64_t *weak, *now;
    int *block; /* Where `now` was defined. */
} depend_values;
typedef struct
{
    const behavior_function *f;
    depend_values slots, cells;
    uint64_t *locals;
    int *addressed, *addressed_now, *addressed_block; /* A cell or an enum above. */
    int block; /* The current block, numbered from 1; 0 in the weak passes. */
    uint64_t memory, accessed;
    bool changed;
} depend_flow;

static void depend_join(depend_flow *s, uint64_t *mask, uint64_t value)
{
    if ((*mask | value) != *mask) {
        *mask |= value;
        s->changed = true;
    }
}
static depend_values depend_values_new(int count)
{
    return (depend_values){ .weak = xcalloc((size_t)count + 1, sizeof(uint64_t)),
                            .now = xcalloc((size_t)count + 1, sizeof(uint64_t)),
                            .block = xcalloc((size_t)count + 1, sizeof(int)) };
}
static void depend_values_free(depend_values v)
{
    free(v.weak);
    free(v.now);
    free(v.block);
}
static uint64_t depend_get(const depend_flow *s, const depend_values *v, int index)
{
    return s->block && v->block[index] == s->block ? v->now[index] : v->weak[index];
}
/* `replace` drops what the value held before this block's first write. */
static void depend_set(depend_flow *s, depend_values *v, int index, uint64_t value, bool replace)
{
    depend_join(s, &v->weak[index], value);
    if (!s->block) {
        return;
    }
    if (v->block[index] != s->block) {
        v->now[index] = replace ? 0 : v->weak[index];
        v->block[index] = s->block;
    }
    v->now[index] |= value;
}
static int depend_address_of(const depend_flow *s, int slot)
{
    return s->block && s->addressed_block[slot] == s->block ? s->addressed_now[slot]
                                                            : s->addressed[slot];
}
static void depend_address(depend_flow *s, int slot, int cell)
{
    int old = s->addressed[slot];
    int merged = old == DEPEND_NO_CELL || old == cell ? cell : DEPEND_ANY_CELL;
    if (merged != old) {
        s->addressed[slot] = merged;
        s->changed = true;
    }
    if (s->block) {
        s->addressed_now[slot] = cell;
        s->addressed_block[slot] = s->block;
    }
}
static uint64_t depend_load(const depend_flow *s, int address)
{
    int cell = depend_address_of(s, address);
    uint64_t value = depend_get(s, &s->slots, address) | s->memory;
    for (int c = 0; cell == DEPEND_ANY_CELL && c < acount(s->f->data_values); c++) {
        value |= depend_get(s, &s->cells, c);
    }
    return cell >= 0 ? value | depend_get(s, &s->cells, cell) : value;
}
static void depend_store(depend_flow *s, const behavior_function_instruction *i)
{
    int cell = depend_address_of(s, i->right);
    uint64_t value = depend_get(s, &s->slots, i->left);
    if (cell >= 0) {
        /* Writing the payload word replaces the pointer a cell held. */
        bool payload = i->offset == offsetof(pin_value, d) &&
                       behavior_scalar_size(i->type) >= sizeof(void *);
        depend_set(s, &s->cells, cell, value, payload);
        return;
    }
    depend_join(s, &s->memory, value);
    for (int c = 0; cell == DEPEND_ANY_CELL && c < acount(s->f->data_values); c++) {
        depend_set(s, &s->cells, c, value, false);
    }
}
static void depend_access(depend_flow *s, uint64_t value)
{
    if (s->block) {
        depend_join(s, &s->accessed, value);
    }
}
static void depend_host(depend_flow *s, const behavior_function_host_call *host)
{
    uint64_t inputs = 0;
    for (int cell = host->first; cell < host->result_first; cell++) {
        inputs |= depend_get(s, &s->cells, cell);
    }
    switch (execution_intrinsic_kind(host->capability_id)) {
        case EXECUTION_INTRINSIC_POINTER_WRITE:
            depend_join(s, &s->memory, inputs);
            depend_access(s, depend_get(s, &s->cells, host->first));
            break;
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_FIELD:
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
            depend_access(s, depend_get(s, &s->cells, host->first));
            break;
        default: depend_join(s, &s->memory, inputs); break;
    }
    for (int k = 0; k < host->result_count; k++) {
        depend_set(s, &s->cells, host->result_first + k, inputs, true);
    }
}
/* A synchronous callee's dereference is this definition's. A task started
 * with the pointer takes its own dependency instead. */
static void depend_invoke(depend_flow *s, const behavior_function_invocation *call)
{
    uint64_t arguments = 0;
    for (int k = 0; k < call->count; k++) {
        uint64_t argument = depend_get(s, &s->cells, call->first + k);
        arguments |= argument;
        if (!call->task &&
            (call->function->task_dependencies & execution_task_argument_bit(k))) {
            depend_access(s, argument);
        }
    }
    if (!call->task) {
        depend_join(s, &s->memory, arguments);
    }
    for (int k = 0; k < call->result_count; k++) {
        depend_set(s, &s->cells, call->result_first + k, call->task ? 0 : arguments, true);
    }
}
static void depend_operands(depend_flow *s, const behavior_function_instruction *i)
{
    int operands[3] = { -1, -1, -1 };
    if (behavior_function_left_slot(i->op)) {
        operands[0] = i->left;
    }
    if (behavior_function_right_slot(i->op)) {
        operands[1] = i->right;
    }
    if (behavior_function_third_slot(i->op)) {
        operands[2] = i->target;
    }
    uint64_t value = 0;
    bool address = false;
    for (int k = 0; k < 3; k++) {
        if (operands[k] >= 0) {
            value |= depend_get(s, &s->slots, operands[k]);
            address |= depend_address_of(s, operands[k]) != DEPEND_NO_CELL;
        }
    }
    depend_set(s, &s->slots, i->output, value, true);
    if (!address) {
        return;
    }
    /* A selected cell address keeps its cell; one computed from a cell's
     * address may point into any cell. */
    int selected = depend_address_of(s, i->right);
    bool same = i->op == BF_SELECT && selected == depend_address_of(s, i->target);
    depend_address(s, i->output, same ? selected : DEPEND_ANY_CELL);
}
static void depend_instruction(depend_flow *s, const behavior_function_instruction *i)
{
    const behavior_function *f = s->f;
    int out = i->output;
    switch (i->op) {
        case BF_ARGUMENT:
            depend_set(s, &s->slots, out, execution_task_argument_bit(i->left), true);
            return;
        case BF_CONTEXT: {
            /* Fields other than the exit and outcome words may reach any
             * argument or cell. */
            bool control = i->target == 1 ||
                           i->offset == offsetof(behavior_function_context, results);
            depend_set(s, &s->slots, out, control ? 0 : UINT64_MAX, true);
            if (!control) {
                depend_address(s, out, DEPEND_ANY_CELL);
            }
            return;
        }
        case BF_HOST_SLOT:
            depend_set(s, &s->slots, out, 0, true);
            depend_address(s, out, i->target ? DEPEND_ANY_CELL : i->left);
            return;
        case BF_LOCAL:
            depend_set(s, &s->slots, out, s->locals[i->left], true);
            return;
        case BF_SAVE:
            depend_join(s, &s->locals[i->target], depend_get(s, &s->slots, i->left));
            return;
        case BF_LOAD:
        case BF_READ:
            depend_set(s, &s->slots, out, depend_load(s, i->left), true);
            return;
        case BF_WRITE: depend_store(s, i); return;
        case BF_HOST_CALL: depend_host(s, &f->host_calls[i->target]); return;
        case BF_INVOKE: depend_invoke(s, &f->calls[i->left]); return;
        default: break;
    }
    if (out >= 0 && out < acount(f->slots)) {
        depend_operands(s, i);
    }
}
/* Blocks start at jump targets and after jumps; resumed continuations
 * restore the values their block had, so they need no boundary. */
static bool *depend_leaders(const behavior_function *f)
{
    int count = acount(f->instructions);
    bool *leaders = xcalloc((size_t)count + 1, sizeof *leaders);
    leaders[0] = true;
    for (int pc = 0; pc < count; pc++) {
        int targets[2], n = behavior_function_successors(f, pc, targets);
        for (int k = 0; k < n; k++) {
            if (targets[k] != pc + 1) {
                leaders[targets[k]] = true;
                leaders[pc + 1] = true;
            }
        }
    }
    return leaders;
}
static uint64_t depend_local(const behavior_function *f)
{
    int slots = acount(f->slots), cells = acount(f->data_values),
        count = acount(f->instructions);
    depend_flow s = { .f = f,
                      .slots = depend_values_new(slots),
                      .cells = depend_values_new(cells) };
    s.locals = xcalloc((size_t)acount(f->locals) + 1, sizeof *s.locals);
    s.addressed = xmalloc(((size_t)slots + 1) * sizeof *s.addressed);
    s.addressed_now = xcalloc((size_t)slots + 1, sizeof *s.addressed_now);
    s.addressed_block = xcalloc((size_t)slots + 1, sizeof *s.addressed_block);
    for (int slot = 0; slot <= slots; slot++) {
        s.addressed[slot] = DEPEND_NO_CELL;
    }
    /* Weak masks and addresses only grow, so the passes end. */
    do {
        s.changed = false;
        for (int pc = 0; pc < count; pc++) {
            depend_instruction(&s, &f->instructions[pc]);
        }
    } while (s.changed);
    bool *leaders = depend_leaders(f);
    for (int pc = 0; pc < count; pc++) {
        s.block += leaders[pc];
        depend_instruction(&s, &f->instructions[pc]);
    }
    free(leaders);
    depend_values_free(s.slots);
    depend_values_free(s.cells);
    free(s.locals);
    free(s.addressed);
    free(s.addressed_now);
    free(s.addressed_block);
    return s.accessed;
}
void execution_prepare_task_dependencies(behavior_function *root)
{
    if (!root || root->task_dependencies_ready) {
        return;
    }
    /* Reachable definitions not yet settled start empty and grow together
     * until no callee's dependencies change a caller's. */
    behavior_function **pending = NULL;
    root->task_dependencies_ready = true;
    apush(pending, root);
    for (int at = 0; at < acount(pending); at++) {
        for (int i = 0; i < acount(pending[at]->calls); i++) {
            behavior_function
                *callee = (behavior_function *)pending[at]->calls[i].function;
            if (!callee->task_dependencies_ready) {
                callee->task_dependencies_ready = true;
                apush(pending, callee);
            }
        }
    }
    bool changed;
    do {
        changed = false;
        for (int at = 0; at < acount(pending); at++) {
            uint64_t dependencies = depend_local(pending[at]);
            changed |= dependencies != pending[at]->task_dependencies;
            pending[at]->task_dependencies = dependencies;
        }
    } while (changed);
    afree(pending);
}

bool execution_query_predicate_valid(const behavior_function *function, unsigned bound)
{
    return query_evaluator_valid(function, bound, false);
}
bool execution_query_evaluator_valid(const behavior_function *function, unsigned bound)
{
    return query_evaluator_valid(function, bound, true);
}
