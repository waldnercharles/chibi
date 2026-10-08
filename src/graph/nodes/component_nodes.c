#include "graph/nodes/component_nodes.h"

#include "graph/authoring/graph_edit.h"
#include "graph/model/local.h"
#include "graph/model/native_definition.h"
#include "runtime/core_definition.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VALUE_PIN(pin_flags)                                                          \
    {                                                                                 \
        .name = "Value", .type = PIN_STRUCT, .flags = (pin_flags), .default_value = { \
            .type = PIN_STRUCT                                                        \
        }                                                                             \
    }

#define TYPE_PIN                                                               \
    {                                                                          \
        .name = "Type", .type = PIN_NAME, .flags = PIN_FLAG_SETTING,           \
        .default_value = {                                                     \
            .type = PIN_NAME                                                   \
        }                                                                      \
    }

/* Set's Value input follows Exec, Component and Entity. */
enum
{
    SET_VALUE = 3,
};

#define COMPONENT_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    GRAPH_NATIVE_PORT_FLAGS(                                                                                  \
        NAME,                                                                                                 \
        KIND,                                                                                                 \
        STORAGE,                                                                                              \
        TYPE_NAME,                                                                                            \
        CONTAINER,                                                                                            \
        TYPE_FLAGS,                                                                                           \
        GROUP,                                                                                                \
        REFERENCE,                                                                                            \
        OWNER,                                                                                                \
        VALUE,                                                                                                \
        ((TYPE_FLAGS & SCRIPT_TYPE_REFERENCE) ? PIN_FLAG_REFERENCE : 0) |                                     \
            ((REFERENCE == SCRIPT_REFERENCE_COMPONENT) ? PIN_FLAG_SETTING : 0)                                \
    )
static pin_spec get_inputs[] = { CORE_COMPONENT_GET_INPUTS(COMPONENT_PORT) };
static pin_spec get_outputs[] = { CORE_COMPONENT_GET_OUTPUTS(COMPONENT_PORT) };
static pin_spec has_outputs[] = { CORE_COMPONENT_HAS_OUTPUTS(COMPONENT_PORT) };
static pin_spec set_inputs[] = { CORE_COMPONENT_SET_INPUTS(COMPONENT_PORT) };
static pin_spec set_outputs[] = { CORE_COMPONENT_SET_OUTPUTS(COMPONENT_PORT) };
static pin_spec add_inputs[] = { CORE_COMPONENT_CHANGE_INPUTS(COMPONENT_PORT) };
static pin_spec add_outputs[] = { CORE_COMPONENT_CHANGE_OUTPUTS(COMPONENT_PORT) };
#undef COMPONENT_PORT
static pin_spec make_inputs[] = { TYPE_PIN };
static pin_spec make_outputs[] = { VALUE_PIN(0) };
static pin_spec break_inputs[] = { VALUE_PIN(0), TYPE_PIN };

node_desc NODE_GetComponent = {
    .source_component_access = NODE_SOURCE_COMPONENT_READ,
    .source_component_entity = 1,
    .source_component_value = -1,
    .name = CORE_COMPONENT_GET_NAME,
    .input_count = 2,
    .inputs = get_inputs,
    .output_count = 1,
    .outputs = get_outputs,
    CORE_COMPONENT_GET_PRESENTATION,
};

node_desc NODE_SetComponent = {
    .source_component_access = NODE_SOURCE_COMPONENT_WRITE,
    .source_component_entity = 1,
    .source_component_value = 2,
    .name = CORE_COMPONENT_SET_NAME,
    .input_count = 4,
    .inputs = set_inputs,
    .output_count = 2,
    .outputs = set_outputs,
    .display_class = NODE_DISPLAY_FUNCTION,
    CORE_COMPONENT_SET_PRESENTATION,
};

node_desc NODE_HasComponent = {
    .source_component_access = NODE_SOURCE_COMPONENT_HAS,
    .source_component_entity = 1,
    .source_component_value = -1,
    .name = CORE_COMPONENT_HAS_NAME,
    .input_count = 2,
    .inputs = get_inputs,
    .output_count = 1,
    .outputs = has_outputs,
    CORE_COMPONENT_HAS_PRESENTATION,
};

node_desc NODE_AddComponent = {
    .source_component_access = NODE_SOURCE_COMPONENT_ADD,
    .source_component_entity = -1,
    .source_component_value = -1,
    .name = CORE_COMPONENT_ADD_NAME,
    .input_count = 2,
    .inputs = add_inputs,
    .output_count = 2,
    .outputs = add_outputs,
    .display_class = NODE_DISPLAY_FUNCTION,
    CORE_COMPONENT_ADD_PRESENTATION,
};

node_desc NODE_RemoveComponent = {
    .source_component_access = NODE_SOURCE_COMPONENT_REMOVE,
    .source_component_entity = -1,
    .source_component_value = -1,
    .name = CORE_COMPONENT_REMOVE_NAME,
    .input_count = 2,
    .inputs = add_inputs,
    .output_count = 2,
    .outputs = add_outputs,
    .display_class = NODE_DISPLAY_FUNCTION,
    CORE_COMPONENT_REMOVE_PRESENTATION,
};

node_desc NODE_MakeStruct = {
    .name = "Make Struct",
    .input_count = 1,
    .inputs = make_inputs,
    .output_count = 1,
    .outputs = make_outputs,
    .category = "Struct",
    .keywords = "make struct construct build value fields",
    .description = "Builds a struct value from its fields.",
};

node_desc NODE_BreakStruct = {
    .name = "Break Struct",
    .input_count = 2,
    .inputs = break_inputs,
    .category = "Struct",
    .keywords = "break struct split fields members",
    .description = "Splits a struct value into its fields.",
};

static node_desc *const COMPONENT_NODES[] = {
    &NODE_GetComponent, &NODE_SetComponent,    &NODE_HasComponent,
    &NODE_AddComponent, &NODE_RemoveComponent, &NODE_MakeStruct,
    &NODE_BreakStruct,
};

const registry_pack CORE_COMPONENT_PACK = {
    .name = "core.component",
    .nodes = COMPONENT_NODES,
    .node_count = sizeof COMPONENT_NODES / sizeof COMPONENT_NODES[0],
};

component_node_kind component_node_kind_of(const node_desc *desc)
{
    for (int kind = COMPONENT_NODE_GET; kind <= COMPONENT_NODE_BREAK; kind++) {
        if (desc == COMPONENT_NODES[kind - 1]) {
            return (component_node_kind)kind;
        }
    }
    return COMPONENT_NODE_NONE;
}

/* Add, Remove, Make and Break become one generated node with the same pins. */
static bool direct(component_node_kind kind)
{
    return kind >= COMPONENT_NODE_ADD;
}

static bool structural(component_node_kind kind)
{
    return kind == COMPONENT_NODE_MAKE || kind == COMPONENT_NODE_BREAK;
}

int component_node_setting(const node *n)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    assert(kind != COMPONENT_NODE_NONE);
    return kind == COMPONENT_NODE_GET || kind == COMPONENT_NODE_HAS ? 0
         : kind == COMPONENT_NODE_SET ? 1
                                      : n->input_count - 1;
}

const char *component_node_key(const node *n)
{
    int setting = component_node_setting(n);
    assert(setting >= 0 && setting < n->input_count);
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

/* The generated node an Add, Remove, Make or Break node with this key becomes. */
static const node_desc *direct_target(component_node_kind kind, const char *key)
{
    assert(direct(kind));
    if (!key || !key[0]) {
        return NULL;
    }
    if (structural(kind)) {
        node_pure_kind pure = kind == COMPONENT_NODE_MAKE ? NODE_PURE_MAKE
                                                          : NODE_PURE_BREAK;
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *d = registry_node(i);
            if (d->pure_kind == pure && d->type && !strcmp(d->type->name, key)) {
                return d;
            }
        }
        return NULL;
    }
    const ecs_component_desc *component = component_by_key(key);
    char name[160];
    if (!component) {
        return NULL;
    }
    snprintf(
        name,
        sizeof name,
        "%s_%s",
        kind == COMPONENT_NODE_ADD ? "Add" : "Remove",
        component->name
    );
    return registry_node_by_name(name);
}

component_node_kind component_generated_kind(const node_desc *d)
{
    switch (d->component_kind) {
        case NODE_COMPONENT_GET_SELF:
        case NODE_COMPONENT_GET_ENTITY: return COMPONENT_NODE_GET;
        case NODE_COMPONENT_HAS_SELF:
        case NODE_COMPONENT_HAS_ENTITY: return COMPONENT_NODE_HAS;
        case NODE_COMPONENT_SET_SELF:
        case NODE_COMPONENT_SET_ENTITY:
        case NODE_COMPONENT_SET_FIELD: return COMPONENT_NODE_SET;
        case NODE_COMPONENT_NONE: break;
    }
    if (d->type && d->pure_kind == NODE_PURE_MAKE) {
        return COMPONENT_NODE_MAKE;
    }
    if (d->type && d->pure_kind == NODE_PURE_BREAK) {
        return COMPONENT_NODE_BREAK;
    }
    /* Reflection names structural component nodes Add_<C> and Remove_<C>. */
    if (!strncmp(d->name, "Add_", 4) && component_by_key(d->name + 4)) {
        return COMPONENT_NODE_ADD;
    }
    if (!strncmp(d->name, "Remove_", 7) && component_by_key(d->name + 7)) {
        return COMPONENT_NODE_REMOVE;
    }
    return COMPONENT_NODE_NONE;
}

const char *component_node_canonical_key(component_node_kind kind, const char *key)
{
    if (structural(kind)) {
        const node_desc *target = direct_target(kind, key);
        return target ? target->type->name : NULL;
    }
    const ecs_component_desc *component = component_by_key(key);
    return component ? component->key : NULL;
}

const ecs_component_desc *component_node_component(const node *n)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (kind) {
        return structural(kind) ? NULL : component_by_key(component_node_key(n));
    }
    return n->desc->component_kind != NODE_COMPONENT_NONE ? n->desc->component : NULL;
}

const node_desc *component_node_struct(const node *n)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (kind) {
        return structural(kind) ? direct_target(kind, component_node_key(n)) : NULL;
    }
    return structural(component_generated_kind(n->desc)) ? n->desc : NULL;
}

const node_desc *component_node_shape(const node_desc *desc, const char *key)
{
    component_node_kind kind = component_node_kind_of(desc);
    return direct(kind) ? direct_target(kind, key) : NULL;
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

static bool same_pins(const pin_spec *pins, int count, const pin_spec *shape, int shape_count)
{
    if (count != shape_count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const pin_spec *a = &pins[i], *b = &shape[i];
        if (a->kind != b->kind || a->type != b->type || a->container != b->container ||
            !(a->name == b->name || (a->name && b->name && !strcmp(a->name, b->name))) ||
            !(a->type_name == b->type_name ||
              (a->type_name && b->type_name && !strcmp(a->type_name, b->type_name)))) {
            return false;
        }
    }
    return true;
}

/* An Add, Remove, Make or Break node has its target's pins, then its setting;
 * without a target, its template's. Unchanged shapes keep their wires. */
static int reshape_direct(graph *g, node *n, const node_desc *target)
{
    const node_desc *shape = target ? target : n->desc;
    int inputs = target ? target->input_count : n->desc->input_count - 1;
    int outputs = 0;
    while (outputs < n->output_count && !n->output_pins[outputs].field_source) {
        outputs++;
    }
    if (same_pins(n->input_pins, n->input_count - 1, shape->inputs, inputs) &&
        same_pins(n->output_pins, outputs, shape->outputs, shape->output_count)) {
        return 0;
    }
    if (hide_exposed_outputs(g, n)) {
        return -1;
    }
    while (n->output_count) {
        if (graph_remove_pin(g, n, 1, n->output_count - 1)) {
            return -1;
        }
    }
    while (n->input_count > 1) {
        if (graph_remove_pin(g, n, 0, 0)) {
            return -1;
        }
    }
    for (int i = 0; i < inputs; i++) {
        if (graph_add_pin(g, n, 0, &shape->inputs[i]) < 0) {
            return -1;
        }
    }
    for (int i = 0; i < shape->output_count; i++) {
        if (graph_add_pin(g, n, 1, &shape->outputs[i]) < 0) {
            return -1;
        }
    }
    return graph_move_pin(g, n, 0, 0, n->input_count - 1) < 0 ? -1 : 0;
}

static void store_key(graph *g, node *n, const char *key)
{
    const char *stored = key ? key : "";
    n->input_pins[component_node_setting(n)]
        .default_value.str = graph_own_str(g, stored, strlen(stored));
}

int component_node_retarget(graph *g, node *n, const char *key)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    assert(kind != COMPONENT_NODE_NONE);
    const char *canonical = component_node_canonical_key(kind, key);
    if (key && key[0] && !canonical) {
        return -1;
    }
    if (direct(kind)) {
        if (reshape_direct(g, n, direct_target(kind, canonical))) {
            return -1;
        }
        store_key(g, n, canonical);
        return 0;
    }
    const ecs_component_desc *component = component_by_key(canonical);
    const ecs_component_desc *previous = component_node_component(n);
    store_key(g, n, canonical);
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
    component_node_kind kind = component_node_kind_of(n->desc);
    const char *canonical = component_node_canonical_key(kind, key);
    if (key && key[0] && !canonical) {
        return -1;
    }
    if (direct(kind)) {
        if (reshape_direct(g, n, direct_target(kind, canonical))) {
            return -1;
        }
        store_key(g, n, canonical);
        return 0;
    }
    if (split && graph_remove_pin(g, n, 0, SET_VALUE)) {
        return -1;
    }
    type_values(g, n, component_by_key(canonical));
    store_key(g, n, canonical);
    return 0;
}

component_node_preset *component_node_presets()
{
    static const node_desc *const access[] = { &NODE_GetComponent,
                                               &NODE_SetComponent,
                                               &NODE_HasComponent,
                                               &NODE_AddComponent,
                                               &NODE_RemoveComponent };
    component_node_preset *presets = NULL;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *d = registry_node(i);
        if (d->type &&
            (d->pure_kind == NODE_PURE_MAKE || d->pure_kind == NODE_PURE_BREAK)) {
            const node_desc *desc = d->pure_kind == NODE_PURE_MAKE
                                      ? &NODE_MakeStruct
                                      : &NODE_BreakStruct;
            apush(
                presets,
                ((component_node_preset){ .desc = desc,
                                          .setting = d->type->name,
                                          .type = d->type })
            );
            continue;
        }
        if (d->component_kind != NODE_COMPONENT_GET_SELF) {
            continue;
        }
        const ecs_component_desc *component = d->component;
        for (size_t a = 0; a < sizeof access / sizeof *access; a++) {
            component_node_kind made = component_node_kind_of(access[a]);
            if (!direct(made) || direct_target(made, component->key)) {
                apush(
                    presets,
                    ((component_node_preset){ .desc = access[a],
                                              .setting = component->key,
                                              .type = component->type })
                );
            }
        }
        for (int f = 0; f < component->type->field_count; f++) {
            const char *field = component->type->fields[f].name;
            if (component_field_settable(component, field)) {
                apush(
                    presets,
                    ((component_node_preset){ .desc = &NODE_SetComponent,
                                              .setting = component->key,
                                              .field = field,
                                              .type = component->type })
                );
            }
        }
    }
    return presets;
}

void component_node_preset_describe(const component_node_preset *preset, char *buf, size_t size)
{
    const char *type = preset->type->name;
    switch (component_node_kind_of(preset->desc)) {
        case COMPONENT_NODE_GET:
            snprintf(buf, size, "Reads %s from Entity, which defaults to Self.", type);
            return;
        case COMPONENT_NODE_SET:
            if (preset->field) {
                snprintf(
                    buf,
                    size,
                    "Writes %s's %s on Entity, which defaults to Self.",
                    type,
                    preset->field
                );
                return;
            }
            snprintf(buf, size, "Writes %s on Entity, which defaults to Self.", type);
            return;
        case COMPONENT_NODE_HAS:
            snprintf(buf, size, "Whether Entity, which defaults to Self, has %s.", type);
            return;
        case COMPONENT_NODE_ADD:
            snprintf(buf, size, "Adds %s with default values to Self; Value is whether it was added.", type);
            return;
        case COMPONENT_NODE_REMOVE:
            snprintf(buf, size, "Removes %s from Self; Value is whether it was there.", type);
            return;
        case COMPONENT_NODE_MAKE:
            snprintf(buf, size, "Builds a %s from its fields.", type);
            return;
        case COMPONENT_NODE_BREAK:
            snprintf(buf, size, "Splits a %s into its fields.", type);
            return;
        case COMPONENT_NODE_NONE: break;
    }
    assert(false);
}

int component_node_configure(graph *g, node *n, const char *setting, const char *field)
{
    pin_value key = { .type = PIN_NAME, .str = setting };
    return graph_set_pin_default(g, n, 0, component_node_setting(n), &key) ||
                   (field && component_node_show_field(g, n, field, true))
             ? -1
             : 0;
}

int component_node_template_count(const node *n, int output)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    const node_desc *target = direct(kind)
                                ? direct_target(kind, component_node_key(n))
                                : NULL;
    if (target) {
        return output ? target->output_count : target->input_count + 1;
    }
    int count = output ? n->desc->output_count : n->desc->input_count;
    return !output && component_node_split(n) ? count - 1 : count;
}

pin_spec component_node_template(const node *n, int output, int index)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    const node_desc *target = direct(kind)
                                ? direct_target(kind, component_node_key(n))
                                : NULL;
    if (target) {
        return output ? target->outputs[index]
             : index < target->input_count
                 ? target->inputs[index]
                 : n->desc->inputs[n->desc->input_count - 1];
    }
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
    return kind && !direct(kind) &&
           !entity_given(g, n, component_node_setting(n) + 1);
}

/* Get, Set and Has read the owner or an explicit entity. */
static node_component_kind access_kind(component_node_kind kind, bool given)
{
    return kind == COMPONENT_NODE_GET
             ? (given ? NODE_COMPONENT_GET_ENTITY : NODE_COMPONENT_GET_SELF)
         : kind == COMPONENT_NODE_HAS
             ? (given ? NODE_COMPONENT_HAS_ENTITY : NODE_COMPONENT_HAS_SELF)
             : (given ? NODE_COMPONENT_SET_ENTITY : NODE_COMPONENT_SET_SELF);
}

const node_desc *component_node_target(const graph *g, const node *n)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (!kind) {
        return n->desc;
    }
    if (direct(kind)) {
        return direct_target(kind, component_node_key(n));
    }
    const ecs_component_desc *component = component_node_component(n);
    if (!component) {
        return NULL;
    }
    if (component_node_split(n)) {
        int field = n->input_count > SET_VALUE
                      ? field_index(component->type, n->input_pins[SET_VALUE].name)
                      : -1;
        return access_desc(component, NODE_COMPONENT_SET_FIELD, field);
    }
    bool given = entity_given(g, n, component_node_setting(n) + 1);
    return access_desc(component, access_kind(kind, given), 0);
}

bool component_node_access(const node *n, const char **verb, const char **name)
{
    static const char *const verbs[] = { NULL,  "Get",    "Set",  "Has",
                                         "Add", "Remove", "Make", "Break" };
    component_node_kind kind = component_node_kind_of(n->desc);
    bool authored = kind != COMPONENT_NODE_NONE;
    kind = authored ? kind : component_generated_kind(n->desc);
    if (!kind) {
        return false;
    }
    const node_desc *made = component_node_struct(n);
    const ecs_component_desc *component = structural(kind) ? NULL
                                        : authored ? component_node_component(n)
                                        : n->desc->component
                                            ? n->desc->component
                                            : component_by_key(
                                                  strchr(n->desc->name, '_') + 1
                                              );
    *verb = verbs[kind];
    *name = made ? made->type->name : component ? component->name : NULL;
    return *name != NULL;
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

/* The node keeps every pin but its setting, which already match the target's. */
static int lower_direct(node *n, component_node_kind kind, char *error, size_t size)
{
    const node_desc *target = direct_target(kind, component_node_key(n));
    if (!target) {
        const char *format = structural(kind) ? "Choose a type for %s"
                                              : "Choose a component for %s";
        return lower_error(error, size, format, n->desc->name);
    }
    pin_spec *inputs = NULL, *outputs = NULL;
    for (int i = 0; i + 1 < n->input_count; i++) {
        apush(inputs, n->input_pins[i]);
    }
    for (int i = 0; i < n->output_count; i++) {
        apush(outputs, n->output_pins[i]);
    }
    replace_pins(n, target, inputs, outputs);
    return 0;
}

static int lower_node(graph *g, node *n, char *error, size_t size)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    if (direct(kind)) {
        return lower_direct(n, kind, error, size);
    }
    const ecs_component_desc *component = component_node_component(n);
    if (!component) {
        return lower_error(error, size, "Choose a component for %s", n->desc->name);
    }
    if (kind == COMPONENT_NODE_SET && component_node_split(n)) {
        return lower_split(g, n, component, error, size);
    }
    int setting = component_node_setting(n);
    bool given = entity_given(g, n, setting + 1);
    node_component_kind target_kind = access_kind(kind, given);
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

int component_node_lower(graph *g, node *n, char *error, size_t error_size)
{
    if (!component_node_kind_of(n->desc)) {
        return 0;
    }
    int result = lower_node(g, n, error, error_size);
    reindexed(g);
    return result;
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

static pin_spec setting_pin(graph *g, const pin_spec *template, const char *key)
{
    pin_spec pin = *template;
    pin.id = graph_alloc_id(g);
    pin.default_value.str = graph_own_str(g, key, strlen(key));
    return pin;
}

/* A generated Add, Remove, Make or Break node gains its setting. */
static bool raise_direct(graph *g, node *n, component_node_kind kind)
{
    const node_desc *d = n->desc;
    const node_desc *desc = COMPONENT_NODES[kind - 1];
    const char *key = structural(kind)
                        ? d->type->name
                        : component_by_key(strchr(d->name, '_') + 1)->key;
    pin_spec *inputs = NULL, *outputs = NULL;
    for (int i = 0; i < n->input_count; i++) {
        apush(inputs, n->input_pins[i]);
    }
    apush(inputs, setting_pin(g, &desc->inputs[desc->input_count - 1], key));
    for (int i = 0; i < n->output_count; i++) {
        apush(outputs, n->output_pins[i]);
    }
    replace_pins(n, desc, inputs, outputs);
    reindexed(g);
    return true;
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
    component_node_kind family = component_generated_kind(d);
    /* Documents key a component node's inputs by their template names. */
    if (!family || g->dataflow || n->output_count < d->output_count ||
        !same_pins(n->input_pins, n->input_count, d->inputs, d->input_count)) {
        return false;
    }
    if (direct(family)) {
        return raise_direct(g, n, family);
    }
    const ecs_component_desc *component = d->component;
    node_component_kind kind = d->component_kind;
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
    apush(inputs, setting_pin(g, &get_inputs[0], component->key));
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
