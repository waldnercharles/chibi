#include "graph/document/serialize_internal.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static uint64_t remap_id(uint64_t old, const uint64_t *old_ids, const uint64_t *new_ids, int count)
{
    for (int i = 0; i < count; i++) {
        if (old_ids[i] == old) {
            return new_ids[i];
        }
    }
    return old;
}

int graph_append_json(graph *g, const char *json, float dx, float dy)
{
    if (!g || g->flow_pause_count) {
        serialize_error_set("cannot append while a flow run is non-idle");
        return -1;
    }
    graph *tmp = graph_load_json(json);
    if (!tmp) {
        return -1;
    }
    int node_count = acount(tmp->nodes);
    int id_count = node_count;
    for (int i = 0; i < node_count; i++) {
        id_count += tmp->nodes[i]->input_count + tmp->nodes[i]->output_count;
    }
    uint64_t *old_ids = (uint64_t *)xmalloc(
        (size_t)(id_count ? id_count : 1) * sizeof *old_ids
    );
    uint64_t *new_ids = (uint64_t *)xmalloc(
        (size_t)(id_count ? id_count : 1) * sizeof *new_ids
    );
    int k = 0;
    for (int i = 0; i < node_count; i++) {
        node *n = tmp->nodes[i];
        old_ids[k] = n->id;
        new_ids[k] = graph_alloc_id(g);
        n->id = new_ids[k];
        k++;
        n->x += dx;
        n->y += dy;
        for (int p = 0; p < n->input_count; p++) {
            old_ids[k] = n->input_pins[p].id;
            new_ids[k] = graph_alloc_id(g);
            n->input_pins[p].id = new_ids[k];
            k++;
        }
        for (int p = 0; p < n->output_count; p++) {
            old_ids[k] = n->output_pins[p].id;
            new_ids[k] = graph_alloc_id(g);
            n->output_pins[p].id = new_ids[k];
            k++;
        }
    }
    for (int i = 0; i < node_count; i++) {
        node *n = tmp->nodes[i];
        for (int p = 0; p < n->output_count; p++) {
            n->output_pins[p]
                .field_source = remap_id(n->output_pins[p].field_source, old_ids, new_ids, k);
        }
    }
    for (int i = 0; i < acount(tmp->edges); i++) {
        edge *e = &tmp->edges[i];
        e->from_id = remap_id(e->from_id, old_ids, new_ids, k);
        e->to_id = remap_id(e->to_id, old_ids, new_ids, k);
        e->from_pin_id = remap_id(e->from_pin_id, old_ids, new_ids, k);
        e->to_pin_id = remap_id(e->to_pin_id, old_ids, new_ids, k);
    }
    free(old_ids);
    free(new_ids);
    int nn = acount(g->nodes) + acount(tmp->nodes),
        ne = acount(g->edges) + acount(tmp->edges),
        ns = acount(g->strings) + acount(tmp->strings),
        nb = acount(g->blocks) + acount(tmp->blocks);
    asetcap(g->nodes, nn);
    asetcap(g->edges, ne);
    asetcap(g->strings, ns);
    asetcap(g->blocks, nb);
    if (acount(tmp->nodes)) {
        memcpy(
            g->nodes + acount(g->nodes),
            tmp->nodes,
            (size_t)acount(tmp->nodes) * sizeof *tmp->nodes
        );
    }
    if (acount(tmp->edges)) {
        memcpy(
            g->edges + acount(g->edges),
            tmp->edges,
            (size_t)acount(tmp->edges) * sizeof *tmp->edges
        );
    }
    if (acount(tmp->strings)) {
        memcpy(
            g->strings + acount(g->strings),
            tmp->strings,
            (size_t)acount(tmp->strings) * sizeof *tmp->strings
        );
    }
    if (acount(tmp->blocks)) {
        memcpy(
            g->blocks + acount(g->blocks),
            tmp->blocks,
            (size_t)acount(tmp->blocks) * sizeof *tmp->blocks
        );
    }
    asetlen(g->nodes, nn);
    asetlen(g->edges, ne);
    asetlen(g->strings, ns);
    asetlen(g->blocks, nb);
    graph_reindex(g);
    g->topology_revision++;
    afree(tmp->nodes);
    afree(tmp->edges);
    afree(tmp->strings);
    afree(tmp->blocks);
    free(tmp);
    return 0;
}
