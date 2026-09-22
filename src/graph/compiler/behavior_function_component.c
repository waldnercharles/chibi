#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/eval.h"
#include "support/array.h"

#include <string.h>

bool behavior_function_bind(
    const behavior_function *f,
    behavior_function_context *ctx,
    const service_binding *services,
    uint64_t owner
)
{
    ctx->owner = owner;
    ctx->services = services;
    if (!f->component_access && !f->component_bindings) {
        return true;
    }
    for (int i = 0; i < acount(services); i++) {
        if (f->component_access && !strcmp(services[i].type_name, "ecs.world")) {
            ctx->world = services[i].value;
        }
        if (f->component_bindings &&
            !strcmp(services[i].type_name, EVAL_COMPONENT_READER_SERVICE)) {
            const eval_component_reader *reader = services[i].value;
            if (reader && reader->read) {
                if (f->component_bindings && !reader->execution) {
                    return false;
                }
                ctx->binding_world = reader->execution;
            }
        }
    }
    return true;
}

void behavior_function_bind_columns(
    const behavior_function *f,
    behavior_function_context *ctx,
    const ecs_execution_column ***columns
)
{
    assert(behavior_function_batchable(f));
    asetlen(*columns, acount(f->components));
    for (int i = 0; i < acount(f->components); i++) {
        behavior_function_component component = f->components[i];
        (*columns)[i] = ecs_execution_find_column(
            component.binding ? ctx->binding_world : ctx->world,
            component.id
        );
    }
    ctx->columns = *columns;
}
int behavior_lower_owner(builder *b)
{
    int value = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE }
    );
    int name = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_TYPE_NAME,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_OWNER }
    );
    return behavior_lower_named_scalar(b, value, name, false);
}
static void require(builder *b, int value, const node *n, int pin, bool flow, const char *message)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_ASSERT,
                                         .left = value,
                                         .node_id = n->id,
                                         .pin = pin,
                                         .target = flow ? BEHAVIOR_FUNCTION_HOST_FLOW
                                                        : BEHAVIOR_FUNCTION_HOST_DATA,
                                         .message = message }
    );
}
static int world(builder *b, node *n, int pin, bool flow, bool binding, const char *message)
{
    b->function->component_access |= !binding;
    int slot = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_WORLD, .type = BF_POINTER_TYPE, .target = binding }
    );
    require(b, slot, n, pin, flow, message);
    return slot;
}
static int entity(builder *b, int world, int owner)
{
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_ENTITY, .type = PIN_U32, .left = world, .right = owner }
    );
}
static int component(
    builder *b,
    node *n,
    int pin,
    int world,
    int entity,
    const ecs_component_desc *desc,
    bool has,
    bool flow,
    bool binding,
    bool metadata
)
{
    int column = 0;
    for (; column < acount(b->function->components); column++) {
        const behavior_function_component *c = &b->function->components[column];
        if (c->id == desc->id && c->binding == binding) {
            break;
        }
    }
    if (column == acount(b->function->components)) {
        apush(b->function->components, ((behavior_function_component){ desc->id, binding }));
    }
    return behavior_lower_value(
        b,
        (behavior_function_instruction){
            .op = metadata ? BF_COLUMN : BF_COMPONENT,
            .component = column,
            .type = has ? PIN_BOOL : BF_POINTER_TYPE,
            .left = world,
            .right = entity,
            .value = { .u64 = desc->id },
            .layout = type_layout_hash(desc->type),
            .offset = desc->element_size,
            .target = flow ? BEHAVIOR_FUNCTION_HOST_FLOW : BEHAVIOR_FUNCTION_HOST_DATA,
            .node_id = n->id,
            .pin = pin,
            .message = "compiled component layout changed; republish the "
                       "behavior" }
    );
}
int behavior_lower_component_target(builder *b, node *n, int world, int owner, const ecs_component_desc *desc)
{
    b->function->component_access = true;
    return component(b, n, -1, world, entity(b, world, owner), desc, false, true, false, false);
}
int behavior_lower_component_column(builder *b, node *n, int pin, int world, const ecs_component_desc *desc)
{
    b->function->component_access = true;
    return component(b, n, pin, world, 0, desc, false, false, false, true);
}
static int load(builder *b, const type_desc *type, int row)
{
    aggregate a = behavior_lower_shape(type->name);
    if (!a.type) {
        return -1;
    }
    a.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < acount(a.fields); i++) {
        aggregate_field *f = &a.fields[i];
        f->slot = behavior_lower_value(
            b,
            (
                behavior_function_instruction
            ){ .op = BF_LOAD, .type = f->type, .left = row, .offset = f->offset }
        );
    }
    return behavior_lower_aggregate_value(b, a);
}
int behavior_lower_component_read(builder *b, node *n)
{
    node_component_kind kind = n->desc->component_kind;
    bool self = kind == NODE_COMPONENT_GET_SELF || kind == NODE_COMPONENT_HAS_SELF;
    bool has = kind == NODE_COMPONENT_HAS_SELF || kind == NODE_COMPONENT_HAS_ENTITY;
    if (!self && kind != NODE_COMPONENT_GET_ENTITY && kind != NODE_COMPONENT_HAS_ENTITY) {
        return -1;
    }
    int owner = self ? behavior_lower_owner(b) : behavior_lower_input(b, n, 0);
    if (owner < 0) {
        return -1;
    }
    int w = world(
        b,
        n,
        0,
        false,
        false,
        !self ? "component access requires ecs.world"
        : has ? "component Has requires an entity and ecs.world"
              : "component Get requires an entity and ecs.world"
    );
    int slot = entity(b, w, owner);
    if (!self) {
        require(b, slot, n, 0, false, "component access requires a live entity");
    }
    int row = component(b, n, 0, w, slot, n->desc->component, has, false, false, false);
    if (has) {
        return behavior_lower_named_constant(b, row, "bool");
    }
    require(b, row, n, 0, false, "entity lacks component");
    return load(b, n->desc->component->type, row);
}
static void write(builder *b, int world, int row, int source, size_t offset)
{
    if (behavior_scalar_string(b->function->slots[source])) {
        source = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_INTERN_STRING,
                                             .type = b->function->slots[source],
                                             .left = source,
                                             .right = world }
        );
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = b->function->slots[source],
                                         .left = source,
                                         .right = row,
                                         .offset = offset }
    );
}
bool behavior_lower_component_write(builder *b, node *n)
{
    node_component_kind kind = n->desc->component_kind;
    bool self = kind == NODE_COMPONENT_SET_SELF,
         field = kind == NODE_COMPONENT_SET_FIELD;
    if (!self && !field && kind != NODE_COMPONENT_SET_ENTITY) {
        return false;
    }
    const ecs_component_desc *desc = n->desc->component;
    apush(b->function->component_writes, desc->id);
    aggregate shape = behavior_lower_shape(desc->type->name);
    if (!shape.type) {
        behavior_lower_unsupported(b, n, 1, false, "component has managed or unsupported fields");
        return false;
    }
    afree(shape.fields);
    int w = world(
        b,
        n,
        -1,
        true,
        false,
        self    ? "component Set requires an entity and ecs.world"
        : field ? "component field Set requires ecs.world"
                : "component Set requires ecs.world"
    );
    int owner = self ? behavior_lower_owner(b)
                     : behavior_lower_input(b, n, field ? 2 : 1);
    if (owner < 0) {
        return false;
    }
    int slot = entity(b, w, owner);
    if (!self) {
        require(b, slot, n, -1, true, field ? "component field Set requires a live Target" : "component Set requires a live entity");
    }
    int source = behavior_lower_input(b, n, kind == NODE_COMPONENT_SET_ENTITY ? 2 : 1);
    if (source < 0) {
        return false;
    }
    aggregate *value = behavior_lower_aggregate(b, source);
    if (value) {
        require(b, value->present, n, -1, true, field ? "component field Set requires a value" : "component Set requires a value");
    }
    int row = component(b, n, -1, w, slot, desc, false, true, false, false);
    require(b, row, n, -1, true, field ? "Target lacks component" : "entity lacks component");
    size_t offset = field ? desc->type->fields[n->desc->component_field].offset : 0;
    if (value) {
        for (int i = 0; i < acount(value->fields); i++) {
            write(b, w, row, value->fields[i].slot, offset + value->fields[i].offset);
        }
    } else {
        write(b, w, row, source, offset);
    }
    /* Keep the output's execution-time snapshot even if later nodes mutate
     * the same row. Unread snapshots are removed by local compaction. */
    int snapshot = load(b, desc->type, row);
    int stored = behavior_lower_local(b, b->frame, n, 1);
    return behavior_lower_store(b, BF_SAVE, snapshot, stored);
}
int behavior_lower_component_binding(builder *b, node *n, int pin)
{
    const pin_spec *spec = &n->input_pins[pin];
    const pin_binding *binding = spec->binding;
    const ecs_component_desc *desc = NULL;
    for (int i = 0; i < registry_node_count(); i++) {
        const ecs_component_desc *c = registry_node(i)->component;
        if (c && c->key && binding->component_key &&
            !strcmp(c->key, binding->component_key)) {
            desc = c;
            break;
        }
    }
    if (!desc) {
        return behavior_lower_unsupported(b, n, pin, false, "component binding identity");
    }
    const type_field *field = binding->kind == PIN_BINDING_COMPONENT_FIELD
                                ? pin_type_field_path(desc->type, binding->field_name)
                                : NULL;
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD && !field) {
        return behavior_lower_unsupported(b, n, pin, false, "component binding field");
    }
    pin_spec actual = { .type = field ? field->type : PIN_STRUCT,
                        .type_name = field ? field->type_name : desc->type->name,
                        .container = PIN_CONTAINER_SINGLE,
                        .kind = PIN_DATA };
    const node_desc *conversion = pin_compatible(&actual, spec)
                                    ? NULL
                                    : conversion_for_pins(&actual, spec);
    if (!pin_compatible(&actual, spec) && !conversion) {
        return behavior_lower_unsupported(b, n, pin, false, "component binding type");
    }
    b->function->component_bindings = true;
    bool flow = n->input_count && n->input_pins[0].kind == PIN_EXEC;
    int error_pin = flow ? -1 : pin;
    int w = world(b, n, error_pin, flow, true, "component binding requires a component reader and behavior instance");
    int owner = behavior_lower_owner(b);
    require(b, owner, n, error_pin, flow, "component binding requires a live Self entity");
    int row = component(b, n, error_pin, w, entity(b, w, owner), desc, false, flow, true, false);
    require(b, row, n, error_pin, flow, "Self does not have the bound component");
    int value = load(b, desc->type, row);
    const char *path = field ? binding->field_name : NULL;
    while (path && value >= 0) {
        const char *end = strchr(path, '.');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        const aggregate *shape = behavior_lower_aggregate(b, value);
        const type_field *member = NULL;
        for (int i = 0; shape && i < shape->type->field_count; i++) {
            const type_field *candidate = &shape->type->fields[i];
            if (strlen(candidate->name) == length &&
                !strncmp(candidate->name, path, length)) {
                member = candidate;
                break;
            }
        }
        assert(member);
        value = behavior_lower_field(b, value, member, n, pin);
        path = end ? end + 1 : NULL;
    }
    if (conversion) {
        value = behavior_lower_apply_conversion(b, value, conversion, n, pin);
    }
    return behavior_lower_convert(b, value, spec->type);
}
