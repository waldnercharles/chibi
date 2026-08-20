#ifndef CBLUEPRINT_GRAPH_H
#define CBLUEPRINT_GRAPH_H

#include "array.h" // IWYU pragma: keep (graph fields are dyna arrays)
#include "enum.h"
#include "node.h"

#include <stddef.h>
#include <stdint.h>

#define GRAPH_EDGE_LIST(X)                                                     \
    X(GRAPH_EDGE_DATA, "data")                                                 \
    X(GRAPH_EDGE_EXEC, "exec")

DEFINE_ENUM(graph_edge_kind, GRAPH_EDGE_LIST)

typedef struct edge
{
    uint64_t from_id;
    uint64_t from_pin_id;
    uint64_t to_id;
    uint64_t to_pin_id;
    graph_edge_kind kind;
    union
    {
        int exec_order;
        int order;
    };
} edge;

typedef struct graph
{
    node **nodes;
    node **id_table; /* id -> node*, NULL after removal; O(1) lookup */
    edge *edges;
    char **strings;
    void **blocks;
    uint64_t id_counter;
    char *name;
    char *description;
    unsigned long long topology_revision;
    unsigned flow_pause_count;
} graph;

void graph_init(graph *g);
void graph_free(graph *g);
node *graph_add_node(graph *g, const node_desc *desc);

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin);
int graph_connect(graph *g, node *from, int from_pin, node *to, int to_pin);
int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order);
int graph_connect_data_ids(graph *g, uint64_t from_node, uint64_t from_pin, uint64_t to_node, uint64_t to_pin);
int graph_connect_exec_ids(
    graph *g,
    uint64_t from_node,
    uint64_t from_pin,
    uint64_t to_node,
    uint64_t to_pin,
    int exec_order
);
unsigned long long graph_topology_revision(const graph *g);

int graph_set_event_name(graph *g, node *event_node, const char *name);
int graph_set_metadata(graph *g, const char *name, const char *description);
const char *graph_event_name(const node *event_node);

void graph_flow_pause(graph *g);
void graph_flow_resume(graph *g);
int graph_remove_node(graph *g, node *n);
int graph_remove_edge(graph *g, node *from, int from_pin, node *to, int to_pin);

int graph_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin);
int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order);
int graph_exec_input_source_at(
    const graph *g,
    const node *n,
    int pin,
    int ordinal,
    node **src,
    int *src_pin,
    int *order
);
int graph_exec_input_count(const graph *g, const node *n, int pin);
int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);
int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);

node *graph_node_by_id(const graph *g, uint64_t id);
const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index);
void graph_reindex(graph *g); /* rebuild id_table from g->nodes */
uint64_t graph_alloc_id(graph *g);
int graph_set_pin_default(graph *g, node *n, int output, int index, const pin_value *value);
int graph_set_pin_name(graph *g, node *n, int output, int index, const char *name);
int graph_set_pin_type(graph *g, node *n, int output, int index, pin_type type, const char *type_name);
int graph_set_pin_container(graph *g, node *n, int output, int index, pin_container container);
int graph_move_pin(graph *g, node *n, int output, int index, int target_index);
int graph_set_node_disabled(graph *g, node *n, int disabled);
int graph_set_node_title(graph *g, node *n, const char *title);
int graph_set_node_comment(graph *g, node *n, const char *comment);
int graph_set_node_reference(graph *g, node *n, uint64_t reference_id, const char *reference_name);
int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin);
int graph_remove_pin(graph *g, node *n, int output, int index);

int pin_types_compatible(const pin_spec *a, const pin_spec *b);
char *graph_own_str(graph *g, const char *s, size_t len);
void *graph_own_block(graph *g, size_t size);

#endif
