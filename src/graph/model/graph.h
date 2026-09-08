#ifndef CBLUEPRINT_GRAPH_H
#define CBLUEPRINT_GRAPH_H

#include "graph/model/node.h"
#include "support/array.h" // IWYU pragma: keep (graph fields are dyna arrays)
#include "support/enum.h"

#include <stddef.h>
#include <stdint.h>

#define GRAPH_EDGE_LIST(X)                                                     \
    X(GRAPH_EDGE_DATA, "data")                                                 \
    X(GRAPH_EDGE_EXEC, "exec")

DEFINE_ENUM(graph_edge_kind, GRAPH_EDGE_LIST)

typedef enum
{
    GRAPH_EVENT = 0,
    GRAPH_FUNCTION,
    GRAPH_MACRO,
} graph_kind;

typedef struct behavior_asset behavior_asset;

typedef struct graph_comment_box
{
    uint64_t id;
    float x;
    float y;
    float width;
    float height;
    char *title;
    uint32_t color;
} graph_comment_box;

#define GRAPH_EDGE_MAX_REROUTES 16

typedef struct edge
{
    uint64_t from_id;
    uint64_t from_pin_id;
    uint64_t to_id;
    uint64_t to_pin_id;
    graph_edge_kind kind;
    const node_desc *conversion; /* borrowed registry descriptor for data edges */
    union
    {
        int exec_order;
        int order;
    };
    /* Authored world-space reroute points. The renderer joins these with
     * smooth cubic segments, so every point is a point the wire passes
     * through (rather than a free Bézier control handle). */
    int reroute_count;
    struct
    {
        float x;
        float y;
    } reroutes[GRAPH_EDGE_MAX_REROUTES];
    /* Kept for compatibility with documents written by the first handle
     * prototype; new editing uses reroutes. */
    float from_handle_x;
    float from_handle_y;
    float to_handle_x;
    float to_handle_y;
    bool custom_handles;
} edge;

typedef struct graph
{
    node **nodes;
    uint64_t graph_id;
    graph_kind kind;
    behavior_asset *owner; /* borrowed; NULL for a standalone graph */
    uint64_t entry_id;
    uint64_t return_id;
    int call_depth_limit;
    node **id_table; /* id -> node*, NULL after removal; O(1) lookup */
    edge *edges;
    graph_comment_box *comment_boxes;
    char **strings;
    void **blocks;
    uint64_t id_counter;
    uint64_t *shared_id_counter; /* Behavior Asset graphs share one ID namespace. */
    char *name;
    char *description;
    char *source_asset; /* linked library source, graph-owned; runtime only */
    uint64_t source_graph_id;
    char *math_expression; /* graph-owned source for generated scalar functions */
    unsigned long long topology_revision;
    unsigned flow_pause_count;
    unsigned breakpoints_enabled;
    unsigned dataflow; /* compact graph executes from data dependencies */
} graph;

void graph_init(graph *g);
void graph_free(graph *g);
const char *graph_kind_name(graph_kind kind);
int graph_kind_from_name(const char *name, graph_kind *kind);
node *graph_add_node(graph *g, const node_desc *desc);

int graph_connect_data(graph *g, node *from, int from_pin, node *to, int to_pin);
int graph_connect_data_conversion(
    graph *g,
    node *from,
    int from_pin,
    node *to,
    int to_pin,
    const node_desc *conversion
);
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
const node_desc *graph_input_conversion(const graph *g, const node *n, int pin);
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
int graph_set_breakpoint(graph *g, node *n, int enabled);
int graph_set_all_breakpoints(graph *g, int enabled);
int graph_set_pin_watch(graph *g, node *n, int output, int index, int enabled);
graph_comment_box *graph_add_comment_box(graph *g, const char *title, float x, float y, float width, float height);
int graph_remove_comment_box(graph *g, uint64_t id);
int graph_set_comment_box(graph *g, graph_comment_box *box, const char *title, uint32_t color);
int graph_set_node_reference(graph *g, node *n, uint64_t reference_id, const char *reference_name);
int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin);
/* Exposed fields append outputs while retaining the aggregate pin. Hide keeps
 * connected projections. Reflected struct outputs include local function signatures. */
const type_desc *graph_output_struct(const node *n, int output_index);
int graph_expose_output_fields(graph *g, node *n, int output_index);
int graph_hide_output_fields(graph *g, node *n, int output_index);
const type_field *graph_output_field(const node *n, int output_index, int *source_index);
int graph_split_pin(graph *g, node *n, int output, int index);
int graph_recombine_pin(graph *g, node *n, int output, int index);
int graph_remove_pin(graph *g, node *n, int output, int index);

int pin_types_compatible(const pin_spec *a, const pin_spec *b);
char *graph_own_str(graph *g, const char *s, size_t len);
void *graph_own_block(graph *g, size_t size);

#endif
