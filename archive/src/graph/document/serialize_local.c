#include "graph/authoring/local_edit.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"

#include <limits.h>
#include <string.h>

int serialize_read_locals(graph *g, av_value *obj)
{
    av_value *locals = av_obj_get(obj, "locals");
    if (!locals) {
        return 0;
    }
    if (!av_is_arr(locals)) {
        serialize_error_set("locals must be an array");
        return -1;
    }
    size_t i, count;
    av_value *value;
    av_arr_foreach(locals, i, count, value)
    {
        av_value *id = av_obj_get(value, "id");
        av_value *name = av_obj_get(value, "name");
        av_value *source = av_obj_get(value, "source");
        av_value *pin = av_obj_get(value, "pin");
        /* A "<node>.<pin>" source resolves once nodes are loaded. */
        bool reference = av_is_str(av_obj_get(value, "from"));
        if (!av_is_uint(id) || !av_get_uint(id) || av_get_uint(id) >= INT_MAX ||
            (!reference && (!av_is_uint(source) || !av_get_uint(source) ||
                            !av_is_uint(pin) || !av_get_uint(pin))) ||
            !av_is_str(name) || !av_get_len(name)) {
            serialize_error_set("local has invalid identity, name, or source");
            return -1;
        }
        graph_local local = { .id = av_get_uint(id),
                              .name = graph_own_str(g, av_get_str(name), av_get_len(name)),
                              .source_id = av_get_uint(source),
                              .source_pin_id = av_get_uint(pin) };
        apush(g->locals, local);
    }
    return 0;
}

int serialize_resolve_locals(graph *g, av_value *obj)
{
    av_value *locals = av_obj_get(obj, "locals");
    size_t i, count;
    av_value *value;
    av_arr_foreach(locals, i, count, value)
    {
        av_value *from = av_obj_get(value, "from");
        node *source = NULL;
        int index = -1;
        if (!av_is_str(from)) {
            continue;
        }
        if (!serialize_resolve_ref(g, av_get_str(from), 1, &source, &index)) {
            serialize_error_set("local names no pin: %s", av_get_str(from));
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

void serialize_write_locals(av_doc *doc, av_value *obj, const graph *g, node *const *subset, int count)
{
    if (!acount(g->locals)) {
        return;
    }
    av_value *locals = av_arr(doc);
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        bool included = !subset;
        for (int n = 0; n < count; n++) {
            included |= subset[n]->id == local->source_id;
        }
        if (!included) {
            continue;
        }
        av_value *value = av_obj(doc);
        av_obj_set(doc, value, "id", av_uint(doc, local->id));
        av_obj_set(doc, value, "name", av_str(doc, local->name));
        node *source = graph_node_by_id(g, local->source_id);
        int index = -1;
        char ref[SERIALIZE_KEY_SIZE + 32];
        if (source && graph_pin_by_id(source, local->source_pin_id, NULL, &index) &&
            serialize_pin_ref(source, 1, index, ref, sizeof ref)) {
            av_obj_set(doc, value, "from", av_str(doc, ref));
        } else {
            av_obj_set(doc, value, "source", av_uint(doc, local->source_id));
            av_obj_set(doc, value, "pin", av_uint(doc, local->source_pin_id));
        }
        av_arr_append(locals, value);
    }
    av_obj_set(doc, obj, "locals", locals);
}
