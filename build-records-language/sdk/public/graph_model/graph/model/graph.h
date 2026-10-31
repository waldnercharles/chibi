#ifndef CHIBI_GRAPH_H
#define CHIBI_GRAPH_H

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

/* Mode inferred for a generated Behavior Expression graph. */
typedef enum
{
    GRAPH_EXPRESSION_NONE = 0,
    GRAPH_EXPRESSION_VALUE,
    GRAPH_EXPRESSION_ACTION,
    GRAPH_EXPRESSION_BRANCH,
} graph_expression_mode;

typedef struct behavior_asset behavior_asset;

typedef struct graph_comment_box
{
    uint64_t id;
    float x;
    float y;
    float width;
    float height;
    char *title;
    uint32_t color; /* 0xRRGGBBAA; 0 takes the canvas's comment color */
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
    uint64_t lifetime_id; /* Process-local identity; never serialized or edited. */
    graph_kind kind;
    behavior_asset *owner; /* borrowed; NULL for a standalone graph */
    uint64_t entry_id;
    uint64_t return_id;
    int call_depth_limit;
    node **id_table; /* id -> node*, NULL after removal; O(1) lookup */
    edge *edges;
    struct graph_local *locals;
    struct graph_edge_index *edge_index; /* Shared lazy edge index; never serialized. */
    graph_comment_box *comment_boxes;
    char **strings;
    void **blocks;
    struct arena *value_storage; /* Deep value snapshots, retired with the graph. */
    uint64_t id_counter;
    uint64_t *shared_id_counter; /* Behavior Asset graphs share one ID namespace. */
    char *name;
    char *description;
    char *source_asset; /* linked library source, graph-owned; runtime only */
    uint64_t source_graph_id;
    uint64_t clipboard_owner_id; /* transient source ownership for pasted graph JSON */
    char *math_expression; /* graph-owned source for generated scalar functions */
    graph_expression_mode expression_mode;
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

int graph_connect_exec(graph *g, node *from, int from_pin, node *to, int to_pin, int exec_order);
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

/* Whether the editor folds n into its single downstream data consumer's pin
 * row instead of drawing a box. The node and edge remain ordinary graph data
 * that evaluation uses unchanged; the canvas, arrangement, extraction and
 * serialization share this predicate.
 * An inline node is pure, enabled, not a flow node such as Function Entry, a
 * function call, reroute or local source, has no setting pins besides a
 * chosen Component, has exactly one outgoing edge (a data wire), and each
 * input is wired from an inline node or holds a value. The `auto`
 * presentation also requires a tree of at most GRAPH_INLINE_AUTO_NODES nodes
 * without breakpoints, watches or comments, outside generated expression
 * graphs. */
#define GRAPH_INLINE_AUTO_NODES 3

bool graph_node_is_vector_constructor(const node *n);

const node_desc *graph_input_conversion(const graph *g, const node *n, int pin);
int graph_exec_input_source(const graph *g, const node *n, int pin, node **src, int *src_pin, int *order);
int graph_exec_input_count(const graph *g, const node *n, int pin);
int graph_exec_output_target(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);
/* Counts all edges, including invalid fan-out; first is borrowed until mutation. */
int graph_exec_output_edges(const graph *g, const node *n, int pin, const edge **first);
int graph_exec_target_at(const graph *g, const node *n, int pin, int order, node **dst, int *dst_pin);

node *graph_node_by_id(const graph *g, uint64_t id);
const pin_spec *graph_pin_by_id(const node *n, uint64_t id, int *is_output, int *index);
/* The literal a node names what it reads by, such as a Field Parameter's or
   Get Global Variable's Name input (Radius, Score); NULL when it has none. */
const char *graph_node_name_literal(const graph *g, const node *n);
void graph_reindex(graph *g); /* rebuild id_table from g->nodes */
uint64_t graph_alloc_id(graph *g);

int graph_set_pin_name(graph *g, node *n, int output, int index, const char *name);
/* Sets an editor-only pin label; NULL or an empty name uses the pin name. */
int graph_set_pin_display_name(graph *g, node *n, int output, int index, const char *name);
int graph_set_pin_type(graph *g, node *n, int output, int index, pin_type type, const char *type_name);
int graph_set_pin_container(graph *g, node *n, int output, int index, pin_container container);
const pin_binding *graph_pin_binding(const graph *g, const node *n, int output, int index);
int graph_set_pin_binding(graph *g, node *n, int output, int index, const pin_binding *binding);
int graph_move_pin(graph *g, node *n, int output, int index, int target_index);
int graph_set_node_disabled(graph *g, node *n, int disabled);
int graph_set_node_presentation(graph *g, node *n, node_presentation presentation);
/* The presentation spelled `name`, or -1. */
int graph_presentation_by_name(const char *name);

int graph_set_breakpoint(graph *g, node *n, int enabled);
/* Sets n's comment; NULL or "" removes it. */
int graph_set_node_comment(graph *g, node *n, const char *comment, bool hidden);
int graph_set_all_breakpoints(graph *g, int enabled);
int graph_set_pin_watch(graph *g, node *n, int output, int index, int enabled);
graph_comment_box *graph_add_comment_box(graph *g, const char *title, float x, float y, float width, float height);
int graph_remove_comment_box(graph *g, uint64_t id);
int graph_set_comment_box(graph *g, graph_comment_box *box, const char *title, uint32_t color);
int graph_set_node_reference(graph *g, node *n, uint64_t reference_id, const char *reference_name);
int graph_set_node_reference_asset(graph *g, node *n, const char *asset_name);
int graph_add_pin(graph *g, node *n, int output, const pin_spec *template_pin);
/* The default value a new pin receives from its template. */
pin_value graph_pin_initial_default(const pin_spec *template_pin);

/* The pin that exposes one field of the struct output at source_index. */
pin_spec graph_field_pin(const node *n, int source_index, const type_field *field);

int graph_remove_pin(graph *g, node *n, int output, int index);

int pin_types_compatible(const pin_spec *a, const pin_spec *b);
char *graph_own_str(graph *g, const char *s, size_t len);
void *graph_own_block(graph *g, size_t size);

#endif
