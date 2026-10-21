#include "toolchain/compiler_target.h"

#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/prepared.h"
#include "types/array.h"
#include "types/map.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <yyjson.h>

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
/* Whether a capability is a native the compiler calls: a data function with
 * no flow, outcomes or suspension. */
static bool callable(const script_catalog_capability *c)
{
    return c->kind == SCRIPT_CAPABILITY_FUNCTION &&
           c->flow.kind == SCRIPT_FLOW_ACCESS_NONE && !c->outcome_count &&
           !(c->effects & SCRIPT_EFFECT_SUSPEND);
}
/* The index among the callable natives of the capability `id`; -1 for none. */
static int native_index(const script_catalog *catalog, uint64_t id)
{
    int index = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (!callable(c)) {
            continue;
        }
        if (c->id == id) {
            return index;
        }
        index++;
    }
    return -1;
}
/* The callable natives of catalog, with their ports and identities. */
static void add_natives(yyjson_mut_doc *doc, yyjson_mut_val *target, const script_catalog *catalog)
{
    yyjson_mut_val *natives = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *ports = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *identities = yyjson_mut_arr_add_arr(doc, target);
    int first = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (!callable(c)) {
            continue;
        }
        yyjson_mut_val *native = yyjson_mut_arr_add_obj(doc, natives);
        yyjson_mut_obj_add_str(doc, native, "name", c->name);
        yyjson_mut_obj_add_int(doc, native, "first", first);
        yyjson_mut_obj_add_int(doc, native, "parameters", c->parameter_count);
        yyjson_mut_obj_add_int(doc, native, "results", c->result_count);
        yyjson_mut_obj_add_bool(doc, native, "variadic", c->variadic || c->variadic_optional);
        for (int j = 0; j < c->parameter_count + c->result_count; j++, first++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            yyjson_mut_val *port = yyjson_mut_arr_add_obj(doc, ports);
            yyjson_mut_obj_add_str(doc, port, "name", declared->name ? declared->name : "");
            yyjson_mut_obj_add_int(doc, port, "storage", port_storage(declared->type));
            yyjson_mut_obj_add_int(doc, port, "container", (int)declared->type.container);
        }
        yyjson_mut_arr_add_uint(doc, identities, c->id);
        yyjson_mut_arr_add_uint(doc, identities, script_catalog_capability_abi(catalog, c));
    }
}
/* The value layout, prepared ABI, instruction names and natives, appended to
 * `target`. */
static void add_target(yyjson_mut_doc *doc, yyjson_mut_val *target, const script_catalog *catalog)
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
        { "results_offset", (int)offsetof(behavior_function_context, results) },
        { "type_offset", (int)offsetof(pin_value, type) },
        { "container_offset", (int)offsetof(pin_value, container) },
        { "name_offset", (int)offsetof(pin_value, type_name) },
        { "record_offset", (int)offsetof(pin_value, record) },
        { "count_offset", (int)offsetof(pin_array, count) },
        { "items_offset", (int)offsetof(pin_array, items) },
        { "data_offset", (int)offsetof(pin_array, data) },
        { "stride_offset", (int)offsetof(pin_array, stride) },
        { "map_count_offset", (int)offsetof(pin_map, count) },
        { "data_failure", BEHAVIOR_FUNCTION_HOST_DATA },
        { "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT },
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
    add_natives(doc, target, catalog);
}
static char *written(yyjson_mut_doc *doc, size_t *length)
{
    char *json = yyjson_mut_write(doc, 0, length);
    yyjson_mut_doc_free(doc);
    assert(json);
    return json;
}
char *compiler_target_json(const script_catalog *catalog, size_t *length)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *target = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, target);
    add_target(doc, target, catalog);
    return written(doc, length);
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
/* Whether the input lists a type: every one that is not internal or a union.
 * Stage zero requires each of them but the namespaces. */
static bool exported(const script_catalog_type *t)
{
    return !t->internal && !(t->type.flags & SCRIPT_TYPE_UNION);
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
static void add_types(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
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
            yyjson_mut_obj_add_int(
                doc,
                field,
                "record",
                type_index(catalog, f->type.nominal)
            );
            yyjson_mut_obj_add_bool(doc, field, "constant", f->constant);
            yyjson_mut_obj_add_int(doc, field, "read", f->read ? native_index(catalog, f->read) : -1);
            yyjson_mut_obj_add_str(doc, field, "read_key", f->read_key ? f->read_key : "");
        }
    }
}
/* Each callable native that combines numbers, and records of them field by
 * field, for a binary operator: its index among the natives and the operator. */
static void add_operators(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
    yyjson_mut_val *operators = yyjson_mut_arr_add_arr(doc, root);
    int native = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (!callable(c)) {
            continue;
        }
        if (c->numeric_aggregate && c->operator_name &&
            c->parameter_count == 2 && c->result_count == 1) {
            yyjson_mut_val *row = yyjson_mut_arr_add_obj(doc, operators);
            yyjson_mut_obj_add_int(doc, row, "native", native);
            yyjson_mut_obj_add_str(doc, row, "operator", c->operator_name);
        }
        native++;
    }
}
/* Each callable native that makes a record of its arguments, with the
 * record's type and where its fields start, then for each argument the
 * position of the record field it gives. */
static void add_makes(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
    yyjson_mut_val *makes = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, root);
    int native = 0;
    size_t first = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (!callable(c)) {
            continue;
        }
        const script_catalog_type *record = script_catalog_type_by_id(catalog, c->aggregate);
        if (c->struct_access == SCRIPT_STRUCT_ACCESS_MAKE && record) {
            yyjson_mut_val *make = yyjson_mut_arr_add_obj(doc, makes);
            yyjson_mut_obj_add_int(doc, make, "native", native);
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
        native++;
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
char *compiler_behavior_input_json(
    const script_catalog *catalog,
    const char *const *identities,
    const char *const *sources,
    int count,
    size_t *length
)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *named = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *texts = yyjson_mut_arr_add_arr(doc, root);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_str(doc, named, identities[i]);
        yyjson_mut_arr_add_str(doc, texts, sources[i]);
    }
    add_target(doc, root, catalog);
    add_events(doc, root, catalog);
    add_types(doc, root, catalog);
    yyjson_mut_arr_add_uint(doc, root, catalog->entity_type);
    add_operators(doc, root, catalog);
    add_globals(doc, root, catalog);
    add_makes(doc, root, catalog);
    return written(doc, length);
}
