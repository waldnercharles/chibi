#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

const pin_spec *serialize_pin_template(const node_desc *desc, int output, int index)
{
    static const pin_spec extension = { .name = "Pin", .type = PIN_FLOW, .kind = PIN_DATA };
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
static void write_str_change(
    yyjson_mut_doc *doc,
    yyjson_mut_val *o,
    const char *key,
    const char *value,
    const char *base
)
{
    if (same_str(value, base)) {
        return;
    }
    if (value) {
        yyjson_mut_obj_add_str(doc, o, key, value);
        return;
    }
    yyjson_mut_obj_add_null(doc, o, key);
}

bool serialize_same_value(const pin_value *a, const pin_value *b)
{
    if (a->type != b->type || a->container != b->container) {
        return false;
    }
    if (a->container >= PIN_CONTAINER_ARRAY && (!a->ptr || !b->ptr)) {
        return a->ptr == b->ptr;
    }
    if (a->container == PIN_CONTAINER_SINGLE &&
        (a->type == PIN_FLOW || a->type == PIN_HANDLE)) {
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
        case PIN_BINDING_DELTA_TIME: return "delta_time";
    }
    return "delta_time";
}

/* Writes <key>: "<node>.<pin>", or the node and pin IDs when the pin's key is
 * ambiguous on its node. */
static void write_pin_ref(
    yyjson_mut_doc *doc,
    yyjson_mut_val *o,
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
        yyjson_mut_obj_add_strcpy(doc, o, key, ref);
        return;
    }
    yyjson_mut_obj_add_uint(doc, o, key, node_id);
    yyjson_mut_obj_add_uint(doc, o, pin_key, pin_id);
}

static void write_binding(yyjson_mut_doc *doc, yyjson_mut_val *o, const graph *g, const pin_binding *b)
{
    yyjson_mut_val *binding = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, binding, "kind", binding_kind_name(b->kind));
    if (b->local_id) {
        yyjson_mut_obj_add_uint(doc, binding, "local_id", b->local_id);
    }
    if (b->variable_id) {
        yyjson_mut_obj_add_uint(doc, binding, "variable_id", b->variable_id);
    }
    if (b->event_node_id) {
        write_pin_ref(doc, binding, "from", "event_pin_id", g, b->event_node_id, b->event_pin_id, 1);
    }
    if (b->source_asset) {
        yyjson_mut_obj_add_str(doc, binding, "source_asset", b->source_asset);
    }
    if (b->component_key) {
        yyjson_mut_obj_add_str(doc, binding, "component", b->component_key);
    }
    if (b->field_name) {
        yyjson_mut_obj_add_str(doc, binding, "field", b->field_name);
    }
    yyjson_mut_obj_add_val(doc, o, "binding", binding);
}

/* Adds the fields where the pin differs from the pin loading starts from. */
static void write_pin_fields(
    yyjson_mut_doc *doc,
    yyjson_mut_val *o,
    const graph *g,
    const pin_spec *p,
    const pin_spec *base
)
{
    if (p->declaration_id != base->declaration_id) {
        yyjson_mut_obj_add_uint(doc, o, "declaration_id", p->declaration_id);
    }
    write_str_change(doc, o, "name", p->name, base->name);
    bool retyped = p->type != base->type;
    if (retyped) {
        yyjson_mut_obj_add_str(doc, o, "type", serialize_pin_type_name(p->type));
    }
    const char *type_name = retyped
                              ? serialize_implied_type_name(p->type, base->type_name)
                              : base->type_name;
    write_str_change(doc, o, "type_name", p->type_name, type_name);
    if (p->kind != base->kind) {
        yyjson_mut_obj_add_str(doc, o, "kind", p->kind == PIN_EXEC ? "exec" : "data");
    }
    if (p->container != base->container) {
        yyjson_mut_obj_add_int(doc, o, "container", p->container);
    }
    if (p->split_parent != base->split_parent) {
        yyjson_mut_obj_add_uint(doc, o, "split_parent", p->split_parent);
    }
    write_str_change(doc, o, "split_type_name", p->split_type_name, base->split_type_name);
    if (p->field_source != base->field_source) {
        yyjson_mut_obj_add_uint(doc, o, "field_source", p->field_source);
    }
    write_str_change(doc, o, "field_name", p->field_name, base->field_name);
    if (p->flags != base->flags) {
        yyjson_mut_obj_add_uint(doc, o, "flags", p->flags);
    }
    if (p->group != base->group) {
        yyjson_mut_obj_add_int(doc, o, "group", p->group);
    }
    if (p->binding) {
        write_binding(doc, o, g, p->binding);
    }
    /* A pin whose type or container changed starts from that shape's zero. */
    pin_value initial = retyped || p->container != base->container
                          ? serialize_zero_value(p->type, p->container, p->type_name)
                          : graph_pin_initial_default(base);
    if (!serialize_same_value(&p->default_value, &initial)) {
        serialize_write_value(doc, o, "default", p->type, p->type_name, &p->default_value);
    }
}

/* A listed side is positional with explicit IDs. It is used when pins do not
 * extend the node's template in order under unique names. */
static void write_listed_pins(
    yyjson_mut_doc *doc,
    yyjson_mut_val *o,
    const char *key,
    const graph *g,
    const node *n,
    int output
)
{
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *pin = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, pin, "id", pins[i].id);
        write_pin_fields(doc, pin, g, &pins[i], serialize_pin_template(n->desc, output, i));
        if (yyjson_mut_obj_size(pin) == 1) {
            yyjson_mut_arr_add_uint(doc, arr, pins[i].id);
        } else {
            yyjson_mut_arr_add_val(arr, pin);
        }
    }
    yyjson_mut_obj_add_val(doc, o, key, arr);
}

/* The pin a keyed entry starts from: its template pin, the exposed field it
 * names, or the extension template under its key. */
static bool keyed_baseline(const node *n, int output, int index, pin_spec *out)
{
    int template_count = output ? n->desc->output_count : n->desc->input_count;
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    if (index < template_count) {
        *out = output ? n->desc->outputs[index] : n->desc->inputs[index];
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
    *out = *serialize_pin_template(n->desc, output, index);
    out->name = pin->name;
    return !pin->field_source && pin->name && !strchr(pin->name, '.');
}

static bool keyed_side(const node *n, int output)
{
    int template_count = output ? n->desc->output_count : n->desc->input_count;
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

static bool scalar_default(yyjson_mut_val *pin)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(pin, "default");
    return yyjson_mut_obj_size(pin) == 1 && value &&
           !yyjson_mut_is_ctn(value) && !yyjson_mut_is_null(value);
}

/* A keyed side maps pin keys to the fields that differ. Pins omitted from the
 * map keep their template, and IDs continue from the node ID unless written. */
static void write_keyed_pins(
    yyjson_mut_doc *doc,
    yyjson_mut_val *o,
    const char *key,
    const graph *g,
    const node *n,
    int output,
    uint64_t *next_id
)
{
    int template_count = output ? n->desc->output_count : n->desc->input_count;
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    yyjson_mut_val *map = yyjson_mut_obj(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *pin = yyjson_mut_obj(doc);
        if (pins[i].id != *next_id) {
            yyjson_mut_obj_add_uint(doc, pin, "id", pins[i].id);
        }
        *next_id = pins[i].id + 1;
        pin_spec base;
        keyed_baseline(n, output, i, &base);
        write_pin_fields(doc, pin, g, &pins[i], &base);
        if (!yyjson_mut_obj_size(pin) && i < template_count) {
            continue;
        }
        char pin_key[SERIALIZE_KEY_SIZE];
        serialize_pin_key(n, output, i, pin_key, sizeof pin_key);
        yyjson_mut_val *value = scalar_default(pin)
                                  ? yyjson_mut_obj_get(pin, "default")
                                  : pin;
        yyjson_mut_obj_add(map, yyjson_mut_strcpy(doc, pin_key), value);
    }
    if (yyjson_mut_obj_size(map)) {
        yyjson_mut_obj_add_val(doc, o, key, map);
    }
}

static void write_pins(yyjson_mut_doc *doc, yyjson_mut_val *o, const graph *g, const node *n)
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

static void write_edge(yyjson_mut_doc *doc, yyjson_mut_val *arr, const graph *g, const edge *e)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    write_pin_ref(doc, o, "from", "from_pin", g, e->from_id, e->from_pin_id, 1);
    write_pin_ref(doc, o, "to", "to_pin", g, e->to_id, e->to_pin_id, 0);
    /* The source pin kind implies the edge kind. */
    if (e->conversion) {
        yyjson_mut_obj_add_str(doc, o, "conversion", e->conversion->name);
    }
    if (e->kind == GRAPH_EDGE_EXEC && e->exec_order) {
        yyjson_mut_obj_add_int(doc, o, "order", e->exec_order);
    }
    if (e->custom_handles) {
        yyjson_mut_obj_add_float(doc, o, "from_handle_x", e->from_handle_x);
        yyjson_mut_obj_add_float(doc, o, "from_handle_y", e->from_handle_y);
        yyjson_mut_obj_add_float(doc, o, "to_handle_x", e->to_handle_x);
        yyjson_mut_obj_add_float(doc, o, "to_handle_y", e->to_handle_y);
    }
    if (e->reroute_count) {
        yyjson_mut_val *points = yyjson_mut_arr(doc);
        for (int i = 0; i < e->reroute_count; i++) {
            yyjson_mut_val *point = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_float(doc, point, "x", e->reroutes[i].x);
            yyjson_mut_obj_add_float(doc, point, "y", e->reroutes[i].y);
            yyjson_mut_arr_add_val(points, point);
        }
        yyjson_mut_obj_add_val(doc, o, "reroutes", points);
    }
    yyjson_mut_arr_add_val(arr, o);
}

/* In-memory documents keep every position. Automatic positions are marked so
 * reloading keeps them automatic; asset files omit them. */
static void write_node(yyjson_mut_doc *doc, yyjson_mut_val *arr, const graph *g, const node *n)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", n->id);
    yyjson_mut_obj_add_str(doc, o, "type", n->desc->name);
    yyjson_mut_obj_add_float(doc, o, "x", n->x);
    yyjson_mut_obj_add_float(doc, o, "y", n->y);
    if (!n->placed) {
        yyjson_mut_obj_add_bool(doc, o, "placed", false);
    }
    if (n->reference_id) {
        yyjson_mut_obj_add_uint(doc, o, "reference_id", n->reference_id);
    }
    if (n->reference_name) {
        yyjson_mut_obj_add_str(doc, o, "reference_name", n->reference_name);
    }
    if (n->reference_asset) {
        yyjson_mut_obj_add_str(doc, o, "reference_asset", n->reference_asset);
    }
    if (n->disabled) {
        yyjson_mut_obj_add_bool(doc, o, "disabled", true);
    }
    if (n->inline_source) {
        yyjson_mut_obj_add_bool(doc, o, "inline_source", true);
    }
    if (n->show_advanced) {
        yyjson_mut_obj_add_bool(doc, o, "show_advanced", true);
    }
    if (n->breakpoint) {
        yyjson_mut_obj_add_bool(doc, o, "breakpoint", true);
    }
    if (n->breakpoint_enabled) {
        yyjson_mut_obj_add_bool(doc, o, "breakpoint_enabled", true);
    }
    write_pins(doc, o, g, n);
    yyjson_mut_arr_add_val(arr, o);
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
    for (int i = 0; i < count; i++) {
        node *target = subset[i];
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

static void write_nodes(yyjson_mut_doc *doc, yyjson_mut_val *arr, const graph *g, node *const *subset, int count)
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

static void write_edges(yyjson_mut_doc *doc, yyjson_mut_val *arr, const graph *g, node *const *subset, int count)
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

static void write_comment_boxes(yyjson_mut_doc *doc, yyjson_mut_val *obj, const graph *g)
{
    if (!acount(g->comment_boxes)) {
        return;
    }
    yyjson_mut_val *boxes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "comment_boxes", boxes);
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        const graph_comment_box *box = &g->comment_boxes[i];
        yyjson_mut_val *value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, value, "id", box->id);
        yyjson_mut_obj_add_float(doc, value, "x", box->x);
        yyjson_mut_obj_add_float(doc, value, "y", box->y);
        yyjson_mut_obj_add_float(doc, value, "width", box->width);
        yyjson_mut_obj_add_float(doc, value, "height", box->height);
        yyjson_mut_obj_add_str(doc, value, "title", box->title ? box->title : "");
        yyjson_mut_obj_add_uint(doc, value, "color", box->color);
        yyjson_mut_arr_add_val(boxes, value);
    }
}

static void write_description(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *description)
{
    if (description && description[0]) {
        yyjson_mut_obj_add_str(doc, obj, "description", description);
    }
}

static void write_graph_body(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const graph *g,
    node *const *subset,
    int count
)
{
    if (!g->breakpoints_enabled) {
        yyjson_mut_obj_add_bool(doc, obj, "breakpoints_enabled", false);
    }
    if (g->dataflow) {
        yyjson_mut_obj_add_bool(doc, obj, "dataflow", true);
    }
    write_comment_boxes(doc, obj, g);
    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    write_nodes(doc, nodes, g, subset, count);
    if (yyjson_mut_arr_size(nodes)) {
        yyjson_mut_obj_add_val(doc, obj, "nodes", nodes);
    }
    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    write_edges(doc, edges, g, subset, count);
    if (yyjson_mut_arr_size(edges)) {
        yyjson_mut_obj_add_val(doc, obj, "edges", edges);
    }
    serialize_write_locals(doc, obj, g, subset, count);
}

static char *write_graph(const graph *g, node *const *subset, int count)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    node **expanded = NULL;
    int expanded_count = count;
    if (subset) {
        expanded = expand_inline_subset(g, subset, count, &expanded_count);
        subset = expanded;
        count = expanded_count;
    }
    yyjson_mut_obj_add_uint(doc, root, "version", GRAPH_SCHEMA_VERSION);
    yyjson_mut_obj_add_uint(doc, root, "id_counter", g->id_counter);
    yyjson_mut_obj_add_str(doc, root, "name", g->name ? g->name : "");
    write_description(doc, root, g->description);
    if (g->owner) {
        yyjson_mut_obj_add_uint(doc, root, "binding_owner_id", g->owner->identity);
    }
    write_graph_body(doc, root, g, subset, count);
    if (!subset) {
        serialize_omit_implied_shapes(root, g);
    }
    char *out = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    free(expanded);
    return out;
}

char *graph_save_json(const graph *g)
{
    return g ? write_graph(g, NULL, 0) : NULL;
}
char *graph_save_nodes_json(const graph *g, node *const *subset, int count)
{
    return g && count >= 0 && (count == 0 || subset) ? write_graph(g, subset, count)
                                                     : NULL;
}

static void write_variable(yyjson_mut_doc *doc, yyjson_mut_val *arr, const behavior_variable *v)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", v->id);
    yyjson_mut_obj_add_str(doc, o, "name", v->name ? v->name : "");
    yyjson_mut_obj_add_str(doc, o, "type", serialize_pin_type_name(v->type));
    if (v->type_name) {
        yyjson_mut_obj_add_str(doc, o, "type_name", v->type_name);
    }
    if (v->container) {
        yyjson_mut_obj_add_int(doc, o, "container", v->container);
    }
    if (v->category) {
        yyjson_mut_obj_add_str(doc, o, "category", v->category);
    }
    /* Matches the zero default a variable receives when none is serialized. */
    pin_value zero = serialize_zero_value(v->type, v->container, v->type_name);
    if (!serialize_same_value(&v->default_value, &zero)) {
        serialize_write_value(doc, o, "default", v->type, v->type_name, &v->default_value);
    }
    yyjson_mut_arr_add_val(arr, o);
}

static void write_behavior_asset_graph(yyjson_mut_doc *doc, yyjson_mut_val *graphs, const graph *g)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (g->graph_id) {
        yyjson_mut_obj_add_uint(doc, obj, "id", g->graph_id);
    }
    yyjson_mut_obj_add_str(doc, obj, "kind", graph_kind_name(g->kind));
    yyjson_mut_obj_add_str(doc, obj, "name", g->name ? g->name : "");
    write_description(doc, obj, g->description);
    if (g->source_asset) {
        yyjson_mut_obj_add_str(doc, obj, "source_asset", g->source_asset);
        yyjson_mut_obj_add_uint(doc, obj, "source_graph_id", g->source_graph_id);
    }
    if (g->math_expression) {
        yyjson_mut_obj_add_str(doc, obj, "math_expression", g->math_expression);
        yyjson_mut_obj_add_int(doc, obj, "expression_mode", g->expression_mode);
    }
    if (g->entry_id) {
        yyjson_mut_obj_add_uint(doc, obj, "entry_id", g->entry_id);
    }
    if (g->return_id) {
        yyjson_mut_obj_add_uint(doc, obj, "return_id", g->return_id);
    }
    if (g->call_depth_limit) {
        yyjson_mut_obj_add_int(doc, obj, "call_depth_limit", g->call_depth_limit);
    }
    write_graph_body(doc, obj, g, NULL, 0);
    serialize_omit_implied_shapes(obj, g);
    yyjson_mut_arr_add_val(graphs, obj);
}

/* FMT-1/FMT-2/FMT-16/FMT-18: the version-3 Behavior Asset document carries the
 * schema version, Behavior Asset name, ID counter, description, optional owner
 * handle type, variable list, and every graph. Runtime state never appears. */
char *behavior_asset_save_json(const behavior_asset *asset)
{
    if (!asset || !asset->graph) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "version", GRAPH_SCHEMA_VERSION);
    yyjson_mut_obj_add_str(doc, root, "name", asset->name ? asset->name : "");
    if (asset->domain != BEHAVIOR_DOMAIN_EVENT) {
        yyjson_mut_obj_add_str(doc, root, "domain", behavior_domain_name(asset->domain));
    }
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE) {
        yyjson_mut_obj_add_float(doc, root, "bounds_x", asset->bounds[0]);
        yyjson_mut_obj_add_float(doc, root, "bounds_y", asset->bounds[1]);
    }
    yyjson_mut_obj_add_uint(doc, root, "id_counter", asset->graph->id_counter);
    write_description(doc, root, asset->description);
    if (asset->owner_type_name) {
        yyjson_mut_obj_add_str(doc, root, "owner_type", asset->owner_type_name);
    }
    if (acount(asset->variables)) {
        yyjson_mut_val *vars = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, root, "variables", vars);
        for (int i = 0; i < acount(asset->variables); i++) {
            write_variable(doc, vars, &asset->variables[i]);
        }
    }
    yyjson_mut_val *graphs = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "graphs", graphs);
    for (int i = 0; i < acount(asset->graphs); i++) {
        write_behavior_asset_graph(doc, graphs, asset->graphs[i]);
    }
    char *out = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}
