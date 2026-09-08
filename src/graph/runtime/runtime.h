#ifndef CBLUEPRINT_RUNTIME_H
#define CBLUEPRINT_RUNTIME_H

#include "graph/model/pin.h"

typedef struct graph graph;
typedef struct node node;

typedef struct runtime_store runtime_store;
typedef struct runtime_node runtime_node;

runtime_store *runtime_store_create(const graph *g);
void runtime_store_free(runtime_store *store);
void runtime_store_reset(runtime_store *store);
void runtime_store_reset_memo(runtime_store *store);
const graph *runtime_store_graph(const runtime_store *store);
runtime_node *runtime_store_node(runtime_store *store, const node *n);
const runtime_node *runtime_store_node_const(const runtime_store *store, const node *n);

pin_value *runtime_node_inputs(runtime_node *state);
const pin_value *runtime_node_inputs_const(const runtime_node *state);
pin_value *runtime_node_outputs(runtime_node *state);
const pin_value *runtime_node_outputs_const(const runtime_node *state);
int runtime_node_computed(const runtime_node *state);
void runtime_node_set_computed(runtime_node *state, int computed);
int runtime_node_visiting(const runtime_node *state);
void runtime_node_set_visiting(runtime_node *state, int visiting);
void runtime_node_error(runtime_node *state, int code, const char *message);

#endif
