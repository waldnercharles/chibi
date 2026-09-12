#include "graph/runtime/runtime.h"

#include "graph/model/graph.h"
#include "graph/model/node.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct runtime_node
{
    runtime_store *store;
    const node *node;
    uint64_t node_id;
    pin_value *inputs;
    pin_value *outputs;
    int input_count;
    int output_count;
    uint64_t computed_epoch;
    uint64_t visiting_epoch;
};

struct runtime_store
{
    const graph *graph;
    uint64_t graph_lifetime;
    runtime_node **nodes;
    runtime_node **by_id;
    uint64_t memo_epoch;
};

static int graph_has_node(const graph *g, const node *n)
{
    return g && n && graph_node_by_id(g, n->id) == n;
}

static void values_resize(pin_value **values, int *count, int want, const pin_spec *pins)
{
    int old = *count;
    if (want == old) {
        return;
    }
    if (want) {
        *values = (pin_value *)xrealloc(*values, (size_t)want * sizeof **values);
    } else {
        free(*values);
        *values = NULL;
    }
    if (want > old) {
        memset(*values + old, 0, (size_t)(want - old) * sizeof **values);
        for (int i = old; i < want; i++) {
            (*values)[i] = pins[i].default_value;
        }
    }
    *count = want;
}

static void runtime_node_resize(runtime_node *state, const node *n)
{
    values_resize(&state->inputs, &state->input_count, n->input_count, n->input_pins);
    values_resize(&state->outputs, &state->output_count, n->output_count, n->output_pins);
}

static _Thread_local uint64_t store_creations;

uint64_t runtime_store_creations()
{
    return store_creations;
}

runtime_store *runtime_store_create(const graph *g)
{
    assert(g);
    store_creations++;
    runtime_store *store = (runtime_store *)xcalloc(1, sizeof *store);
    store->graph = g;
    store->graph_lifetime = g->lifetime_id;
    store->memo_epoch = 1;
    return store;
}

void runtime_store_free(runtime_store *store)
{
    if (!store) {
        return;
    }
    for (int i = 0; i < acount(store->nodes); i++) {
        runtime_node *state = store->nodes[i];
        free(state->inputs);
        free(state->outputs);
        free(state);
    }
    afree(store->nodes);
    afree(store->by_id);
    free(store);
}

static void runtime_node_reset_memo(runtime_node *state)
{
    state->computed_epoch = 0;
    state->visiting_epoch = 0;
}

static void runtime_node_reset(runtime_node *state)
{
    runtime_node_reset_memo(state);
    for (int i = 0; i < state->input_count; i++) {
        state->inputs[i] = state->node->input_pins[i].default_value;
    }
    for (int i = 0; i < state->output_count; i++) {
        state->outputs[i] = state->node->output_pins[i].default_value;
    }
}

static void runtime_node_free(runtime_node *state)
{
    free(state->inputs);
    free(state->outputs);
    free(state);
}

void runtime_store_reset(runtime_store *store)
{
    if (!store || !runtime_store_matches_graph(store, store->graph)) {
        return;
    }
    if (acount(store->by_id)) {
        memset(store->by_id, 0, (size_t)acount(store->by_id) * sizeof *store->by_id);
    }
    for (int i = 0; i < acount(store->nodes);) {
        runtime_node *state = store->nodes[i];
        if (graph_node_by_id(store->graph, state->node_id) != state->node) {
            runtime_node_free(state);
            memmove(
                store->nodes + i,
                store->nodes + i + 1,
                (size_t)(acount(store->nodes) - i - 1) * sizeof *store->nodes
            );
            asetlen(store->nodes, acount(store->nodes) - 1);
            continue;
        }
        runtime_node_reset(state);
        store->by_id[state->node_id] = state;
        i++;
    }
}

void runtime_store_reset_memo(runtime_store *store)
{
    if (!store) {
        return;
    }
    /* Every Exec step invalidates pure reads, but must retain earlier flow
       outputs. Advancing an epoch avoids walking the graph for every step. */
    if (++store->memo_epoch == 0) {
        for (int i = 0; i < acount(store->nodes); i++) {
            runtime_node_reset_memo(store->nodes[i]);
        }
        store->memo_epoch = 1;
    }
}

const graph *runtime_store_graph(const runtime_store *store)
{
    return store ? store->graph : NULL;
}

bool runtime_store_matches_graph(const runtime_store *store, const graph *g)
{
    return store && g && store->graph == g && store->graph_lifetime == g->lifetime_id;
}

static runtime_node *runtime_store_find(const runtime_store *store, const node *n)
{
    if (n->id >= (uint64_t)acount(store->by_id)) {
        return NULL;
    }
    runtime_node *state = store->by_id[n->id];
    return state && state->node == n && state->node_id == n->id ? state : NULL;
}

runtime_node *runtime_store_node(runtime_store *store, const node *n)
{
    assert(store && n);
    if (!runtime_store_matches_graph(store, store->graph)) {
        return NULL;
    }
    assert(graph_has_node(store->graph, n));
    runtime_node *state = runtime_store_find(store, n);
    if (state) {
        if (state->input_count != n->input_count ||
            state->output_count != n->output_count) {
            runtime_node_resize(state, n);
        }
        return state;
    }
    state = (runtime_node *)xcalloc(1, sizeof *state);
    state->store = store;
    state->node = n;
    state->node_id = n->id;
    runtime_node_resize(state, n);
    runtime_node_reset(state);
    apush(store->nodes, state);
    if ((uint64_t)acount(store->by_id) <= n->id) {
        assert(n->id < INT_MAX);
        int old = acount(store->by_id);
        int count = (int)n->id + 1;
        asetlen(store->by_id, count);
        memset(store->by_id + old, 0, (size_t)(count - old) * sizeof *store->by_id);
    }
    store->by_id[n->id] = state;
    return state;
}

const runtime_node *runtime_store_node_const(const runtime_store *store, const node *n)
{
    assert(store && n);
    if (!runtime_store_matches_graph(store, store->graph)) {
        return NULL;
    }
    assert(graph_has_node(store->graph, n));
    return runtime_store_find(store, n);
}

pin_value *runtime_node_inputs(runtime_node *state)
{
    assert(state);
    return state->inputs;
}

const pin_value *runtime_node_inputs_const(const runtime_node *state)
{
    assert(state);
    return state->inputs;
}

pin_value *runtime_node_outputs(runtime_node *state)
{
    assert(state);
    return state->outputs;
}

const pin_value *runtime_node_outputs_const(const runtime_node *state)
{
    assert(state);
    return state->outputs;
}

int runtime_node_computed(const runtime_node *state)
{
    assert(state);
    return state->computed_epoch == state->store->memo_epoch;
}

void runtime_node_set_computed(runtime_node *state, int computed)
{
    assert(state);
    state->computed_epoch = computed ? state->store->memo_epoch : 0;
}

int runtime_node_visiting(const runtime_node *state)
{
    assert(state);
    return state->visiting_epoch == state->store->memo_epoch;
}

void runtime_node_set_visiting(runtime_node *state, int visiting)
{
    assert(state);
    state->visiting_epoch = visiting ? state->store->memo_epoch : 0;
}
