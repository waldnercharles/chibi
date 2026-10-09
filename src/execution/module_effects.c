#include "execution/module_effects.h"

#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/scalar.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#define EFFECT_DEPTH 64
#define EFFECT_RECORDS 65536
#define EFFECT_STEPS 200000

typedef struct
{
    bool owner, context, world, constant;
    int bank;
    uint64_t bits, component;
    size_t offset, extent;
} origin;
typedef struct
{
    execution_effect_summary *summary;
    uint64_t *reads, *writes;
    size_t capacity, records;
    bool counting, valid;
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
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: expected = 9; break;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD: expected = 4; break;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_SNAPSHOT: expected = 1; break;
        case EXECUTION_INTRINSIC_REFERENCE_WRITE: expected = 2; break;
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
                return none;
            }
            if (address.bank == bank + 1) {
                if (crossing || i->offset != offsetof(pin_value, d)) {
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
                entity.owner && component.constant && component.bits &&
                context.context && plain(f->data_values[call->first + 1].initial)) {
                return (origin){ .owner = true, .component = component.bits };
            }
            return none;
        }
        if (id == execution_intrinsic_id(&EXECUTION_reference_field_call) ||
            id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
            return bank_origin(f, j, call->first, depth + 1, steps);
        }
        return none;
    }
    const pin_value *v = &f->data_values[bank].initial;
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
            case BF_CONTEXT: return (origin){ .context = i->target == 1 };
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
                                 .component = i->value.u64,
                                 .extent = i->type == PIN_BOOL ? 0 : i->offset };
            }
            case BF_ADD: {
                origin address = slot_origin(f, j, i->left, depth + 1, steps);
                origin offset = slot_origin(f, j, i->right, depth + 1, steps);
                if (!offset.constant || !address.extent ||
                    offset.bits > address.extent - address.offset ||
                    behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) !=
                        BEHAVIOR_ADDRESS) {
                    return none;
                }
                address.offset += (size_t)offset.bits;
                if (address.offset) {
                    address.bank = 0;
                }
                return address;
            }
            case BF_CONSTANT:
                return (origin){ .constant = true, .bits = i->value.u64 };
            case BF_HOST_SLOT:
                return (origin){ .bank = i->target == 0 ? i->left + 1 : 0,
                                 .extent = sizeof(pin_value) };
            case BF_LOAD: {
                origin address = slot_origin(f, j, i->left, depth + 1, steps);
                if (address.bank && i->offset == offsetof(pin_value, d)) {
                    return bank_origin(f, j, address.bank - 1, depth + 1, steps);
                }
                return none;
            }
            default: return none;
        }
    }
    return none;
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
static void access(effects *e, const node_access *a)
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
static void function(effects *e, const behavior_function *f, int depth)
{
    if (!f || !e->valid) {
        return;
    }
    if (depth > EFFECT_DEPTH || ++e->records > EFFECT_RECORDS) {
        e->valid = false;
        return;
    }
    for (int j = 0; j < acount(f->components); j++) {
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
                    id == execution_intrinsic_id(&EXECUTION_reference_field_call)) {
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
                    if (!target.owner || !target.component || !plain(*value)) {
                        unknown(e);
                    }
                    add(e, write, target.component);
                    break;
                }
                if (id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
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
                access(e, call->access);
                break;
            }
            case BF_WRITE:
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->right, 0, &steps);
                    size_t width = behavior_scalar_size(i->type);
                    if (!address.bank || i->offset != offsetof(pin_value, d) || !width ||
                        width > sizeof(pin_value) - offsetof(pin_value, d)) {
                        unknown(e);
                    }
                }
                break;
            case BF_LOAD:
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin address = slot_origin(f, pc, i->left, 0, &steps);
                    size_t width = i->type >= PIN_BOOL && i->type <= PIN_F64
                                     ? behavior_scalar_size(i->type)
                                     : 0;
                    bool row = address.owner && address.component &&
                               behavior_function_kind(f, BEHAVIOR_BANK_SLOT, i->output) ==
                                   BEHAVIOR_VALUE &&
                               i->type >= PIN_BOOL && i->type <= PIN_F64 &&
                               address.offset <= address.extent &&
                               i->offset <= address.extent - address.offset &&
                               width <= address.extent - address.offset - i->offset;
                    if (!address.bank && !row) {
                        unknown(e);
                    }
                }
                break;
            case BF_CONTEXT:
                if (!i->target &&
                    i->offset != offsetof(behavior_function_context, delta_seconds) &&
                    i->offset != offsetof(behavior_function_context, now)) {
                    unknown(e);
                }
                break;
            case BF_COMPONENT:
                if (!e->counting) {
                    int steps = EFFECT_STEPS;
                    origin world = slot_origin(f, pc, i->left, 0, &steps);
                    if (!world.world || !slot_origin(f, pc, i->right, 0, &steps).owner) {
                        unknown(e);
                    }
                }
                break;
            case BF_WORLD:
            case BF_ENTITY: break;
            case BF_READ:
            case BF_ARRAY:
            case BF_INDEX:
            case BF_COLUMN:
            case BF_COLUMN_TRY:
            case BF_ROW:
            case BF_ARGUMENT:
            case BF_LOCAL:
            case BF_SAVE:
            case BF_RANDOM:
            case BF_RANDOM_RANGE:
            case BF_STATE_SELECT:
            case BF_RETRIGGER:
            case BF_QUERY:
            case BF_COMMAND:
            case BF_COMMAND_BEGIN:
            case BF_INVOKE:
            case BF_SUSPEND:
            case BF_DELAY:
            case BF_STORE:
            case BF_STORE_VALUE:
            case BF_VARIABLE:
            case BF_STORE_HEADER:
            case BF_STORE_TYPE_NAME:
            case BF_INTERN_STRING:
            case BF_HOST_SERVICE: unknown(e); break;
            default: break;
        }
    }
}
static void plan(effects *e, const execution_module_plan *p)
{
    for (int i = 0; i < p->query_count; i++) {
        if (p->queries[i].component) {
            add(e, false, p->queries[i].component);
        }
    }
    if (!e->counting && (p->state_count || p->binding_count ||
                         p->action_count || p->import_count)) {
        e->summary->parallel_safe = false;
    }
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *f = &p->functions[i];
        e->function = f->id;
        if (!e->counting && (f->kind != EXECUTION_FUNCTION_SYSTEM || f->subscription)) {
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
    effects e = { .counting = true, .valid = true };
    plan(&e, p);
    return !e.valid || e.capacity > EFFECT_RECORDS ||
                   e.capacity > SIZE_MAX / (2 * sizeof(uint64_t))
             ? SIZE_MAX
             : e.capacity * 2 * sizeof(uint64_t);
}
bool execution_module_build_effects(execution_module *m)
{
    size_t bytes = execution_module_effects_bytes(&m->plan);
    if (bytes == SIZE_MAX) {
        return false;
    }
    m->effect_ids = bytes ? xcalloc(1, bytes) : NULL;
    m->effect_bytes = bytes;
    size_t capacity = bytes / (2 * sizeof(uint64_t));
    m->effects = (execution_effect_summary){
        .reads = m->effect_ids,
        .writes = capacity ? m->effect_ids + capacity : NULL,
        .declared = true,
        .deterministic = true,
        .unknown_operation = -1,
        .unknown_instruction = -1,
        .parallel_safe = m->plan.function_count > 0
    };
    effects e = { .summary = &m->effects,
                  .reads = m->effect_ids,
                  .writes = capacity ? m->effect_ids + capacity : NULL,
                  .valid = true };
    plan(&e, &m->plan);
    return e.valid;
}
execution_effect_summary execution_module_effects(const execution_module *m)
{
    assert(m);
    return m->effects;
}
