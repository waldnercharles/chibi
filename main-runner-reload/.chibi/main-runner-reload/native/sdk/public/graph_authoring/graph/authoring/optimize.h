#ifndef CHIBI_GRAPH_OPTIMIZE_H
#define CHIBI_GRAPH_OPTIMIZE_H

#include "graph/model/graph.h"

typedef enum
{
    GRAPH_OPTIMIZE_DEBUG,
    GRAPH_OPTIMIZE_CONVERSION,
    GRAPH_OPTIMIZE_DORMANT_BINDING,
    GRAPH_OPTIMIZE_SEMANTICS,
    GRAPH_OPTIMIZE_BACKEND,
    GRAPH_OPTIMIZE_PIN,
    GRAPH_OPTIMIZE_WIRE_ROUTE,
    GRAPH_OPTIMIZE_SKIP_COUNT,
} graph_optimize_skip_reason;

typedef struct graph_optimize_skip
{
    uint64_t node_id;
    uint64_t pin_id;
    graph_optimize_skip_reason reason;
} graph_optimize_skip;

typedef struct graph_optimize_options
{
    int fanout_threshold;
    bool preserve_debug;
    bool local_aliases;
    bool dry_run;
} graph_optimize_options;

typedef struct graph_optimize_report
{
    int folded_inputs;
    int removed_nodes;
    int created_aliases;
    int changed_edges;
    graph_optimize_skip *skipped;
} graph_optimize_report;

graph_optimize_options graph_optimize_defaults();
void graph_optimize_report_free(graph_optimize_report *report);
/* Analyzes each source fanout before changing any of it. Does not arrange or
 * own history. Dry runs execute identical passes on a private graph copy. */
int graph_optimize(graph *g, const graph_optimize_options *options, graph_optimize_report *report);
/* Used after wire drags; intentionally limited to one component consumer. */
bool graph_fold_component_getter(graph *g, node *source);
/* A free name for a local of n's output, after what the output carries. */
void graph_local_name(const graph *g, const node *n, int pin, char *name, size_t size);

#endif
