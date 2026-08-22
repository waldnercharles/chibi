#ifndef CBLUEPRINT_BLUEPRINT_H
#define CBLUEPRINT_BLUEPRINT_H

#include "graph.h"
#include "node.h"

#include <stdint.h>

/* BP-1 / BP-8 / BP-12: an authored Blueprint definition owns its ID counter
 * (kept on its single default event graph), metadata, optional nominal owner
 * handle type, and variable list. Runtime state never lives here. */
typedef struct blueprint_variable
{
    uint64_t id;
    char *name;
    pin_type type;
    char *type_name;
    pin_container container;
    pin_value default_value; /* authored default; strings are graph-owned */
    char *category;
} blueprint_variable;

typedef struct blueprint
{
    graph *graph;          /* the one non-deletable default event graph */
    struct graph **graphs; /* includes graph; graph 0 is always the default */
    char *name;            /* graph-owned */
    char *description;     /* graph-owned */
    char *owner_type_name; /* optional nominal PIN_HANDLE owner type */
    blueprint_variable *variables;
} blueprint;

/* Create a blueprint with one empty default event graph. `name` is copied. */
blueprint *blueprint_new(const char *name);

/* BP-2..BP-4: named graphs share the Blueprint ID namespace. The default
 * event graph is created by blueprint_new() and cannot be deleted. */
graph *blueprint_add_graph(blueprint *bp, graph_kind kind, const char *name);
graph *blueprint_add_function(blueprint *bp, const char *name, int pure);
graph *blueprint_add_macro(blueprint *bp, const char *name);
graph *blueprint_find_graph(const blueprint *bp, uint64_t graph_id);
graph *blueprint_find_graph_by_name(const blueprint *bp, const char *name);
int blueprint_delete_graph(blueprint *bp, uint64_t graph_id);
void blueprint_free(blueprint *bp);

/* The definition is mutable only while no runtime instance is attached and no
 * legacy flow_run holds the graph guard (graph->flow_pause_count == 0). */
int blueprint_mutable(const blueprint *bp);
int blueprint_set_name(blueprint *bp, const char *name);
void blueprint_set_description(blueprint *bp, const char *description);
int blueprint_set_owner_type(blueprint *bp, const char *type_name);

/* Variables. Names are unique within the blueprint (VAR-3). Default strings
 * are copied into blueprint-owned storage. */
blueprint_variable *blueprint_add_variable(
    blueprint *bp,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
);
blueprint_variable *blueprint_find_variable_by_name(const blueprint *bp, const char *name);
blueprint_variable *blueprint_find_variable_by_id(const blueprint *bp, uint64_t id);

/* VAR-5: rename/retype update every referencing Get/Set node's display and
 * pin type without rewriting references. Retype invalidates edges that are no
 * longer compatible (VAR-6). Delete leaves orphaned references (VAR-7). */
int blueprint_rename_variable(blueprint *bp, uint64_t id, const char *new_name);
int blueprint_retype_variable(blueprint *bp, uint64_t id, pin_type type, const char *type_name);
int blueprint_set_variable_container(blueprint *bp, uint64_t id, pin_container container);
int blueprint_set_variable_category(blueprint *bp, uint64_t id, const char *category);
blueprint_variable *blueprint_promote_pin_to_variable(
    blueprint *bp,
    node *n,
    int output,
    int index,
    const char *name
);
int blueprint_delete_variable(blueprint *bp, uint64_t id);
int blueprint_set_variable_default(blueprint *bp, uint64_t id, const pin_value *value);

/* Node-creation helpers for the variable and owner surfaces. All mutate the
 * default event graph and are rejected while the definition is guarded. */
node *blueprint_add_get_node(blueprint *bp, uint64_t variable_id);
node *blueprint_add_set_node(blueprint *bp, uint64_t variable_id);
node *blueprint_add_self_node(blueprint *bp); /* NULL when no owner type */
node *blueprint_add_call_event_node(blueprint *bp, node *event_node);
node *blueprint_add_function_call_node(blueprint *bp, graph *function_graph);
node *blueprint_add_macro_call_node(blueprint *bp, graph *macro_graph);

#endif
