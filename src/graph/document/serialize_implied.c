#include "graph/document/serialize_internal.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

/* Loading connects edges in document order, and connecting a wildcard pin
 * resolves its type and its group's. A document omits a wildcard pin's type,
 * type name and container when loading the written graph without them
 * reproduces the pin. A trial load checks each candidate; mismatches keep
 * their fields, along with candidates on the nodes that feed them. */

#define IMPLIED_TRIALS 16

typedef struct
{
    uint64_t node_id, pin_id;
} pin_ref;

static const char *const SHAPE_KEYS[] = { "type", "type_name", "container" };

static yyjson_mut_val *pin_side(yyjson_mut_val *node_obj, int output)
{
    return yyjson_mut_obj_get(node_obj, output ? "outputs" : "inputs");
}

/* A keyed side maps pin keys to entries; a listed side holds them in order. */
static yyjson_mut_val *pin_entry(yyjson_mut_val *side, const node *n, int output, int index)
{
    if (yyjson_mut_is_arr(side)) {
        return yyjson_mut_arr_get(side, (size_t)index);
    }
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    return yyjson_mut_obj_get(side, key);
}

static int find_ref(const pin_ref *refs, uint64_t node_id, uint64_t pin_id)
{
    for (int i = 0; i < acount(refs); i++) {
        if (refs[i].node_id == node_id && refs[i].pin_id == pin_id) {
            return i;
        }
    }
    return -1;
}

static bool candidate(const pin_spec *p, yyjson_mut_val *entry)
{
    if (p->kind != PIN_DATA || !(p->flags & PIN_FLAG_WILDCARD) || p->binding ||
        !yyjson_mut_is_obj(entry) || yyjson_mut_obj_get(entry, "default") ||
        yyjson_mut_obj_get(entry, "binding")) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (yyjson_mut_obj_get(entry, SHAPE_KEYS[i])) {
            return true;
        }
    }
    return false;
}

static void strip_pin(yyjson_mut_val *node_obj, const node *n, int output, int index)
{
    yyjson_mut_val *side = pin_side(node_obj, output);
    yyjson_mut_val *entry = pin_entry(side, n, output, index);
    for (int i = 0; i < 3; i++) {
        yyjson_mut_obj_remove_key(entry, SHAPE_KEYS[i]);
    }
    if (yyjson_mut_is_arr(side)) {
        /* A listed pin with nothing but its ID is written as the ID. */
        if (yyjson_mut_obj_size(entry) == 1 && yyjson_mut_obj_get(entry, "id")) {
            yyjson_mut_arr_replace(side, (size_t)index, yyjson_mut_obj_remove_key(entry, "id"));
        }
        return;
    }
    int template_count = output ? n->desc->output_count : n->desc->input_count;
    if (yyjson_mut_obj_size(entry) || index >= template_count) {
        return;
    }
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    yyjson_mut_obj_remove_key(side, key);
    if (!yyjson_mut_obj_size(side)) {
        yyjson_mut_obj_remove_key(node_obj, output ? "outputs" : "inputs");
    }
}

static const node *json_node(const graph *g, yyjson_mut_val *node_obj)
{
    return graph_node_by_id(g, yyjson_mut_get_uint(yyjson_mut_obj_get(node_obj, "id")));
}

static pin_ref *collect(yyjson_mut_val *obj, const graph *g)
{
    pin_ref *refs = NULL;
    size_t i, count;
    yyjson_mut_val *node_obj;
    yyjson_mut_arr_foreach(yyjson_mut_obj_get(obj, "nodes"), i, count, node_obj)
    {
        const node *n = json_node(g, node_obj);
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                if (candidate(&pins[p], pin_entry(pin_side(node_obj, output), n, output, p))) {
                    apush(refs, ((pin_ref){ n->id, pins[p].id }));
                }
            }
        }
    }
    return refs;
}

static void strip(yyjson_mut_val *obj, const graph *g, const pin_ref *refs)
{
    size_t i, count;
    yyjson_mut_val *node_obj;
    yyjson_mut_arr_foreach(yyjson_mut_obj_get(obj, "nodes"), i, count, node_obj)
    {
        const node *n = json_node(g, node_obj);
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                if (find_ref(refs, n->id, pins[p].id) >= 0) {
                    strip_pin(node_obj, n, output, p);
                }
            }
        }
    }
}

static bool same_shape(const pin_spec *a, const pin_spec *b)
{
    return a->type == b->type && a->container == b->container &&
           (a->type_name == b->type_name ||
            (a->type_name && b->type_name && !strcmp(a->type_name, b->type_name))) &&
           serialize_same_value(&a->default_value, &b->default_value);
}

static int remove_node_candidates(pin_ref *refs, uint64_t node_id)
{
    int removed = 0;
    for (int i = acount(refs) - 1; i >= 0; i--) {
        if (refs[i].node_id == node_id) {
            refs[i] = refs[acount(refs) - 1];
            asetlen(refs, acount(refs) - 1);
            removed++;
        }
    }
    return removed;
}

/* A pin that loads differently keeps its fields when it is a candidate;
 * otherwise candidates on its node and on the nodes feeding it may have caused
 * the difference. Returns the number of candidates removed, or -1 when the
 * trial could not load. */
static int keep_mismatches(const graph *g, const graph *loaded, pin_ref *refs)
{
    int removed = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i], *m = graph_node_by_id(loaded, n->id);
        if (!m || m->input_count != n->input_count || m->output_count != n->output_count) {
            return -1;
        }
        bool mismatch = false;
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            const pin_spec *trial = output ? m->output_pins : m->input_pins;
            for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                if (same_shape(&pins[p], &trial[p])) {
                    continue;
                }
                int index = find_ref(refs, n->id, pins[p].id);
                if (index < 0) {
                    mismatch = true;
                    continue;
                }
                refs[index] = refs[acount(refs) - 1];
                asetlen(refs, acount(refs) - 1);
                removed++;
            }
        }
        if (!mismatch) {
            continue;
        }
        removed += remove_node_candidates(refs, n->id);
        for (int e = 0; e < acount(g->edges); e++) {
            if (g->edges[e].to_id == n->id && g->edges[e].kind == GRAPH_EDGE_DATA) {
                removed += remove_node_candidates(refs, g->edges[e].from_id);
            }
        }
    }
    return removed;
}

static int trial(yyjson_mut_val *obj, const graph *g, pin_ref *refs)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *copy = yyjson_mut_val_mut_copy(doc, obj);
    yyjson_mut_doc_set_root(doc, copy);
    strip(copy, g, refs);
    yyjson_doc *written = yyjson_mut_doc_imut_copy(doc, NULL);
    yyjson_mut_doc_free(doc);
    graph *loaded = written
                      ? serialize_load_graph_body(yyjson_doc_get_root(written), g->id_counter)
                      : NULL;
    int removed = loaded ? keep_mismatches(g, loaded, refs) : -1;
    if (loaded) {
        graph_free(loaded);
        free(loaded);
    }
    yyjson_doc_free(written);
    return removed;
}

void serialize_omit_implied_shapes(yyjson_mut_val *obj, const graph *g)
{
    pin_ref *refs = collect(obj, g);
    int removed = 1;
    for (int round = 0; acount(refs) && removed > 0 && round < IMPLIED_TRIALS; round++) {
        removed = trial(obj, g, refs);
    }
    if (removed) {
        asetlen(refs, 0);
    }
    strip(obj, g, refs);
    afree(refs);
}
