#include "toolchain/compiler_target.h"

#include "ecs/ecs.h"
#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/prepared.h"
#include "runtime/native_capabilities.h"
#include "support/array.h"
#include "types/array.h"
#include "types/map.h"
#include "types/text.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <yyjson.h>

/* Whether the input lists a type: every one that is not internal or a union.
 * Stage zero requires each of them but the namespaces. */
static bool exported(const script_catalog_type *t)
{
    return !t->internal && !(t->type.flags & SCRIPT_TYPE_UNION);
}
/* A port's storage as Compile takes it: 0 for a generic port, whose call
 * infers its type, -2 for a wildcard, which takes each argument's own type,
 * and -1 for a type the compiler cannot pass. */
static int port_storage(script_type type)
{
    unsigned inferred = SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC;
    if (type.flags & SCRIPT_TYPE_UNTYPED) {
        if (type.flags & ~inferred) {
            return -1;
        }
        return type.flags & SCRIPT_TYPE_GENERIC ? 0 : -2;
    }
    return type.flags || type.nominal || type.storage == PIN_FLOW ? -1
                                                                  : (int)type.storage;
}
/* A fixed native type, including every alias it follows, must have an
 * exported native declaration of the same storage and target layout. */
static bool native_port_type(const script_catalog *catalog, script_type type)
{
    const script_catalog_type *first = script_catalog_type_by_id(catalog, type.nominal);
    const script_catalog_type *at = first;
    for (int i = 0; i < catalog->type_count; i++) {
        if (!at || !exported(at) || at->authored_record || at->type.flags ||
            at->type.container != PIN_CONTAINER_SINGLE || at->type.collection ||
            at->type.storage != type.storage || at->type.nominal != at->id ||
            !at->size || at->size != first->size || at->layout != first->layout ||
            (type.storage == PIN_STRUCT && !at->layout)) {
            return false;
        }
        if (!at->underlying) {
            return true;
        }
        at = script_catalog_type_by_id(catalog, at->underlying);
    }
    return false;
}
/* CatalogPort adds fixed native records and handles to the bootstrap ports.
 * Nested collections, nominal arrays and other nominal types stay unsupported. */
static int catalog_port_storage(const script_catalog *catalog, script_type type)
{
    if (type.collection || (type.container != PIN_CONTAINER_SINGLE &&
                            type.container != PIN_CONTAINER_ARRAY)) {
        return -1;
    }
    if (!type.nominal) {
        return port_storage(type);
    }
    if (type.flags || type.container != PIN_CONTAINER_SINGLE ||
        (type.storage != PIN_STRUCT && type.storage != PIN_HANDLE) ||
        !native_port_type(catalog, type)) {
        return -1;
    }
    return (int)type.storage;
}
/* Whether a capability is a native the compiler calls: a data function with
 * no flow, outcomes or suspension. */
static bool callable(const script_catalog_capability *c)
{
    return c->kind == SCRIPT_CAPABILITY_FUNCTION &&
           c->flow.kind == SCRIPT_FLOW_ACCESS_NONE && !c->outcome_count &&
           !(c->effects & SCRIPT_EFFECT_SUSPEND);
}
typedef struct
{
    const script_catalog *catalog;
    const native_capabilities *bindings;
    const script_catalog_capability **natives;
    bool rich;
} target_input;
/* Callable functions keep the bootstrap's indices. Rich inputs then append
 * unsupported functions and actions, each in catalog order, so lookup retains
 * declarations an ordinary source call cannot execute. */
static target_input input_new(const script_catalog *catalog, const native_capabilities *bindings, bool rich)
{
    target_input input = { .catalog = catalog, .bindings = bindings, .rich = rich };
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (callable(c)) {
            apush(input.natives, c);
        }
    }
    if (!rich) {
        return input;
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind == SCRIPT_CAPABILITY_FUNCTION && !callable(c)) {
            apush(input.natives, c);
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind == SCRIPT_CAPABILITY_ACTION) {
            apush(input.natives, c);
        }
    }
    return input;
}
/* The index in every table emitted from this selection; -1 for none. */
static int native_index(const target_input *input, uint64_t id)
{
    for (int i = 0; i < acount(input->natives); i++) {
        if (input->natives[i]->id == id) {
            return i;
        }
    }
    return -1;
}
/* Existing functions retain their admission. An action additionally needs a
 * synchronous execution binding whose completion needs no outcome dispatch. */
static bool direct_native(const target_input *input, const script_catalog_capability *c)
{
    if (callable(c)) {
        return true;
    }
    if (!input->bindings || c->kind != SCRIPT_CAPABILITY_ACTION ||
        c->entry_count != 1 || c->outcome_count != 1 ||
        c->completion_outcome != 0 || c->state_access != SCRIPT_STATE_ACCESS_NONE ||
        c->state_kind != NATIVE_STATE_NONE ||
        c->component_access != SCRIPT_COMPONENT_ACCESS_NONE ||
        c->struct_access != SCRIPT_STRUCT_ACCESS_NONE ||
        c->flow.kind != SCRIPT_FLOW_ACCESS_NONE ||
        (c->effects & SCRIPT_EFFECT_SUSPEND) || c->event_sender ||
        c->variadic || c->variadic_optional) {
        return false;
    }
    uint64_t abi = script_catalog_capability_abi(input->catalog, c);
    if (!abi) {
        return false;
    }
    const native_capability_binding
        *binding = native_capabilities_find(input->bindings, c->id, abi);
    if (!binding || !binding->call || binding->command || binding->query ||
        binding->query_service || binding->intrinsic || binding->instance_state) {
        return false;
    }
    const node_host_call *call = binding->call;
    return (!!call->invoke + !!call->context) == 1 && !call->value &&
           !call->action && !call->variadic;
}
/* Intrinsic lowering follows the exact linked binding, independently of the
 * capability's canonical name or source alias. */
static int native_intrinsic(const target_input *input, const script_catalog_capability *c)
{
    uint64_t abi = script_catalog_capability_abi(input->catalog, c);
    const native_capability_binding
        *binding = input->bindings && abi
                     ? native_capabilities_find(input->bindings, c->id, abi)
                     : NULL;
    return binding ? (int)binding->intrinsic : 0;
}
/* Scalar defaults are values, never pointers. Aggregate and inferred defaults
 * remain explicit unsupported omissions without disabling a supplied argument. */
static bool default_payload(
    const script_catalog *catalog,
    const script_catalog_parameter *parameter,
    uint64_t *bits,
    pin_string *text
)
{
    script_type type = parameter->type;
    pin_value value = parameter->default_value;
    if (type.flags || type.container != PIN_CONTAINER_SINGLE ||
        value.container != PIN_CONTAINER_SINGLE || value.type != type.storage ||
        value.record || catalog_port_storage(catalog, type) <= 0) {
        return false;
    }
    if (type.nominal) {
        /* Stage zero keeps the default's header in the host bank. This input
         * can reproduce only the declared nominal header. */
        const script_catalog_type *declared = script_catalog_type_by_id(catalog, type.nominal);
        if (!value.type_name || strcmp(value.type_name, declared->name)) {
            return false;
        }
    }
    script_type owner = { .storage = PIN_HANDLE, .nominal = catalog->entity_type };
    if (parameter->default_owner &&
        (type.storage != PIN_HANDLE || !catalog->entity_type ||
         catalog_port_storage(catalog, owner) != PIN_HANDLE ||
         !script_catalog_type_assignable(catalog, owner, type))) {
        return false;
    }
    switch (value.type) {
        case PIN_BOOL: *bits = value.b; return true;
        case PIN_I8: *bits = (uint64_t)value.i8; return true;
        case PIN_U8: *bits = value.u8; return true;
        case PIN_I16: *bits = (uint64_t)value.i16; return true;
        case PIN_U16: *bits = value.u16; return true;
        case PIN_I32: *bits = (uint64_t)value.i; return true;
        case PIN_U32: *bits = value.u; return true;
        case PIN_I64: *bits = (uint64_t)value.i64; return true;
        case PIN_U64: *bits = value.u64; return true;
        case PIN_HANDLE:
            *bits = value.u64;
            return parameter->default_owner || value.u64 == 0;
        case PIN_F32: {
            uint32_t encoded;
            memcpy(&encoded, &value.f, sizeof encoded);
            *bits = encoded;
            return true;
        }
        case PIN_F64: memcpy(bits, &value.d, sizeof *bits); return true;
        case PIN_NAME:
        case PIN_STR: *text = pin_text(value); return true;
        default: return false;
    }
}
static void add_defaults(
    yyjson_mut_doc *doc,
    yyjson_mut_val *port,
    const script_catalog *catalog,
    const script_catalog_parameter *declared,
    bool parameter
)
{
    uint64_t bits = 0;
    pin_string text = { .bytes = "", .length = 0 };
    bool has_default = parameter && declared->has_default;
    bool supported = has_default && default_payload(catalog, declared, &bits, &text);
    yyjson_mut_obj_add_bool(doc, port, "has_default", has_default);
    yyjson_mut_obj_add_bool(doc, port, "default_owner", parameter && declared->default_owner);
    yyjson_mut_obj_add_bool(doc, port, "default_supported", supported);
    yyjson_mut_obj_add_uint(doc, port, "default_bits", bits);
    yyjson_mut_obj_add_strn(doc, port, "default_text", text.bytes, text.length);
}
/* The callable natives of catalog, with their ports and identities. */
static void add_natives(yyjson_mut_doc *doc, yyjson_mut_val *target, const target_input *input)
{
    const script_catalog *catalog = input->catalog;
    yyjson_mut_val *natives = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *ports = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *identities = yyjson_mut_arr_add_arr(doc, target);
    int first = 0;
    for (int i = 0; i < acount(input->natives); i++) {
        const script_catalog_capability *c = input->natives[i];
        yyjson_mut_val *native = yyjson_mut_arr_add_obj(doc, natives);
        yyjson_mut_obj_add_str(doc, native, "name", c->name);
        yyjson_mut_obj_add_int(doc, native, "first", first);
        yyjson_mut_obj_add_int(doc, native, "parameters", c->parameter_count);
        yyjson_mut_obj_add_int(doc, native, "results", c->result_count);
        yyjson_mut_obj_add_bool(doc, native, "variadic", c->variadic || c->variadic_optional);
        yyjson_mut_obj_add_uint(doc, native, "effects", c->effects);
        if (input->rich) {
            yyjson_mut_obj_add_bool(doc, native, "direct", direct_native(input, c));
            yyjson_mut_obj_add_str(
                doc,
                native,
                "qualified_name",
                c->qualified_name ? c->qualified_name : c->name
            );
            yyjson_mut_obj_add_int(doc, native, "kind", (int)c->kind);
            yyjson_mut_obj_add_int(doc, native, "intrinsic", native_intrinsic(input, c));
        }
        for (int j = 0; j < c->parameter_count + c->result_count; j++, first++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            yyjson_mut_val *port = yyjson_mut_arr_add_obj(doc, ports);
            yyjson_mut_obj_add_str(doc, port, "name", declared->name ? declared->name : "");
            int storage = input->rich ? catalog_port_storage(catalog, declared->type)
                                      : port_storage(declared->type);
            yyjson_mut_obj_add_int(doc, port, "storage", storage);
            yyjson_mut_obj_add_int(doc, port, "container", (int)declared->type.container);
            yyjson_mut_obj_add_bool(doc, port, "c_string", declared->c_string);
            if (input->rich) {
                yyjson_mut_obj_add_uint(doc, port, "nominal", declared->type.nominal);
                add_defaults(doc, port, catalog, declared, j < c->parameter_count);
            }
        }
        yyjson_mut_arr_add_uint(doc, identities, c->id);
        yyjson_mut_arr_add_uint(doc, identities, script_catalog_capability_abi(catalog, c));
    }
}
/* The value layout, prepared ABI, instruction names and natives, appended to
 * `target`. */
static void add_target(yyjson_mut_doc *doc, yyjson_mut_val *target, const target_input *input)
{
    const struct
    {
        const char *name;
        int value;
    } layout[] = {
        { "pointer_storage", BF_POINTER_TYPE },
        { "pointer_size", (int)sizeof(void *) },
        { "value_size", (int)sizeof(pin_value) },
        { "payload_offset", (int)offsetof(pin_value, d) },
        { "arguments_offset", (int)offsetof(behavior_function_context, arguments) },
        { "results_offset", (int)offsetof(behavior_function_context, results) },
        { "delta_seconds_offset",
          (int)offsetof(behavior_function_context, delta_seconds) },
        { "type_offset", (int)offsetof(pin_value, type) },
        { "container_offset", (int)offsetof(pin_value, container) },
        { "name_offset", (int)offsetof(pin_value, type_name) },
        { "record_offset", (int)offsetof(pin_value, record) },
        { "schema_offset", (int)offsetof(pin_value, schema) },
        { "count_offset", (int)offsetof(pin_array, count) },
        { "items_offset", (int)offsetof(pin_array, items) },
        { "data_offset", (int)offsetof(pin_array, data) },
        { "stride_offset", (int)offsetof(pin_array, stride) },
        { "map_count_offset", (int)offsetof(pin_map, count) },
        { "data_failure", BEHAVIOR_FUNCTION_HOST_DATA },
        { "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT },
        { "assert_failure", BEHAVIOR_FUNCTION_DATA },
    };
    yyjson_mut_val *values = yyjson_mut_arr_add_obj(doc, target);
    for (size_t i = 0; i < sizeof layout / sizeof *layout; i++) {
        yyjson_mut_obj_add_int(doc, values, layout[i].name, layout[i].value);
    }
    yyjson_mut_arr_add_uint(doc, target, execution_prepared_abi());
    yyjson_mut_val *names = yyjson_mut_arr_add_arr(doc, target);
    for (int i = 0; i < execution_prepared_instruction_count(); i++) {
        yyjson_mut_arr_add_str(doc, names, execution_prepared_instruction_name(i));
    }
    add_natives(doc, target, input);
}
static char *written(yyjson_mut_doc *doc, size_t *length, bool rich)
{
    char *json = yyjson_mut_write(doc, rich ? YYJSON_WRITE_ALLOW_INVALID_UNICODE : 0, length);
    yyjson_mut_doc_free(doc);
    assert(json);
    return json;
}
char *compiler_target_json(const script_catalog *catalog, size_t *length)
{
    target_input input = input_new(catalog, NULL, false);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *target = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, target);
    add_target(doc, target, &input);
    afree(input.natives);
    return written(doc, length, false);
}

static void add_effects(yyjson_mut_doc *doc, yyjson_mut_val *effects, const target_input *input)
{
    for (int i = 0; i < acount(input->natives); i++) {
        yyjson_mut_arr_add_uint(doc, effects, input->natives[i]->effects);
    }
}
char *compiler_target_effects_json(const script_catalog *catalog, size_t *length)
{
    target_input input = input_new(catalog, NULL, false);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    add_effects(doc, root, &input);
    afree(input.natives);
    return written(doc, length, false);
}
/* Each event, with a range of payload fields, then the fields. */
static void add_events(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
    yyjson_mut_val *events = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, root);
    size_t first = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_EVENT) {
            continue;
        }
        yyjson_mut_val *event = yyjson_mut_arr_add_obj(doc, events);
        yyjson_mut_obj_add_str(doc, event, "name", c->name);
        yyjson_mut_obj_add_uint(doc, event, "id", c->id);
        yyjson_mut_obj_add_uint(doc, event, "abi", script_catalog_capability_abi(catalog, c));
        yyjson_mut_obj_add_uint(doc, event, "first", first);
        yyjson_mut_obj_add_int(doc, event, "count", c->parameter_count);
        first += (size_t)c->parameter_count;
        for (int j = 0; j < c->parameter_count; j++) {
            const script_catalog_parameter *parameter = &c->parameters[j];
            yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
            yyjson_mut_obj_add_str(doc, field, "name", parameter->name);
            yyjson_mut_obj_add_int(doc, field, "storage", (int)parameter->type.storage);
            yyjson_mut_obj_add_uint(doc, field, "nominal", parameter->type.nominal);
            yyjson_mut_obj_add_bool(doc, field, "has_default", parameter->has_default);
            yyjson_mut_obj_add_bool(doc, field, "default_owner", parameter->default_owner);
        }
    }
}
/* The index among the exported types of the type `id`; -1 for none. */
static int type_index(const script_catalog *catalog, uint64_t id)
{
    int index = 0;
    for (int i = 0; id && i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (!exported(t)) {
            continue;
        }
        if (t->id == id) {
            return index;
        }
        index++;
    }
    return -1;
}
/* Each exported type, with a range of fields, then the fields. */
static void add_types(yyjson_mut_doc *doc, yyjson_mut_val *root, const target_input *input)
{
    const script_catalog *catalog = input->catalog;
    yyjson_mut_val *types = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, root);
    size_t first = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (!exported(t)) {
            continue;
        }
        yyjson_mut_val *type = yyjson_mut_arr_add_obj(doc, types);
        yyjson_mut_obj_add_bool(doc, type, "required", !(t->type.flags & SCRIPT_TYPE_NAMESPACE));
        yyjson_mut_obj_add_int(doc, type, "underlying", type_index(catalog, t->underlying));
        yyjson_mut_obj_add_str(doc, type, "name", t->name);
        yyjson_mut_obj_add_uint(doc, type, "id", t->id);
        yyjson_mut_obj_add_uint(doc, type, "layout", t->layout);
        yyjson_mut_obj_add_int(doc, type, "storage", (int)t->type.storage);
        yyjson_mut_obj_add_uint(doc, type, "size", t->size);
        yyjson_mut_obj_add_uint(doc, type, "component", t->component ? t->component_id : 0);
        yyjson_mut_obj_add_bool(doc, type, "relationship", false);
        yyjson_mut_obj_add_uint(doc, type, "traits", 0);
        yyjson_mut_obj_add_uint(doc, type, "first", first);
        yyjson_mut_obj_add_int(doc, type, "count", t->field_count);
        first += (size_t)t->field_count;
        for (int j = 0; j < t->field_count; j++) {
            const script_catalog_field *f = &t->fields[j];
            yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
            yyjson_mut_obj_add_str(doc, field, "name", f->name);
            yyjson_mut_obj_add_int(doc, field, "storage", (int)f->type.storage);
            yyjson_mut_obj_add_uint(doc, field, "offset", f->offset);
            yyjson_mut_obj_add_bool(doc, field, "writable", f->writable);
            yyjson_mut_obj_add_bool(doc, field, "c_string", f->c_string);
            yyjson_mut_obj_add_int(
                doc,
                field,
                "record",
                type_index(catalog, f->type.nominal)
            );
            yyjson_mut_obj_add_bool(doc, field, "constant", f->constant);
            yyjson_mut_obj_add_int(doc, field, "read", f->read ? native_index(input, f->read) : -1);
            yyjson_mut_obj_add_str(doc, field, "read_key", f->read_key ? f->read_key : "");
        }
    }
    for (int i = 0; i < catalog->relationship_count; i++) {
        const script_catalog_relationship *relationship = &catalog->relationships[i];
        yyjson_mut_val *type = yyjson_mut_arr_add_obj(doc, types);
        yyjson_mut_obj_add_bool(doc, type, "required", false);
        yyjson_mut_obj_add_int(doc, type, "underlying", -1);
        yyjson_mut_obj_add_str(doc, type, "name", relationship->name);
        yyjson_mut_obj_add_uint(doc, type, "id", relationship->id);
        yyjson_mut_obj_add_uint(doc, type, "layout", 0);
        yyjson_mut_obj_add_int(doc, type, "storage", PIN_HANDLE);
        yyjson_mut_obj_add_uint(doc, type, "size", sizeof(uint64_t));
        yyjson_mut_obj_add_uint(doc, type, "component", relationship->id);
        yyjson_mut_obj_add_bool(doc, type, "relationship", true);
        yyjson_mut_obj_add_uint(
            doc,
            type,
            "traits",
            (relationship->exclusive ? ECS_REL_EXCLUSIVE : 0) |
                (relationship->traversable ? ECS_REL_TRAVERSABLE : 0)
        );
        yyjson_mut_obj_add_uint(doc, type, "first", first);
        yyjson_mut_obj_add_int(doc, type, "count", 0);
    }
}
/* Each callable native that combines numbers, and records of them field by
 * field, for a binary operator: its index among the natives and the operator. */
static void add_operators(yyjson_mut_doc *doc, yyjson_mut_val *root, const target_input *input)
{
    yyjson_mut_val *operators = yyjson_mut_arr_add_arr(doc, root);
    for (int i = 0; i < acount(input->natives); i++) {
        const script_catalog_capability *c = input->natives[i];
        if (callable(c) && c->numeric_aggregate && c->operator_name &&
            c->parameter_count == 2 && c->result_count == 1) {
            yyjson_mut_val *row = yyjson_mut_arr_add_obj(doc, operators);
            yyjson_mut_obj_add_int(doc, row, "native", i);
            yyjson_mut_obj_add_str(doc, row, "operator", c->operator_name);
        }
    }
}
/* Each callable native that makes a record of its arguments, with the
 * record's type and where its fields start, then for each argument the
 * position of the record field it gives. */
static void add_makes(yyjson_mut_doc *doc, yyjson_mut_val *root, const target_input *input)
{
    const script_catalog *catalog = input->catalog;
    yyjson_mut_val *makes = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, root);
    size_t first = 0;
    for (int i = 0; i < acount(input->natives); i++) {
        const script_catalog_capability *c = input->natives[i];
        if (!callable(c)) {
            continue;
        }
        const script_catalog_type *record = script_catalog_type_by_id(catalog, c->aggregate);
        if (c->struct_access == SCRIPT_STRUCT_ACCESS_MAKE && record) {
            yyjson_mut_val *make = yyjson_mut_arr_add_obj(doc, makes);
            yyjson_mut_obj_add_int(doc, make, "native", i);
            yyjson_mut_obj_add_int(doc, make, "record", type_index(catalog, c->aggregate));
            yyjson_mut_obj_add_uint(doc, make, "first", first);
            for (int j = 0; j < c->parameter_count; j++, first++) {
                int field = -1;
                for (int k = 0; field < 0 && k < record->field_count; k++) {
                    field = record->fields[k].id == c->parameters[j].member ? k : -1;
                }
                yyjson_mut_arr_add_int(doc, fields, field);
            }
        }
    }
}
/* Each global, with its storage and the index of its nominal type. */
static void add_globals(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
    yyjson_mut_val *globals = yyjson_mut_arr_add_arr(doc, root);
    for (int i = 0; i < catalog->global_count; i++) {
        const script_catalog_global *g = &catalog->globals[i];
        yyjson_mut_val *global = yyjson_mut_arr_add_obj(doc, globals);
        yyjson_mut_obj_add_str(doc, global, "name", g->name);
        yyjson_mut_obj_add_int(doc, global, "storage", (int)g->type.storage);
        yyjson_mut_obj_add_int(
            doc,
            global,
            "record",
            type_index(catalog, g->type.nominal)
        );
    }
}
char *compiler_behavior_catalog_input_json(
    const script_catalog *catalog,
    const native_capabilities *bindings,
    const char *const *identities,
    const char *const *sources,
    int count,
    size_t *length
)
{
    target_input input = input_new(catalog, bindings, true);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *named = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *texts = yyjson_mut_arr_add_arr(doc, root);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_str(doc, named, identities[i]);
        yyjson_mut_arr_add_str(doc, texts, sources[i]);
    }
    add_target(doc, root, &input);
    add_events(doc, root, catalog);
    add_types(doc, root, &input);
    yyjson_mut_arr_add_uint(doc, root, catalog->entity_type);
    add_operators(doc, root, &input);
    add_globals(doc, root, catalog);
    add_makes(doc, root, &input);
    add_effects(doc, yyjson_mut_arr_add_arr(doc, root), &input);
    afree(input.natives);
    return written(doc, length, true);
}
