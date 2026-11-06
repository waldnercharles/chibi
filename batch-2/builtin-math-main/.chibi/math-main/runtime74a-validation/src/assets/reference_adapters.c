#include "assets/reference_adapters.h"

#include "assets/prototype.h"
#include "assets/ui_document.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *reference_text(pin_value value)
{
    pin_string text = pin_text(value);
    return text.length && !memchr(text.bytes, 0, text.length) && !text.bytes[text.length]
             ? text.bytes
             : NULL;
}

static reference_symbol reference_asset_symbol(int asset)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
}
static reference_symbol reference_event_symbol(const reference_index *index, const char *name)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                               .asset = reference_index_event_asset(index, name),
                               .name = name };
}
static int reference_named_field(const char *component, const char *field)
{
    for (int i = 0; i < reference_named_field_count(); i++) {
        reference_named_field_entry entry = reference_named_field_at(i);
        if (!strcmp(entry.component, component) && !strcmp(entry.field, field)) {
            return i;
        }
    }
    return -1;
}
static void reference_add_named(reference_index *index, reference at, int field, const char *value)
{
    reference_named_field_entry entry = reference_named_field_at(field);
    reference_index_add_named_field(index, at, entry.component, entry.field, value);
}
static bool reference_run_contains(const uint64_t *nodes, uint64_t id)
{
    for (int i = 0; i < acount(nodes); i++) {
        if (nodes[i] == id) {
            return true;
        }
    }
    return false;
}

/* MeshParts.parts holds code parts, each naming its material. */
#define MESH_PARTS "MeshParts"

reference_asset_kind reference_domain_kind(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_EVENT: return REFERENCE_ASSET_BEHAVIOR;
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return REFERENCE_ASSET_LIBRARY;
        case BEHAVIOR_DOMAIN_MATERIAL: return REFERENCE_ASSET_MATERIAL;
        case BEHAVIOR_DOMAIN_FIELD: return REFERENCE_ASSET_FIELD;
        case BEHAVIOR_DOMAIN_MODEL:
        case BEHAVIOR_DOMAIN_SHAPE: return REFERENCE_ASSET_SHAPE;
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return REFERENCE_ASSET_EVENT;
    }
    return REFERENCE_ASSET_BEHAVIOR;
}

static void add_mesh_parts(reference_index *index, reference at, const char *parts)
{
    av_doc *doc = av_value_read(parts, NULL, 0);
    av_value *root = av_doc_root(doc);
    at.role = REFERENCE_BIND;
    at.component = MESH_PARTS ".parts";
    size_t i, count;
    av_value *part;
    av_arr_foreach(root, i, count, part)
    {
        const char *material = av_get_str(av_obj_get(part, "material"));
        if (material && material[0]) {
            at.symbol = reference_asset_symbol(
                reference_index_named_asset(index, REFERENCE_ASSET_MATERIAL, material)
            );
            reference_index_add_reference(index, at);
        }
    }
    av_doc_free(doc);
}

/* ---------- Graphs ---------- */

/* Where in a graph asset references are being added. */
typedef struct
{
    reference_index *index;
    int source;
    const graph *graph;
    const node *node; /* NULL outside nodes */
} site;

/* Where the site is: its node, pin and graph, when it has them. */
static reference place_of(const site *s, uint64_t pin)
{
    const graph *g = s->graph;
    int output, at;
    const pin_spec *spec = s->node && pin
                             ? graph_pin_by_id(s->node, pin, &output, &at)
                             : NULL;
    return (reference){ .source = s->source,
                        .graph_id = g ? g->graph_id : 0,
                        .node_id = s->node ? s->node->id : 0,
                        .pin_id = pin,
                        .graph = g && g->name && g->name[0] ? g->name : NULL,
                        .pin = spec && spec->name && spec->name[0] ? spec->name : NULL };
}

static void add_at(const site *s, reference_role role, reference_symbol symbol, uint64_t pin)
{
    reference r = place_of(s, pin);
    r.role = role;
    r.symbol = symbol;
    reference_index_add_reference(s->index, r);
}

/* A graph, variable, parameter or local of the site's asset. */
static reference_symbol own_symbol(const site *s, reference_symbol_kind kind, uint64_t graph_id, uint64_t id)
{
    return (
        reference_symbol
    ){ .kind = kind, .asset = s->source, .graph_id = graph_id, .id = id };
}

/* One of them where it is declared, with its name. */
static void add_definition(const site *s, reference_symbol symbol, const char *name, uint64_t pin)
{
    symbol.name = name && name[0] ? name : NULL;
    add_at(s, REFERENCE_DEFINE, symbol, pin);
}

static const char *pin_reference_text(const pin_spec *pin)
{
    bool text = pin->default_value.type == PIN_NAME ||
                pin->default_value.type == PIN_STR;
    return pin->container == PIN_CONTAINER_SINGLE && text &&
                   reference_text(pin->default_value)
             ? reference_text(pin->default_value)
             : NULL;
}

static bool graph_call(const node *n)
{
    return n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
           n->desc == &NODE_CallMacro ||
           (n->reference_asset && n->reference_asset[0]);
}

/* A call names its graph, in its library or its own asset, and each pin
 * that follows a parameter or result names that parameter. */
static void add_call(const site *s)
{
    const node *n = s->node;
    int asset = n->reference_asset && n->reference_asset[0]
                  ? reference_index_named_asset(s->index, REFERENCE_ASSET_LIBRARY, n->reference_asset)
                  : s->source;
    reference_symbol parameter = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                   .asset = asset,
                                   .graph_id = n->reference_id };
    add_at(
        s,
        REFERENCE_CALL,
        (reference_symbol){ .kind = REFERENCE_SYMBOL_GRAPH, .asset = asset, .id = n->reference_id },
        0
    );
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        for (int i = 0; i < (output ? n->output_count : n->input_count); i++) {
            parameter.id = pins[i].declaration_id;
            if (parameter.id) {
                add_at(s, REFERENCE_ARGUMENT, parameter, pins[i].id);
            }
        }
    }
}

static void add_event(const site *s)
{
    const node *n = s->node;
    bool handle = !strcmp(n->desc->name, "Event");
    bool send = n->desc == &NODE_CallEvent || !strcmp(n->desc->name, "Send Event");
    if ((handle || send) && n->reference_name && n->reference_name[0]) {
        add_at(
            s,
            handle ? REFERENCE_HANDLE : REFERENCE_SEND,
            reference_event_symbol(s->index, n->reference_name),
            0
        );
    }
}

static void add_spawn(const site *s)
{
    const node *n = s->node;
    for (int i = 0; !strcmp(n->desc->name, "Spawn Prototype") && i < n->input_count;
         i++) {
        const char *prototype = pin_reference_text(&n->input_pins[i]);
        if (!strcmp(n->input_pins[i].name, "Prototype") && prototype) {
            int asset = reference_index_named_asset(s->index, REFERENCE_ASSET_PROTOTYPE, prototype);
            add_at(
                s,
                REFERENCE_SPAWN,
                reference_asset_symbol(asset),
                n->input_pins[i].id
            );
        }
    }
}

static const char *component_name(const char *key)
{
    const ecs_component_desc *component = component_by_key(key);
    return component ? component->name : key;
}

static reference_symbol field_symbol(const char *component, const char *field)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_FIELD,
                               .asset = -1,
                               .name = component,
                               .field = field };
}

static const pin_spec *output_by_id(const node *n, uint64_t id)
{
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].id == id) {
            return &n->output_pins[i];
        }
    }
    return NULL;
}

/* The dot path of the field a component getter's output exposes, as
 * exposed fields (field_source) name it; false for the component itself. */
static bool exposed_path(const node *n, const pin_spec *pin, char *path, size_t size)
{
    if (!pin->field_source || !pin->field_name) {
        return false;
    }
    const pin_spec *parent = output_by_id(n, pin->field_source);
    char prefix[128];
    if (parent && exposed_path(n, parent, prefix, sizeof prefix)) {
        snprintf(path, size, "%s.%s", prefix, pin->field_name);
        return true;
    }
    snprintf(path, size, "%s", pin->field_name);
    return true;
}

/* A split pin names a field of the struct it split: the component's own, or
 * an exposed field's. */
static bool field_path(const node *n, const pin_spec *pin, const char *type, char *path, size_t size)
{
    if (!pin->split_parent) {
        return pin->kind == PIN_DATA && exposed_path(n, pin, path, size);
    }
    char prefix[128];
    if (exposed_path(n, pin, prefix, sizeof prefix)) {
        snprintf(path, size, "%s.%s", prefix, pin->name);
        return true;
    }
    if (pin->split_type_name && !strcmp(pin->split_type_name, type)) {
        snprintf(path, size, "%s", pin->name);
        return true;
    }
    return false;
}

/* The field a component setter's input writes: a field setter's Value, a
 * split Set Component's field input, or a split Value's part. */
static bool set_field(const node *n, int input, const ecs_component_desc *c, char *path, size_t size)
{
    const pin_spec *pin = &n->input_pins[input];
    if (pin->kind != PIN_DATA) {
        return false;
    }
    if (pin->split_parent) {
        return field_path(n, pin, c->type->name, path, size);
    }
    if (n->desc->component_kind == NODE_COMPONENT_SET_FIELD) {
        snprintf(path, size, "%s", c->type->fields[n->desc->component_field].name);
        return !strcmp(pin->name, "Value");
    }
    snprintf(path, size, "%s", pin->name);
    return component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
           component_node_split(n) && input >= component_node_setting(n) + 2;
}

/* A field's text default that names an asset, as a Set of Ui.document. */
static void add_field_default(const site *s, const char *component, const char *field, const pin_spec *pin)
{
    int named = reference_named_field(component, field);
    const char *value = pin_reference_text(pin);
    if (named >= 0 && value) {
        reference_add_named(s->index, place_of(s, pin->id), named, value);
    }
}

static void add_component_fields(const site *s, const ecs_component_desc *c, bool write)
{
    const node *n = s->node;
    char path[256];
    for (int i = 0; write && i < n->input_count; i++) {
        if (set_field(n, i, c, path, sizeof path)) {
            add_at(
                s,
                REFERENCE_WRITE,
                field_symbol(c->name, path),
                n->input_pins[i].id
            );
            add_field_default(s, c->name, path, &n->input_pins[i]);
        }
    }
    for (int i = 0; !write && i < n->output_count; i++) {
        if (field_path(n, &n->output_pins[i], c->type->name, path, sizeof path)) {
            add_at(
                s,
                REFERENCE_READ,
                field_symbol(c->name, path),
                n->output_pins[i].id
            );
        }
    }
}

/* Get, Has, Set, Add and Remove nodes, authored or generated, and the fields
 * a getter exposes or a setter writes. */
static void add_component_access(const site *s)
{
    const char *verb, *name;
    if (!component_node_access(s->node, &verb, &name) ||
        !strcmp(verb, "Make") || !strcmp(verb, "Break")) {
        return;
    }
    bool write = strcmp(verb, "Get") && strcmp(verb, "Has");
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_COMPONENT,
                                .asset = -1,
                                .name = name };
    add_at(s, write ? REFERENCE_WRITE : REFERENCE_READ, symbol, 0);
    const ecs_component_desc *component = component_node_component(s->node);
    if (component && (!strcmp(verb, "Get") || !strcmp(verb, "Set"))) {
        add_component_fields(s, component, write);
    }
}

/* A struct default of a component type whose fields name assets, as a
 * Shader value's material. */
static void add_struct_default(const site *s, const pin_spec *pin)
{
    const pin_value *value = &pin->default_value;
    const type_desc *type = value->type == PIN_STRUCT && value->ptr &&
                                    pin->type_name && pin->container == PIN_CONTAINER_SINGLE
                              ? type_catalog_find(pin->type_name)
                              : NULL;
    arena storage = { 0 };
    pin_value field;
    for (int i = 0; type && i < reference_named_field_count(); i++) {
        if (!strcmp(reference_named_field_at(i).component, type->name) &&
            pin_read_field_path(
                &storage,
                value->ptr,
                type,
                reference_named_field_at(i).field,
                &field
            ) == 0 &&
            reference_text(field)) {
            reference_add_named(s->index, place_of(s, pin->id), i, reference_text(field));
        }
    }
    if (type && !strcmp(type->name, MESH_PARTS) &&
        pin_read_field_path(&storage, value->ptr, type, "parts", &field) == 0 &&
        reference_text(field)) {
        add_mesh_parts(s->index, place_of(s, pin->id), reference_text(field));
    }
    arena_free(&storage);
}

/* Variable, local and component bindings read what they name. */
static void add_binding(const site *s, const pin_spec *pin)
{
    const pin_binding *b = pin->binding;
    switch (b->kind) {
        case PIN_BINDING_VARIABLE:
            add_at(s, REFERENCE_READ, own_symbol(s, REFERENCE_SYMBOL_VARIABLE, 0, b->variable_id), pin->id);
            return;
        case PIN_BINDING_LOCAL:
            add_at(
                s,
                REFERENCE_READ,
                own_symbol(s, REFERENCE_SYMBOL_LOCAL, s->graph->graph_id, b->local_id),
                pin->id
            );
            return;
        case PIN_BINDING_COMPONENT:
            add_at(
                s,
                REFERENCE_READ,
                (reference_symbol){ .kind = REFERENCE_SYMBOL_COMPONENT,
                                    .asset = -1,
                                    .name = component_name(b->component_key) },
                pin->id
            );
            return;
        case PIN_BINDING_COMPONENT_FIELD:
            add_at(
                s,
                REFERENCE_READ,
                field_symbol(component_name(b->component_key), b->field_name),
                pin->id
            );
            return;
        case PIN_BINDING_EVENT_OUTPUT: return;
    }
}

static void add_node(const site *s)
{
    const node *n = s->node;
    if (!strcmp(n->desc->name, "Model Instance") && n->reference_asset &&
        n->reference_asset[0]) {
        int asset = reference_index_named_asset(s->index, REFERENCE_ASSET_SHAPE, n->reference_asset);
        add_at(s, REFERENCE_CALL, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset }, 0);
        for (int i = 0; i < n->input_count; i++) {
            const pin_spec *pin = &n->input_pins[i];
            if (pin->declaration_id) {
                add_at(
                    s,
                    REFERENCE_ARGUMENT,
                    (reference_symbol){ .kind = REFERENCE_SYMBOL_PARAMETER,
                                        .asset = asset,
                                        .id = pin->declaration_id },
                    pin->id
                );
            }
        }
        for (int i = 1; i < n->output_count; i++) {
            const pin_spec *pin = &n->output_pins[i];
            if (pin->declaration_id) {
                add_at(
                    s,
                    REFERENCE_ARGUMENT,
                    (reference_symbol){ .kind = REFERENCE_SYMBOL_PARAMETER,
                                        .asset = asset,
                                        .id = pin->declaration_id },
                    pin->id
                );
            }
        }
    } else if (!strcmp(n->desc->name, "Model Material Slot") && n->reference_asset && n->reference_asset[0]) {
        int asset = reference_index_named_asset(s->index, REFERENCE_ASSET_MATERIAL, n->reference_asset);
        add_at(s, REFERENCE_CALL, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset }, 0);
    } else if (graph_call(n)) {
        add_call(s);
    }
    if (!strcmp(n->desc->name, "Model Parameter") ||
        !strcmp(n->desc->name, "Model Material Slot") ||
        !strcmp(n->desc->name, "Model Anchor")) {
        add_definition(
            s,
            own_symbol(s, REFERENCE_SYMBOL_PARAMETER, 0, n->id),
            n->reference_name ? n->reference_name : "",
            0
        );
    }
    if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        reference_role role = n->desc == &NODE_SetVariable ? REFERENCE_WRITE
                                                           : REFERENCE_READ;
        add_at(s, role, own_symbol(s, REFERENCE_SYMBOL_VARIABLE, 0, n->reference_id), 0);
    }
    add_event(s);
    add_spawn(s);
    add_component_access(s);
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        if (pin->kind == PIN_DATA && pin->binding) {
            add_binding(s, pin);
        }
        if (pin->kind == PIN_DATA && pin->type == PIN_STR && pin->type_name &&
            !strcmp(pin->type_name, "MaterialAsset") &&
            reference_text(pin->default_value)) {
            int asset = reference_index_named_asset(
                s->index,
                REFERENCE_ASSET_MATERIAL,
                reference_text(pin->default_value)
            );
            add_at(
                s,
                REFERENCE_BIND,
                (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset },
                pin->id
            );
        }
        if (pin->kind == PIN_DATA && pin->type == PIN_STRUCT) {
            add_struct_default(s, pin);
        }
    }
}

/* A function's or macro's Entry outputs and Return inputs declare its
 * parameters and results. */
static void add_signature(site *s, uint64_t node_id, bool output)
{
    s->node = graph_node_by_id(s->graph, node_id);
    const node *n = s->node;
    for (int i = 0; n && i < (output ? n->output_count : n->input_count); i++) {
        const pin_spec *pin = output ? &n->output_pins[i] : &n->input_pins[i];
        if (graph_signature_declared(s->graph, pin)) {
            add_definition(
                s,
                own_symbol(s, REFERENCE_SYMBOL_PARAMETER, s->graph->graph_id, pin->id),
                pin->name,
                pin->id
            );
        }
    }
}

/* Wires from Entry read its parameters. */
static void add_parameter_reads(site *s)
{
    const graph *g = s->graph;
    const node *entry = graph_node_by_id(g, g->entry_id);
    for (int i = 0; entry && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        const pin_spec *pin = e->kind == GRAPH_EDGE_DATA && e->from_id == g->entry_id
                                ? output_by_id(entry, e->from_pin_id)
                                : NULL;
        s->node = graph_node_by_id(g, e->to_id);
        if (pin && s->node && graph_signature_declared(g, pin)) {
            add_at(
                s,
                REFERENCE_READ,
                own_symbol(s, REFERENCE_SYMBOL_PARAMETER, g->graph_id, pin->id),
                e->to_pin_id
            );
        }
    }
}

/* The node a wire leads to from `from` in a run: an exec wire's target, or a
 * pure node feeding it data. 0 for neither. */
static uint64_t run_step(const graph *g, const edge *e, uint64_t from)
{
    if (e->kind == GRAPH_EDGE_EXEC) {
        return e->from_id == from ? e->to_id : 0;
    }
    const node *feed = e->to_id == from ? graph_node_by_id(g, e->from_id) : NULL;
    return feed && node_behavior_for(feed) == NODE_BEHAVIOR_PURE ? feed->id : 0;
}

static void add_run(reference_index *index, int source, const graph *g, const node *event)
{
    uint64_t *nodes = NULL;
    apush(nodes, event->id);
    for (int at = 0; at < acount(nodes); at++) {
        uint64_t from = nodes[at];
        for (int i = 0; i < acount(g->edges); i++) {
            uint64_t next = run_step(g, &g->edges[i], from);
            if (next && !reference_run_contains(nodes, next)) {
                apush(nodes, next);
            }
        }
    }
    reference_index_add_reachability(index, source, g->graph_id, event->id, nodes, acount(nodes));
    afree(nodes);
}

static void add_graph(reference_index *index, int source, const graph *g)
{
    site s = { index, source, g, NULL };
    if (g->kind != GRAPH_EVENT) {
        s.node = graph_node_by_id(g, g->entry_id);
        add_definition(&s, own_symbol(&s, REFERENCE_SYMBOL_GRAPH, 0, g->graph_id), g->name, 0);
        add_signature(&s, g->entry_id, true);
        add_signature(&s, g->return_id, false);
        add_parameter_reads(&s);
    }
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        s.node = graph_node_by_id(g, local->source_id);
        add_definition(
            &s,
            own_symbol(&s, REFERENCE_SYMBOL_LOCAL, g->graph_id, local->id),
            local->name,
            local->source_pin_id
        );
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        s.node = g->nodes[i];
        if (s.node && s.node->desc) {
            add_node(&s);
        }
        if (s.node && s.node->desc && !strcmp(s.node->desc->name, "Event")) {
            add_run(index, source, g, s.node);
        }
    }
}

void reference_index_add_graphs(reference_index *index, int source, const behavior_asset *asset)
{
    site s = { index, source, NULL, NULL };
    for (int i = 0; i < acount(asset->variables); i++) {
        add_definition(
            &s,
            own_symbol(&s, REFERENCE_SYMBOL_VARIABLE, 0, asset->variables[i].id),
            asset->variables[i].name,
            0
        );
    }
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        const char *name = reference_index_asset_at(index, source)->name;
        add_at(
            &s,
            REFERENCE_DEFINE,
            (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT, .asset = source, .name = name },
            0
        );
    }
    /* Expression graphs are generated from their source text. */
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i] && !asset->graphs[i]->math_expression) {
            add_graph(index, source, asset->graphs[i]);
        }
    }
}

/* ---------- Prototypes and scene objects ---------- */

/* An override's components: the object itself, or its components. */
static av_value *override_components(av_value *root)
{
    av_value *components = av_obj_get(root, "components");
    return components ? components : root;
}

/* An override that sets the field, or removes its component. */
static bool overridden(av_value *components, const char *component, const char *field)
{
    av_value *value = av_obj_get(components, component);
    return av_is_null(value) || av_obj_get(value, field);
}

static void add_prototype_fields(
    reference_index *index,
    reference at,
    const prototype *prototype,
    av_value *overrides
)
{
    arena storage = { 0 };
    pin_value value;
    for (int i = 0; i < reference_named_field_count(); i++) {
        if (!overridden(
                overrides,
                reference_named_field_at(i).component,
                reference_named_field_at(i).field
            ) &&
            prototype_read_field(
                &storage,
                prototype,
                reference_named_field_at(i).component,
                reference_named_field_at(i).field,
                &value
            ) == 0 &&
            reference_text(value)) {
            reference_add_named(index, at, i, reference_text(value));
        }
    }
    if (!overridden(overrides, MESH_PARTS, "parts") &&
        prototype_read_field(&storage, prototype, MESH_PARTS, "parts", &value) == 0 &&
        reference_text(value)) {
        add_mesh_parts(index, at, reference_text(value));
    }
    arena_free(&storage);
}

static void add_override_fields(reference_index *index, reference at, av_value *components)
{
    for (int i = 0; i < reference_named_field_count(); i++) {
        av_value *component = av_obj_get(components, reference_named_field_at(i).component);
        const char *value = av_get_str(
            av_obj_get(component, reference_named_field_at(i).field)
        );
        if (value && value[0]) {
            reference_add_named(index, at, i, value);
        }
    }
    const char *parts = av_get_str(av_obj_get(av_obj_get(components, MESH_PARTS), "parts"));
    if (parts && parts[0]) {
        add_mesh_parts(index, at, parts);
    }
}

static void add_behavior(reference_index *index, reference at, const char *behavior)
{
    at.role = REFERENCE_ATTACH;
    at.symbol = reference_asset_symbol(
        reference_index_named_asset(index, REFERENCE_ASSET_BEHAVIOR, behavior)
    );
    reference_index_add_reference(index, at);
}

void reference_index_add_prototype(reference_index *index, int source, const prototype *prototype)
{
    reference at = { .source = source };
    reference use = at;
    use.role = REFERENCE_USE;
    for (int i = 0; i < acount(prototype->includes); i++) {
        use.symbol = reference_asset_symbol(reference_index_named_asset(
            index,
            REFERENCE_ASSET_PROTOTYPE,
            prototype->includes[i]
        ));
        reference_index_add_reference(index, use);
    }
    for (int i = 0; i < acount(prototype->graphs); i++) {
        add_behavior(index, at, prototype->graphs[i]);
    }
    add_prototype_fields(index, at, prototype, NULL);
    const scene_document *children = prototype->children;
    for (int i = 0; children && i < acount(children->entities); i++) {
        const scene_entity *child = &children->entities[i];
        char *path = scene_document_entity_path(children, i);
        reference_object object = {
            .name = path,
            .prototype = child->prototype,
            .overrides = child->components,
            .behaviors = (const char *const *)child->behaviors,
            .behavior_count = acount(child->behaviors),
        };
        reference_index_add_object(index, source, &object);
        free(path);
    }
}

void reference_index_add_object(reference_index *index, int source, const reference_object *object)
{
    reference at = { .source = source, .object = object->name };
    reference use = at;
    use.role = REFERENCE_USE;
    use.symbol = reference_asset_symbol(
        reference_index_named_asset(index, REFERENCE_ASSET_PROTOTYPE, object->prototype)
    );
    reference_index_add_reference(index, use);
    for (int i = 0; i < object->behavior_count; i++) {
        add_behavior(index, at, object->behaviors[i]);
    }
    av_value *components = override_components(object->overrides);
    if (object->base) {
        add_prototype_fields(index, at, object->base, components);
    }
    add_override_fields(index, at, components);
}

void reference_index_add_ui(reference_index *index, int source, const char *rml)
{
    reference at = { .source = source };
    char **names = NULL;
    ui_document_use_names(rml, &names);
    at.role = REFERENCE_USE;
    for (int i = 0; i < acount(names); i++) {
        at.symbol = reference_asset_symbol(
            reference_index_named_asset(index, REFERENCE_ASSET_UI, names[i])
        );
        reference_index_add_reference(index, at);
        free(names[i]);
    }
    afree(names);
    ui_document_event *events = NULL;
    ui_document_events(rml, &events);
    at.role = REFERENCE_SEND;
    for (int i = 0; i < acount(events); i++) {
        at.symbol = reference_event_symbol(index, events[i].name);
        reference_index_add_reference(index, at);
    }
    ui_document_events_free(events);
}
