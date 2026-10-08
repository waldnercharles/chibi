#include "graph/document/serialize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
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

static av_value *pin_side(av_value *node_obj, int output)
{
    return av_obj_get(node_obj, output ? "outputs" : "inputs");
}

/* A keyed side maps pin keys to entries; a listed side holds them in order. */
static av_value *pin_entry(av_value *side, const node *n, int output, int index)
{
    if (av_is_arr(side)) {
        return av_arr_get(side, (size_t)index);
    }
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    return av_obj_get(side, key);
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

static bool candidate(const pin_spec *p, av_value *entry)
{
    if (p->kind != PIN_DATA || !(p->flags & PIN_FLAG_WILDCARD) || p->binding ||
        !av_is_obj(entry) || av_obj_get(entry, "default") ||
        av_obj_get(entry, "binding")) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (av_obj_get(entry, SHAPE_KEYS[i])) {
            return true;
        }
    }
    return false;
}

/* Rewrites an entry that lost fields the way the writer would have written it. */
static void tidy_entry(av_doc *doc, av_value *node_obj, const node *n, int output, int index)
{
    av_value *side = pin_side(node_obj, output);
    av_value *entry = pin_entry(side, n, output, index);
    if (av_is_arr(side)) {
        /* A listed pin with nothing but its ID is written as the ID. */
        if (av_obj_size(entry) == 1 && av_obj_get(entry, "id")) {
            av_arr_replace(side, (size_t)index, av_obj_get(entry, "id"));
        }
        return;
    }
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    av_value *value = av_obj_get(entry, "default");
    if (doc && av_obj_size(entry) == 1 && value && !av_is_ctn(value)) {
        /* A pin with nothing but a scalar default is written as the value. */
        av_obj_set(doc, side, key, value);
        return;
    }
    int template_count = output ? n->desc->output_count : n->desc->input_count;
    if (av_obj_size(entry) || index >= template_count) {
        return;
    }
    av_obj_remove_key(side, key);
    if (!av_obj_size(side)) {
        av_obj_remove_key(node_obj, output ? "outputs" : "inputs");
    }
}

static void strip_pin(av_value *node_obj, const node *n, int output, int index)
{
    av_value *entry = pin_entry(pin_side(node_obj, output), n, output, index);
    for (int i = 0; i < 3; i++) {
        av_obj_remove_key(entry, SHAPE_KEYS[i]);
    }
    tidy_entry(NULL, node_obj, n, output, index);
}

static const node *document_node(const graph *g, av_value *node_obj)
{
    return graph_node_by_id(g, av_get_uint(av_obj_get(node_obj, "id")));
}

static pin_ref *collect(av_value *obj, const graph *g)
{
    pin_ref *refs = NULL;
    size_t i, count;
    av_value *node_obj;
    av_arr_foreach(av_obj_get(obj, "nodes"), i, count, node_obj)
    {
        const node *n = document_node(g, node_obj);
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

static void strip(av_value *obj, const graph *g, const pin_ref *refs)
{
    size_t i, count;
    av_value *node_obj;
    av_arr_foreach(av_obj_get(obj, "nodes"), i, count, node_obj)
    {
        const node *n = document_node(g, node_obj);
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

static int trial(av_value *obj, const graph *g, pin_ref *refs)
{
    av_doc *doc = av_doc_new();
    av_value *copy = av_clone(doc, obj);
    av_doc_set_root(doc, copy);
    strip(copy, g, refs);
    graph *loaded = serialize_load_graph_body(copy, g->id_counter);
    int removed = loaded ? keep_mismatches(g, loaded, refs) : -1;
    if (loaded) {
        graph_free(loaded);
        free(loaded);
    }
    av_doc_free(doc);
    return removed;
}

void serialize_omit_implied_shapes(av_value *obj, const graph *g)
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

bool serialize_local_call(const node *n)
{
    return !n->reference_asset &&
           (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
            n->desc == &NODE_BehaviorExpression);
}

uint64_t serialize_call_declaration(const behavior_asset *asset, const node *call, int output, int index)
{
    const graph *target = asset && serialize_local_call(call)
                            ? behavior_asset_find_graph(asset, call->reference_id)
                            : NULL;
    const node *signature = target ? graph_node_by_id(
                                         target,
                                         output ? target->return_id : target->entry_id
                                     )
                                   : NULL;
    const pin_spec *pin = output ? &call->output_pins[index]
                                 : &call->input_pins[index];
    if (!signature || pin->kind != PIN_DATA || pin->field_source || !pin->name) {
        return 0;
    }
    const pin_spec *pins = output ? signature->input_pins : signature->output_pins;
    int count = output ? signature->input_count : signature->output_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_DATA && !pins[i].field_source && pins[i].name &&
            !strcmp(pins[i].name, pin->name)) {
            return pins[i].id;
        }
    }
    return 0;
}

void serialize_derive_declarations(behavior_asset *asset)
{
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            node *n = asset->graphs[g]->nodes[i];
            for (int output = 0; output < 2; output++) {
                pin_spec *pins = output ? n->output_pins : n->input_pins;
                for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                    if (pins[p].declaration_id == SERIALIZE_DERIVED_DECLARATION) {
                        pins[p].declaration_id = serialize_call_declaration(asset, n, output, p);
                    }
                }
            }
        }
    }
}

/* Loading derives a declaration the entry omits, so the entry records one only
 * when it differs from the derived pin, including an explicit 0. */
static void write_declaration(av_doc *doc, av_value *node_obj, const node *n, int output, int index, uint64_t derived)
{
    const pin_spec *p = output ? &n->output_pins[index] : &n->input_pins[index];
    av_value *side = pin_side(node_obj, output);
    av_value *entry = pin_entry(side, n, output, index);
    if (!entry) {
        return;
    }
    if (av_is_obj(entry)) {
        if (p->declaration_id == derived) {
            av_obj_remove_key(entry, "declaration_id");
            tidy_entry(doc, node_obj, n, output, index);
        } else if (!av_obj_get(entry, "declaration_id")) {
            av_obj_set(doc, entry, "declaration_id", av_uint(doc, p->declaration_id));
        }
        return;
    }
    if (p->declaration_id == derived) {
        return;
    }
    av_value *expanded = av_obj(doc);
    av_obj_set(doc, expanded, av_is_arr(side) ? "id" : "default", av_clone(doc, entry));
    av_obj_set(doc, expanded, "declaration_id", av_uint(doc, p->declaration_id));
    if (av_is_arr(side)) {
        av_arr_replace(side, (size_t)index, expanded);
        return;
    }
    char key[SERIALIZE_KEY_SIZE];
    serialize_pin_key(n, output, index, key, sizeof key);
    av_obj_set(doc, side, key, expanded);
}

void serialize_omit_derived_declarations(av_doc *doc, av_value *obj, const graph *g)
{
    size_t i, count;
    av_value *node_obj;
    av_arr_foreach(av_obj_get(obj, "nodes"), i, count, node_obj)
    {
        const node *n = document_node(g, node_obj);
        for (int output = 0; serialize_local_call(n) && output < 2; output++) {
            for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                write_declaration(
                    doc,
                    node_obj,
                    n,
                    output,
                    p,
                    serialize_call_declaration(g->owner, n, output, p)
                );
            }
        }
    }
}
