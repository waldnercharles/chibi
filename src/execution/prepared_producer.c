#include "execution/prepared_producer.h"

#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MODULES = 32,
    FUNCTIONS = 256,
    INSTRUCTIONS = 100000,
    PROOF_STEPS = 2000000,
    LIVENESS_WORDS = 2000000
};
typedef struct
{
    const execution_producer_contract *contract;
    const execution_module *modules[MODULES];
    const behavior_function *functions[FUNCTIONS];
    const execution_module *owners[FUNCTIONS];
    int module_count, function_count, instructions, steps;
    char *error;
    size_t size;
} proof;

static bool fail(proof *p, const char *message)
{
    if (p->size) {
        snprintf(p->error, p->size, "%s", message);
    }
    return false;
}
static bool text_equal(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_shape(pin_value a, pin_value b)
{
    if (a.type != b.type || a.container != b.container || !!a.record != !!b.record) {
        return false;
    }
    if (a.record && !pin_record_same(a.record, b.record)) {
        return false;
    }
    return (a.type != PIN_STRUCT && a.type != PIN_HANDLE && a.type != PIN_ENUM) ||
           text_equal(a.type_name, b.type_name);
}
static bool scalar(pin_value value)
{
    return value.container == PIN_CONTAINER_SINGLE && !value.record &&
           ((value.type >= PIN_BOOL && value.type <= PIN_F64) || value.type == PIN_STR ||
            value.type == PIN_NAME || value.type == PIN_HANDLE);
}
static const type_desc *layout(proof *p, pin_value value)
{
    if (value.record) {
        return &value.record->type;
    }
    for (int i = 0; i < p->contract->native_type_count; i++) {
        const type_desc *type = p->contract->native_types[i];
        if (text_equal(value.type_name, type->name)) {
            return type;
        }
    }
    return NULL;
}
static bool shape_valid(proof *p, pin_value value)
{
    if (value.container != PIN_CONTAINER_SINGLE &&
        value.container != PIN_CONTAINER_ARRAY) {
        return false;
    }
    value.container = PIN_CONTAINER_SINGLE;
    if (scalar(value)) {
        return value.type != PIN_HANDLE || text_equal(value.type_name, "ecs.entity");
    }
    if (value.type != PIN_STRUCT || !value.type_name) {
        return false;
    }
    const type_desc *type = layout(p, value);
    if (!type || !text_equal(type->name, value.type_name)) {
        return false;
    }
    if (value.record) {
        if (!pin_record_valid(value.record)) {
            return false;
        }
        for (int i = 0; i < p->contract->required_record_count; i++) {
            const pin_record *required = p->contract->required_records[i];
            if ((required->id == value.record->id ||
                 text_equal(required->type.name, value.type_name)) &&
                !pin_record_same(required, value.record)) {
                return false;
            }
        }
    }
    for (int i = 0; i < type->field_count; i++) {
        pin_value field = { .type = type->fields[i].type,
                            .type_name = type->fields[i].type_name };
        if (!scalar(field) || (field.type == PIN_HANDLE &&
                               !text_equal(field.type_name, "ecs.entity"))) {
            return false;
        }
    }
    return true;
}
static bool same_call(const node_host_call *a, const node_host_call *b)
{
    return a && b && text_equal(a->service, b->service) &&
           text_equal(a->missing_service, b->missing_service) &&
           a->invoke == b->invoke && a->value == b->value &&
           a->context == b->context && a->action == b->action &&
           a->optional_service == b->optional_service &&
           a->variadic == b->variadic && a->reusable == b->reusable;
}
static bool same_access(const node_access *a, const node_access *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->flags != b->flags || a->target_input != b->target_input ||
        a->read_count != b->read_count || a->write_count != b->write_count ||
        !!a->read_refs != !!b->read_refs) {
        return false;
    }
    for (int i = 0; i < a->read_count; i++) {
        if (a->reads[i] != b->reads[i]) {
            return false;
        }
    }
    for (int i = 0; i < a->write_count; i++) {
        if (a->writes[i] != b->writes[i]) {
            return false;
        }
    }
    if (a->read_refs) {
        for (int i = 0; i < 64; i++) {
            if (!a->read_refs[i] || !b->read_refs[i]) {
                return a->read_refs[i] == b->read_refs[i];
            }
            if (*a->read_refs[i] != *b->read_refs[i]) {
                return false;
            }
        }
        return false;
    }
    return true;
}
static bool ports_valid(
    const behavior_function *f,
    int first,
    int count,
    const execution_producer_port *ports,
    int declared,
    bool variadic,
    bool optional,
    pin_value *groups,
    bool *present
)
{
    if ((!variadic && count != declared) ||
        (variadic && (!declared || count < declared - (optional ? 1 : 0)))) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const execution_producer_port *port = &ports[i < declared ? i : declared - 1];
        pin_value value = f->data_values[first + i].initial;
        if (!port->generic) {
            if (!same_shape(value, port->shape)) {
                return false;
            }
            continue;
        }
        if (port->shape.container != value.container || port->group < 0 ||
            port->group >= 16) {
            return false;
        }
        value.container = PIN_CONTAINER_SINGLE;
        if (port->group && present[port->group] &&
            !same_shape(groups[port->group], value)) {
            return false;
        }
        if (port->group) {
            groups[port->group] = value;
            present[port->group] = true;
        }
    }
    return true;
}
static bool intrinsic_valid(proof *p, const behavior_function *f, const behavior_function_host_call *h)
{
    const node_host_call *call = NULL;
    if (h->capability_id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
        call = &EXECUTION_snapshot_call;
    } else if (h->capability_id == execution_intrinsic_id(&EXECUTION_construct_call)) {
        call = &EXECUTION_construct_call;
    } else if (h->capability_id == execution_intrinsic_id(&EXECUTION_value_equal_call)) {
        call = &EXECUTION_value_equal_call;
    }
    if (!call || h->capability_abi != execution_intrinsic_abi(h->capability_id) ||
        !same_call(h->call, call) || !same_access(h->access, &EXECUTION_value_access) ||
        h->result_count != 1 || h->outcome_count) {
        return false;
    }
    int count = h->result_first - h->first;
    pin_value result = f->data_values[h->result_first].initial;
    pin_value base = f->data_values[h->first].initial;
    if (call == &EXECUTION_snapshot_call) {
        return count == 1 && same_shape(base, result);
    }
    if (call == &EXECUTION_value_equal_call) {
        return count == 2 && base.record && base.container == PIN_CONTAINER_SINGLE &&
               same_shape(base, f->data_values[h->first + 1].initial) &&
               result.type == PIN_BOOL && result.container == PIN_CONTAINER_SINGLE;
    }
    if (count < 1 || !same_shape(base, result)) {
        return false;
    }
    if (base.container == PIN_CONTAINER_ARRAY) {
        base.container = PIN_CONTAINER_SINGLE;
        for (int i = 1; i < count; i++) {
            if (!same_shape(base, f->data_values[h->first + i].initial)) {
                return false;
            }
        }
        return true;
    }
    /* Native records are captured facts, never constructor destinations. */
    const type_desc *type = layout(p, base);
    if (!base.record || !type || !(count & 1)) {
        return false;
    }
    for (int i = 1; i < count; i += 2) {
        pin_value offset = f->data_values[h->first + i].initial;
        pin_value value = f->data_values[h->first + i + 1].initial;
        bool found = false;
        for (int j = 0; j < type->field_count; j++) {
            const type_field *field = &type->fields[j];
            found |= offset.type == PIN_U64 &&
                     offset.container == PIN_CONTAINER_SINGLE &&
                     offset.u64 == field->offset &&
                     same_shape(
                         value,
                         (pin_value){ .type = field->type, .type_name = field->type_name }
                     );
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
static bool hosts_valid(proof *p, const behavior_function *f)
{
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *h = &f->host_calls[i];
        if (h->outcome_count || h->first < 0 || h->result_first < h->first ||
            h->result_count < 0 ||
            h->result_first > acount(f->data_values) - h->result_count) {
            return fail(p, "Invalid producer host bank");
        }
        if (intrinsic_valid(p, f, h)) {
            continue;
        }
        const execution_producer_call *expected = NULL;
        for (int j = 0; j < p->contract->call_count; j++) {
            if (p->contract->calls[j].id == h->capability_id) {
                expected = &p->contract->calls[j];
                break;
            }
        }
        if (!expected || expected->abi != h->capability_abi ||
            !same_call(h->call, expected->call) ||
            !same_access(h->access, expected->access)) {
            return fail(p, "Producer calls an unapproved native capability or implementation");
        }
        pin_value groups[16] = { 0 };
        bool present[16] = { 0 };
        if (!ports_valid(
                f,
                h->first,
                h->result_first - h->first,
                expected->inputs,
                expected->input_count,
                expected->variadic,
                expected->variadic_optional,
                groups,
                present
            ) ||
            !ports_valid(
                f,
                h->result_first,
                h->result_count,
                expected->outputs,
                expected->output_count,
                false,
                false,
                groups,
                present
            )) {
            return fail(p, "Producer concrete host arguments differ from the trusted ABI");
        }
    }
    return true;
}

typedef enum
{
    UNKNOWN,
    BAD,
    SCALAR,
    ZERO,
    PAYLOAD,
    HEADER,
    RESULTS,
    EXIT_CONTEXT,
    NAME,
    RECORD,
    TYPE_NAME
} origin_kind;
typedef struct
{
    origin_kind kind;
    pin_value shape;
    int bank; /* Host bank, or negative one-based result index. */
    bool constant;
    uint64_t bits;
} origin;
static origin payload(pin_value shape)
{
    return (origin){ .kind = shape.type == PIN_STRUCT || shape.container != PIN_CONTAINER_SINGLE
                               ? PAYLOAD
                               : SCALAR,
                     .shape = shape };
}
static origin merge(origin a, origin b)
{
    if (a.kind == UNKNOWN) {
        return b;
    }
    if (b.kind == UNKNOWN) {
        return a;
    }
    if (a.kind == ZERO && b.kind == PAYLOAD) {
        return b;
    }
    if (b.kind == ZERO && a.kind == PAYLOAD) {
        return a;
    }
    if (a.kind != b.kind || !same_shape(a.shape, b.shape) ||
        (a.kind == HEADER && a.bank != b.bank) ||
        (a.kind == TYPE_NAME && !text_equal(a.shape.str, b.shape.str))) {
        return (origin){ .kind = BAD };
    }
    if (!b.constant || a.bits != b.bits) {
        a.constant = false;
    }
    return a;
}
static bool origin_equal(origin a, origin b)
{
    return a.kind == b.kind && same_shape(a.shape, b.shape) && a.bank == b.bank &&
           a.constant == b.constant && (!a.constant || a.bits == b.bits) &&
           (a.kind != TYPE_NAME || text_equal(a.shape.str, b.shape.str));
}
static bool numeric(origin a)
{
    return a.kind == SCALAR && a.shape.type >= PIN_BOOL && a.shape.type <= PIN_F64;
}
static bool matches(origin value, pin_value shape)
{
    origin expected = payload(shape);
    return (value.kind == ZERO && expected.kind == PAYLOAD) ||
           (value.kind == expected.kind && same_shape(value.shape, shape));
}
static bool metadata(origin value, origin header, size_t offset, pin_type type)
{
    if (!same_shape(value.shape, header.shape)) {
        return false;
    }
    if (offset == offsetof(pin_value, type)) {
        return type == PIN_I32 && value.kind == SCALAR && value.constant &&
               value.bits == (uint64_t)header.shape.type;
    }
    if (offset == offsetof(pin_value, container)) {
        return type == PIN_I32 && value.kind == SCALAR && value.constant &&
               value.bits == (uint64_t)header.shape.container;
    }
    return type == BF_POINTER_TYPE &&
           ((offset == offsetof(pin_value, type_name) && value.kind == NAME) ||
            (offset == offsetof(pin_value, record) && value.kind == RECORD));
}
static origin loaded(proof *p, origin address, size_t offset, pin_type type)
{
    if (address.kind == UNKNOWN) {
        return address;
    }
    if (address.kind == HEADER) {
        if (offset == offsetof(pin_value, d)) {
            origin value = payload(address.shape);
            pin_type physical = value.kind == PAYLOAD ? BF_POINTER_TYPE
                                                      : address.shape.type;
            return physical == type ? value : (origin){ .kind = BAD };
        }
        if (offset == offsetof(pin_value, type_name) && type == BF_POINTER_TYPE) {
            return (origin){ .kind = NAME, .shape = address.shape };
        }
        if (offset == offsetof(pin_value, record) && type == BF_POINTER_TYPE) {
            return (origin){ .kind = RECORD, .shape = address.shape };
        }
        /* Header metadata keeps its source shape, and cannot be passed as a
         * scalar value or an address. Only an identical result header takes it. */
        if (type == PIN_I32 && (offset == offsetof(pin_value, type) ||
                                offset == offsetof(pin_value, container))) {
            return (origin){ .kind = SCALAR,
                             .shape = address.shape,
                             .constant = true,
                             .bits = offset == offsetof(pin_value, type)
                                       ? (uint64_t)address.shape.type
                                       : (uint64_t)address.shape.container };
        }
    }
    if (address.kind == PAYLOAD && address.shape.type == PIN_STRUCT &&
        address.shape.container == PIN_CONTAINER_SINGLE) {
        const type_desc *desc = layout(p, address.shape);
        for (int i = 0; desc && i < desc->field_count; i++) {
            const type_field *field = &desc->fields[i];
            if (field->offset == offset && field->type == type) {
                return payload((pin_value){ .type = type, .type_name = field->type_name });
            }
        }
    }
    return (origin){ .kind = BAD };
}
static bool immutable_constructor_offset(const behavior_function *f, int bank)
{
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *h = &f->host_calls[i];
        if (h->capability_id == execution_intrinsic_id(&EXECUTION_construct_call) &&
            f->data_values[h->first].initial.container == PIN_CONTAINER_SINGLE &&
            bank > h->first && bank < h->result_first && (bank - h->first) % 2) {
            return true;
        }
    }
    return false;
}
static bool instruction(
    proof *p,
    const behavior_function *f,
    const behavior_function_instruction *i,
    origin *slots,
    origin *locals,
    origin *out,
    bool final
)
{
    origin a = i->left_slot ? slots[i->left] : (origin){ 0 };
    origin b = i->right_slot ? slots[i->right] : (origin){ 0 };
    if (i->indirect || i->frame || i->component ||
        i->left_slot != behavior_function_left_slot(i->op) ||
        i->right_slot != behavior_function_right_slot(i->op)) {
        return false;
    }
    if (final && ((i->left_slot && (a.kind == UNKNOWN || a.kind == BAD)) ||
                  (i->right_slot && (b.kind == UNKNOWN || b.kind == BAD)))) {
        return false;
    }
    /* Authored return lowers to setting this invocation's exit flag. Its
     * context address may only be the destination of that exact true write. */
    if (a.kind == EXIT_CONTEXT || (b.kind == EXIT_CONTEXT && i->op != BF_WRITE)) {
        return false;
    }
    *out = (origin){ 0 };
    switch (i->op) {
        case BF_CONSTANT:
            if (f->slot_kinds[i->output] == BEHAVIOR_ADDRESS) {
                if (i->value.u64) {
                    return false;
                }
                *out = (origin){ .kind = ZERO };
                return true;
            }
            if (!scalar(i->value)) {
                return false;
            }
            *out = payload(i->value);
            /* Literal bits prove integer indices and boolean control writes.
             * Floating values retain their scalar type, never integer facts. */
            out->constant = i->value.type >= PIN_BOOL && i->value.type <= PIN_U64;
            if (out->constant) {
                out->bits = behavior_scalar_bits(i->value);
            }
            return true;
        case BF_VARIABLE:
            *out = payload(f->variable_defaults[i->left]);
            return true;
        case BF_ARGUMENT:
            *out = payload(f->argument_defaults[i->left]);
            return true;
        case BF_LOCAL: *out = locals[i->left]; return true;
        case BF_SAVE:
            if (a.kind == HEADER || a.kind == RESULTS || a.kind == NAME ||
                a.kind == RECORD || a.kind == TYPE_NAME) {
                return false;
            }
            locals[i->target] = merge(locals[i->target], a);
            return !final || locals[i->target].kind != BAD;
        case BF_ARRAY:
            if (i->target != BF_ARGUMENT && i->target != BF_CONSTANT) {
                return false;
            }
            *out = payload(
                i->target == BF_ARGUMENT ? f->argument_defaults[i->left] : i->value
            );
            return out->kind == PAYLOAD && shape_valid(p, out->shape);
        case BF_TYPE_NAME:
            if (i->target != BF_CONSTANT || i->value.type != PIN_NAME) {
                return false;
            }
            *out = (origin){ .kind = TYPE_NAME, .shape = i->value };
            return true;
        case BF_HOST_SLOT:
            if (i->target) {
                return false;
            }
            *out = (origin){ .kind = HEADER,
                             .shape = f->data_values[i->left].initial,
                             .bank = i->left };
            return true;
        case BF_CONTEXT:
            if (i->target == 1 && i->type == BF_POINTER_TYPE && !i->offset) {
                *out = (origin){ .kind = EXIT_CONTEXT };
                return true;
            }
            if (i->target || i->type != BF_POINTER_TYPE ||
                i->offset != offsetof(behavior_function_context, results)) {
                return false;
            }
            *out = (origin){ .kind = RESULTS };
            return true;
        case BF_INDEX:
            if (a.kind == UNKNOWN || b.kind == UNKNOWN) {
                return !final;
            }
            if (a.kind != RESULTS || !b.constant || b.shape.type != PIN_I32 ||
                b.bits >= (uint64_t)f->result_count ||
                i->offset != sizeof(pin_value)) {
                return false;
            }
            *out = (origin){ .kind = HEADER,
                             .bank = -(int)b.bits - 1,
                             .shape = f->result_defaults[b.bits] };
            return true;
        case BF_LOAD:
        case BF_READ:
            *out = loaded(p, a, i->offset, i->type);
            return out->kind != BAD && (!final || out->kind != UNKNOWN);
        case BF_WRITE:
            if (a.kind == UNKNOWN || b.kind == UNKNOWN) {
                return !final;
            }
            if (b.kind == EXIT_CONTEXT) {
                return i->offset == offsetof(behavior_function_context, exit_requested) &&
                       i->type == PIN_BOOL && a.kind == SCALAR &&
                       a.shape.type == PIN_BOOL && a.constant && a.bits == 1;
            }
            if (b.kind != HEADER) {
                return false;
            }
            if (b.bank >= 0 && immutable_constructor_offset(f, b.bank)) {
                return false;
            }
            if (i->offset == offsetof(pin_value, d)) {
                return matches(a, b.shape) && i->type == (payload(b.shape).kind == PAYLOAD
                                                              ? BF_POINTER_TYPE
                                                              : b.shape.type);
            }
            return b.bank < 0 && metadata(a, b, i->offset, i->type);
        case BF_STORE:
            return (!final && a.kind == UNKNOWN) ||
                   matches(a, f->variable_defaults[i->target]);
        case BF_RESULT:
            return (!final && a.kind == UNKNOWN) ||
                   matches(a, f->result_defaults[i->target]);
        case BF_RESULT_ARRAY:
            return (!final && (a.kind == UNKNOWN || b.kind == UNKNOWN)) ||
                   (matches(a, i->value) &&
                    same_shape(i->value, f->result_defaults[i->target]) &&
                    b.kind == TYPE_NAME &&
                    text_equal(b.shape.str, i->value.type_name));
        case BF_SELECT: {
            origin third = slots[i->target];
            *out = merge(b, third);
            return (!final || numeric(a)) && out->kind != BAD;
        }
        case BF_CAST:
        case BF_CONVERT:
            if (a.kind == UNKNOWN) {
                return !final;
            }
            if (!numeric(a)) {
                return false;
            }
            *out = payload((pin_value){ .type = i->type });
            return numeric(*out);
        case BF_STRING_EQUAL:
            *out = payload((pin_value){ .type = PIN_BOOL });
            return !final || (a.kind == SCALAR && b.kind == SCALAR &&
                              behavior_scalar_string(a.shape.type) &&
                              behavior_scalar_string(b.shape.type));
        case BF_HOST_CALL:
        case BF_INVOKE:
        case BF_CHECK:
        case BF_CALL_CHECK:
        case BF_JUMP:
        case BF_RETURN:
        case BF_END:
        case BF_SOURCE_POINT:
        case BF_SOURCE_WATCH: return true;
        case BF_BRANCH:
        case BF_REQUIRE:
        case BF_ASSERT: return !final || numeric(a);
        default:
            if (i->op < BF_ADD || i->op > BF_SHR) {
                return false;
            }
            if (i->op == BF_FLAGS) {
                return false;
            }
            if (final &&
                ((!numeric(a) && !(a.kind == SCALAR && a.shape.type == PIN_HANDLE &&
                                   (i->op == BF_EQ || i->op == BF_NE))) ||
                 (i->right_slot &&
                  (!numeric(b) && !(b.kind == SCALAR && b.shape.type == PIN_HANDLE &&
                                    (i->op == BF_EQ || i->op == BF_NE)))))) {
                return false;
            }
            *out = payload((pin_value){
                .type = i->op >= BF_LT && i->op <= BF_NE ? PIN_BOOL : i->type });
            return true;
    }
}

static bool backed(uint64_t *bits, int index)
{
    return (bits[index / 64] & (UINT64_C(1) << (index % 64))) != 0;
}
static void set_backed(uint64_t *bits, int index, bool value)
{
    uint64_t mask = UINT64_C(1) << (index % 64);
    if (value) {
        bits[index / 64] |= mask;
    } else {
        bits[index / 64] &= ~mask;
    }
}
static bool struct_value(pin_value value)
{
    return value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE;
}
static bool constant_backing(proof *p, pin_value value)
{
    if (value.type != PIN_STRUCT || value.container != PIN_CONTAINER_ARRAY ||
        !value.ptr) {
        return true;
    }
    const pin_array *array = value.ptr;
    if (!pin_array_valid(array) || array->count > PROOF_STEPS - p->steps ||
        array->element_type != value.type ||
        !text_equal(array->type_name, value.type_name) ||
        !!array->record != !!value.record ||
        (array->record && !pin_record_same(array->record, value.record))) {
        return false;
    }
    for (int i = 0; i < array->count; i++) {
        p->steps++;
        pin_value item = pin_array_get(array, i);
        pin_value expected = value;
        expected.container = PIN_CONTAINER_SINGLE;
        if (!item.ptr || !same_shape(item, expected)) {
            return false;
        }
    }
    return true;
}
static bool host_backed(proof *p, const behavior_function_host_call *host)
{
    if (host->capability_id == execution_intrinsic_id(&EXECUTION_construct_call) ||
        host->capability_id == execution_intrinsic_id(&EXECUTION_snapshot_call)) {
        return true;
    }
    for (int i = 0; i < p->contract->call_count; i++) {
        if (host->capability_id == p->contract->calls[i].id) {
            return p->contract->calls[i].backed_results;
        }
    }
    return false;
}
/* A separate forward must-analysis tracks payload backing at every control-flow
 * join. Shape inference alone cannot certify a read after one branch cleared
 * its host bank, or a local whose initializer was skipped. A false fact means
 * possibly NULL; only a successful checked producer or an owned initial buffer
 * establishes backing. Cleared headers do not invalidate retained slot copies.
 * Values never point into another invocation's mutable header storage. */
static bool liveness_step(
    proof *p,
    const behavior_function *f,
    const origin *slots,
    const behavior_function_instruction *i,
    uint64_t *bits,
    bool check
)
{
    int locals = acount(f->slots), banks = locals + acount(f->locals),
        results = banks + acount(f->data_values);
    bool value = false;
    switch (i->op) {
        case BF_HOST_SLOT:
        case BF_CONTEXT:
        case BF_INDEX: value = true; break;
        case BF_ARRAY:
            value = i->target == BF_ARGUMENT
                      ? struct_value(f->argument_defaults[i->left])
                      : i->value.ptr != NULL;
            break;
        case BF_ARGUMENT:
            value = struct_value(f->argument_defaults[i->left]);
            break;
        case BF_LOCAL: value = backed(bits, locals + i->left); break;
        case BF_SAVE:
            set_backed(bits, locals + i->target, backed(bits, i->left));
            break;
        case BF_SELECT:
            value = backed(bits, i->right) && backed(bits, i->target);
            break;
        case BF_LOAD:
        case BF_READ: {
            origin address = slots[i->left];
            if (check && i->op == BF_LOAD && !backed(bits, i->left)) {
                return false;
            }
            if (address.kind == HEADER && i->offset == offsetof(pin_value, d)) {
                value = backed(
                    bits,
                    address.bank >= 0 ? banks + address.bank : results - address.bank - 1
                );
            }
            break;
        }
        case BF_WRITE: {
            origin address = slots[i->right];
            if (check && !backed(bits, i->right)) {
                return false;
            }
            if (i->offset == offsetof(pin_value, d)) {
                int at = address.bank >= 0 ? banks + address.bank
                                           : results - address.bank - 1;
                set_backed(bits, at, backed(bits, i->left));
            }
            break;
        }
        case BF_RESULT_ARRAY:
            set_backed(bits, results + i->target, backed(bits, i->left));
            break;
        case BF_HOST_CALL: {
            const behavior_function_host_call *host = &f->host_calls[i->target];
            bool construct = host->capability_id ==
                             execution_intrinsic_id(&EXECUTION_construct_call);
            for (int j = host->first; check && j < host->result_first; j++) {
                if (struct_value(f->data_values[j].initial) &&
                    !(construct && j == host->first) && !backed(bits, banks + j)) {
                    return false;
                }
            }
            bool produces = host_backed(p, host);
            for (int j = 0; j < host->result_count; j++) {
                set_backed(bits, banks + host->result_first + j, produces);
            }
            break;
        }
        case BF_INVOKE: {
            const behavior_function_invocation *call = &f->calls[i->left];
            for (int j = 0; check && j < call->count; j++) {
                if (struct_value(f->data_values[call->first + j].initial) &&
                    !backed(bits, banks + call->first + j)) {
                    return false;
                }
            }
            /* All callable returns are checked independently below. */
            for (int j = 0; j < call->result_count; j++) {
                set_backed(
                    bits,
                    banks + call->result_first + j,
                    struct_value(call->function->result_defaults[j])
                );
            }
            break;
        }
        case BF_RETURN:
        case BF_END:
            for (int j = 0; check && j < f->result_count; j++) {
                if (struct_value(f->result_defaults[j]) && !backed(bits, results + j)) {
                    return false;
                }
            }
            break;
        default: break;
    }
    if (i->op < BF_STORE) {
        set_backed(bits, i->output, value);
    }
    return true;
}
static bool liveness_valid(proof *p, const behavior_function *f, const origin *slots)
{
    int count = acount(f->instructions), local_count = acount(f->locals),
        bank_count = acount(f->data_values),
        banks = acount(f->slots) + local_count, results = banks + bank_count;
    size_t words = ((size_t)results + (size_t)f->result_count + 63) / 64;
    if (!words) {
        words = 1;
    }
    if (!count || words > LIVENESS_WORDS / (size_t)count) {
        return fail(p, "Producer payload liveness proof limit exceeded");
    }
    for (int i = 0; i < bank_count; i++) {
        if (!constant_backing(p, f->data_values[i].initial)) {
            return fail(p, "Producer constant array has an unbacked record");
        }
    }
    for (int i = 0; i < count; i++) {
        if (f->instructions[i].op == BF_ARRAY && f->instructions[i].target == BF_CONSTANT &&
            !constant_backing(p, f->instructions[i].value)) {
            return fail(p, "Producer literal array has an unbacked record");
        }
    }
    uint64_t *incoming = xcalloc((size_t)count * words, sizeof *incoming);
    uint64_t *next = xcalloc(words, sizeof *next);
    bool *seen = xcalloc((size_t)count, sizeof *seen);
    seen[0] = true;
    for (int i = 0; i < bank_count; i++) {
        const behavior_function_data_value *bank = &f->data_values[i];
        set_backed(incoming, banks + i, bank->size || bank->initial.ptr);
    }
    for (int i = 0; i < f->result_count; i++) {
        set_backed(incoming, results + i, struct_value(f->result_defaults[i]));
    }
    bool valid = true, changed = true;
    size_t work = 0;
    while (valid && changed) {
        changed = false;
        for (int pc = 0; pc < count; pc++) {
            if (!seen[pc]) {
                continue;
            }
            if ((work += words) > 64000000) {
                valid = false;
                break;
            }
            memcpy(next, incoming + (size_t)pc * words, words * sizeof *next);
            const behavior_function_instruction *i = &f->instructions[pc];
            liveness_step(p, f, slots, i, next, false);
            int targets[2] = { pc + 1, -1 };
            if (i->op == BF_JUMP) {
                targets[0] = i->target;
            } else if (i->op == BF_BRANCH) {
                targets[1] = i->target;
            } else if (i->op == BF_RETURN || i->op == BF_END) {
                targets[0] = -1;
            }
            for (int edge = 0; edge < 2; edge++) {
                int target = targets[edge];
                if (target < 0 || target >= count) {
                    continue;
                }
                uint64_t *destination = incoming + (size_t)target * words;
                if (!seen[target]) {
                    memcpy(destination, next, words * sizeof *next);
                    seen[target] = true;
                    changed = true;
                    continue;
                }
                for (size_t word = 0; word < words; word++) {
                    uint64_t both = destination[word] & next[word];
                    changed |= both != destination[word];
                    destination[word] = both;
                }
            }
        }
    }
    for (int pc = 0; valid && pc < count; pc++) {
        if (!seen[pc]) {
            continue;
        }
        memcpy(next, incoming + (size_t)pc * words, words * sizeof *next);
        valid = liveness_step(p, f, slots, &f->instructions[pc], next, true);
        if (!valid && p->size) {
            snprintf(p->error, p->size, "Producer payload may be unbacked at instruction %d", pc);
        }
    }
    free(seen);
    free(next);
    free(incoming);
    if (!valid && (!p->size || !p->error[0])) {
        fail(p, "Producer payload proof work limit exceeded");
    }
    return valid;
}

static void instruction_error(proof *p, int pc, const behavior_function_instruction *i)
{
    if (!p->size) {
        return;
    }
    if (i->op == BF_CONTEXT) {
        snprintf(
            p->error,
            p->size,
            "Unproved producer instruction %d (opcode %d target %d type %d "
            "offset %zu; results %zu frame %d component %llu)",
            pc,
            i->op,
            i->target,
            i->type,
            i->offset,
            offsetof(behavior_function_context, results),
            i->frame,
            (unsigned long long)i->component
        );
        return;
    }
    snprintf(p->error, p->size, "Unproved producer instruction %d (opcode %d)", pc, i->op);
}
static bool body_valid(proof *p, const behavior_function *f, const execution_module *owner)
{
    if (!f || !f->frozen || f->waits || f->component_access || f->component_bindings ||
        f->parallel || acount(f->queries) || acount(f->commands) ||
        acount(f->state_selections) || acount(f->components)) {
        return fail(p, "Producer contains asynchronous, component or query execution");
    }
    if (f->variable_count != owner->plan.state_count) {
        return fail(p, "Producer function uses a foreign state bank");
    }
    for (int i = 0; i < f->variable_count; i++) {
        if (!same_shape(f->variable_defaults[i], owner->plan.states[i].initial)) {
            return fail(p, "Producer function state schema differs from its module");
        }
    }
    for (int i = 0; i < f->argument_count; i++) {
        if (!shape_valid(p, f->argument_defaults[i]) ||
            f->argument_kinds[i] != BEHAVIOR_VALUE) {
            return fail(p, "Producer has an unsupported input or reference parameter");
        }
    }
    for (int i = 0; i < f->result_count; i++) {
        if (!shape_valid(p, f->result_defaults[i]) ||
            f->result_kinds[i] != BEHAVIOR_VALUE) {
            return fail(p, "Producer has an unsupported result");
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        if (!shape_valid(p, f->data_values[i].initial) ||
            f->data_values[i].kind != BEHAVIOR_VALUE) {
            return fail(p, "Producer has an unsupported host value or reference");
        }
        if (struct_value(f->data_values[i].initial)) {
            const type_desc *type = layout(p, f->data_values[i].initial);
            if (!type || f->data_values[i].size != type->size) {
                return fail(p, "Producer struct bank allocation differs from its layout");
            }
        } else if (f->data_values[i].size) {
            return fail(p, "Producer scalar or array bank has foreign backing storage");
        }
    }
    if (!hosts_valid(p, f)) {
        return false;
    }
    int count = acount(f->instructions);
    if (count > INSTRUCTIONS - p->instructions ||
        acount(f->slots) > INSTRUCTIONS || acount(f->locals) > INSTRUCTIONS) {
        return fail(p, "Producer instruction limit exceeded");
    }
    p->instructions += count;
    origin *slots = xcalloc((size_t)acount(f->slots), sizeof *slots);
    origin *locals = xcalloc((size_t)acount(f->locals), sizeof *locals);
    for (int i = 0; i < acount(f->locals); i++) {
        const behavior_function_local *local = &f->locals[i];
        /* Lowering reserves result locals before writable results move to
         * private host banks. Such unused roots retain their initial NULL.
         * A known null address has a shape-compatible empty value, but never
         * establishes the backing required by the separate CFG proof. */
        if (local->kind == BEHAVIOR_ADDRESS && !local->parameter &&
            !local->array_default && local->initial.type == BF_POINTER_TYPE &&
            local->initial.container == PIN_CONTAINER_SINGLE &&
            !local->initial.record && !local->initial.u64) {
            locals[i] = (origin){ .kind = ZERO };
        }
    }
    int *definitions = xmalloc((size_t)acount(f->slots) * sizeof *definitions);
    for (int i = 0; i < acount(f->slots); i++) {
        definitions[i] = -1;
    }
    bool valid = true;
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            if (definitions[i->output] >= 0) {
                valid = false;
                break;
            }
            definitions[i->output] = pc;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
            valid = false;
            break;
        }
    }
    /* Local values may carry immutable payloads across a loop. All their
     * assignments must converge to one shape; no write can invent an address.
     * Canonical verifier already proves operand initialization on every path. */
    bool changed = true;
    for (int pass = 0; valid && changed; pass++) {
        changed = false;
        if (pass == 64) {
            valid = false;
            break;
        }
        for (int pc = 0; valid && pc < count; pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if (++p->steps > PROOF_STEPS) {
                valid = false;
                break;
            }
            origin previous = i->op == BF_SAVE ? locals[i->target] : (origin){ 0 }, out;
            valid = instruction(p, f, i, slots, locals, &out, false);
            if (!valid) {
                instruction_error(p, pc, i);
            }
            if (i->op == BF_SAVE) {
                changed |= !origin_equal(previous, locals[i->target]);
            }
            if (valid && i->op < BF_STORE) {
                origin combined = merge(slots[i->output], out);
                changed |= !origin_equal(slots[i->output], combined);
                slots[i->output] = combined;
                valid = combined.kind != BAD;
            }
        }
    }
    for (int pc = 0; valid && pc < count; pc++) {
        origin out;
        valid = instruction(p, f, &f->instructions[pc], slots, locals, &out, true);
        if (!valid) {
            instruction_error(p, pc, &f->instructions[pc]);
        }
    }
    for (int i = 0; valid && i < acount(f->value_roots); i++) {
        const behavior_value_root *root = &f->value_roots[i];
        origin value = root->bank == BEHAVIOR_BANK_SLOT ? slots[root->index]
                                                        : locals[root->index];
        if (!shape_valid(p, root->shape) || !matches(value, root->shape)) {
            valid = fail(p, "Producer saved aggregate metadata differs from its proven value");
            if (p->size) {
                snprintf(
                    p->error,
                    p->size,
                    "Producer saved aggregate differs (bank %d index %d origin "
                    "%d; expected %d/%d %.48s, proven %d/%d %.48s)",
                    root->bank,
                    root->index,
                    value.kind,
                    root->shape.type,
                    root->shape.container,
                    root->shape.type_name ? root->shape.type_name : "",
                    value.shape.type,
                    value.shape.container,
                    value.shape.type_name ? value.shape.type_name : ""
                );
            }
        }
    }
    if (valid) {
        valid = liveness_valid(p, f, slots);
    }
    free(definitions);
    free(locals);
    free(slots);
    if (!valid && (!p->size || !p->error[0])) {
        fail(p, "Producer address or instruction proof failed");
    }
    return valid;
}

static bool add_function(proof *p, const behavior_function *function, const execution_module *owner)
{
    for (int i = 0; i < p->function_count; i++) {
        if (p->functions[i] == function) {
            return p->owners[i] == owner;
        }
    }
    if (!function || p->function_count == FUNCTIONS) {
        return fail(p, "Producer function closure limit exceeded");
    }
    p->functions[p->function_count] = function;
    p->owners[p->function_count++] = owner;
    return true;
}
static bool event_valid(const execution_module_event_declaration *actual, const execution_producer_event *expected)
{
    if (actual->id != expected->id || actual->abi != expected->abi ||
        !text_equal(actual->name, expected->name) ||
        actual->field_count != expected->field_count) {
        return false;
    }
    for (int i = 0; i < actual->field_count; i++) {
        const execution_module_event_field *field = &actual->fields[i];
        if (!text_equal(field->name, expected->fields[i].name) ||
            !same_shape(field->initial, expected->fields[i].shape) ||
            field->nominal || field->has_default || field->default_owner) {
            return false;
        }
    }
    return true;
}
static bool module_valid(proof *p, const execution_module *module, bool root)
{
    const execution_module_plan *m = &module->plan;
    const execution_producer_contract *c = p->contract;
    if (!m->valid || m->query_count || m->binding_count || m->action_count ||
        m->query >= 0 || m->query_filter || m->query_variable_count ||
        module->retained_tasks || m->state_count != (root ? c->state_count : 0)) {
        return fail(p, "Producer contains unapproved persistent state or executable roots");
    }
    for (int i = 0; i < m->service_count; i++) {
        if (!text_equal(m->services[i], c->service)) {
            return fail(p, "Producer requires an unapproved service");
        }
    }
    for (int i = 0; i < m->type_count; i++) {
        const execution_type_requirement *type = &m->types[i];
        for (int j = 0; j < c->native_type_count; j++) {
            const type_desc *native = c->native_types[j];
            if (text_equal(type->name, native->name) &&
                (type->record || type->storage != PIN_STRUCT ||
                 type->size != native->size ||
                 type->layout != type_layout_hash(native))) {
                return fail(p, "Producer native fact layout differs");
            }
        }
    }
    for (int i = 0; i < m->state_count; i++) {
        const execution_module_state *state = &m->states[i];
        const execution_producer_state *expected = NULL;
        for (int j = 0; j < c->state_count; j++) {
            if (state->id == c->states[j].id) {
                expected = &c->states[j];
            }
        }
        if (!expected || state->shared || state->contains_tasks ||
            !same_shape(state->initial, expected->initial) ||
            behavior_scalar_bits(state->initial) !=
                behavior_scalar_bits(expected->initial)) {
            return fail(p, "Producer protected state identity, type or initial value differs");
        }
        for (int j = 0; j < i; j++) {
            if (state->id == m->states[j].id) {
                return fail(p, "Producer repeats a protected state identity");
            }
        }
    }
    if (m->event_count != (root ? c->event_count : 0)) {
        return fail(p, "Producer event declaration set differs");
    }
    for (int i = 0; i < m->event_count; i++) {
        int found = 0;
        for (int j = 0; j < c->event_count; j++) {
            found += event_valid(&m->events[i], &c->events[j]);
        }
        if (found != 1) {
            return fail(p, "Producer event identity, ABI or payload differs");
        }
        for (int j = 0; j < i; j++) {
            if (m->events[i].id == m->events[j].id) {
                return fail(p, "Producer repeats an event declaration");
            }
        }
    }
    int handlers[32] = { 0 };
    for (int i = 0; i < m->function_count; i++) {
        const execution_module_function *f = &m->functions[i];
        if (f->subscription || f->observer.component || f->action || f->parent ||
            f->outcome || f->capture_count || f->local_capture_count ||
            (f->kind != EXECUTION_FUNCTION_AUTHORED &&
             f->kind != EXECUTION_FUNCTION_HANDLER)) {
            return fail(p, "Producer contains an unapproved handler or continuation root");
        }
        if (f->kind == EXECUTION_FUNCTION_HANDLER) {
            int event = -1;
            for (int j = 0; root && j < c->event_count; j++) {
                if (f->event == c->events[j].id && f->event_abi == c->events[j].abi &&
                    text_equal(f->event_name, c->events[j].name)) {
                    event = j;
                }
            }
            if (event < 0 || ++handlers[event] != 1 || f->continuation_wait ||
                f->body->argument_count != c->events[event].field_count ||
                f->body->result_count) {
                return fail(p, "Producer handler identity or multiplicity differs");
            }
            for (int j = 0; j < f->body->argument_count; j++) {
                if (!same_shape(
                        f->body->argument_defaults[j],
                        c->events[event].fields[j].shape
                    )) {
                    return fail(p, "Producer handler input bank differs from its event");
                }
            }
        } else if (f->event || f->event_abi || f->event_name) {
            return fail(p, "Producer callable disguises an event root");
        }
        if (!add_function(p, f->body, module)) {
            return false;
        }
    }
    for (int i = 0; root && i < c->event_count; i++) {
        if (handlers[i] != 1) {
            return fail(p, "Producer is missing a required handler");
        }
    }
    return true;
}
bool execution_prepared_producer_valid(
    const execution_module *module,
    const execution_producer_contract *contract,
    char *error,
    size_t size
)
{
    proof p = { .contract = contract, .error = error, .size = size };
    if (size) {
        error[0] = 0;
    }
    if (!module || !contract || contract->event_count < 0 ||
        contract->event_count > 32 || module->plan.id != contract->module) {
        return fail(&p, "Producer module identity differs");
    }
    p.modules[p.module_count++] = module;
    for (int at = 0; at < p.module_count; at++) {
        const execution_module *current = p.modules[at];
        if (!module_valid(&p, current, !at)) {
            return false;
        }
        for (int i = 0; i < current->plan.import_count; i++) {
            const execution_module *import = current->plan.imports[i];
            bool seen = false;
            for (int j = 0; j < p.module_count; j++) {
                seen |= p.modules[j] == import;
            }
            if (!seen) {
                if (!import || p.module_count == MODULES) {
                    return fail(&p, "Producer import closure limit exceeded");
                }
                p.modules[p.module_count++] = import;
            }
        }
    }
    for (int at = 0; at < p.function_count; at++) {
        const behavior_function *f = p.functions[at];
        for (int i = 0; i < acount(f->definitions); i++) {
            if (!add_function(&p, f->definitions[i], p.owners[at])) {
                return false;
            }
        }
    }
    for (int at = 0; at < p.function_count; at++) {
        const behavior_function *f = p.functions[at];
        for (int i = 0; i < acount(f->calls); i++) {
            const behavior_function_invocation *call = &f->calls[i];
            bool found = false;
            for (int j = 0; j < p.function_count; j++) {
                found |= call->function == p.functions[j];
            }
            if (!found || call->task) {
                return fail(&p, "Producer callee is outside the validated synchronous closure");
            }
            if (call->count != call->function->argument_count ||
                call->result_count != call->function->result_count) {
                return fail(&p, "Producer callee bank size differs");
            }
            for (int j = 0; j < call->count; j++) {
                if (!same_shape(
                        f->data_values[call->first + j].initial,
                        call->function->argument_defaults[j]
                    )) {
                    return fail(&p, "Producer callee input shape differs");
                }
            }
            for (int j = 0; j < call->result_count; j++) {
                if (!same_shape(
                        f->data_values[call->result_first + j].initial,
                        call->function->result_defaults[j]
                    )) {
                    return fail(&p, "Producer callee output shape differs");
                }
            }
        }
        if (!body_valid(&p, f, p.owners[at])) {
            return false;
        }
    }
    for (int r = 0; r < contract->required_record_count; r++) {
        bool found = false;
        for (int m = 0; m < p.module_count; m++) {
            for (int i = 0; i < p.modules[m]->plan.type_count; i++) {
                const execution_type_requirement *type = &p.modules[m]->plan.types[i];
                const pin_record *required = contract->required_records[r];
                if (type->id != required->id &&
                    !text_equal(type->name, required->type.name)) {
                    continue;
                }
                if (type->id != required->id || !type->record ||
                    !pin_record_same(type->record, required) ||
                    type->layout != required->layout ||
                    type->size != required->type.size || type->storage != PIN_STRUCT ||
                    !text_equal(type->name, required->type.name)) {
                    return fail(&p, "Producer required authored record layout differs");
                }
                found = true;
            }
        }
        if (!found) {
            return fail(&p, "Producer required authored record is missing");
        }
    }
    return true;
}
