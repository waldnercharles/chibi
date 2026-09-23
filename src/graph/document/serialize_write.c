#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void write_pin(yyjson_mut_doc *doc, yyjson_mut_val *arr, const pin_spec *p)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", p->id);
    if (p->declaration_id) {
        yyjson_mut_obj_add_uint(doc, o, "declaration_id", p->declaration_id);
    }
    yyjson_mut_obj_add_str(doc, o, "name", p->name ? p->name : "");
    yyjson_mut_obj_add_int(doc, o, "type", p->type);
    if (p->type_name) {
        yyjson_mut_obj_add_str(doc, o, "type_name", p->type_name);
    }
    yyjson_mut_obj_add_str(doc, o, "kind", p->kind == PIN_EXEC ? "exec" : "data");
    yyjson_mut_obj_add_int(doc, o, "container", p->container);
    yyjson_mut_obj_add_uint(doc, o, "split_parent", p->split_parent);
    if (p->split_type_name) {
        yyjson_mut_obj_add_str(doc, o, "split_type_name", p->split_type_name);
    }
    if (p->field_source) {
        yyjson_mut_obj_add_uint(doc, o, "field_source", p->field_source);
    }
    if (p->field_name) {
        yyjson_mut_obj_add_str(doc, o, "field_name", p->field_name);
    }
    yyjson_mut_obj_add_uint(doc, o, "flags", p->flags);
    yyjson_mut_obj_add_int(doc, o, "group", p->group);
    if (p->binding) {
        const char *kind = p->binding->kind == PIN_BINDING_LOCAL    ? "local"
                         : p->binding->kind == PIN_BINDING_VARIABLE ? "variable"
                         : p->binding->kind == PIN_BINDING_COMPONENT_FIELD
                             ? "component_field"
                         : p->binding->kind == PIN_BINDING_COMPONENT
                             ? "component"
                         : p->binding->kind == PIN_BINDING_EVENT_OUTPUT
                             ? "event_output"
                             : "delta_time";
        yyjson_mut_val *binding = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, binding, "kind", kind);
        if (p->binding->local_id) {
            yyjson_mut_obj_add_uint(doc, binding, "local_id", p->binding->local_id);
        }
        if (p->binding->variable_id) {
            yyjson_mut_obj_add_uint(doc, binding, "variable_id", p->binding->variable_id);
        }
        if (p->binding->event_node_id) {
            yyjson_mut_obj_add_uint(doc, binding, "event_node_id", p->binding->event_node_id);
        }
        if (p->binding->event_pin_id) {
            yyjson_mut_obj_add_uint(doc, binding, "event_pin_id", p->binding->event_pin_id);
        }
        if (p->binding->source_asset) {
            yyjson_mut_obj_add_str(doc, binding, "source_asset", p->binding->source_asset);
        }
        if (p->binding->component_key) {
            yyjson_mut_obj_add_str(doc, binding, "component", p->binding->component_key);
        }
        if (p->binding->field_name) {
            yyjson_mut_obj_add_str(doc, binding, "field", p->binding->field_name);
        }
        yyjson_mut_obj_add_val(doc, o, "binding", binding);
    }
    serialize_write_value(doc, o, "default", p->type, p->type_name, &p->default_value);
    yyjson_mut_arr_add_val(arr, o);
}

static void write_edge(yyjson_mut_doc *doc, yyjson_mut_val *arr, const edge *e)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "from", e->from_id);
    yyjson_mut_obj_add_uint(doc, o, "from_pin", e->from_pin_id);
    yyjson_mut_obj_add_uint(doc, o, "to", e->to_id);
    yyjson_mut_obj_add_uint(doc, o, "to_pin", e->to_pin_id);
    yyjson_mut_obj_add_str(doc, o, "kind", graph_edge_kind_name(e->kind));
    if (e->conversion) {
        yyjson_mut_obj_add_str(doc, o, "conversion", e->conversion->name);
    }
    if (e->kind == GRAPH_EDGE_EXEC) {
        yyjson_mut_obj_add_int(doc, o, "order", e->exec_order);
    }
    if (e->custom_handles) {
        yyjson_mut_obj_add_real(doc, o, "from_handle_x", e->from_handle_x);
        yyjson_mut_obj_add_real(doc, o, "from_handle_y", e->from_handle_y);
        yyjson_mut_obj_add_real(doc, o, "to_handle_x", e->to_handle_x);
        yyjson_mut_obj_add_real(doc, o, "to_handle_y", e->to_handle_y);
    }
    if (e->reroute_count) {
        yyjson_mut_val *points = yyjson_mut_arr(doc);
        for (int i = 0; i < e->reroute_count; i++) {
            yyjson_mut_val *point = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, point, "x", e->reroutes[i].x);
            yyjson_mut_obj_add_real(doc, point, "y", e->reroutes[i].y);
            yyjson_mut_arr_add_val(points, point);
        }
        yyjson_mut_obj_add_val(doc, o, "reroutes", points);
    }
    yyjson_mut_arr_add_val(arr, o);
}

static void write_node(yyjson_mut_doc *doc, yyjson_mut_val *arr, const node *n)
{
    yyjson_mut_val *o = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, o, "id", n->id);
    yyjson_mut_obj_add_str(doc, o, "type", n->desc->name);
    yyjson_mut_obj_add_real(doc, o, "x", n->x);
    yyjson_mut_obj_add_real(doc, o, "y", n->y);
    yyjson_mut_obj_add_uint(doc, o, "reference_id", n->reference_id);
    if (n->reference_name) {
        yyjson_mut_obj_add_str(doc, o, "reference_name", n->reference_name);
    }
    if (n->reference_asset) {
        yyjson_mut_obj_add_str(doc, o, "reference_asset", n->reference_asset);
    }
    yyjson_mut_obj_add_bool(doc, o, "disabled", n->disabled != 0);
    if (n->inline_source) {
        yyjson_mut_obj_add_bool(doc, o, "inline_source", true);
    }
    yyjson_mut_obj_add_bool(doc, o, "show_advanced", n->show_advanced);
    yyjson_mut_obj_add_bool(doc, o, "breakpoint", n->breakpoint != 0);
    yyjson_mut_obj_add_bool(doc, o, "breakpoint_enabled", n->breakpoint_enabled != 0);
    yyjson_mut_val *ins = yyjson_mut_arr(doc), *outs = yyjson_mut_arr(doc);
    for (int i = 0; i < n->input_count; i++) {
        write_pin(doc, ins, &n->input_pins[i]);
    }
    for (int i = 0; i < n->output_count; i++) {
        pin_spec p = n->output_pins[i];
        if (n->desc->pure_kind == NODE_PURE_CONST) {
            p.default_value = n->output_pins[i].default_value;
        }
        write_pin(doc, outs, &p);
    }
    yyjson_mut_obj_add_val(doc, o, "inputs", ins);
    yyjson_mut_obj_add_val(doc, o, "outputs", outs);
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
                const graph_local *local = graph_input_local(g, &copy, p);
                if (subset && local &&
                    !subset_contains(subset, count, graph_node_by_id(g, local->source_id))) {
                    pins[p].binding = NULL;
                }
            }
            copy.input_pins = pins;
            write_node(doc, arr, &copy);
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
        write_edge(doc, arr, &ordered_edges[i]);
    }
    free(ordered_edges);
}

static void write_comment_boxes(yyjson_mut_doc *doc, yyjson_mut_val *obj, const graph *g)
{
    yyjson_mut_val *boxes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "comment_boxes", boxes);
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        const graph_comment_box *box = &g->comment_boxes[i];
        yyjson_mut_val *value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, value, "id", box->id);
        yyjson_mut_obj_add_real(doc, value, "x", box->x);
        yyjson_mut_obj_add_real(doc, value, "y", box->y);
        yyjson_mut_obj_add_real(doc, value, "width", box->width);
        yyjson_mut_obj_add_real(doc, value, "height", box->height);
        yyjson_mut_obj_add_str(doc, value, "title", box->title ? box->title : "");
        yyjson_mut_obj_add_uint(doc, value, "color", box->color);
        yyjson_mut_arr_add_val(boxes, value);
    }
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
    yyjson_mut_obj_add_str(doc, root, "description", g->description ? g->description : "");
    if (g->owner) {
        yyjson_mut_obj_add_uint(doc, root, "binding_owner_id", g->owner->identity);
    }
    yyjson_mut_obj_add_bool(doc, root, "breakpoints_enabled", g->breakpoints_enabled != 0);
    if (g->dataflow) {
        yyjson_mut_obj_add_bool(doc, root, "dataflow", true);
    }
    write_comment_boxes(doc, root, g);
    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);
    write_nodes(doc, nodes, g, subset, count);
    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "edges", edges);
    write_edges(doc, edges, g, subset, count);
    serialize_write_locals(doc, root, g, subset, count);
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
    yyjson_mut_obj_add_int(doc, o, "type", v->type);
    if (v->type_name) {
        yyjson_mut_obj_add_str(doc, o, "type_name", v->type_name);
    }
    yyjson_mut_obj_add_int(doc, o, "container", v->container);
    if (v->category) {
        yyjson_mut_obj_add_str(doc, o, "category", v->category);
    }
    serialize_write_value(doc, o, "default", v->type, v->type_name, &v->default_value);
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
    yyjson_mut_obj_add_str(doc, obj, "description", g->description ? g->description : "");
    yyjson_mut_obj_add_bool(doc, obj, "breakpoints_enabled", g->breakpoints_enabled != 0);
    if (g->dataflow) {
        yyjson_mut_obj_add_bool(doc, obj, "dataflow", true);
    }
    if (g->source_asset) {
        yyjson_mut_obj_add_str(doc, obj, "source_asset", g->source_asset);
        yyjson_mut_obj_add_uint(doc, obj, "source_graph_id", g->source_graph_id);
    }
    if (g->math_expression) {
        yyjson_mut_obj_add_str(doc, obj, "math_expression", g->math_expression);
        yyjson_mut_obj_add_int(doc, obj, "expression_mode", g->expression_mode);
    }
    write_comment_boxes(doc, obj, g);
    if (g->entry_id) {
        yyjson_mut_obj_add_uint(doc, obj, "entry_id", g->entry_id);
    }
    if (g->return_id) {
        yyjson_mut_obj_add_uint(doc, obj, "return_id", g->return_id);
    }
    if (g->call_depth_limit) {
        yyjson_mut_obj_add_int(doc, obj, "call_depth_limit", g->call_depth_limit);
    }
    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "nodes", nodes);
    write_nodes(doc, nodes, g, NULL, 0);
    yyjson_mut_val *edges = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "edges", edges);
    write_edges(doc, edges, g, NULL, 0);
    serialize_write_locals(doc, obj, g, NULL, 0);
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
    yyjson_mut_obj_add_str(doc, root, "domain", behavior_domain_name(asset->domain));
    if (asset->domain == BEHAVIOR_DOMAIN_SHAPE) {
        yyjson_mut_obj_add_real(doc, root, "bounds_x", asset->bounds[0]);
        yyjson_mut_obj_add_real(doc, root, "bounds_y", asset->bounds[1]);
    }
    yyjson_mut_obj_add_uint(doc, root, "id_counter", asset->graph->id_counter);
    yyjson_mut_obj_add_str(doc, root, "description", asset->description ? asset->description : "");
    if (asset->owner_type_name) {
        yyjson_mut_obj_add_str(doc, root, "owner_type", asset->owner_type_name);
    }
    yyjson_mut_val *vars = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "variables", vars);
    for (int i = 0; i < acount(asset->variables); i++) {
        write_variable(doc, vars, &asset->variables[i]);
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
