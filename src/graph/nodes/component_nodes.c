#include "graph/nodes/component_nodes.h"

#include "graph/model/local.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTING_PIN                                                            \
    {                                                                          \
        .name = "Component", .type = PIN_NAME, .flags = PIN_FLAG_SETTING,      \
        .default_value = {                                                     \
            .type = PIN_NAME                                                   \
        }                                                                      \
    }
#define ENTITY_PIN                                                             \
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF }
#define VALUE_PIN(pin_flags)                                                          \
    {                                                                                 \
        .name = "Value", .type = PIN_STRUCT, .flags = (pin_flags), .default_value = { \
            .type = PIN_STRUCT                                                        \
        }                                                                             \
    }

/* Set's Value input follows Exec, Component and Entity. */
enum
{
    SET_VALUE = 3,
};

static pin_spec get_inputs[] = { SETTING_PIN, ENTITY_PIN };
static pin_spec get_outputs[] = { VALUE_PIN(PIN_FLAG_REFERENCE) };
static pin_spec has_outputs[] = { { .name = "Value", .type = PIN_BOOL, .type_name = "bool" } };
static pin_spec set_inputs[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                                 SETTING_PIN,
                                 ENTITY_PIN,
                                 VALUE_PIN(0) };
static pin_spec set_outputs[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
                                  VALUE_PIN(0) };

node_desc NODE_GetComponent = {
    .name = "Get Component",
    .input_count = 2,
    .inputs = get_inputs,
    .output_count = 1,
    .outputs = get_outputs,
    .category = "Components",
    .keywords = "component read field entity self",
};

node_desc NODE_SetComponent = {
    .name = "Set Component",
    .input_count = 4,
    .inputs = set_inputs,
    .output_count = 2,
    .outputs = set_outputs,
    .display_class = NODE_DISPLAY_FUNCTION,
    .category = "Components",
    .keywords = "component write assign field entity self",
};

node_desc NODE_HasComponent = {
    .name = "Has Component",
    .input_count = 2,
    .inputs = get_inputs,
    .output_count = 1,
    .outputs = has_outputs,
    .category = "Components",
    .keywords = "component present exists check entity self",
};

static node_desc *const COMPONENT_NODES[] = { &NODE_GetComponent,
                                              &NODE_SetComponent,
                                              &NODE_HasComponent };

const registry_pack CORE_COMPONENT_PACK = {
    .name = "core.component",
    .nodes = COMPONENT_NODES,
    .node_count = sizeof COMPONENT_NODES / sizeof COMPONENT_NODES[0],
};

component_node_kind component_node_kind_of(const node_desc *desc)
{
    return desc == &NODE_GetComponent ? COMPONENT_NODE_GET
         : desc == &NODE_SetComponent ? COMPONENT_NODE_SET
         : desc == &NODE_HasComponent ? COMPONENT_NODE_HAS
                                      : COMPONENT_NODE_NONE;
}

int component_node_setting(component_node_kind kind)
{
    assert(kind != COMPONENT_NODE_NONE);
    return kind == COMPONENT_NODE_SET;
}

const char *component_node_key(const node *n)
{
    int setting = component_node_setting(component_node_kind_of(n->desc));
    assert(setting < n->input_count);
    const char *key = n->input_pins[setting].default_value.str;
    return key ? key : "";
}

/* Documents and tools may name a component by its stable key or its name. */
const ecs_component_desc *component_by_key(const char *key)
{
    for (int i = 0; key && key[0] && i < registry_node_count(); i++) {
        const node_desc *d = registry_node(i);
        if (d->component_kind == NODE_COMPONENT_GET_SELF &&
            ((d->component->key && !strcmp(d->component->key, key)) ||
             !strcmp(d->component->name, key))) {
            return d->component;
        }
    }
    return NULL;
}

const ecs_component_desc *component_node_component(const node *n)
{
    if (component_node_kind_of(n->desc)) {
        return component_by_key(component_node_key(n));
    }
    return n->desc->component_kind != NODE_COMPONENT_NONE ? n->desc->component : NULL;
}

static const node_desc *access_desc(const ecs_component_desc *component, node_component_kind kind, int field)
{
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *d = registry_node(i);
        if (d->component == component && d->component_kind == kind &&
            (kind != NODE_COMPONENT_SET_FIELD || d->component_field == field)) {
            return d;
        }
    }
    return NULL;
}

static int field_index(const type_desc *type, const char *name)
{
    for (int i = 0; name && i < type->field_count; i++) {
        if (!strcmp(type->fields[i].name, name)) {
            return i;
        }
    }
    return -1;
}

bool component_field_settable(const ecs_component_desc *component, const char *field)
{
    int index = component ? field_index(component->type, field) : -1;
    return index >= 0 && access_desc(component, NODE_COMPONENT_SET_FIELD, index);
}

bool component_node_split(const node *n)
{
    return component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
           (n->input_count <= SET_VALUE ||
            strcmp(n->input_pins[SET_VALUE].name, "Value"));
}

bool component_node_field_pin(const node *n, const char *field, pin_spec *out)
{
    const ecs_component_desc *component = component_node_component(n);
    if (component_node_kind_of(n->desc) != COMPONENT_NODE_SET ||
        !component_field_settable(component, field)) {
        return false;
    }
    const type_field *f = &component->type->fields[field_index(component->type, field)];
    *out = (pin_spec){ .name = f->name, .type = f->type, .type_name = f->type_name };
    return true;
}

static void type_value(graph *g, node *n, int output, int index, const ecs_component_desc *component)
{
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    const char *type_name = component ? component->type->name : NULL;
    if (pin->type_name == type_name ||
        (pin->type_name && type_name && !strcmp(pin->type_name, type_name))) {
        return;
    }
    int result = graph_set_pin_type(g, n, output, index, PIN_STRUCT, type_name);
    assert(!result);
    (void)result;
}

static void type_values(graph *g, node *n, const ecs_component_desc *component)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (kind == COMPONENT_NODE_HAS) {
        return;
    }
    type_value(g, n, 1, kind == COMPONENT_NODE_SET, component);
    if (kind == COMPONENT_NODE_SET && !component_node_split(n)) {
        type_value(g, n, 0, SET_VALUE, component);
    }
}

static int restore_value(graph *g, node *n, const ecs_component_desc *component)
{
    pin_spec value = set_inputs[SET_VALUE];
    value.type_name = component ? component->type->name : NULL;
    int at = graph_add_pin(g, n, 0, &value);
    return at < 0 || graph_move_pin(g, n, 0, at, SET_VALUE) < 0 ? -1 : 0;
}

/* Exposed fields go deepest first: a pin with exposed children stays. */
static int hide_exposed_outputs(graph *g, node *n)
{
    for (int i = n->output_count - 1; i >= 0; i--) {
        bool parent = false;
        for (int j = 0; j < n->output_count; j++) {
            parent |= n->output_pins[j].field_source == n->output_pins[i].id;
        }
        if (!n->output_pins[i].field_source || parent) {
            continue;
        }
        if (graph_remove_pin(g, n, 1, i)) {
            return -1;
        }
        i = n->output_count;
    }
    return 0;
}

int component_node_retarget(graph *g, node *n, const char *key)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    assert(kind != COMPONENT_NODE_NONE);
    const ecs_component_desc *component = component_by_key(key);
    if (key && key[0] && !component) {
        return -1;
    }
    const ecs_component_desc *previous = component_node_component(n);
    pin_spec *setting = &n->input_pins[component_node_setting(kind)];
    const char *stored = component ? component->key : "";
    setting->default_value.str = graph_own_str(g, stored, strlen(stored));
    if (component != previous && hide_exposed_outputs(g, n)) {
        return -1;
    }
    if (component != previous && component_node_split(n)) {
        while (n->input_count > SET_VALUE) {
            if (graph_remove_pin(g, n, 0, SET_VALUE)) {
                return -1;
            }
        }
        if (restore_value(g, n, component)) {
            return -1;
        }
    }
    type_values(g, n, component);
    return 0;
}

static int shown_field(const node *n, const char *field)
{
    for (int i = SET_VALUE; component_node_split(n) && i < n->input_count; i++) {
        if (!strcmp(n->input_pins[i].name, field)) {
            return i;
        }
    }
    return -1;
}

int component_node_show_field(graph *g, node *n, const char *field, bool shown)
{
    const ecs_component_desc *component = component_node_component(n);
    pin_spec pin;
    if (!component_node_field_pin(n, field, &pin)) {
        return -1;
    }
    int at = shown_field(n, field);
    if (!shown) {
        if (at < 0) {
            return 0;
        }
        if (graph_remove_pin(g, n, 0, at)) {
            return -1;
        }
        return n->input_count == SET_VALUE ? restore_value(g, n, component) : 0;
    }
    if (at >= 0) {
        return 0;
    }
    if (!component_node_split(n) && graph_remove_pin(g, n, 0, SET_VALUE)) {
        return -1;
    }
    /* Field inputs keep the component's field order. */
    int index = field_index(component->type, field);
    int position = SET_VALUE;
    while (position < n->input_count &&
           field_index(component->type, n->input_pins[position].name) < index) {
        position++;
    }
    at = graph_add_pin(g, n, 0, &pin);
    return at < 0 || graph_move_pin(g, n, 0, at, position) < 0 ? -1 : 0;
}

int component_node_split_value(graph *g, node *n, bool split)
{
    const ecs_component_desc *component = component_node_component(n);
    if (component_node_kind_of(n->desc) != COMPONENT_NODE_SET || !component) {
        return -1;
    }
    bool shown = false;
    for (int i = 0; i < component->type->field_count; i++) {
        const char *field = component->type->fields[i].name;
        if (!component_field_settable(component, field)) {
            continue;
        }
        if (component_node_show_field(g, n, field, split)) {
            return -1;
        }
        shown = true;
    }
    return shown ? 0 : -1;
}

int component_node_prepare(graph *g, node *n, const char *key, bool split)
{
    const ecs_component_desc *component = component_by_key(key);
    if (key && key[0] && !component) {
        return -1;
    }
    if (split && graph_remove_pin(g, n, 0, SET_VALUE)) {
        return -1;
    }
    type_values(g, n, component);
    return 0;
}

int component_node_template_count(const node *n, int output)
{
    int count = output ? n->desc->output_count : n->desc->input_count;
    return !output && component_node_split(n) ? count - 1 : count;
}

pin_spec component_node_template(const node *n, int output, int index)
{
    pin_spec pin = output ? n->desc->outputs[index] : n->desc->inputs[index];
    if (pin.type == PIN_STRUCT) {
        const ecs_component_desc *component = component_node_component(n);
        pin.type_name = component ? component->type->name : NULL;
        pin.default_value.type_name = pin.type_name;
    }
    return pin;
}

/* ---------- lowering and raising ---------- */

static void reindexed(graph *g)
{
    g->topology_revision++;
    graph_reindex(g);
}

static bool same_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

/* A pin of the `to` template carrying an existing pin's identity and authored
 * state. Its name and flags keep their authored differences from the `from`
 * template. */
static pin_spec kept_pin(const pin_spec *to, const pin_spec *from, const pin_spec *authored, uint64_t id)
{
    pin_spec pin = *to;
    pin.id = id;
    pin.default_value = graph_pin_initial_default(to);
    if (authored) {
        pin.name = same_name(authored->name, from->name) ? to->name : authored->name;
        pin.default_value = authored->default_value;
        pin.binding = authored->binding;
        pin.flags = authored->flags ^ from->flags ^ to->flags;
    }
    return pin;
}

static pin_spec *exact_pins(pin_spec *pins)
{
    int count = acount(pins);
    pin_spec *copy = count ? xmalloc((size_t)count * sizeof *copy) : NULL;
    if (count) {
        memcpy(copy, pins, (size_t)count * sizeof *copy);
    }
    afree(pins);
    return copy;
}

static void replace_pins(node *n, const node_desc *desc, pin_spec *inputs, pin_spec *outputs)
{
    free(n->input_pins);
    free(n->output_pins);
    n->desc = desc;
    n->input_count = acount(inputs);
    n->output_count = acount(outputs);
    n->input_pins = exact_pins(inputs);
    n->output_pins = exact_pins(outputs);
}

/* Exposed fields reference Value when their Value does. */
static void keep_exposed(
    pin_spec **outputs,
    const pin_spec *pins,
    int first,
    int count,
    const pin_spec *from,
    const pin_spec *to
)
{
    for (int i = first; i < count; i++) {
        pin_spec pin = pins[i];
        pin.flags ^= (from->flags ^ to->flags) & PIN_FLAG_REFERENCE;
        apush(*outputs, pin);
    }
}

/* A wire, a binding or a literal handle supplies the entity; otherwise Self. */
static bool entity_given(const graph *g, const node *n, int pin)
{
    node *source = NULL;
    int source_pin = -1;
    return n->input_pins[pin].binding || n->input_pins[pin].default_value.u64 ||
           graph_input_source(g, n, pin, &source, &source_pin);
}

bool component_node_self(const graph *g, const node *n)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    return kind && !entity_given(g, n, component_node_setting(kind) + 1);
}

const node_desc *component_node_getter(const graph *g, const node *n)
{
    if (n->desc->component_kind == NODE_COMPONENT_GET_SELF) {
        return n->desc;
    }
    const ecs_component_desc *component = component_node_kind_of(n->desc) == COMPONENT_NODE_GET
                                            ? component_node_component(n)
                                            : NULL;
    return component && component_node_self(g, n)
             ? access_desc(component, NODE_COMPONENT_GET_SELF, 0)
             : NULL;
}

static int lower_error(char *error, size_t size, const char *format, const char *name)
{
    if (error && size) {
        snprintf(error, size, format, name);
    }
    return -1;
}

/* Each field input becomes one field setter; Exec runs them in field order. */
static int lower_split(graph *g, node *n, const ecs_component_desc *component, char *error, size_t size)
{
    int count = n->input_count - SET_VALUE;
    if (count <= 0) {
        return lower_error(error, size, "Set %s writes no fields", component->name);
    }
    const node_desc **setters = NULL;
    for (int i = 0; i < count; i++) {
        const char *name = n->input_pins[SET_VALUE + i].name;
        int index = field_index(component->type, name);
        const node_desc *setter = access_desc(component, NODE_COMPONENT_SET_FIELD, index);
        if (!setter) {
            afree(setters);
            return lower_error(error, size, "Set Component cannot write field %s", name);
        }
        apush(setters, setter);
    }
    /* Step 0 replaces the node's pins; keep what later steps need. */
    pin_spec exec = n->input_pins[0], entity = n->input_pins[2];
    pin_spec *fields = NULL, *outputs_before = NULL;
    for (int i = 0; i < count; i++) {
        apush(fields, n->input_pins[SET_VALUE + i]);
    }
    for (int i = 0; i < n->output_count; i++) {
        apush(outputs_before, n->output_pins[i]);
    }
    pin_spec then = outputs_before[0], value = outputs_before[1];
    node **steps = NULL;
    for (int i = 0; i < count; i++) {
        const node_desc *setter = setters[i];
        node *step = i ? graph_add_node(g, setter) : n;
        assert(step);
        step->x = n->x;
        step->y = n->y;
        step->placed = n->placed;
        step->disabled = n->disabled;
        bool last = i == count - 1;
        pin_spec *inputs = NULL, *outputs = NULL;
        uint64_t exec_id = i ? step->input_pins[0].id : exec.id;
        uint64_t entity_id = i ? step->input_pins[2].id : entity.id;
        uint64_t then_id = last ? then.id
                         : i    ? step->output_pins[0].id
                                : graph_alloc_id(g);
        uint64_t value_id = last ? value.id
                          : i    ? step->output_pins[1].id
                                 : graph_alloc_id(g);
        apush(inputs, kept_pin(&setter->inputs[0], &set_inputs[0], i ? NULL : &exec, exec_id));
        pin_spec field = {
            .name = component->type->fields[setter->component_field].name
        };
        apush(inputs, kept_pin(&setter->inputs[1], &field, &fields[i], fields[i].id));
        apush(inputs, kept_pin(&setter->inputs[2], &set_inputs[2], &entity, entity_id));
        apush(outputs, kept_pin(&setter->outputs[0], &set_outputs[0], last ? &then : NULL, then_id));
        apush(outputs, kept_pin(&setter->outputs[1], &set_outputs[1], last ? &value : NULL, value_id));
        if (last) {
            int exposed = acount(outputs_before);
            keep_exposed(
                &outputs,
                outputs_before,
                2,
                exposed,
                &set_outputs[1],
                &setter->outputs[1]
            );
        }
        replace_pins(step, setter, inputs, outputs);
        apush(steps, step);
    }
    node *final = alast(steps);
    int edge_count = acount(g->edges);
    for (int e = 0; e < edge_count; e++) {
        edge *edge = &g->edges[e];
        edge->from_id = edge->from_id == n->id ? final->id : edge->from_id;
        for (int i = 1; edge->to_id == n->id && i < count; i++) {
            if (edge->to_pin_id == fields[i].id) {
                edge->to_id = steps[i]->id;
            }
        }
        if (edge->to_id != n->id || edge->to_pin_id != entity.id) {
            continue;
        }
        for (int i = 1; i < count; i++) {
            struct edge copy = g->edges[e];
            copy.to_id = steps[i]->id;
            copy.to_pin_id = steps[i]->input_pins[2].id;
            apush(g->edges, copy);
        }
    }
    for (int i = 0; i + 1 < count; i++) {
        apush(
            g->edges,
            ((edge){ .from_id = steps[i]->id,
                     .from_pin_id = steps[i]->output_pins[0].id,
                     .to_id = steps[i + 1]->id,
                     .to_pin_id = steps[i + 1]->input_pins[0].id,
                     .kind = GRAPH_EDGE_EXEC })
        );
    }
    for (int i = 0; i < acount(g->locals); i++) {
        if (g->locals[i].source_id == n->id) {
            g->locals[i].source_id = final->id;
        }
    }
    afree(setters);
    afree(fields);
    afree(outputs_before);
    afree(steps);
    return 0;
}

static int lower_node(graph *g, node *n, char *error, size_t size)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    const ecs_component_desc *component = component_node_component(n);
    if (!component) {
        return lower_error(error, size, "Choose a component for %s", n->desc->name);
    }
    if (kind == COMPONENT_NODE_SET && component_node_split(n)) {
        return lower_split(g, n, component, error, size);
    }
    int setting = component_node_setting(kind);
    bool given = entity_given(g, n, setting + 1);
    node_component_kind target_kind = kind == COMPONENT_NODE_GET
                                        ? (given ? NODE_COMPONENT_GET_ENTITY
                                                 : NODE_COMPONENT_GET_SELF)
                                    : kind == COMPONENT_NODE_HAS
                                        ? (given ? NODE_COMPONENT_HAS_ENTITY
                                                 : NODE_COMPONENT_HAS_SELF)
                                        : (given ? NODE_COMPONENT_SET_ENTITY
                                                 : NODE_COMPONENT_SET_SELF);
    const node_desc *target = access_desc(component, target_kind, 0);
    assert(target);
    pin_spec *inputs = NULL, *outputs = NULL;
    bool set = kind == COMPONENT_NODE_SET;
    const node_desc *from = n->desc;
    if (set) {
        apush(
            inputs,
            kept_pin(
                &target->inputs[0],
                &from->inputs[0],
                &n->input_pins[0],
                n->input_pins[0].id
            )
        );
    }
    if (given) {
        const pin_spec *entity = &n->input_pins[setting + 1];
        apush(
            inputs,
            kept_pin(
                &target->inputs[set],
                &from->inputs[setting + 1],
                entity,
                entity->id
            )
        );
    }
    if (set) {
        const pin_spec *value = &n->input_pins[SET_VALUE];
        const pin_spec *to = &target->inputs[target->input_count - 1];
        apush(inputs, kept_pin(to, &from->inputs[SET_VALUE], value, value->id));
    }
    for (int i = 0; i < target->output_count; i++) {
        const pin_spec *pin = &n->output_pins[i];
        apush(outputs, kept_pin(&target->outputs[i], &from->outputs[i], pin, pin->id));
    }
    int value = target->output_count - 1;
    keep_exposed(
        &outputs,
        n->output_pins,
        target->output_count,
        n->output_count,
        &from->outputs[value],
        &target->outputs[value]
    );
    replace_pins(n, target, inputs, outputs);
    return 0;
}

int graph_lower_component_nodes(graph *g, char *error, size_t error_size)
{
    int count = acount(g->nodes);
    for (int i = 0; i < count; i++) {
        node *n = g->nodes[i];
        if (!component_node_kind_of(n->desc)) {
            continue;
        }
        int result = lower_node(g, n, error, error_size);
        reindexed(g);
        if (result) {
            return -1;
        }
    }
    return 0;
}

int behavior_asset_lower_component_nodes(behavior_asset *asset, char *error, size_t error_size)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (graph_lower_component_nodes(asset->graphs[i], error, error_size)) {
            return -1;
        }
    }
    return 0;
}

bool behavior_asset_has_component_nodes(const behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            if (component_node_kind_of(g->nodes[j]->desc)) {
                return true;
            }
        }
    }
    return false;
}

static pin_spec setting_pin(graph *g, const ecs_component_desc *component)
{
    pin_spec pin = get_inputs[0];
    pin.id = graph_alloc_id(g);
    pin.default_value.str = graph_own_str(g, component->key, strlen(component->key));
    return pin;
}

static pin_spec typed(pin_spec pin, const ecs_component_desc *component)
{
    pin.type_name = component->type->name;
    pin.default_value.type_name = pin.type_name;
    return pin;
}

bool component_node_raise(graph *g, node *n)
{
    const node_desc *d = n->desc;
    const ecs_component_desc *component = d->component;
    node_component_kind kind = d->component_kind;
    if (g->dataflow || kind == NODE_COMPONENT_NONE ||
        n->input_count != d->input_count || n->output_count < d->output_count) {
        return false;
    }
    bool given = kind == NODE_COMPONENT_GET_ENTITY ||
                 kind == NODE_COMPONENT_HAS_ENTITY ||
                 kind == NODE_COMPONENT_SET_ENTITY ||
                 kind == NODE_COMPONENT_SET_FIELD;
    int entity = kind == NODE_COMPONENT_SET_FIELD  ? 2
               : kind == NODE_COMPONENT_SET_ENTITY ? 1
                                                   : 0;
    /* An explicit entity that nothing supplies fails today; Self would not. */
    if (given && kind != NODE_COMPONENT_SET_FIELD && !entity_given(g, n, entity)) {
        return false;
    }
    bool set = kind == NODE_COMPONENT_SET_SELF ||
               kind == NODE_COMPONENT_SET_ENTITY || kind == NODE_COMPONENT_SET_FIELD;
    bool has = kind == NODE_COMPONENT_HAS_SELF || kind == NODE_COMPONENT_HAS_ENTITY;
    const node_desc *desc = set ? &NODE_SetComponent
                          : has ? &NODE_HasComponent
                                : &NODE_GetComponent;
    pin_spec *inputs = NULL, *outputs = NULL;
    if (set) {
        apush(
            inputs,
            kept_pin(
                &set_inputs[0],
                &d->inputs[0],
                &n->input_pins[0],
                n->input_pins[0].id
            )
        );
    }
    apush(inputs, setting_pin(g, component));
    if (given) {
        const pin_spec *authored = &n->input_pins[entity];
        apush(
            inputs,
            kept_pin(&get_inputs[1], &d->inputs[entity], authored, authored->id)
        );
    } else {
        apush(inputs, kept_pin(&get_inputs[1], NULL, NULL, graph_alloc_id(g)));
    }
    if (kind == NODE_COMPONENT_SET_FIELD) {
        const type_field *field = &component->type->fields[d->component_field];
        pin_spec to = { .name = field->name,
                        .type = field->type,
                        .type_name = field->type_name };
        apush(
            inputs,
            kept_pin(&to, &d->inputs[1], &n->input_pins[1], n->input_pins[1].id)
        );
    } else if (set) {
        int last = n->input_count - 1;
        const pin_spec *value = &n->input_pins[last];
        pin_spec pin = kept_pin(
            &set_inputs[SET_VALUE],
            &d->inputs[last],
            value,
            value->id
        );
        apush(inputs, typed(pin, component));
    }
    const pin_spec *to_outputs = set ? set_outputs
                               : has ? has_outputs
                                     : get_outputs;
    for (int i = 0; i < d->output_count; i++) {
        const pin_spec *authored = &n->output_pins[i];
        pin_spec
            pin = kept_pin(&to_outputs[i], &d->outputs[i], authored, authored->id);
        apush(outputs, pin.type == PIN_STRUCT ? typed(pin, component) : pin);
    }
    int value = d->output_count - 1;
    keep_exposed(
        &outputs,
        n->output_pins,
        d->output_count,
        n->output_count,
        &d->outputs[value],
        &to_outputs[value]
    );
    replace_pins(n, desc, inputs, outputs);
    reindexed(g);
    return true;
}
