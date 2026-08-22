#include "runtime.h"

#include "array.h"
#include "graph.h"
#include "node.h"
#include "xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct runtime_node
{
    const node *node;
    uint64_t node_id;
    pin_value *inputs;
    pin_value *outputs;
    int input_count;
    int output_count;
    int computed;
    int visiting;
    int eval_error;
    const char *eval_error_message;
};

struct runtime_store
{
    const graph *graph;
    runtime_node **nodes;
};

static int graph_has_node(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return 1;
        }
    }
    return 0;
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

runtime_store *runtime_store_create(const graph *g)
{
    assert(g);
    runtime_store *store = (runtime_store *)xcalloc(1, sizeof *store);
    store->graph = g;
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
    free(store);
}

static void runtime_node_reset_memo(runtime_node *state)
{
    state->computed = 0;
    state->visiting = 0;
    state->eval_error = 0;
    state->eval_error_message = NULL;
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
    if (!store) {
        return;
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
        i++;
    }
}

void runtime_store_reset_memo(runtime_store *store)
{
    if (!store) {
        return;
    }
    for (int i = 0; i < acount(store->nodes); i++) {
        runtime_node_reset_memo(store->nodes[i]);
    }
}

const graph *runtime_store_graph(const runtime_store *store)
{
    return store ? store->graph : NULL;
}

static runtime_node *runtime_store_find(runtime_store *store, const node *n)
{
    for (int i = 0; i < acount(store->nodes); i++) {
        if (store->nodes[i]->node == n && store->nodes[i]->node_id == n->id) {
            return store->nodes[i];
        }
    }
    return NULL;
}

runtime_node *runtime_store_node(runtime_store *store, const node *n)
{
    assert(store && n);
    assert(graph_has_node(store->graph, n));
    runtime_node *state = runtime_store_find(store, n);
    if (state) {
        runtime_node_resize(state, n);
        return state;
    }
    state = (runtime_node *)xcalloc(1, sizeof *state);
    state->node = n;
    state->node_id = n->id;
    runtime_node_resize(state, n);
    runtime_node_reset(state);
    apush(store->nodes, state);
    return state;
}

const runtime_node *runtime_store_node_const(const runtime_store *store, const node *n)
{
    assert(store && n);
    assert(graph_has_node(store->graph, n));
    return runtime_store_find((runtime_store *)store, n);
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
    return state->computed;
}

void runtime_node_set_computed(runtime_node *state, int computed)
{
    assert(state);
    state->computed = computed != 0;
}

int runtime_node_visiting(const runtime_node *state)
{
    assert(state);
    return state->visiting;
}

void runtime_node_set_visiting(runtime_node *state, int visiting)
{
    assert(state);
    state->visiting = visiting != 0;
}

void runtime_node_error(runtime_node *state, int code, const char *message)
{
    assert(state);
    state->eval_error = code;
    state->eval_error_message = message;
}
