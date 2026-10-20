#include "toolchain/compiler_target.h"

#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/prepared.h"
#include "types/array.h"

#include <assert.h>
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
/* The callable natives of catalog, with their ports and identities. */
static void add_natives(yyjson_mut_doc *doc, yyjson_mut_val *target, const script_catalog *catalog)
{
    yyjson_mut_val *natives = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *ports = yyjson_mut_arr_add_arr(doc, target);
    yyjson_mut_val *identities = yyjson_mut_arr_add_arr(doc, target);
    int first = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION ||
            c->flow.kind != SCRIPT_FLOW_ACCESS_NONE || c->outcome_count ||
            (c->effects & SCRIPT_EFFECT_SUSPEND)) {
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
/* Each type stage zero lists as a requirement, with a range of fields, then
 * the fields. */
static void add_types(yyjson_mut_doc *doc, yyjson_mut_val *root, const script_catalog *catalog)
{
    yyjson_mut_val *types = yyjson_mut_arr_add_arr(doc, root);
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, root);
    size_t first = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (t->internal ||
            (t->type.flags & (SCRIPT_TYPE_NAMESPACE | SCRIPT_TYPE_UNION))) {
            continue;
        }
        yyjson_mut_val *type = yyjson_mut_arr_add_obj(doc, types);
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
        }
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
    return written(doc, length);
}
