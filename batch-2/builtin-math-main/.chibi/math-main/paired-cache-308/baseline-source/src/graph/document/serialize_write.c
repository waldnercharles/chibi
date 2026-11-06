#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/state_nodes.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/value_document.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

const pin_spec *serialize_pin_template(const node_desc *desc, int output, int index)
{
    static const pin_spec extension = { .name = "Pin", .type = PIN_FLOW, .kind = PIN_DATA };
    /* An expression call's pins mirror its generated function: wildcards
       whose groups and reference flag loading repairs from it. */
    static const pin_spec expression = { .name = "Pin",
                                         .type = PIN_FLOW,
                                         .kind = PIN_DATA,
                                         .flags = PIN_FLAG_WILDCARD };
    if (desc == &NODE_BehaviorExpression) {
        return &expression;
    }
    const pin_spec *pins = output ? desc->outputs : desc->inputs;
    int count = output ? desc->output_count : desc->input_count;
    if (index < count) {
        return &pins[index];
    }
    return count ? &pins[count - 1] : &extension;
}

static bool same_str(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

/* Strings match the template when omitted; null clears a template string. */
static void write_str_change(av_doc *doc, av_value *o, const char *key, const char *value, const char *base)
{
    if (same_str(value, base)) {
        return;
    }
    if (value) {
        av_obj_set(doc, o, key, av_str(doc, value));
        return;
    }
    av_obj_set(doc, o, key, av_null(doc));
}

bool serialize_same_value(const pin_value *a, const pin_value *b)
{
    if (a->type != b->type || a->container != b->container) {
        return false;
    }
    if (a->container >= PIN_CONTAINER_ARRAY && (!a->ptr || !b->ptr)) {
        return a->ptr == b->ptr;
    }
    if (a->container == PIN_CONTAINER_SINGLE && a->type == PIN_FLOW) {
        return true;
    }
    return pin_value_equal(a, b);
}

static const char *binding_kind_name(pin_binding_kind kind)
{
    switch (kind) {
        case PIN_BINDING_LOCAL: return "local";
        case PIN_BINDING_VARIABLE: return "variable";
        case PIN_BINDING_COMPONENT_FIELD: return "component_field";
        case PIN_BINDING_COMPONENT: return "component";
        case PIN_BINDING_EVENT_OUTPUT: return "event_output";
    }
    return "event_output";
}

/* Writes <key>: "<node>.<pin>", or the node and pin IDs when the pin's key is
 * ambiguous on its node. */
static void write_pin_ref(
    av_doc *doc,
    av_value *o,
    const char *key,
    const char *pin_key,
    const graph *g,
    uint64_t node_id,
    uint64_t pin_id,
    int output
)
{
    node *n = graph_node_by_id(g, node_id);
    int index = -1;
    char ref[SERIALIZE_KEY_SIZE + 32];
    if (n && graph_pin_by_id(n, pin_id, NULL, &index) &&
        serialize_pin_ref(n, output, index, ref, sizeof ref)) {
        av_obj_set(doc, o, key, av_str(doc, ref));
        return;
    }
    av_obj_set(doc, o, key, av_uint(doc, node_id));
    av_obj_set(doc, o, pin_key, av_uint(doc, pin_id));
}

static void write_binding(av_doc *doc, av_value *o, const graph *g, const pin_binding *b)
{
    av_value *binding = av_obj(doc);
    av_obj_set(doc, binding, "kind", av_str(doc, binding_kind_name(b->kind)));
    if (b->local_id) {
        av_obj_set(doc, binding, "local_id", av_uint(doc, b->local_id));
    }
    if (b->variable_id) {
        av_obj_set(doc, binding, "variable_id", av_uint(doc, b->variable_id));
    }
    if (b->event_node_id) {
        write_pin_ref(doc, binding, "from", "event_pin_id", g, b->event_node_id, b->event_pin_id, 1);
    }
    if (b->source_asset) {
        av_obj_set(doc, binding, "source_asset", av_str(doc, b->source_asset));
    }
    if (b->component_key) {
        av_obj_set(doc, binding, "component", av_str(doc, b->component_key));
    }
    if (b->field_name) {
        av_obj_set(doc, binding, "field", av_str(doc, b->field_name));
    }
    av_obj_set(doc, o, "binding", binding);
}

/* What loading repairs on an expression call's pin from its generated
   function: its group and reference flag. */
static pin_spec repaired_baseline(const node *n, const pin_spec *p, pin_spec base)
{
    if (n->desc != &NODE_BehaviorExpression) {
        return base;
    }
    if (p->kind == PIN_EXEC) {
        base.flags &= ~PIN_FLAG_WILDCARD;
    }
    base.group = p->group;
    if (!((p->flags ^ base.flags) & ~PIN_FLAG_REFERENCE)) {
        base.flags = p->flags;
    }
    return base;
}

/* Adds the fields where the pin differs from the pin loading starts from. */
static void write_pin_fields(av_doc *doc, av_value *o, const graph *g, const pin_spec *p, const pin_spec *base)
{
    if (p->declaration_id != base->declaration_id) {
        av_obj_set(doc, o, "declaration_id", av_uint(doc, p->declaration_id));
    }
    write_str_change(doc, o, "name", p->name, base->name);
    write_str_change(doc, o, "display_name", p->display_name, base->display_name);
    bool retyped = p->type != base->type;
    if (retyped) {
        av_obj_set(doc, o, "type", av_str(doc, serialize_pin_type_name(p->type)));
    }
    const char *type_name = retyped
                              ? serialize_implied_type_name(p->type, base->type_name)
                              : base->type_name;
    /* A primitive's type already names it; shader floats leave it unset. */
    bool implied = !p->type_name && same_str(type_name, pin_type_name(p->type));
    if (!implied) {
        write_str_change(doc, o, "type_name", p->type_name, type_name);
    }
    if (p->kind != base->kind) {
        av_obj_set(doc, o, "kind", av_str(doc, p->kind == PIN_EXEC ? "exec" : "data"));
    }
    if (p->container != base->container) {
        av_obj_set(doc, o, "container", av_sint(doc, p->container));
    }
    if (p->split_parent != base->split_parent) {
        av_obj_set(doc, o, "split_parent", av_uint(doc, p->split_parent));
    }
    write_str_change(doc, o, "split_type_name", p->split_type_name, base->split_type_name);
    if (p->field_source != base->field_source) {
        av_obj_set(doc, o, "field_source", av_uint(doc, p->field_source));
    }
    write_str_change(doc, o, "field_name", p->field_name, base->field_name);
    if (p->flags != base->flags) {
        av_obj_set(doc, o, "flags", av_uint(doc, p->flags));
    }
    if (p->group != base->group) {
        av_obj_set(doc, o, "group", av_sint(doc, p->group));
    }
    if (p->binding) {
        write_binding(doc, o, g, p->binding);
    }
    /* A pin whose type or container changed starts from that shape's zero. */
    pin_value initial = retyped || p->container != base->container
                          ? serialize_zero_value(p->type, p->container, p->type_name)
                          : graph_pin_initial_default(base);
    if (!serialize_same_value(&p->default_value, &initial)) {
        pin_value_write_document(doc, o, "default", p->type, p->type_name, &p->default_value);
        if (p->type == PIN_U32 && p->container == PIN_CONTAINER_SINGLE &&
            (p->flags & PIN_FLAG_COLOR)) {
            av_obj_set(doc, o, "default", av_color(doc, p->default_value.u));
        }
    }
}

/* A component node's template follows its Component or Type setting. */
static int keyed_template_count(const node *n, int output)
{
    return component_node_kind_of(n->desc) ? component_node_template_count(n, output)
         : output ? n->desc->output_count
                  : n->desc->input_count;
}

/* A listed side is positional with explicit IDs. It is used when pins do not
 * extend the node's template in order under unique names. */
static void write_listed_pins(
    av_doc *doc,
    av_value *o,
    const char *key,
    const graph *g,
    const node *n,
    int output
)
{
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    av_value *arr = av_arr(doc);
    for (int i = 0; i < count; i++) {
        av_value *pin = av_obj(doc);
        av_obj_set(doc, pin, "id", av_uint(doc, pins[i].id));
        /* Loading prepares a component node's template pins from its setting. */
        pin_spec base = component_node_kind_of(n->desc) &&
                                i < keyed_template_count(n, output)
                          ? component_node_template(n, output, i)
                          : *serialize_pin_template(n->desc, output, i);
        base = repaired_baseline(n, &pins[i], base);
        write_pin_fields(doc, pin, g, &pins[i], &base);
        if (av_obj_size(pin) == 1) {
            av_arr_add_uint(doc, arr, pins[i].id);
        } else {
            av_arr_append(arr, pin);
        }
    }
    av_obj_set(doc, o, key, arr);
}

/* The pin a keyed entry starts from: its template pin, the exposed field it
 * names, a component field input, or the extension template under its key. */
static bool keyed_baseline(const node *n, int output, int index, pin_spec *out)
{
    int template_count = keyed_template_count(n, output);
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    bool component = component_node_kind_of(n->desc);
    if (index < template_count) {
        *out = component ? component_node_template(n, output, index)
             : output    ? n->desc->outputs[index]
                         : n->desc->inputs[index];
        return true;
    }
    int source = -1;
    const type_field *field = output && pin->field_source
                                ? graph_output_field(n, index, &source)
                                : NULL;
    if (field) {
        *out = graph_field_pin(n, source, field);
        return true;
    }
    if (component) {
        return !output && component_node_field_pin(n, pin->name, out);
    }
    *out = *serialize_pin_template(n->desc, output, index);
    out->name = pin->name;
    return !pin->field_source && pin->name && !strchr(pin->name, '.');
}

static bool keyed_side(const node *n, int output)
{
    int template_count = keyed_template_count(n, output);
    int count = output ? n->output_count : n->input_count;
    if (count < template_count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = output ? &n->output_pins[i] : &n->input_pins[i];
        pin_spec base;
        char key[SERIALIZE_KEY_SIZE];
        serialize_pin_key(n, output, i, key, sizeof key);
        if (!keyed_baseline(n, output, i, &base) || !same_str(pin->name, base.name) ||
            serialize_pin_by_key(n, output, key) != i) {
            return false;
        }
    }
    return true;
}

static bool scalar_default(av_value *pin)
{
    av_value *value = av_obj_get(pin, "default");
    return av_obj_size(pin) == 1 && value && !av_is_ctn(value) && !av_is_null(value);
}

/* A keyed side maps pin keys to the fields that differ. Pins omitted from the
 * map keep their template, and IDs continue from the node ID unless written. */
static void write_keyed_pins(
    av_doc *doc,
    av_value *o,
    const char *key,
    const graph *g,
    const node *n,
    int output,
    uint64_t *next_id
)
{
    int template_count = keyed_template_count(n, output);
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    av_value *map = av_obj(doc);
    for (int i = 0; i < count; i++) {
        av_value *pin = av_obj(doc);
        /* Extension keys can be reordered by direct editing or source patches.
         * Their IDs, and outputs after extended inputs, must not depend on
         * the document's object-field order. */
        if (pins[i].id != *next_id || i >= template_count ||
            (output && n->input_count > keyed_template_count(n, 0))) {
            av_obj_set(doc, pin, "id", av_uint(doc, pins[i].id));
        }
        *next_id = pins[i].id + 1;
        pin_spec base;
        keyed_baseline(n, output, i, &base);
        base = repaired_baseline(n, &pins[i], base);
        write_pin_fields(doc, pin, g, &pins[i], &base);
        if (!av_obj_size(pin) && i < template_count) {
            continue;
        }
        char pin_key[SERIALIZE_KEY_SIZE];
        serialize_pin_key(n, output, i, pin_key, sizeof pin_key);
        av_value *value = scalar_default(pin) ? av_obj_get(pin, "default") : pin;
        av_obj_set(doc, map, pin_key, value);
    }
    if (av_obj_size(map)) {
        av_obj_set(doc, o, key, map);
    }
}

static void write_pins(av_doc *doc, av_value *o, const graph *g, const node *n)
{
    uint64_t next_id = n->id + 1;
    for (int output = 0; output < 2; output++) {
        const char *key = output ? "outputs" : "inputs";
        int count = output ? n->output_count : n->input_count;
        if (keyed_side(n, output)) {
            write_keyed_pins(doc, o, key, g, n, output, &next_id);
        } else {
            /* An empty list still records pins removed from the template. */
            write_listed_pins(doc, o, key, g, n, output);
            if (count) {
                next_id = (output ? n->output_pins : n->input_pins)[count - 1].id + 1;
            }
        }
    }
}

static void write_edge(av_doc *doc, av_value *arr, const graph *g, const edge *e)
{
    av_value *o = av_obj(doc);
    write_pin_ref(doc, o, "from", "from_pin", g, e->from_id, e->from_pin_id, 1);
    write_pin_ref(doc, o, "to", "to_pin", g, e->to_id, e->to_pin_id, 0);
    /* The source pin kind implies the edge kind. */
    if (e->conversion) {
        av_obj_set(doc, o, "conversion", av_str(doc, e->conversion->name));
    }
    if (e->kind == GRAPH_EDGE_EXEC && e->exec_order) {
        av_obj_set(doc, o, "order", av_sint(doc, e->exec_order));
    }
    if (e->custom_handles) {
        av_obj_set(doc, o, "from_handle_x", av_float(doc, e->from_handle_x));
        av_obj_set(doc, o, "from_handle_y", av_float(doc, e->from_handle_y));
        av_obj_set(doc, o, "to_handle_x", av_float(doc, e->to_handle_x));
        av_obj_set(doc, o, "to_handle_y", av_float(doc, e->to_handle_y));
    }
    if (e->reroute_count) {
        av_value *points = av_arr(doc);
        for (int i = 0; i < e->reroute_count; i++) {
            av_value *point = av_obj(doc);
            av_obj_set(doc, point, "x", av_float(doc, e->reroutes[i].x));
            av_obj_set(doc, point, "y", av_float(doc, e->reroutes[i].y));
            av_arr_append(points, point);
        }
        av_obj_set(doc, o, "reroutes", points);
    }
    av_arr_append(arr, o);
}

/* In-memory documents keep every position. Automatic positions are marked so
 * reloading keeps them automatic; asset files omit them. */
static void write_node(av_doc *doc, av_value *arr, const graph *g, const node *n)
{
    av_value *o = av_obj(doc);
    av_obj_set(doc, o, "id", av_uint(doc, n->id));
    av_obj_set(doc, o, "type", av_str(doc, n->desc->name));
    av_obj_set(doc, o, "x", av_float(doc, n->x));
    av_obj_set(doc, o, "y", av_float(doc, n->y));
    /* A folded node can become visible again after an edit. Snapshots retain
     * its placement just as they retain the positions of visible nodes. */
    if (!n->placed) {
        av_obj_set(doc, o, "placed", av_bool(doc, false));
    }
    if (n->reference_id) {
        av_obj_set(doc, o, "reference_id", av_uint(doc, n->reference_id));
    }
    const char *reference_name = n->reference_name;
    if (!reference_name && !n->reference_asset && g->owner) {
        if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
            const behavior_variable
                *variable = behavior_asset_find_variable_by_id(g->owner, n->reference_id);
            reference_name = variable ? variable->name : NULL;
        }
        if (serialize_local_call(n) || n->desc == &NODE_CallMacro) {
            const graph *target = behavior_asset_find_graph(g->owner, n->reference_id);
            reference_name = target && target->name && *target->name ? target->name
                                                                     : NULL;
        }
    }
    if (reference_name) {
        av_obj_set(doc, o, "reference_name", av_str(doc, reference_name));
    }
    if (n->reference_asset) {
        av_obj_set(doc, o, "reference_asset", av_str(doc, n->reference_asset));
    }
    if (n->disabled) {
        av_obj_set(doc, o, "disabled", av_bool(doc, true));
    }
    if (n->presentation != NODE_PRESENTATION_AUTO) {
        av_obj_set(doc, o, "presentation", av_str(doc, node_presentation_name(n->presentation)));
    }
    if (n->show_advanced) {
        av_obj_set(doc, o, "show_advanced", av_bool(doc, true));
    }
    if (n->breakpoint) {
        av_obj_set(doc, o, "breakpoint", av_bool(doc, true));
    }
    if (n->breakpoint_enabled) {
        av_obj_set(doc, o, "breakpoint_enabled", av_bool(doc, true));
    }
    if (n->comment) {
        av_obj_set(doc, o, "comment", av_str(doc, n->comment));
    }
    if (n->comment_hidden) {
        av_obj_set(doc, o, "comment_hidden", av_bool(doc, true));
    }
    write_pins(doc, o, g, n);
    av_arr_append(arr, o);
}

static int compare_nodes(const void *a, const void *b)
{
    const node *left = *(node *const *)a, *right = *(node *const *)b;
    return left->id < right->id ? -1 : left->id > right->id;
}

static int compare_edges(const void *a, const void *b)
{
    const edge *left = a, *right = b;
#define CMP_FIELD(field)                                                       \
    if (left->field != right->field) return left->field < right->field ? -1 : 1
    CMP_FIELD(from_id);
    CMP_FIELD(from_pin_id);
    CMP_FIELD(to_id);
    CMP_FIELD(to_pin_id);
    CMP_FIELD(kind);
    CMP_FIELD(exec_order);
#undef CMP_FIELD
    return 0;
}

static int subset_contains(node *const *subset, int count, const node *candidate)
{
    for (int i = 0; i < count; i++) {
        if (subset[i] == candidate) {
            return 1;
        }
    }
    return 0;
}

static node **expand_inline_subset(const graph *g, node *const *subset, int count, int *expanded_count)
{
    int capacity = count + acount(g->nodes);
    node **expanded = xmalloc((size_t)(capacity > 0 ? capacity : 1) * sizeof *expanded);
    int total = count;
    for (int i = 0; i < count; i++) {
        expanded[i] = subset[i];
    }
    for (int i = 0; i < total; i++) {
        node *target = expanded[i];
        if (!target) {
            continue;
        }
        for (int pin = 0; pin < target->input_count; pin++) {
            node *source = NULL;
            int source_pin = -1;
            if (graph_inline_source(g, target, pin, &source, &source_pin) &&
                !subset_contains(expanded, total, source)) {
                assert(total < capacity);
                expanded[total++] = source;
            }
        }
    }
    *expanded_count = total;
    return expanded;
}

static void write_nodes(av_doc *doc, av_value *arr, const graph *g, node *const *subset, int count)
{
    int node_total = subset ? count : acount(g->nodes);
    node **ordered_nodes = node_total
                             ? malloc((size_t)node_total * sizeof *ordered_nodes)
                             : NULL;
    if (node_total && !ordered_nodes) {
        return;
    }
    if (!node_total) {
        return;
    }
    if (!subset) {
        memcpy(ordered_nodes, g->nodes, (size_t)node_total * sizeof *ordered_nodes);
    } else {
        for (int i = 0; i < node_total; i++) {
            ordered_nodes[i] = subset[i];
        }
    }
    qsort(ordered_nodes, (size_t)node_total, sizeof *ordered_nodes, compare_nodes);
    for (int i = 0; i < node_total; i++) {
        if (ordered_nodes[i]) {
            node copy = *ordered_nodes[i];
            pin_spec *pins = copy.input_count
                               ? xmalloc((size_t)copy.input_count * sizeof *pins)
                               : NULL;
            for (int p = 0; p < copy.input_count; p++) {
                pins[p] = copy.input_pins[p];
                /* Bindings whose source stays behind cannot be named by the subset. */
                const graph_local *local = graph_input_local(g, &copy, p);
                const pin_binding *binding = pins[p].binding;
                uint64_t source = local ? local->source_id
                                : binding && binding->kind == PIN_BINDING_EVENT_OUTPUT
                                    ? binding->event_node_id
                                    : 0;
                if (subset && source &&
                    !subset_contains(subset, count, graph_node_by_id(g, source))) {
                    pins[p].binding = NULL;
                }
            }
            copy.input_pins = pins;
            write_node(doc, arr, g, &copy);
            free(pins);
        }
    }
    free(ordered_nodes);
}

static void write_edges(av_doc *doc, av_value *arr, const graph *g, node *const *subset, int count)
{
    edge *ordered_edges = acount(g->edges)
                            ? malloc((size_t)acount(g->edges) * sizeof *ordered_edges)
                            : NULL;
    int edge_total = 0;
    if (acount(g->edges) && !ordered_edges) {
        return;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (subset) {
            node *from_node = graph_node_by_id(g, e->from_id);
            node *to_node = graph_node_by_id(g, e->to_id);
            int from = 0, to = 0;
            for (int j = 0; j < count; j++) {
                from |= subset[j] == from_node;
                to |= subset[j] == to_node;
            }
            if (!from || !to) {
                continue;
            }
        }
        ordered_edges[edge_total++] = *e;
    }
    if (edge_total) {
        qsort(ordered_edges, (size_t)edge_total, sizeof *ordered_edges, compare_edges);
    }
    for (int i = 0; i < edge_total; i++) {
        write_edge(doc, arr, g, &ordered_edges[i]);
    }
    free(ordered_edges);
}

static void write_comment_boxes(av_doc *doc, av_value *obj, const graph *g)
{
    if (!acount(g->comment_boxes)) {
        return;
    }
    av_value *boxes = av_arr(doc);
    av_obj_set(doc, obj, "comment_boxes", boxes);
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        const graph_comment_box *box = &g->comment_boxes[i];
        av_value *value = av_obj(doc);
        av_obj_set(doc, value, "id", av_uint(doc, box->id));
        av_obj_set(doc, value, "x", av_float(doc, box->x));
        av_obj_set(doc, value, "y", av_float(doc, box->y));
        av_obj_set(doc, value, "width", av_float(doc, box->width));
        av_obj_set(doc, value, "height", av_float(doc, box->height));
        av_obj_set(doc, value, "title", av_str(doc, box->title ? box->title : ""));
        if (box->color) {
            av_obj_set(doc, value, "color", av_color(doc, box->color));
        }
        av_arr_append(boxes, value);
    }
}

static void write_description(av_doc *doc, av_value *obj, const char *description)
{
    if (description && description[0]) {
        av_obj_set(doc, obj, "description", av_str(doc, description));
    }
}

static void write_graph_body(av_doc *doc, av_value *obj, const graph *g, node *const *subset, int count)
{
    if (!g->breakpoints_enabled) {
        av_obj_set(doc, obj, "breakpoints_enabled", av_bool(doc, false));
    }
    if (g->dataflow) {
        av_obj_set(doc, obj, "dataflow", av_bool(doc, true));
    }
    write_comment_boxes(doc, obj, g);
    av_value *nodes = av_arr(doc);
    write_nodes(doc, nodes, g, subset, count);
    if (av_arr_size(nodes)) {
        av_obj_set(doc, obj, "nodes", nodes);
    }
    av_value *edges = av_arr(doc);
    write_edges(doc, edges, g, subset, count);
    if (av_arr_size(edges)) {
        av_obj_set(doc, obj, "edges", edges);
    }
    serialize_write_locals(doc, obj, g, subset, count);
}

static av_value *write_graph(av_doc *doc, const graph *g, node *const *subset, int count)
{
    av_value *root = av_obj(doc);
    node **expanded = NULL;
    int expanded_count = count;
    if (subset) {
        expanded = expand_inline_subset(g, subset, count, &expanded_count);
        subset = expanded;
        count = expanded_count;
    }
    av_obj_set(doc, root, "version", av_uint(doc, GRAPH_SCHEMA_VERSION));
    av_obj_set(doc, root, "id_counter", av_uint(doc, g->id_counter));
    av_obj_set(doc, root, "name", av_str(doc, g->name ? g->name : ""));
    write_description(doc, root, g->description);
    if (g->owner) {
        av_obj_set(doc, root, "binding_owner_id", av_uint(doc, g->owner->identity));
    }
    write_graph_body(doc, root, g, subset, count);
    if (!subset) {
        serialize_omit_implied_shapes(root, g);
    }
    free(expanded);
    return root;
}

av_value *graph_to_value(av_doc *doc, const graph *g)
{
    return g ? write_graph(doc, g, NULL, 0) : NULL;
}

av_value *graph_nodes_to_value(av_doc *doc, const graph *g, node *const *subset, int count)
{
    return g && count >= 0 && (!count || subset) ? write_graph(doc, g, subset, count)
                                                 : NULL;
}

char *graph_save_nodes(const graph *g, node *const *subset, int count)
{
    if (!g || count < 0 || (count && !subset)) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, write_graph(doc, g, subset, count));
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

char *graph_save(const graph *g)
{
    return graph_save_nodes(g, NULL, 0);
}

static void write_variable(av_doc *doc, av_value *arr, const behavior_variable *v)
{
    av_value *o = av_obj(doc);
    av_obj_set(doc, o, "id", av_uint(doc, v->id));
    av_obj_set(doc, o, "name", av_str(doc, v->name ? v->name : ""));
    av_obj_set(doc, o, "type", av_str(doc, serialize_pin_type_name(v->type)));
    if (v->type_name) {
        av_obj_set(doc, o, "type_name", av_str(doc, v->type_name));
    }
    if (v->container) {
        av_obj_set(doc, o, "container", av_sint(doc, v->container));
    }
    if (v->category) {
        av_obj_set(doc, o, "category", av_str(doc, v->category));
    }
    if (v->shared) {
        av_obj_set(doc, o, "shared", av_bool(doc, true));
    }
    /* Matches the zero default a variable receives when none is serialized. */
    pin_value zero = serialize_zero_value(v->type, v->container, v->type_name);
    if (!serialize_same_value(&v->default_value, &zero)) {
        pin_value_write_document(doc, o, "default", v->type, v->type_name, &v->default_value);
    }
    av_arr_append(arr, o);
}

/* An expression function omits a body that rebuilding from its source
 * reproduces. */
static bool derived_body(const graph *g)
{
    graph *body = g->owner && g->math_expression
                    ? behavior_expression_rebuild(g->owner, g)
                    : NULL;
    if (!body) {
        return false;
    }
    av_doc *doc = av_doc_new();
    av_value *written = av_obj(doc), *rebuilt = av_obj(doc);
    write_graph_body(doc, written, g, NULL, 0);
    write_graph_body(doc, rebuilt, body, NULL, 0);
    bool same = body->entry_id == g->entry_id && body->return_id == g->return_id &&
                body->expression_mode == g->expression_mode &&
                body->call_depth_limit == g->call_depth_limit &&
                av_equals(written, rebuilt);
    av_doc_free(doc);
    graph_free(body);
    free(body);
    return same;
}

static void write_behavior_asset_graph(av_doc *doc, av_value *graphs, const graph *g)
{
    av_value *obj = av_obj(doc);
    if (g->graph_id) {
        av_obj_set(doc, obj, "id", av_uint(doc, g->graph_id));
    }
    av_obj_set(doc, obj, "kind", av_str(doc, graph_kind_name(g->kind)));
    /* A derived expression keeps its source and the ID its rebuild starts
       from; its name follows the source unless renamed, and rebuilding
       restores its mode, return node and depth limit. */
    bool derived = derived_body(g);
    if (!derived || strcmp(g->name ? g->name : "", g->math_expression)) {
        av_obj_set(doc, obj, "name", av_str(doc, g->name ? g->name : ""));
    }
    write_description(doc, obj, g->description);
    if (g->source_asset) {
        av_obj_set(doc, obj, "source_asset", av_str(doc, g->source_asset));
        av_obj_set(doc, obj, "source_graph_id", av_uint(doc, g->source_graph_id));
    }
    if (g->math_expression) {
        av_obj_set(doc, obj, "math_expression", av_str(doc, g->math_expression));
    }
    if (g->math_expression) {
        av_obj_set(doc, obj, "expression_mode", av_sint(doc, g->expression_mode));
    }
    if (g->entry_id) {
        av_obj_set(doc, obj, "entry_id", av_uint(doc, g->entry_id));
    }
    if (g->return_id && !derived) {
        av_obj_set(doc, obj, "return_id", av_uint(doc, g->return_id));
    }
    if (g->call_depth_limit && !derived) {
        av_obj_set(doc, obj, "call_depth_limit", av_sint(doc, g->call_depth_limit));
    }
    if (!derived) {
        write_graph_body(doc, obj, g, NULL, 0);
        serialize_omit_implied_shapes(obj, g);
        serialize_omit_derived_declarations(doc, obj, g);
    }
    av_arr_append(graphs, obj);
}

/* FMT-1/FMT-2/FMT-16/FMT-18: the version-3 Behavior Asset document carries the
 * schema version, Behavior Asset name, ID counter, description, optional owner
 * handle type, variable list, and every graph. Runtime state never appears. */
/* An expression's generated graph lives only while a node calls it; deleting
   the last call drops it on save, and undo restores it from memory. A named
   function stays, and a library's functions are called from other assets. */
static bool kept(const behavior_asset *asset, const graph *g)
{
    if (!g->math_expression || strcmp(g->name, g->math_expression) ||
        asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
        asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return true;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            const node *n = caller->nodes[j];
            if (n->reference_id == g->graph_id &&
                (n->desc == &NODE_BehaviorExpression ||
                 n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallFunction)) {
                return true;
            }
        }
    }
    return false;
}

av_value *behavior_asset_to_value(av_doc *doc, const behavior_asset *asset)
{
    if (!asset || !asset->graph) {
        return NULL;
    }
    av_value *root = av_obj(doc);
    av_obj_set(doc, root, "version", av_uint(doc, GRAPH_SCHEMA_VERSION));
    av_obj_set(doc, root, "name", av_str(doc, asset->name ? asset->name : ""));
    if (asset->domain != BEHAVIOR_DOMAIN_EVENT) {
        av_obj_set(doc, root, "domain", av_str(doc, behavior_domain_name(asset->domain)));
    }
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE) {
        av_obj_set(doc, root, "bounds_x", av_float(doc, asset->bounds[0]));
        av_obj_set(doc, root, "bounds_y", av_float(doc, asset->bounds[1]));
    }
    if (asset->domain == BEHAVIOR_DOMAIN_MODEL && asset->reference.image) {
        const model_reference *reference = &asset->reference;
        av_value *value = av_obj(doc);
        av_obj_set(doc, root, "reference", value);
        av_obj_set(doc, value, "image", av_str(doc, reference->image));
        av_obj_set(doc, value, "x", av_float(doc, reference->center[0]));
        av_obj_set(doc, value, "y", av_float(doc, reference->center[1]));
        av_obj_set(doc, value, "span", av_float(doc, reference->span));
        av_obj_set(doc, value, "angle", av_float(doc, reference->angle));
        av_obj_set(doc, value, "locked", av_bool(doc, reference->locked));
    }
    av_obj_set(doc, root, "id_counter", av_uint(doc, asset->graph->id_counter));
    write_description(doc, root, asset->description);
    if (asset->owner_type_name) {
        av_obj_set(doc, root, "owner_type", av_str(doc, asset->owner_type_name));
    }
    if (acount(asset->component_parameters)) {
        av_value *parameters = av_obj_add_arr(doc, root, "component_parameters");
        for (int i = 0; i < acount(asset->component_parameters); i++) {
            const behavior_component_parameter *p = &asset->component_parameters[i];
            av_value *item = av_arr_add_obj(doc, parameters);
            av_obj_add_str(doc, item, "name", p->name);
            av_obj_add_str(doc, item, "key", p->key);
            av_obj_add_bool(doc, item, "optional", p->optional);
        }
    }
    if (acount(asset->variables)) {
        av_value *vars = av_arr(doc);
        av_obj_set(doc, root, "variables", vars);
        for (int i = 0; i < acount(asset->variables); i++) {
            write_variable(doc, vars, &asset->variables[i]);
        }
    }
    av_value *graphs = av_arr(doc);
    if (acount(asset->reactives)) {
        av_value *reactives = av_arr(doc);
        av_obj_set(doc, root, "reactives", reactives);
        for (int i = 0; i < acount(asset->reactives); i++) {
            const behavior_reactive *r = &asset->reactives[i];
            av_value *value = av_obj(doc);
            av_arr_add_val(reactives, value);
            av_obj_set(doc, value, "kind", av_uint(doc, r->kind));
            av_obj_set(doc, value, "graph_id", av_uint(doc, r->graph_id));
            av_obj_set(doc, value, "event_id", av_uint(doc, r->event_id));
            av_obj_set(doc, value, "source_node_id", av_uint(doc, r->source_node_id));
            av_obj_set(doc, value, "source_pin_id", av_uint(doc, r->source_pin_id));
            av_obj_set(doc, value, "statement_node_id", av_uint(doc, r->statement_node_id));
            av_obj_set(doc, value, "value_node_id", av_uint(doc, r->value_node_id));
            av_obj_set(doc, value, "value_pin_id", av_uint(doc, r->value_pin_id));
        }
    }
    av_obj_set(doc, root, "graphs", graphs);
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (kept(asset, g)) {
            write_behavior_asset_graph(doc, graphs, g);
        }
    }
    return root;
}

static void name_bindings(av_doc *doc, av_value *value, const graph *g)
{
    if (av_is_obj(value)) {
        av_value *binding = av_obj_get(value, "binding");
        uint64_t local_id = av_get_uint(av_obj_get(binding, "local_id"));
        uint64_t variable_id = av_get_uint(av_obj_get(binding, "variable_id"));
        const graph_local *local = graph_local_by_id(g, local_id);
        const behavior_variable *variable = behavior_asset_find_variable_by_id(g->owner, variable_id);
        const char *source_asset = av_get_str(av_obj_get(binding, "source_asset"));
        if (local) {
            av_obj_set(doc, binding, "name", av_str(doc, local->name));
            av_obj_remove_key(binding, "local_id");
        }
        if (variable && (!source_asset || !strcmp(source_asset, g->owner->name))) {
            av_obj_set(doc, binding, "name", av_str(doc, variable->name));
            av_obj_remove_key(binding, "variable_id");
        }
        for (size_t i = 0; i < av_obj_size(value); i++) {
            name_bindings(doc, av_obj_value_at(value, i), g);
        }
    }
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            name_bindings(doc, av_arr_get(value, i), g);
        }
    }
}

/* Expressions with one source share a name, so their calls keep the ID. */
static bool graph_name_shared(const behavior_asset *asset, const graph *target)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *other = asset->graphs[i];
        if (other != target && other->name && !strcmp(other->name, target->name)) {
            return true;
        }
    }
    return false;
}

av_value *behavior_asset_to_disk_value(av_doc *doc, const behavior_asset *asset)
{
    av_value *root = behavior_asset_to_value(doc, asset);
    av_value *graphs = av_obj_get(root, "graphs");
    for (size_t i = 0; i < av_arr_size(graphs); i++) {
        av_value *written = av_arr_get(graphs, i);
        const graph *g = behavior_asset_find_graph(asset, av_get_uint(av_obj_get(written, "id")));
        name_bindings(doc, written, g);
        av_value *nodes = av_obj_get(written, "nodes");
        for (size_t j = 0; j < av_arr_size(nodes); j++) {
            av_value *value = av_arr_get(nodes, j);
            const node *n = graph_node_by_id(g, av_get_uint(av_obj_get(value, "id")));
            if (n->reference_asset) {
                continue;
            }
            const char *name = NULL;
            bool shared = false;
            if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
                const behavior_variable
                    *variable = behavior_asset_find_variable_by_id(asset, n->reference_id);
                name = variable ? variable->name : NULL;
            }
            if (serialize_local_call(n) || n->desc == &NODE_CallMacro) {
                const graph *target = behavior_asset_find_graph(asset, n->reference_id);
                name = target && target->name && *target->name ? target->name : NULL;
                shared = name && graph_name_shared(asset, target);
            }
            if (name) {
                av_obj_set(doc, value, "reference_name", av_str(doc, name));
            }
            if (name && !shared) {
                av_obj_remove_key(value, "reference_id");
            }
        }
    }
    return root;
}

char *behavior_asset_save(const behavior_asset *asset)
{
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT ||
        asset->domain == BEHAVIOR_DOMAIN_LIBRARY) {
        serialize_error_set("Executable source is saved through the canonical source document");
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *value = behavior_asset_to_disk_value(doc, asset);
    av_doc_set_root(doc, value);
    char *text = value ? av_doc_write(doc) : NULL;
    av_doc_free(doc);
    return text;
}

behavior_asset *behavior_asset_clone(const behavior_asset *asset)
{
    av_doc *doc = av_doc_new();
    av_value *value = behavior_asset_to_value(doc, asset);
    behavior_asset *copy = value ? behavior_asset_from_value(value) : NULL;
    if (copy) {
        copy->transient_function_ids = asset->transient_function_ids;
    }
    av_doc_free(doc);
    return copy;
}

bool behavior_asset_equal(const behavior_asset *a, const behavior_asset *b)
{
    av_doc *doc = av_doc_new();
    bool equal = av_equals_ordered(behavior_asset_to_value(doc, a), behavior_asset_to_value(doc, b));
    av_doc_free(doc);
    return equal;
}
