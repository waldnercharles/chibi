#ifndef CHIBI_GRAPH_AUTHORING_GRAPH_EDIT_H
#define CHIBI_GRAPH_AUTHORING_GRAPH_EDIT_H

#include "graph/model/graph.h"

int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin);

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin);

int graph_connect_data_conversion(
    graph *g,
    node *from,
    int from_pin,
    node *to,
    int to_pin,
    const node_desc *conversion
);

/* Reveals one registered field without changing the aggregate or its wires. */
int graph_expose_output_field(graph *g, node *n, int output_index, const char *field_name);

int graph_expose_output_fields(graph *g, node *n, int output_index);

int graph_hide_output_fields(graph *g, node *n, int output_index);

/* Folds n, persisting `inline` only when `auto` would draw its box. */
int graph_inline_node(graph *g, node *n);

/* The visible node that draws n: n itself unless n is inline. */
node *graph_inline_owner(const graph *g, node *n);

int graph_inline_source(const graph *g, const node *target, int pin, node **src, int *src_pin);

/* Appends the IDs of n and the inline nodes drawn inside it to *ids
 * (support/array.h). */
void graph_inline_tree(const graph *g, const node *n, uint64_t **ids);

/* Whether n would be inline with the `inline` presentation. */
bool graph_node_can_inline(const graph *g, const node *n);

bool graph_node_is_inline(const graph *g, const node *n);

const type_field *graph_output_field(const node *n, int output_index, int *source_index);

/* Exposed fields append outputs while retaining the aggregate pin. Hide keeps
 * connected projections. Struct outputs include event and local function
 * signatures and can expose nested struct fields. */
const type_desc *graph_output_struct(const node *n, int output_index);

int graph_recombine_pin(graph *g, node *n, int output, int index);

/* Removes n and the inline nodes drawn inside it. */
int graph_remove_node_tree(graph *g, node *n);

/* Resolve a reference-capable data output to a stable writable binding. The
 * returned strings are borrowed from the graph or registry; a field path
 * assembled from nested projections is graph-owned. */
int graph_resolve_reference(graph *g, const node *source, int source_pin, pin_binding *binding);

/* Copies the value and owns its strings, arrays and registered struct data. */
int graph_set_pin_default(graph *g, node *n, int output, int index, const pin_value *value);

int graph_split_pin(graph *g, node *n, int output, int index);

const node_desc *graph_vector_constructor(const pin_spec *pin);

#endif
