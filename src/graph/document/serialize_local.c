#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"

#include <limits.h>
#include <string.h>

int serialize_read_locals(graph *g, yyjson_val *obj)
{
    yyjson_val *locals = yyjson_obj_get(obj, "locals");
    if (!locals) {
        return 0;
    }
    if (!yyjson_is_arr(locals)) {
        serialize_error_set("locals must be an array");
        return -1;
    }
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(locals, i, count, value)
    {
        yyjson_val *id = yyjson_obj_get(value, "id");
        yyjson_val *name = yyjson_obj_get(value, "name");
        yyjson_val *source = yyjson_obj_get(value, "source");
        yyjson_val *pin = yyjson_obj_get(value, "pin");
        /* A "<node>.<pin>" source resolves once nodes are loaded. */
        bool reference = yyjson_is_str(yyjson_obj_get(value, "from"));
        if (!yyjson_is_uint(id) || !yyjson_get_uint(id) || yyjson_get_uint(id) >= INT_MAX ||
            (!reference && (!yyjson_is_uint(source) || !yyjson_get_uint(source) ||
                            !yyjson_is_uint(pin) || !yyjson_get_uint(pin))) ||
            !yyjson_is_str(name) || !yyjson_get_len(name)) {
            serialize_error_set("local has invalid identity, name, or source");
            return -1;
        }
        graph_local local = {
            .id = yyjson_get_uint(id),
            .name = graph_own_str(g, yyjson_get_str(name), yyjson_get_len(name)),
            .source_id = yyjson_get_uint(source),
            .source_pin_id = yyjson_get_uint(pin)
        };
        apush(g->locals, local);
    }
    return 0;
}

int serialize_resolve_locals(graph *g, yyjson_val *obj)
{
    yyjson_val *locals = yyjson_obj_get(obj, "locals");
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(locals, i, count, value)
    {
        yyjson_val *from = yyjson_obj_get(value, "from");
        node *source = NULL;
        int index = -1;
        if (!yyjson_is_str(from)) {
            continue;
        }
        if (!serialize_resolve_ref(g, yyjson_get_str(from), 1, &source, &index)) {
            serialize_error_set("local names no pin: %s", yyjson_get_str(from));
            return -1;
        }
        g->locals[i].source_id = source->id;
        g->locals[i].source_pin_id = source->output_pins[index].id;
    }
    return 0;
}

int serialize_validate_locals(graph *g)
{
    const char *error = graph_validate_locals(g);
    if (error) {
        serialize_error_set("%s", error);
        return -1;
    }
    for (int i = 0; i < acount(g->locals); i++) {
        uint64_t id = g->locals[i].id;
        for (int n = 0; n < acount(g->nodes); n++) {
            if (g->nodes[n]->id == id || graph_pin_by_id(g->nodes[n], id, NULL, NULL)) {
                serialize_error_set("local ID collides with a node or pin");
                return -1;
            }
        }
        if (g->id_counter <= id) {
            g->id_counter = id + 1;
        }
        if (g->shared_id_counter && *g->shared_id_counter <= id) {
            *g->shared_id_counter = id + 1;
        }
    }
    return 0;
}

void serialize_write_locals(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const graph *g,
    node *const *subset,
    int count
)
{
    if (!acount(g->locals)) {
        return;
    }
    yyjson_mut_val *locals = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        bool included = !subset;
        for (int n = 0; n < count; n++) {
            included |= subset[n]->id == local->source_id;
        }
        if (!included) {
            continue;
        }
        yyjson_mut_val *value = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, value, "id", local->id);
        yyjson_mut_obj_add_str(doc, value, "name", local->name);
        node *source = graph_node_by_id(g, local->source_id);
        int index = -1;
        char ref[SERIALIZE_KEY_SIZE + 32];
        if (source && graph_pin_by_id(source, local->source_pin_id, NULL, &index) &&
            serialize_pin_ref(source, 1, index, ref, sizeof ref)) {
            yyjson_mut_obj_add_strcpy(doc, value, "from", ref);
        } else {
            yyjson_mut_obj_add_uint(doc, value, "source", local->source_id);
            yyjson_mut_obj_add_uint(doc, value, "pin", local->source_pin_id);
        }
        yyjson_mut_arr_add_val(locals, value);
    }
    yyjson_mut_obj_add_val(doc, obj, "locals", locals);
}
