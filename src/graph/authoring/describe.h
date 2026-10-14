#ifndef CHIBI_GRAPH_DESCRIBE_H
#define CHIBI_GRAPH_DESCRIBE_H

#include "graph/document/events.h"

#include <stdbool.h>
#include <stddef.h>

/* Hover text: one sentence on what a node does, for Studio's node tooltips
 * and add-node search, and for searchNodes results. */

/* What a node stands for: its type, and what configures it. */
typedef struct
{
    const node_desc *desc;
    const char *setting; /* a component node's component or struct type */
    const char *field;   /* the one field a Set writes */
    const host_event_desc *host;      /* an engine event */
    const behavior_asset *definition; /* an event definition */
} node_subject;

/* A placed node's subject; `catalog` holds the project's event definitions. */
node_subject node_subject_of(const node *n, const behavior_asset *const *catalog, int count);
/* Writes the subject's sentence: a configured component node's, an event's,
 * or its node type's. False when it has none. */
bool node_subject_describe(const node_subject *subject, char *text, size_t size);

#endif
