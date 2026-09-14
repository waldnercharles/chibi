#include "../third_party/yyjson/yyjson.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/math_expression.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local char serialize_error[256];
void serialize_error_set(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(serialize_error, sizeof serialize_error, fmt, ap);
    va_end(ap);
}
const char *graph_last_error(void)
{
    return serialize_error;
}

static int json_uint(yyjson_val *v, uint64_t *out, const char *path)
{
    if (!v || !yyjson_is_uint(v) || yyjson_get_uint(v) == 0) {
        serialize_error_set("%s must be a non-zero unsigned integer", path);
        return -1;
    }
    *out = yyjson_get_uint(v);
    return 0;
}

static int compact_component_node(const graph *g, const node *n)
{
    return g && g->dataflow && n && n->desc && n->desc->type &&
           n->desc->access && n->desc->name &&
           ((!strncmp(n->desc->name, "Get_", 4) && n->desc->access->read_count == 1) ||
            (!strncmp(n->desc->name, "Set_", 4) && n->desc->access->write_count == 1));
}

static int load_pins(graph *g, node *n, yyjson_val *arr, int output)
{
    if (!arr || !yyjson_is_arr(arr)) {
        serialize_error_set(
            "node %llu has a missing or invalid %s pin list",
            (unsigned long long)n->id,
            output ? "output" : "input"
        );
        return -1;
    }
    yyjson_arr_iter it;
    yyjson_arr_iter_init(arr, &it);
    yyjson_val *v;
    int i = 0;
    int count = output ? n->output_count : n->input_count;
    int serialized_count = (int)yyjson_arr_size(arr);
    if (serialized_count < count &&
        (n->desc == &NODE_FunctionEntry || n->desc == &NODE_Return ||
         compact_component_node(g, n))) {
        while (count > serialized_count) {
            if (graph_remove_pin(g, n, output, count - 1)) {
                serialize_error_set(
                    "node %llu cannot shrink %s pin list",
                    (unsigned long long)n->id,
                    output ? "output" : "input"
                );
                return -1;
            }
            count--;
        }
    }
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v)) {
            serialize_error_set(
                "node %llu has invalid %s pin list",
                (unsigned long long)n->id,
                output ? "output" : "input"
            );
            return -1;
        }
        if (i >= count) {
            pin_spec extension = { .name = "Pin", .type = PIN_FLOW, .kind = PIN_DATA };
            const pin_spec *template_pin = &extension;
            if (output && n->desc->output_count) {
                template_pin = &n->desc->outputs[n->desc->output_count - 1];
            } else if (!output && n->desc->input_count) {
                template_pin = &n->desc->inputs[n->desc->input_count - 1];
            }
            if (graph_add_pin(g, n, output, template_pin) < 0) {
                serialize_error_set(
                    "node %llu cannot extend %s pin list",
                    (unsigned long long)n->id,
                    output ? "output" : "input"
                );
                return -1;
            }
            count++;
        }
        pin_spec *p = output ? &n->output_pins[i] : &n->input_pins[i];
        uint64_t id = 0;
        if (json_uint(yyjson_obj_get(v, "id"), &id, "pin.id")) {
            return -1;
        }
        p->id = id;
        yyjson_val *name = yyjson_obj_get(v, "name");
        yyjson_val *type = yyjson_obj_get(v, "type");
        if (!yyjson_is_str(name) || !yyjson_is_int(type) ||
            yyjson_get_sint(type) < PIN_FLOW || yyjson_get_sint(type) > PIN_ENUM) {
            serialize_error_set(
                "node %llu has malformed pin %d",
                (unsigned long long)n->id,
                i
            );
            return -1;
        }
        p->name = graph_own_str(g, yyjson_get_str(name), yyjson_get_len(name));
        p->type = (pin_type)yyjson_get_sint(type);
        yyjson_val *tn = yyjson_obj_get(v, "type_name");
        p->type_name = yyjson_is_str(tn)
                         ? graph_own_str(g, yyjson_get_str(tn), yyjson_get_len(tn))
                         : NULL;
        yyjson_val *kind = yyjson_obj_get(v, "kind");
        if (!yyjson_is_str(kind)) {
            serialize_error_set("pin %d missing kind", i);
            return -1;
        }
        p->kind = strcmp(yyjson_get_str(kind), "exec") == 0 ? PIN_EXEC : PIN_DATA;
        yyjson_val *container = yyjson_obj_get(v, "container");
        if (container && yyjson_is_int(container)) {
            p->container = (pin_container)yyjson_get_sint(container);
        }
        yyjson_val *split = yyjson_obj_get(v, "split_parent");
        if (split && yyjson_is_uint(split)) {
            p->split_parent = yyjson_get_uint(split);
        }
        yyjson_val *split_type = yyjson_obj_get(v, "split_type_name");
        p->split_type_name = yyjson_is_str(split_type)
                               ? graph_own_str(g, yyjson_get_str(split_type), yyjson_get_len(split_type))
                               : NULL;
        yyjson_val *field_source = yyjson_obj_get(v, "field_source");
        yyjson_val *field_name = yyjson_obj_get(v, "field_name");
        if ((field_source || field_name) &&
            (!output || !yyjson_is_uint(field_source) || !yyjson_get_uint(field_source) ||
             !yyjson_is_str(field_name) || !yyjson_get_len(field_name))) {
            serialize_error_set("invalid exposed field metadata");
            return -1;
        }
        p->field_source = field_source ? yyjson_get_uint(field_source) : 0;
        p->field_name = field_name
                          ? graph_own_str(g, yyjson_get_str(field_name), yyjson_get_len(field_name))
                          : NULL;
        yyjson_val *flags = yyjson_obj_get(v, "flags");
        p->declaration_id = yyjson_get_uint(yyjson_obj_get(v, "declaration_id"));
        if (flags && yyjson_is_uint(flags)) {
            p->flags = (unsigned)yyjson_get_uint(flags);
        }
        yyjson_val *group = yyjson_obj_get(v, "group");
        if (group && yyjson_is_int(group)) {
            p->group = (int)yyjson_get_sint(group);
        }
        yyjson_val *def = yyjson_obj_get(v, "default");
        if (def) {
            pin_value pv;
            if (p->container >= PIN_CONTAINER_ARRAY) {
                if (serialize_read_array(p->type, p->type_name, def, &pv, g)) {
                    return -1;
                }
            } else if (serialize_read_value(p->type, p->type_name, def, &pv, g)) {
                if (p->type != PIN_STRUCT) {
                    serialize_error_set(
                        "bad default for pin %d on %s (type %d)",
                        i,
                        n->desc->name,
                        p->type
                    );
                }
                return -1;
            }
            pv.type_name = p->type_name;
            pv.container = p->container;
            p->default_value = pv;
        }
        yyjson_val *binding_json = yyjson_obj_get(v, "binding");
        if (binding_json) {
            yyjson_val *binding_kind = yyjson_obj_get(binding_json, "kind");
            if (output || !yyjson_is_obj(binding_json) || !yyjson_is_str(binding_kind)) {
                serialize_error_set("invalid pin binding on %s pin %d", output ? "output" : "input", i);
                return -1;
            }
            const char *kind_name = yyjson_get_str(binding_kind);
            pin_binding binding = { 0 };
            if (!strcmp(kind_name, "variable")) {
                binding.kind = PIN_BINDING_VARIABLE;
                yyjson_val *id = yyjson_obj_get(binding_json, "variable_id");
                if (!id || !yyjson_is_uint(id) || !yyjson_get_uint(id)) {
                    serialize_error_set("variable pin binding %d has no variable_id", i);
                    return -1;
                }
                binding.variable_id = yyjson_get_uint(id);
            } else if (!strcmp(kind_name, "component_field")) {
                binding.kind = PIN_BINDING_COMPONENT_FIELD;
                yyjson_val *component = yyjson_obj_get(binding_json, "component");
                yyjson_val *field = yyjson_obj_get(binding_json, "field");
                if (!yyjson_is_str(component) || !yyjson_get_len(component) ||
                    !yyjson_is_str(field) || !yyjson_get_len(field)) {
                    serialize_error_set("component pin binding %d is incomplete", i);
                    return -1;
                }
                binding.component_key = yyjson_get_str(component);
                binding.field_name = yyjson_get_str(field);
            } else if (!strcmp(kind_name, "component")) {
                binding.kind = PIN_BINDING_COMPONENT;
                yyjson_val *component = yyjson_obj_get(binding_json, "component");
                if (!yyjson_is_str(component) || !yyjson_get_len(component)) {
                    serialize_error_set("component pin binding %d has no component key", i);
                    return -1;
                }
                binding.component_key = yyjson_get_str(component);
            } else if (!strcmp(kind_name, "local")) {
                binding.kind = PIN_BINDING_LOCAL;
                yyjson_val *local_id = yyjson_obj_get(binding_json, "local_id");
                if (!yyjson_is_uint(local_id) || !yyjson_get_uint(local_id)) {
                    serialize_error_set("local binding has no local_id");
                    return -1;
                }
                binding.local_id = yyjson_get_uint(local_id);
            } else if (!strcmp(kind_name, "delta_time")) {
                binding.kind = PIN_BINDING_DELTA_TIME;
            } else {
                serialize_error_set("unknown pin binding kind '%s'", kind_name);
                return -1;
            }
            yyjson_val *source_asset = yyjson_obj_get(binding_json, "source_asset");
            if (source_asset && !yyjson_is_str(source_asset)) {
                serialize_error_set("invalid pin binding source asset");
                return -1;
            }
            binding.source_asset = source_asset ? yyjson_get_str(source_asset) : NULL;
            if (graph_set_pin_binding(g, n, 0, i, &binding) != 0) {
                serialize_error_set("could not store pin binding %d", i);
                return -1;
            }
        }
        i++;
    }
    return i == count ? 0
                      : (serialize_error_set(
                             "node %llu pin count does not match descriptor",
                             (unsigned long long)n->id
                         ),
                         -1);
}

static int load_comment_boxes(graph *g, yyjson_val *json)
{
    if (!json) {
        return 0;
    }
    if (!yyjson_is_arr(json)) {
        serialize_error_set("comment_boxes must be an array");
        return -1;
    }
    yyjson_arr_iter it;
    yyjson_arr_iter_init(json, &it);
    yyjson_val *value;
    while ((value = yyjson_arr_iter_next(&it))) {
        uint64_t id = 0;
        yyjson_val *title = yyjson_obj_get(value, "title");
        if (!yyjson_is_obj(value) ||
            json_uint(yyjson_obj_get(value, "id"), &id, "comment_box.id") ||
            !yyjson_is_str(title)) {
            serialize_error_set("malformed comment box");
            return -1;
        }
        graph_comment_box box = {
            .id = id,
            .x = (float)yyjson_get_num(yyjson_obj_get(value, "x")),
            .y = (float)yyjson_get_num(yyjson_obj_get(value, "y")),
            .width = (float)yyjson_get_num(yyjson_obj_get(value, "width")),
            .height = (float)yyjson_get_num(yyjson_obj_get(value, "height")),
            .title = graph_own_str(g, yyjson_get_str(title), yyjson_get_len(title)),
            .color = (uint32_t)yyjson_get_uint(yyjson_obj_get(value, "color")),
        };
        apush(g->comment_boxes, box);
        if (id >= g->id_counter) {
            g->id_counter = id + 1;
        }
    }
    return 0;
}

static int load_nodes(graph *g, yyjson_val *nodes, uint64_t requested_counter)
{
    yyjson_arr_iter it;
    yyjson_arr_iter_init(nodes, &it);
    yyjson_val *v;
    while ((v = yyjson_arr_iter_next(&it))) {
        if (!yyjson_is_obj(v)) {
            serialize_error_set("node is not an object");
            return -1;
        }
        yyjson_val *type = yyjson_obj_get(v, "type");
        if (!yyjson_is_str(type)) {
            serialize_error_set("node is missing type");
            return -1;
        }
        const node_desc *desc = registry_node_by_name(yyjson_get_str(type));
        if (!desc) {
            serialize_error_set("unknown node type \"%s\"", yyjson_get_str(type));
            return -1;
        }
        node *n = graph_add_node(g, desc);
        if (!n) {
            serialize_error_set("out of memory");
            return -1;
        }
        uint64_t id = 0;
        if (json_uint(yyjson_obj_get(v, "id"), &id, "node.id")) {
            return -1;
        }
        n->id = id;
        yyjson_val *x = yyjson_obj_get(v, "x"), *y = yyjson_obj_get(v, "y");
        if (x && yyjson_is_num(x)) {
            n->x = (float)yyjson_get_num(x);
        }
        if (y && yyjson_is_num(y)) {
            n->y = (float)yyjson_get_num(y);
        }
        yyjson_val *refasset = yyjson_obj_get(v, "reference_asset");
        if (yyjson_is_str(refasset)) {
            n->reference_asset = graph_own_str(g, yyjson_get_str(refasset), yyjson_get_len(refasset));
        }
        yyjson_val *refname = yyjson_obj_get(v, "reference_name");
        yyjson_val *refid = yyjson_obj_get(v, "reference_id");
        if (refid && yyjson_is_uint(refid)) {
            n->reference_id = yyjson_get_uint(refid);
        }
        if (refname && yyjson_is_str(refname)) {
            n->reference_name = graph_own_str(g, yyjson_get_str(refname), yyjson_get_len(refname));
        }
        yyjson_val *disabled = yyjson_obj_get(v, "disabled");
        n->disabled = disabled && yyjson_is_bool(disabled) &&
                      yyjson_get_bool(disabled);
        yyjson_val *inline_source = yyjson_obj_get(v, "inline_source");
        if (inline_source && !yyjson_is_bool(inline_source)) {
            serialize_error_set("node inline_source must be a boolean");
            return -1;
        }
        n->inline_source = inline_source && yyjson_get_bool(inline_source);
        yyjson_val *breakpoint = yyjson_obj_get(v, "breakpoint");
        yyjson_val *breakpoint_enabled = yyjson_obj_get(v, "breakpoint_enabled");
        n->show_advanced = yyjson_get_bool(yyjson_obj_get(v, "show_advanced"));
        n->breakpoint = breakpoint && yyjson_is_bool(breakpoint) &&
                        yyjson_get_bool(breakpoint);
        n->breakpoint_enabled = breakpoint_enabled &&
                                yyjson_is_bool(breakpoint_enabled) &&
                                yyjson_get_bool(breakpoint_enabled);
        if (load_pins(g, n, yyjson_obj_get(v, "inputs"), 0) ||
            load_pins(g, n, yyjson_obj_get(v, "outputs"), 1)) {
            return -1;
        }
        if (n->id == 0) {
            serialize_error_set("node ID zero is not allowed");
            return -1;
        }
        for (int j = 0; j < acount(g->nodes) - 1; j++) {
            node *other = g->nodes[j];
            if (other->id == n->id) {
                serialize_error_set("duplicate node ID");
                return -1;
            }
            for (int p = 0; p < other->input_count; p++) {
                if (other->input_pins[p].id == n->id) {
                    serialize_error_set("node ID collides with pin ID");
                    return -1;
                }
            }
            for (int p = 0; p < other->output_count; p++) {
                if (other->output_pins[p].id == n->id) {
                    serialize_error_set("node ID collides with pin ID");
                    return -1;
                }
            }
            for (int p = 0; p < n->input_count; p++) {
                for (int q = 0; q < other->input_count; q++) {
                    if (other->input_pins[q].id == n->input_pins[p].id) {
                        serialize_error_set("duplicate pin ID");
                        return -1;
                    }
                }
            }
            for (int p = 0; p < n->output_count; p++) {
                for (int q = 0; q < other->output_count; q++) {
                    if (other->output_pins[q].id == n->output_pins[p].id) {
                        serialize_error_set("duplicate pin ID");
                        return -1;
                    }
                }
            }
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].id == 0) {
                serialize_error_set("pin ID zero is not allowed");
                return -1;
            }
            for (int q = p + 1; q < n->input_count; q++) {
                if (n->input_pins[p].id == n->input_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
            for (int q = 0; q < n->output_count; q++) {
                if (n->input_pins[p].id == n->output_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].id == 0) {
                serialize_error_set("pin ID zero is not allowed");
                return -1;
            }
            for (int q = p + 1; q < n->output_count; q++) {
                if (n->output_pins[p].id == n->output_pins[q].id) {
                    serialize_error_set("duplicate pin ID");
                    return -1;
                }
            }
        }
        if (n->id >= g->id_counter) {
            g->id_counter = n->id + 1;
        }
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].id >= g->id_counter) {
                g->id_counter = n->input_pins[i].id + 1;
            }
        }
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].id >= g->id_counter) {
                g->id_counter = n->output_pins[i].id + 1;
            }
        }
    }
    /* graph_add_node inflates the counter while allocating scratch ids;
     * restore the serialized floor, then ID-2 keeps the counter at or above
     * every loaded id so new allocations never reissue a retired ID. */
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->output_count; p++) {
            if ((n->output_pins[p].field_source || n->output_pins[p].field_name) &&
                !graph_output_field(n, p, NULL)) {
                serialize_error_set("invalid exposed field metadata");
                return -1;
            }
        }
    }
    g->id_counter = requested_counter;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->id >= g->id_counter) {
            g->id_counter = n->id + 1;
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].id >= g->id_counter) {
                g->id_counter = n->input_pins[p].id + 1;
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].id >= g->id_counter) {
                g->id_counter = n->output_pins[p].id + 1;
            }
        }
    }
    graph_reindex(g);
    return 0;
}

static int load_edges(graph *g, yyjson_val *edges)
{
    yyjson_arr_iter eit;
    yyjson_arr_iter_init(edges, &eit);
    yyjson_val *v;
    while ((v = yyjson_arr_iter_next(&eit))) {
        if (!yyjson_is_obj(v)) {
            serialize_error_set("edge is not an object");
            return -1;
        }
        uint64_t from_id, from_pin, to_id, to_pin;
        if (json_uint(yyjson_obj_get(v, "from"), &from_id, "edge.from") ||
            json_uint(yyjson_obj_get(v, "from_pin"), &from_pin, "edge.from_pin") ||
            json_uint(yyjson_obj_get(v, "to"), &to_id, "edge.to") ||
            json_uint(yyjson_obj_get(v, "to_pin"), &to_pin, "edge.to_pin")) {
            return -1;
        }
        node *from = graph_node_by_id(g, from_id), *to = graph_node_by_id(g, to_id);
        int fp = -1, tp = -1, fo = 0, to_out = 0;
        const pin_spec *fps = graph_pin_by_id(from, from_pin, &fo, &fp),
                       *tps = graph_pin_by_id(to, to_pin, &to_out, &tp);
        yyjson_val *kind = yyjson_obj_get(v, "kind");
        if (!from || !to || !fps || !tps || fo != 1 || to_out != 0 ||
            !yyjson_is_str(kind)) {
            serialize_error_set("edge references missing node or pin ID");
            return -1;
        }
        const char *kind_name = yyjson_get_str(kind);
        yyjson_val *conversion_value = yyjson_obj_get(v, "conversion");
        const node_desc *conversion = NULL;
        if (conversion_value) {
            if (!yyjson_is_str(conversion_value)) {
                serialize_error_set("edge conversion is not a node name");
                return -1;
            }
            conversion = registry_node_by_name(yyjson_get_str(conversion_value));
            if (!conversion) {
                serialize_error_set("unknown edge conversion \"%s\"", yyjson_get_str(conversion_value));
                return -1;
            }
        }
        int r;
        if (strcmp(kind_name, "data") == 0) {
            if (yyjson_obj_get(v, "order")) {
                serialize_error_set("data edge has an exec order");
                return -1;
            }
            r = graph_connect_data_conversion(g, from, fp, to, tp, conversion);
        } else if (strcmp(kind_name, "exec") == 0) {
            if (conversion) {
                serialize_error_set("exec edge cannot have a conversion");
                return -1;
            }
            yyjson_val *order = yyjson_obj_get(v, "order");
            if (!yyjson_is_int(order) || yyjson_get_sint(order) < 0 ||
                yyjson_get_sint(order) > INT_MAX) {
                serialize_error_set("exec edge has an invalid order");
                return -1;
            }
            r = graph_connect_exec(g, from, fp, to, tp, (int)yyjson_get_sint(order));
        } else {
            serialize_error_set("unsupported edge kind");
            return -1;
        }
        if (r) {
            serialize_error_set("invalid edge endpoints or incompatible types");
            return -1;
        }
        yyjson_val *handle = yyjson_obj_get(v, "from_handle_x");
        yyjson_val *handle_y = yyjson_obj_get(v, "from_handle_y");
        yyjson_val *to_handle = yyjson_obj_get(v, "to_handle_x");
        yyjson_val *to_handle_y = yyjson_obj_get(v, "to_handle_y");
        if (handle || handle_y || to_handle || to_handle_y) {
            if (!handle || !handle_y || !to_handle || !to_handle_y ||
                !yyjson_is_num(handle) || !yyjson_is_num(handle_y) ||
                !yyjson_is_num(to_handle) || !yyjson_is_num(to_handle_y)) {
                serialize_error_set("edge has invalid bezier handles");
                return -1;
            }
            edge *loaded = NULL;
            for (int i = acount(g->edges) - 1; i >= 0; i--) {
                if (g->edges[i].from_id == from_id && g->edges[i].from_pin_id == from_pin &&
                    g->edges[i].to_id == to_id && g->edges[i].to_pin_id == to_pin) {
                    loaded = &g->edges[i];
                    break;
                }
            }
            assert(loaded);
            loaded->from_handle_x = (float)yyjson_get_num(handle);
            loaded->from_handle_y = (float)yyjson_get_num(handle_y);
            loaded->to_handle_x = (float)yyjson_get_num(to_handle);
            loaded->to_handle_y = (float)yyjson_get_num(to_handle_y);
            loaded->custom_handles = true;
        }
        yyjson_val *points = yyjson_obj_get(v, "reroutes");
        if (points) {
            if (!yyjson_is_arr(points) ||
                yyjson_arr_size(points) > GRAPH_EDGE_MAX_REROUTES) {
                serialize_error_set("edge has invalid reroute points");
                return -1;
            }
            edge *loaded = NULL;
            for (int i = acount(g->edges) - 1; i >= 0; i--) {
                if (g->edges[i].from_id == from_id && g->edges[i].from_pin_id == from_pin &&
                    g->edges[i].to_id == to_id && g->edges[i].to_pin_id == to_pin) {
                    loaded = &g->edges[i];
                    break;
                }
            }
            assert(loaded);
            loaded->reroute_count = 0;
            yyjson_arr_iter pit;
            yyjson_arr_iter_init(points, &pit);
            yyjson_val *point;
            while ((point = yyjson_arr_iter_next(&pit))) {
                yyjson_val *x = yyjson_obj_get(point, "x"),
                           *y = yyjson_obj_get(point, "y");
                if (!yyjson_is_obj(point) || !yyjson_is_num(x) || !yyjson_is_num(y)) {
                    serialize_error_set("edge has invalid reroute point");
                    return -1;
                }
                loaded->reroutes[loaded->reroute_count].x = (float)yyjson_get_num(x);
                loaded->reroutes[loaded->reroute_count].y = (float)yyjson_get_num(y);
                loaded->reroute_count++;
            }
        }
    }
    return serialize_migrate_component_reads(g);
}

graph *graph_load_json(const char *json)
{
    if (!json) {
        serialize_error_set("null JSON input");
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        serialize_error_set("malformed JSON");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported graph schema version (expected 3)");
        yyjson_doc_free(doc);
        return NULL;
    }
    yyjson_val *nodes = yyjson_obj_get(root, "nodes"),
               *edges = yyjson_obj_get(root, "edges");
    if (!yyjson_is_arr(nodes) || !yyjson_is_arr(edges)) {
        serialize_error_set("missing or invalid nodes or edges array");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph *g = (graph *)calloc(1, sizeof *g);
    if (!g) {
        serialize_error_set("out of memory");
        yyjson_doc_free(doc);
        return NULL;
    }
    graph_init(g);
    uint64_t requested_counter = 1;
    yyjson_val *counter = yyjson_obj_get(root, "id_counter");
    if (!counter || !yyjson_is_uint(counter) || yyjson_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = yyjson_get_uint(counter);
    g->id_counter = requested_counter;
    yyjson_val *metadata_name = yyjson_obj_get(root, "name");
    yyjson_val *metadata_description = yyjson_obj_get(root, "description");
    if (yyjson_is_str(metadata_name)) {
        g->name = graph_own_str(g, yyjson_get_str(metadata_name), yyjson_get_len(metadata_name));
    }
    if (yyjson_is_str(metadata_description)) {
        g->description = graph_own_str(g, yyjson_get_str(metadata_description), yyjson_get_len(metadata_description));
    }
    if (!g->name || !g->description) {
        serialize_error_set("missing behavior_asset metadata");
        goto fail;
    }
    yyjson_val *binding_owner = yyjson_obj_get(root, "binding_owner_id");
    if (binding_owner) {
        if (!yyjson_is_uint(binding_owner) || !yyjson_get_uint(binding_owner)) {
            serialize_error_set("invalid binding clipboard owner");
            goto fail;
        }
        g->clipboard_owner_id = yyjson_get_uint(binding_owner);
    }
    yyjson_val *breakpoints = yyjson_obj_get(root, "breakpoints_enabled");
    if (breakpoints && yyjson_is_bool(breakpoints)) {
        g->breakpoints_enabled = yyjson_get_bool(breakpoints);
    }
    yyjson_val *dataflow = yyjson_obj_get(root, "dataflow");
    if (dataflow && yyjson_is_bool(dataflow)) {
        g->dataflow = yyjson_get_bool(dataflow);
    }
    if (load_comment_boxes(g, yyjson_obj_get(root, "comment_boxes"))) {
        goto fail;
    }
    if (serialize_read_locals(g, root) || load_nodes(g, nodes, requested_counter) ||
        load_edges(g, edges) || serialize_validate_locals(g)) {
        goto fail;
    }
    yyjson_doc_free(doc);
    return g;
fail:
    yyjson_doc_free(doc);
    graph_free(g);
    free(g);
    return NULL;
}

static int load_variable(behavior_asset *asset, yyjson_val *v)
{
    if (!yyjson_is_obj(v)) {
        serialize_error_set("variable is not an object");
        return -1;
    }
    uint64_t id = 0;
    if (json_uint(yyjson_obj_get(v, "id"), &id, "variable.id")) {
        return -1;
    }
    yyjson_val *name = yyjson_obj_get(v, "name");
    yyjson_val *type = yyjson_obj_get(v, "type");
    if (!yyjson_is_str(name) || !yyjson_is_int(type)) {
        serialize_error_set("variable has malformed name or type");
        return -1;
    }
    pin_type pt = (pin_type)yyjson_get_sint(type);
    if (pt < PIN_FLOW || pt > PIN_ENUM) {
        serialize_error_set("variable has invalid type");
        return -1;
    }
    yyjson_val *tn = yyjson_obj_get(v, "type_name");
    const char *type_name = yyjson_is_str(tn) ? yyjson_get_str(tn) : NULL;
    yyjson_val *container = yyjson_obj_get(v, "container");
    pin_container pc = container && yyjson_is_int(container)
                         ? (pin_container)yyjson_get_sint(container)
                         : PIN_CONTAINER_SINGLE;
    yyjson_val *category = yyjson_obj_get(v, "category");
    const char *cat = yyjson_is_str(category) ? yyjson_get_str(category) : NULL;
    yyjson_val *def = yyjson_obj_get(v, "default");
    pin_value pv = { 0 };
    if (def) {
        if (pc >= PIN_CONTAINER_ARRAY) {
            if (serialize_read_array(pt, type_name, def, &pv, asset->graph)) {
                return -1;
            }
        } else if (serialize_read_value(pt, type_name, def, &pv, asset->graph)) {
            if (pt != PIN_STRUCT) {
                serialize_error_set("bad default for variable %llu", (unsigned long long)id);
            }
            return -1;
        }
    }
    behavior_variable *var = behavior_asset_add_variable(
        asset,
        yyjson_get_str(name),
        pt,
        type_name,
        pc,
        def ? &pv : NULL,
        cat
    );
    if (!var) {
        serialize_error_set("cannot create variable %s", yyjson_get_str(name));
        return -1;
    }
    var->id = id;
    return 0;
}

static int load_behavior_asset_graph(behavior_asset *asset, yyjson_val *obj, uint64_t requested_counter, int is_default)
{
    if (!yyjson_is_obj(obj)) {
        serialize_error_set("graph is not an object");
        return -1;
    }
    yyjson_val *kind_json = yyjson_obj_get(obj, "kind");
    yyjson_val *name_json = yyjson_obj_get(obj, "name");
    yyjson_val *description_json = yyjson_obj_get(obj, "description");
    yyjson_val *nodes = yyjson_obj_get(obj, "nodes");
    yyjson_val *edges = yyjson_obj_get(obj, "edges");
    graph_kind kind;
    if (!yyjson_is_str(kind_json) ||
        graph_kind_from_name(yyjson_get_str(kind_json), &kind) ||
        !yyjson_is_str(name_json) ||
        (description_json && !yyjson_is_str(description_json)) ||
        !yyjson_is_arr(nodes) || !yyjson_is_arr(edges)) {
        serialize_error_set("graph is missing kind, name, nodes, or edges");
        return -1;
    }
    if (is_default && kind != GRAPH_EVENT) {
        serialize_error_set("the first Behavior Asset graph must be an event graph");
        return -1;
    }
    uint64_t graph_id = 0;
    if (!is_default && json_uint(yyjson_obj_get(obj, "id"), &graph_id, "graph.id")) {
        return -1;
    }
    graph *g = is_default ? asset->graph : NULL;
    if (!g) {
        g = (graph *)xcalloc(1, sizeof *g);
        graph_init(g);
        g->owner = asset;
        g->shared_id_counter = &asset->graph->id_counter;
        g->graph_id = graph_id;
        g->kind = kind;
        g->name = graph_own_str(g, yyjson_get_str(name_json), yyjson_get_len(name_json));
        apush(asset->graphs, g);
    } else {
        g->kind = kind;
        g->graph_id = graph_id;
        g->name = graph_own_str(g, yyjson_get_str(name_json), yyjson_get_len(name_json));
    }
    if (!g->name) {
        serialize_error_set("graph name allocation failed");
        return -1;
    }
    const char *description = description_json ? yyjson_get_str(description_json) : "";
    g->description = graph_own_str(g, description, strlen(description));
    if (!g->description) {
        serialize_error_set("graph description allocation failed");
        return -1;
    }
    yyjson_val *source_asset = yyjson_obj_get(obj, "source_asset");
    if (source_asset) {
        if (!yyjson_is_str(source_asset) ||
            json_uint(yyjson_obj_get(obj, "source_graph_id"), &g->source_graph_id, "graph.source_graph_id")) {
            serialize_error_set("linked graph source requires an asset name and graph ID");
            return -1;
        }
        g->source_asset = graph_own_str(g, yyjson_get_str(source_asset), yyjson_get_len(source_asset));
    }
    yyjson_val *expression = yyjson_obj_get(obj, "math_expression");
    if (expression) {
        if (!yyjson_is_str(expression) || kind != GRAPH_FUNCTION) {
            serialize_error_set("math_expression must be source text on a function graph");
            return -1;
        }
        g->math_expression = graph_own_str(g, yyjson_get_str(expression), yyjson_get_len(expression));
    }
    yyjson_val *breakpoints = yyjson_obj_get(obj, "breakpoints_enabled");
    if (breakpoints && yyjson_is_bool(breakpoints)) {
        g->breakpoints_enabled = yyjson_get_bool(breakpoints);
    }
    yyjson_val *dataflow = yyjson_obj_get(obj, "dataflow");
    if (dataflow && yyjson_is_bool(dataflow)) {
        g->dataflow = yyjson_get_bool(dataflow);
    }
    yyjson_val *entry_json = yyjson_obj_get(obj, "entry_id");
    yyjson_val *return_json = yyjson_obj_get(obj, "return_id");
    yyjson_val *depth_json = yyjson_obj_get(obj, "call_depth_limit");
    if (entry_json && yyjson_is_uint(entry_json)) {
        g->entry_id = yyjson_get_uint(entry_json);
    }
    if (return_json && yyjson_is_uint(return_json)) {
        g->return_id = yyjson_get_uint(return_json);
    }
    if (depth_json && yyjson_is_int(depth_json)) {
        g->call_depth_limit = (int)yyjson_get_sint(depth_json);
    }
    if (load_comment_boxes(g, yyjson_obj_get(obj, "comment_boxes"))) {
        return -1;
    }
    if (serialize_read_locals(g, obj) || load_nodes(g, nodes, requested_counter) ||
        load_edges(g, edges) || serialize_validate_locals(g)) {
        return -1;
    }
    if (g->id_counter > asset->graph->id_counter) {
        asset->graph->id_counter = g->id_counter;
    }
    return 0;
}

/* Node and pin constructors allocate temporary IDs during loading. Only
 * identities retained in the loaded asset should advance its shared counter. */
static int loaded_asset_counter(behavior_asset *asset, uint64_t requested)
{
    uint64_t maximum = 0;
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id > maximum) {
            maximum = asset->variables[i].id;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (g->graph_id > maximum) {
            maximum = g->graph_id;
        }
        for (int j = 0; j < acount(g->comment_boxes); j++) {
            if (g->comment_boxes[j].id > maximum) {
                maximum = g->comment_boxes[j].id;
            }
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->id > maximum) {
                maximum = n->id;
            }
            for (int output = 0; output < 2; output++) {
                pin_spec *pins = output ? n->output_pins : n->input_pins;
                int count = output ? n->output_count : n->input_count;
                for (int p = 0; p < count; p++) {
                    if (pins[p].id > maximum) {
                        maximum = pins[p].id;
                    }
                    if (pins[p].split_parent > maximum) {
                        maximum = pins[p].split_parent;
                    }
                }
            }
        }
    }
    if (maximum == UINT64_MAX) {
        serialize_error_set("behavior_asset IDs leave no room for a next ID");
        return -1;
    }
    uint64_t next = maximum >= requested ? maximum + 1 : requested;
    for (int i = 0; i < acount(asset->graphs); i++) {
        asset->graphs[i]->id_counter = next;
    }
    return 0;
}

behavior_asset *behavior_asset_load_json(const char *json)
{
    behavior_asset *asset = NULL;
    if (!json) {
        serialize_error_set("null JSON input");
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        serialize_error_set("malformed JSON");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != GRAPH_SCHEMA_VERSION) {
        serialize_error_set("unsupported behavior_asset schema version (expected 3)");
        goto fail;
    }
    yyjson_val *graphs = yyjson_obj_get(root, "graphs");
    if (!yyjson_is_arr(graphs) || yyjson_arr_size(graphs) < 1) {
        serialize_error_set("behavior_asset requires at least one graph");
        goto fail;
    }
    yyjson_val *metadata_name = yyjson_obj_get(root, "name");
    yyjson_val *metadata_description = yyjson_obj_get(root, "description");
    if (!yyjson_is_str(metadata_name) || !yyjson_is_str(metadata_description)) {
        serialize_error_set("behavior_asset requires name and description");
        goto fail;
    }
    uint64_t requested_counter = 1;
    yyjson_val *counter = yyjson_obj_get(root, "id_counter");
    if (!counter || !yyjson_is_uint(counter) || yyjson_get_uint(counter) == 0) {
        serialize_error_set("missing or invalid id_counter");
        goto fail;
    }
    requested_counter = yyjson_get_uint(counter);
    asset = behavior_asset_new(yyjson_get_str(metadata_name));
    if (!asset) {
        serialize_error_set("out of memory");
        goto fail;
    }
    asset->graph->id_counter = requested_counter;
    const char *domain = yyjson_get_str(yyjson_obj_get(root, "domain"));
    if (domain) {
        int found = 0;
        for (int i = BEHAVIOR_DOMAIN_EVENT; i <= BEHAVIOR_DOMAIN_SHADER_LIBRARY; i++) {
            if (!strcmp(domain, behavior_domain_name((behavior_domain)i))) {
                asset->domain = (behavior_domain)i;
                found = 1;
                break;
            }
        }
        if (!found) {
            serialize_error_set("unknown graph domain: %s", domain);
            goto fail;
        }
    }
    asset->bounds[0] = (float)yyjson_get_num(yyjson_obj_get(root, "bounds_x"));
    asset->bounds[1] = (float)yyjson_get_num(yyjson_obj_get(root, "bounds_y"));
    behavior_asset_set_description(asset, yyjson_get_str(metadata_description));
    yyjson_val *owner = yyjson_obj_get(root, "owner_type");
    if (yyjson_is_str(owner) &&
        behavior_asset_set_owner_type(asset, yyjson_get_str(owner))) {
        serialize_error_set("cannot set owner type");
        goto fail;
    }
    yyjson_val *vars = yyjson_obj_get(root, "variables");
    if (vars && !yyjson_is_arr(vars)) {
        serialize_error_set("variables must be an array");
        goto fail;
    }
    if (vars) {
        yyjson_arr_iter vit;
        yyjson_arr_iter_init(vars, &vit);
        yyjson_val *vv;
        while ((vv = yyjson_arr_iter_next(&vit))) {
            if (load_variable(asset, vv)) {
                goto fail;
            }
        }
    }
    yyjson_arr_iter git;
    yyjson_arr_iter_init(graphs, &git);
    yyjson_val *graph_json;
    int graph_index = 0;
    while ((graph_json = yyjson_arr_iter_next(&git))) {
        if (load_behavior_asset_graph(asset, graph_json, requested_counter, graph_index == 0)) {
            goto fail;
        }
        graph_index++;
    }
    if (graph_index == 0) {
        serialize_error_set("behavior_asset requires a default event graph");
        goto fail;
    }
    if (serialize_repair_math_calls(asset)) {
        goto fail;
    }
    if (loaded_asset_counter(asset, requested_counter)) {
        goto fail;
    }
    yyjson_doc_free(doc);
    return asset;
fail:
    yyjson_doc_free(doc);
    if (asset) {
        behavior_asset_free(asset);
    }
    return NULL;
}
