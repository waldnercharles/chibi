#ifndef CBLUEPRINT_BEHAVIOR_ASSET_H
#define CBLUEPRINT_BEHAVIOR_ASSET_H

#include "graph.h"
#include "node.h"

#include <stdint.h>

/* BP-1 / BP-8 / BP-12: an authored Behavior Asset definition owns its ID counter
 * (kept on its single default event graph), metadata, optional nominal owner
 * handle type, and variable list. Runtime state never lives here. */
typedef struct behavior_variable
{
    uint64_t id;
    char *name;
    pin_type type;
    char *type_name;
    pin_container container;
    pin_value default_value; /* authored default; strings are graph-owned */
    char *category;
} behavior_variable;

typedef struct behavior_asset
{
    graph *graph;          /* the one non-deletable default event graph */
    struct graph **graphs; /* includes graph; graph 0 is always the default */
    char *name;            /* graph-owned */
    char *description;     /* graph-owned */
    char *owner_type_name; /* optional nominal PIN_HANDLE owner type */
    behavior_variable *variables;
} behavior_asset;

/* Create a behavior_asset with one empty default event graph. `name` is copied. */
behavior_asset *behavior_asset_new(const char *name);

/* BP-2..BP-4: named graphs share the Behavior Asset ID namespace. The default
 * event graph is created by behavior_asset_new() and cannot be deleted. */
graph *behavior_asset_add_graph(behavior_asset *bp, graph_kind kind, const char *name);
graph *behavior_asset_add_function(behavior_asset *bp, const char *name, int pure);
graph *behavior_asset_add_macro(behavior_asset *bp, const char *name);
graph *behavior_asset_find_graph(const behavior_asset *bp, uint64_t graph_id);
graph *behavior_asset_find_graph_by_name(const behavior_asset *bp, const char *name);
int behavior_asset_rename_graph(behavior_asset *bp, uint64_t graph_id, const char *name);
int behavior_asset_delete_graph(behavior_asset *bp, uint64_t graph_id);
void behavior_asset_free(behavior_asset *bp);

/* The definition is mutable only while no runtime instance is attached and no
 * legacy flow_run holds the graph guard (graph->flow_pause_count == 0). */
int behavior_asset_mutable(const behavior_asset *bp);
int behavior_asset_set_name(behavior_asset *bp, const char *name);
void behavior_asset_set_description(behavior_asset *bp, const char *description);
int behavior_asset_set_owner_type(behavior_asset *bp, const char *type_name);

/* Variables. Names are unique within the behavior_asset (VAR-3). Default
 * strings are copied into behavior_asset-owned storage. */
behavior_variable *behavior_asset_add_variable(
    behavior_asset *bp,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
);
behavior_variable *behavior_asset_find_variable_by_name(const behavior_asset *bp, const char *name);
behavior_variable *behavior_asset_find_variable_by_id(const behavior_asset *bp, uint64_t id);

/* VAR-5: rename/retype update every referencing Get/Set node's display and
 * pin type without rewriting references. Retype invalidates edges that are no
 * longer compatible (VAR-6). Delete leaves orphaned references (VAR-7). */
int behavior_asset_rename_variable(behavior_asset *bp, uint64_t id, const char *new_name);
int behavior_asset_retype_variable(behavior_asset *bp, uint64_t id, pin_type type, const char *type_name);
int behavior_asset_set_variable_container(behavior_asset *bp, uint64_t id, pin_container container);
int behavior_asset_set_variable_category(behavior_asset *bp, uint64_t id, const char *category);
behavior_variable *behavior_asset_promote_pin_to_variable(
    behavior_asset *bp,
    node *n,
    int output,
    int index,
    const char *name
);
int behavior_asset_delete_variable(behavior_asset *bp, uint64_t id);
int behavior_asset_set_variable_default(behavior_asset *bp, uint64_t id, const pin_value *value);

/* Node-creation helpers for the variable and owner surfaces. All mutate the
 * default event graph and are rejected while the definition is guarded. */
node *behavior_asset_add_get_node(behavior_asset *bp, uint64_t variable_id);
node *behavior_asset_add_set_node(behavior_asset *bp, uint64_t variable_id);
node *behavior_asset_add_self_node(behavior_asset *bp); /* NULL when no owner type */
/* Add a call node to `caller`. Both graphs must belong to `bp`. */
node *behavior_asset_add_function_call_node_to_graph(behavior_asset *bp, graph *caller, graph *function_graph);
node *behavior_asset_add_macro_call_node_to_graph(behavior_asset *bp, graph *caller, graph *macro_graph);
/* Convenience forms that add the call to the default event graph. */
node *behavior_asset_add_function_call_node(behavior_asset *bp, graph *function_graph);
node *behavior_asset_add_macro_call_node(behavior_asset *bp, graph *macro_graph);

#endif
