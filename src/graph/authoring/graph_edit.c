#include "graph/authoring/graph_edit.h"

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"
#include "graph/model/graph_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/model/value_copy.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/arena.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int graph_numeric_node(const node *n)
{
    const node_desc *d = n->desc;
    return d == &NODE_Add || d == &NODE_Subtract || d == &NODE_Multiply ||
           d == &NODE_Divide || d == &NODE_Min || d == &NODE_Max || d == &NODE_Clamp ||
           d == &NODE_Modulo || d == &NODE_Negate || d == &NODE_Abs;
}
static const node_desc *graph_numeric_unwrap(const pin_spec *pin)
{
    if (pin->type != PIN_STRUCT || pin->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    const type_desc *type = type_catalog_find(pin->type_name);
    if (!type || type->field_count != 1) {
        return NULL;
    }
    const type_field *field = &type->fields[0];
    pin_spec value = { .type = field->type,
                       .type_name = field->type_name,
                       .kind = PIN_DATA,
                       .container = PIN_CONTAINER_SINGLE };
    return conversion_for_pins(pin, &value);
}
int graph_connect_data_conversion(graph *g, node *from, int from_pin, node *to, int to_pin, const node_desc *conversion)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, from) ||
        !graph_contains_node(g, to)) {
        return -1;
    }
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count) {
        return -1;
    }
    const pin_spec *out = &from->output_pins[from_pin];
    const pin_spec *in = &to->input_pins[to_pin];
    if ((out->flags | in->flags) & PIN_FLAG_SETTING) {
        return -1;
    }
    if (in->flags & PIN_FLAG_REFERENCE) {
        pin_binding binding = { 0 };
        if (conversion || graph_resolve_reference(g, from, from_pin, &binding) != 0) {
            return -1;
        }
    }
    /* Scalar shader inputs broadcast into an already authored vector group.
     * Replaying its scalar driver must not erase the other vector literals. */
    if (!conversion && (in->flags & PIN_FLAG_WILDCARD) && to->desc->category &&
        !strcmp(to->desc->category, "Shader") && in->type == PIN_STRUCT && in->type_name &&
        !strncmp(in->type_name, "ShaderVec", 9) && out->type != PIN_STRUCT) {
        const node_desc *splat = conversion_for_pins(out, in);
        if (splat && conversion_is_vector_splat(splat)) {
            conversion = splat;
        }
    }
    /* A generated expression's concrete Result is its callee's return ABI.
       A consumer coerces that value; only an explicit pin edit changes it. */
    bool expression_result = from->desc == &NODE_BehaviorExpression &&
                             out->container == PIN_CONTAINER_SINGLE &&
                             out->type >= PIN_I8 && out->type <= PIN_F64;
    if (!conversion && expression_result && !(in->flags & PIN_FLAG_WILDCARD) &&
        in->container == PIN_CONTAINER_SINGLE && in->type >= PIN_I8 &&
        in->type <= PIN_F64 && !pin_compatible(out, in)) {
        conversion = conversion_for_pins(out, in);
    }
    int numeric_wildcard = (in->flags & PIN_FLAG_WILDCARD) && graph_numeric_node(to);
    if (!conversion && numeric_wildcard) {
        conversion = graph_numeric_unwrap(out);
    }
    const pin_spec *target = conversion && numeric_wildcard &&
                                     conversion->output_count == 1
                               ? &conversion->outputs[0]
                               : in;
    if (out->kind != PIN_DATA || in->kind != PIN_DATA ||
        (conversion && !conversion_matches_pins(out, target, conversion))) {
        return -1;
    }
    /* Compatible storage and nominal aliases already carry the requested value.
       Preserve the producer's authored type instead of inferring it backwards. */
    if (!conversion && !expression_result &&
        (out->type != in->type || !pin_compatible(out, in)) &&
        (out->flags & PIN_FLAG_WILDCARD) && !(in->flags & PIN_FLAG_WILDCARD)) {
        graph_wildcard_resolve_group(g, from, out->group, in->type, in->type_name);
        out = &from->output_pins[from_pin];
    }
    if (in->flags & PIN_FLAG_WILDCARD) {
        if (conversion && !numeric_wildcard && !conversion_is_vector_splat(conversion)) {
            return -1;
        }
        const pin_spec *value = conversion ? &conversion->outputs[0] : out;
        char *owned = value->type_name
                        ? graph_own_str(g, value->type_name, strlen(value->type_name))
                        : NULL;
        if (value->type_name && !owned) {
            return -1;
        }
        pin_spec *p = &to->input_pins[to_pin];
        p->container = value->container;
        /* An ungrouped wildcard resolves alone. */
        if (!p->group) {
            graph_wildcard_resolve_pin(g, p, value->type, owned);
        }
        p->type = value->type;
        p->type_name = owned;
        graph_wildcard_resolve_group(g, to, p->group, value->type, value->type_name);
        in = &to->input_pins[to_pin];
    }
    if (!conversion && !pin_compatible(out, in)) {
        return -1;
    }
    /* Reject a conflicting edge before changing the input's binding. */
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *old = &g->edges[i];
        if (old->to_id == to->id && old->to_pin_id == in->id &&
            old->kind != GRAPH_EDGE_DATA) {
            return -1;
        }
    }
    const graph_local *local = graph_input_local(g, to, to_pin);
    node *previous = NULL;
    int previous_pin = -1;
    if (local && graph_input_source(g, to, to_pin, &previous, &previous_pin) &&
        (local->source_id != from->id || local->source_pin_id != out->id)) {
        to->input_pins[to_pin].binding = NULL;
        graph_prune_locals(g);
    }
    if (in->flags & PIN_FLAG_REFERENCE) {
        to->input_pins[to_pin].binding = NULL;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge *old = &g->edges[i];
        if (old->to_id == to->id && old->to_pin_id == in->id) {
            *old = graph_make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0, conversion);
            graph_changed(g);
            return 0;
        }
    }
    apush(g->edges, graph_make_edge(from, from_pin, to, to_pin, GRAPH_EDGE_DATA, 0, conversion));
    graph_changed(g);
    return 0;
}
int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    return graph_connect_data_conversion(g, from, from_pin, to, to_pin, NULL);
}
int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin)
{
    return graph_connect_data(g, from, from_pin, to, to_pin);
}
void graph_inline_tree(const graph *g, const node *n, uint64_t **ids)
{
    apush(*ids, n->id);
    for (int pin = 0; pin < n->input_count; pin++) {
        node *source = NULL;
        int source_pin = -1;
        if (graph_inline_source(g, n, pin, &source, &source_pin)) {
            graph_inline_tree(g, source, ids);
        }
    }
}
int graph_remove_node_tree(graph *g, node *n)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    uint64_t *ids = NULL;
    graph_inline_tree(g, n, &ids);
    for (int i = 0; i < acount(ids); i++) {
        int removed = graph_remove_node(g, graph_node_by_id(g, ids[i]));
        assert(removed == 0);
        (void)removed;
    }
    afree(ids);
    return 0;
}
const node_desc *graph_vector_constructor(const pin_spec *pin)
{
    if (!pin_vector_width(pin)) {
        return NULL;
    }
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        if (graph_vector_descriptor(desc) && pin_compatible(&desc->outputs[0], pin)) {
            return desc;
        }
    }
    return NULL;
}
/* A component node's Component or Type setting names what it reads, which its
 * chip shows, so it does not keep the node expanded once chosen. */
static bool component_setting(const node *n, int pin)
{
    component_node_kind kind = component_node_kind_of(n->desc);
    return kind && pin == component_node_setting(n) && component_node_key(n)[0];
}
static bool inline_input(const graph *g, const node *n, int pin)
{
    const pin_spec *spec = &n->input_pins[pin];
    if (spec->kind != PIN_DATA || spec->type == PIN_FLOW) {
        return false;
    }
    if (spec->flags & PIN_FLAG_SETTING) {
        return component_setting(n, pin);
    }
    if (graph_input_edge(g, n, pin) || spec->binding ||
        (spec->flags & (PIN_FLAG_DEFAULT_SELF | PIN_FLAG_SERVICE))) {
        return true;
    }
    return spec->type == PIN_STRUCT ? spec->default_value.ptr != NULL
         : spec->type == PIN_HANDLE ? spec->default_value.u64 != 0
                                    : true;
}
/* The single outgoing wire of a node that may fold into its consumer,
 * whatever its presentation. */
static const edge *inline_edge(const graph *g, const node *n)
{
    /* A flow node such as a pure function's Function Entry has no exec pins
     * but anchors its graph. Source presentation calls encode effects with
     * exec pins, so a read-only call with one result can fold like a formula.
     * Executable function nodes retain their existing presentation. */
    if (n->disabled || n->desc->flow_kind != NODE_FLOW_NONE ||
        node_behavior_for(n) != NODE_BEHAVIOR_PURE ||
        (n->desc->pure_kind == NODE_PURE_FUNCTION &&
         (!n->desc->metadata_only || n->output_count != 1)) ||
        n->desc->pure_kind == NODE_PURE_COPY) {
        return NULL;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (!inline_input(g, n, p)) {
            return NULL;
        }
    }
    for (int p = 0; p < n->output_count; p++) {
        if ((n->output_pins[p].flags & PIN_FLAG_SETTING) ||
            graph_output_local(g, n, p)) {
            return NULL;
        }
    }
    graph_edge_index *index = graph_edges_index(g);
    int slot = index && n->id < (uint64_t)acount(index->sources)
                 ? index->sources[n->id]
                 : 0;
    const edge *outgoing = slot > 0 ? &g->edges[slot - 1] : NULL;
    if (!outgoing || outgoing->kind != GRAPH_EDGE_DATA || outgoing->to_id == n->id) {
        return NULL;
    }
    node *target = graph_node_by_id(g, outgoing->to_id);
    int is_output = 0, is_input_output = 0;
    const pin_spec *source_pin = graph_pin_by_id(n, outgoing->from_pin_id, &is_output, NULL);
    const pin_spec *target_pin = target
                                   ? graph_pin_by_id(target, outgoing->to_pin_id, &is_input_output, NULL)
                                   : NULL;
    if (!source_pin || !target_pin || !is_output || is_input_output ||
        target_pin->kind != PIN_DATA || (target_pin->flags & PIN_FLAG_SETTING)) {
        return NULL;
    }
    return outgoing;
}
/* Watches, breakpoints and comments draw on the node's own box. */
static bool annotated(const node *n)
{
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
            if (pins[p].flags & PIN_FLAG_WATCH) {
                return true;
            }
        }
    }
    return n->breakpoint || n->comment;
}
/* Nodes in the inline tree rooted at n, or 0 when n draws its own box.
 * `forced` is treated as explicitly inline. */
static int inline_size(const graph *g, const node *n, const node *forced, int remaining)
{
    node_presentation presentation = n == forced ? NODE_PRESENTATION_INLINE
                                                 : n->presentation;
    if (!remaining || presentation == NODE_PRESENTATION_EXPANDED ||
        !inline_edge(g, n)) {
        return 0;
    }
    bool automatic = presentation == NODE_PRESENTATION_AUTO;
    if ((automatic && g->math_expression) ||
        ((automatic || n->desc->pure_kind == NODE_PURE_FUNCTION) && annotated(n))) {
        return 0;
    }
    int size = 1;
    for (int p = 0; p < n->input_count; p++) {
        /* A local is a named value, like a binding. */
        const edge *e = graph_input_edge(g, n, p);
        if (!e || graph_input_local(g, n, p)) {
            continue;
        }
        int source = inline_size(g, graph_node_by_id(g, e->from_id), forced, remaining - 1);
        if (!source) {
            return 0;
        }
        size += source;
        if (automatic && size > GRAPH_INLINE_AUTO_NODES) {
            return 0;
        }
    }
    return size;
}
static bool node_indexed(const graph *g, const node *n)
{
    return g && n && n->id < (uint64_t)acount(g->id_table) && g->id_table[n->id] == n;
}
bool graph_node_is_inline(const graph *g, const node *n)
{
    return node_indexed(g, n) && inline_size(g, n, NULL, acount(g->nodes)) > 0;
}
bool graph_node_can_inline(const graph *g, const node *n)
{
    return node_indexed(g, n) && inline_size(g, n, n, acount(g->nodes)) > 0;
}
node *graph_inline_owner(const graph *g, node *n)
{
    while (graph_node_is_inline(g, n)) {
        n = graph_node_by_id(g, inline_edge(g, n)->to_id);
    }
    return n;
}
int graph_inline_source(const graph *g, const node *target, int pin, node **src, int *src_pin)
{
    if (!node_indexed(g, target) || !src || !src_pin || pin < 0 ||
        pin >= target->input_count || target->input_pins[pin].kind != PIN_DATA) {
        return 0;
    }
    const edge *e = graph_input_edge(g, target, pin);
    node *source = e ? graph_edge_source(g, e, src_pin) : NULL;
    if (!source || !graph_node_is_inline(g, source)) {
        return 0;
    }
    *src = source;
    return 1;
}
static int graph_append_reference_field(graph *g, const pin_binding *base, const char *field, pin_binding *out)
{
    if (!g || !base || !field || !field[0] || !out ||
        (base->kind != PIN_BINDING_COMPONENT &&
         base->kind != PIN_BINDING_COMPONENT_FIELD)) {
        return -1;
    }
    char path[256];
    int written = snprintf(
        path,
        sizeof path,
        "%s%s%s",
        base->field_name ? base->field_name : "",
        base->field_name ? "." : "",
        field
    );
    if (written < 0 || (size_t)written >= sizeof path) {
        return -1;
    }
    char *owned = graph_own_str(g, path, (size_t)written);
    if (!owned) {
        return -1;
    }
    *out = *base;
    out->kind = PIN_BINDING_COMPONENT_FIELD;
    out->field_name = owned;
    return 0;
}
int graph_resolve_reference(graph *g, const node *source, int source_pin, pin_binding *binding)
{
    if (!g || !source || !binding || source_pin < 0 ||
        source_pin >= source->output_count || !graph_contains_node(g, source)) {
        return -1;
    }
    const pin_spec *pin = &source->output_pins[source_pin];
    if (pin->kind != PIN_DATA || !(pin->flags & PIN_FLAG_REFERENCE)) {
        return -1;
    }
    if (pin->field_source && !pin->split_parent) {
        int parent_pin = -1;
        const type_field *field = graph_output_field(source, source_pin, &parent_pin);
        if (!field || parent_pin == source_pin) {
            return -1;
        }
        pin_binding base = { 0 };
        if (graph_resolve_reference(g, source, parent_pin, &base) != 0) {
            return -1;
        }
        return graph_append_reference_field(g, &base, field->name, binding);
    }
    if (pin->split_parent && pin->split_type_name && pin->name &&
        source->desc->component_kind == NODE_COMPONENT_GET_SELF &&
        source->desc->component && source->desc->component->key) {
        const type_desc *type = type_catalog_find(pin->split_type_name);
        const type_field *field = type ? pin_type_field_path(type, pin->name) : NULL;
        if (!field || field->type != pin->type ||
            strcmp(field->type_name ? field->type_name : "", pin->type_name ? pin->type_name : "")) {
            return -1;
        }
        pin_binding base = { 0 };
        if (pin->field_source) {
            int parent_pin = -1;
            for (int i = 0; i < source->output_count; i++) {
                if (source->output_pins[i].id == pin->field_source) {
                    parent_pin = i;
                    break;
                }
            }
            if (parent_pin < 0 ||
                graph_resolve_reference(g, source, parent_pin, &base) != 0) {
                return -1;
            }
            const pin_spec *parent = &source->output_pins[parent_pin];
            if (!parent->field_source && pin->field_name) {
                pin_binding projected = { 0 };
                if (graph_append_reference_field(g, &base, pin->field_name, &projected) !=
                    0) {
                    return -1;
                }
                base = projected;
            }
        } else {
            base.kind = PIN_BINDING_COMPONENT;
            base.component_key = source->desc->component->key;
        }
        return graph_append_reference_field(g, &base, field->name, binding);
    }
    memset(binding, 0, sizeof *binding);
    if (source->desc->pure_kind == NODE_PURE_GET) {
        if (!source->reference_id) {
            return -1;
        }
        binding->kind = PIN_BINDING_VARIABLE;
        binding->variable_id = source->reference_id;
        binding->source_asset = g->owner ? g->owner->name : NULL;
        return 0;
    }
    if (source->desc->component_kind == NODE_COMPONENT_GET_SELF &&
        source->desc->component && source->desc->component->key &&
        source->desc->component->key[0]) {
        binding->kind = PIN_BINDING_COMPONENT;
        binding->component_key = source->desc->component->key;
        return 0;
    }
    const node_desc *getter = component_node_getter(g, source);
    if (getter && getter != source->desc) {
        binding->kind = PIN_BINDING_COMPONENT;
        binding->component_key = getter->component->key;
        return 0;
    }
    return -1;
}
int graph_set_pin_default(graph *g, node *n, int output, int index, const pin_value *value)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) || !value) {
        return -1;
    }
    pin_spec *p = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || p[index].kind != PIN_DATA) {
        return -1;
    }
    component_node_kind component = component_node_kind_of(n->desc);
    if (!output && component && index == component_node_setting(n)) {
        if (value->type != PIN_NAME || component_node_retarget(g, n, value->str)) {
            return -1;
        }
        graph_changed(g);
        return 0;
    }
    if ((p[index].flags & PIN_FLAG_WILDCARD) && p[index].type == PIN_FLOW) {
        if (graph_set_pin_type(g, n, output, index, value->type, value->type_name) != 0) {
            return -1;
        }
        p = output ? n->output_pins : n->input_pins;
    }
    if (value->type != p[index].type) {
        return -1;
    }
    pin_value owned = *value;
    owned.container = p[index].container;
    owned.type_name = p[index].type_name;
    pin_value source = owned;
    if (owned.container == PIN_CONTAINER_SINGLE && owned.type == PIN_STRUCT &&
        owned.ptr && pin_value_copy_to_graph(g, &owned, &source)) {
        return -1;
    }
    p[index].default_value = owned;
    p[index].default_value.container = p[index].container;
    p[index].default_value.type_name = p[index].type_name;
    if (p[index].container >= PIN_CONTAINER_ARRAY && value->ptr) {
        pin_value array = p[index].default_value;
        if (pin_value_copy_to_graph(g, &p[index].default_value, &array)) {
            return -1;
        }
    }
    if (p[index].container == PIN_CONTAINER_SINGLE &&
        (value->type == PIN_STR || value->type == PIN_NAME)) {
        if (pin_value_copy_to_graph(g, &p[index].default_value, value)) {
            return -1;
        }
    }
    p[index].default_value.type_name = p[index].type_name;
    graph_changed(g);
    return 0;
}
int graph_inline_node(graph *g, node *n)
{
    if (!graph_node_can_inline(g, n) ||
        graph_set_node_presentation(g, n, NODE_PRESENTATION_AUTO) != 0) {
        return -1;
    }
    return graph_node_is_inline(g, n)
             ? 0
             : graph_set_node_presentation(g, n, NODE_PRESENTATION_INLINE);
}
static const type_desc *graph_find_type(const char *name)
{
    return type_catalog_find(name);
}
const type_desc *graph_output_struct(const node *n, int index)
{
    if (!n || index < 0 || index >= n->output_count) {
        return NULL;
    }
    const node_desc *desc = n->desc;
    bool signature = desc == &NODE_Event || desc == &NODE_FunctionEntry ||
                     desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
                     desc == &NODE_BehaviorExpression || desc == &NODE_CallMacro;
    if (!signature && index >= desc->output_count && !n->output_pins[index].field_source) {
        return NULL;
    }
    const pin_spec *pin = &n->output_pins[index];
    if (pin->kind != PIN_DATA || pin->type != PIN_STRUCT ||
        pin->container != PIN_CONTAINER_SINGLE || pin->split_parent) {
        return NULL;
    }
    return pin->type_name ? type_catalog_find(pin->type_name) : NULL;
}
const type_field *graph_output_field(const node *n, int index, int *source_index)
{
    if (!n || index < 0 || index >= n->output_count) {
        return NULL;
    }
    const pin_spec *pin = &n->output_pins[index];
    if (!pin->field_source || !pin->field_name || !pin->field_name[0] ||
        pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE ||
        pin->split_parent) {
        return NULL;
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].id != pin->field_source) {
            continue;
        }
        const type_desc *type = graph_output_struct(n, i);
        if (!type) {
            return NULL;
        }
        for (int f = 0; f < type->field_count; f++) {
            const type_field *field = &type->fields[f];
            if (strcmp(field->name, pin->field_name) != 0) {
                continue;
            }
            if (pin->type != field->type || strcmp(
                                                pin->type_name ? pin->type_name : "",
                                                field->type_name ? field->type_name : ""
                                            ) != 0) {
                return NULL;
            }
            if (source_index) {
                *source_index = i;
            }
            return field;
        }
    }
    return NULL;
}
int graph_expose_output_field(graph *g, node *n, int index, const char *name)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) || !name) {
        return -1;
    }
    const type_desc *type = graph_output_struct(n, index);
    if (!type) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (strcmp(field->name, name)) {
            continue;
        }
        uint64_t source = n->output_pins[index].id;
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if (pin->field_source == source && pin->field_name &&
                !strcmp(pin->field_name, name)) {
                return p;
            }
        }
        pin_spec pin = graph_field_pin(n, index, field);
        return graph_add_pin(g, n, true, &pin);
    }
    return -1;
}
int graph_expose_output_fields(graph *g, node *n, int index)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    const type_desc *type = graph_output_struct(n, index);
    if (!type || !type->field_count) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        if (graph_expose_output_field(g, n, index, type->fields[i].name) < 0) {
            return -1;
        }
    }
    return 0;
}
static bool graph_output_has_children(const node *n, uint64_t source)
{
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].field_source == source) {
            return true;
        }
    }
    return false;
}
int graph_hide_output_fields(graph *g, node *n, int index)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n) ||
        !graph_output_struct(n, index)) {
        return -1;
    }
    uint64_t source = n->output_pins[index].id;
    bool removed;
    do {
        removed = false;
        for (int i = n->output_count - 1; i >= 0; i--) {
            pin_spec *pin = &n->output_pins[i];
            if (pin->field_source != source || graph_output_has_children(n, pin->id) ||
                graph_output_local(g, n, i)) {
                continue;
            }
            bool connected = false;
            for (int e = 0; e < acount(g->edges); e++) {
                connected |= g->edges[e].from_id == n->id &&
                             g->edges[e].from_pin_id == pin->id;
            }
            if (connected) {
                continue;
            }
            if (graph_remove_pin(g, n, 1, i)) {
                return -1;
            }
            removed = true;
            break;
        }
    } while (removed);
    return 0;
}
int graph_split_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    /* Component nodes split into their field pins: Set writes each field it
     * shows, and Get exposes fields beside Value. */
    component_node_kind component = component_node_kind_of(n->desc);
    if (component) {
        if (index < 0 || index >= (output ? n->output_count : n->input_count)) {
            return -1;
        }
        const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
        if (pin->type != PIN_STRUCT || pin->field_source) {
            return -1;
        }
        int result = output ? graph_expose_output_fields(g, n, index)
                            : component_node_split_value(g, n, true);
        return result ? -1 : index;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || pins[index].kind != PIN_DATA ||
        pins[index].split_parent) {
        return -1;
    }
    const pin_spec parent = pins[index];
    const type_desc *type = parent.type == PIN_STRUCT
                              ? graph_find_type(parent.type_name)
                              : NULL;
    if (!type || !type->field_count) {
        return -1;
    }
    uint64_t parent_id = parent.id;
    int first = index;
    if (graph_remove_pin(g, n, output, index)) {
        return -1;
    }
    for (int i = 0; i < type->field_count; i++) {
        pin_spec child = {
            .name = type->fields[i].name,
            .type = type->fields[i].type,
            .type_name = type->fields[i].type_name,
            .kind = PIN_DATA,
            .container = PIN_CONTAINER_SINGLE,
            .split_parent = parent_id,
            .field_source = parent.field_source,
            .field_name = parent.field_name,
            .split_type_name = type->name,
            .flags = (parent.flags & PIN_FLAG_REFERENCE) |
                     (type->fields[i].color ? PIN_FLAG_COLOR : 0),
        };
        int at = graph_add_pin(g, n, output, &child);
        if (at < 0) {
            return -1;
        }
        if (output) {
            n->output_pins[at].split_parent = parent_id;
        } else {
            n->input_pins[at].split_parent = parent_id;
        }
    }
    int last = output ? n->output_count - 1 : n->input_count - 1;
    for (int i = last; i >= first; i--) {
        graph_move_pin(g, n, output, i, first);
    }
    return first;
}
static const type_desc *graph_split_type(const pin_spec *pin)
{
    for (int i = 0; i < type_catalog_count(); i++) {
        const type_desc *type = type_catalog_at(i);
        for (int field = 0; field < type->field_count; field++) {
            if (strcmp(type->fields[field].name, pin->name ? pin->name : "") == 0 &&
                type->fields[field].type == pin->type &&
                strcmp(
                    type->fields[field].type_name ? type->fields[field].type_name : "",
                    pin->type_name ? pin->type_name : ""
                ) == 0) {
                return type;
            }
        }
    }
    return NULL;
}
int graph_recombine_pin(graph *g, node *n, int output, int index)
{
    if (!graph_mutation_allowed(g) || !graph_contains_node(g, n)) {
        return -1;
    }
    component_node_kind component = component_node_kind_of(n->desc);
    if (component) {
        /* Set's field inputs, and Value, follow Exec, Component and Entity. */
        int first = component_node_setting(n) + 2;
        return !output && component_node_split(n) && index >= first &&
                       index < n->input_count &&
                       !component_node_split_value(g, n, false)
                 ? first
                 : -1;
    }
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    if (index < 0 || index >= count || !pins[index].split_parent) {
        return -1;
    }
    uint64_t parent_id = pins[index].split_parent;
    uint64_t field_source = pins[index].field_source;
    const char *field_name = pins[index].field_name;
    const char *parent_name = pins[index].field_source ? pins[index].field_name : NULL;
    const type_desc *type = pins[index].split_type_name
                              ? graph_find_type(pins[index].split_type_name)
                              : graph_split_type(&pins[index]);
    if (!type) {
        return -1;
    }
    int first = index;
    while (first > 0 && pins[first - 1].split_parent == parent_id) {
        first--;
    }
    bool reference = false;
    for (int i = first; i < count; i++) {
        if (pins[i].split_parent == parent_id && (pins[i].flags & PIN_FLAG_REFERENCE)) {
            reference = true;
            break;
        }
    }
    for (int i = count - 1; i >= 0; i--) {
        pins = output ? n->output_pins : n->input_pins;
        if (pins[i].split_parent == parent_id && graph_remove_pin(g, n, output, i)) {
            return -1;
        }
    }
    pin_spec parent = {
        .name = parent_name ? parent_name : type->name,
        .type = PIN_STRUCT,
        .type_name = type->name,
        .kind = PIN_DATA,
        .field_source = field_source,
        .field_name = field_name,
        .flags = reference ? PIN_FLAG_REFERENCE : 0,
    };
    int at = graph_add_pin(g, n, output, &parent);
    if (at < 0) {
        return -1;
    }
    pins = output ? n->output_pins : n->input_pins;
    pins[at].id = parent_id;
    graph_move_pin(g, n, output, at, first < (output ? n->output_count : n->input_count) ? first : at);
    return first;
}
