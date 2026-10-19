#include "execution/module_effects.h"

#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/scalar.h"
#include "types/union.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

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
        actual->reusable != canonical->reusable) {
        return 0;
    }
    int count = host->result_first - host->first;
    int expected = -1;
    switch ((execution_intrinsic)(host->capability_id & UINT64_C(0xffffffff))) {
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_UNION_MAKE:
            if (count < 1 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET: {
            int minimum = (host->capability_id & UINT64_C(0xffffffff)) ==
                                  EXECUTION_INTRINSIC_UNION_TEST
                            ? 2
                            : 4;
            if (count < minimum || host->result_count != 1) {
                return 0;
            }
            break;
        }
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: expected = 9; break;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD: expected = 4; break;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_SNAPSHOT: expected = 1; break;
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
static bool plain(pin_value v)
{
    if (v.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (v.type >= PIN_BOOL && v.type <= PIN_F64) {
        return true;
    }
    const type_desc *type = v.type == PIN_STRUCT ? type_catalog_find(v.type_name)
                                                 : NULL;
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
            return none;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_field_call) ||
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
                                      ? type_catalog_find(value.type_name)
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
static bool predicate_host(const behavior_function *f, int pc)
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
    switch ((execution_intrinsic)(id & UINT64_C(0xffffffff))) {
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: {
            origin kind = bank_origin(f, pc, host->first + 2, 0, &steps);
            return kind.constant && kind.bits != LANGUAGE_REFERENCE_STATE;
        }
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
        case EXECUTION_INTRINSIC_ARRAY_STORE:
        case EXECUTION_INTRINSIC_ARRAY_EXTEND: {
            /* In-place updates write only the function's own local array. */
            origin target = bank_origin(f, pc, host->first, 0, &steps);
            return target.private && target.bank;
        }
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
        case EXECUTION_INTRINSIC_UNION_MAKE:
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET:
        case EXECUTION_INTRINSIC_LOCAL_RELEASE: return true;
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
                case BF_HOST_CALL: valid = predicate_host(f, pc); break;
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
                if (id == execution_intrinsic_id(&EXECUTION_reference_read_call) ||
                    id == execution_intrinsic_id(&EXECUTION_reference_write_call)) {
                    if (e->counting) {
                        add(e,
                            id == execution_intrinsic_id(&EXECUTION_reference_write_call),
                            0);
                        break;
                    }
                    int steps = EFFECT_STEPS;
                    origin target = bank_origin(f, pc, call->first, 0, &steps);
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
                    payload_read(e, f, pc, call->first);
                    if (!e->counting &&
                        !plain(f->data_values[call->result_first].initial)) {
                        unknown(e);
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
           value.type == PIN_UNION || value.type == PIN_TASK;
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
    switch ((execution_intrinsic)(id & UINT64_C(0xffffffff))) {
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
        case EXECUTION_INTRINSIC_EVENT_READ:
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
