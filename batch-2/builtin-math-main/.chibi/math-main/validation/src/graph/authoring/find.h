#ifndef CHIBI_GRAPH_FIND_H
#define CHIBI_GRAPH_FIND_H

#include "graph/model/graph.h"

#include <stdbool.h>

/* Find in graph (Blender's Find Node, Unreal's Find Results): which of a
 * node's texts contains a query, ignoring case. */
typedef enum
{
    GRAPH_FIND_TITLE,
    GRAPH_FIND_PIN,     /* a data pin's name */
    GRAPH_FIND_VALUE,   /* an unwired input's or a literal's value */
    GRAPH_FIND_BINDING, /* a bound variable, component, field or named value */
    GRAPH_FIND_EXPRESSION, /* a Behavior Expression's source */
    GRAPH_FIND_COMMENT,
} graph_find_field;

typedef struct
{
    graph_find_field field;
    char text[160]; /* what matched; "Pin: value" where a pin holds it */
} graph_find_match;

/* The first of n's texts that contains query: the title the caller shows,
 * then pin names, values, bindings, expression source and comment. An empty
 * query matches nothing. */
bool graph_find_node(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    const char *title,
    const char *query,
    graph_find_match *out
);

#endif
