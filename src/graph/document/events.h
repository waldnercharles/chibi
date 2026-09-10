#ifndef CHIBI_EVENTS_H
#define CHIBI_EVENTS_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

extern node_desc NODE_CallEvent;
extern const registry_pack CORE_EVENT_PACK;

typedef enum
{
    GRAPH_HOST_EVENT_BEGIN,
    GRAPH_HOST_EVENT_END,
    GRAPH_HOST_EVENT_TICK,
    GRAPH_HOST_EVENT_FIRE,
    GRAPH_HOST_EVENT_DASH,
    GRAPH_HOST_EVENT_INPUT,
    GRAPH_HOST_EVENT_CONTACT_BEGIN,
    GRAPH_HOST_EVENT_COUNT,
} graph_host_event;

const char *graph_host_event_name(graph_host_event event);
bool graph_host_event_is(const char *name, graph_host_event event);

/* Event-definition variables declare payload fields. Existing connected fields
 * retain IDs; incompatible/removal edits are rejected atomically. */
int graph_event_apply(graph *g, node *n, const behavior_asset *definition);
bool graph_event_host_name(const char *name);
bool graph_event_matches(const node *n, const behavior_asset *definition);

#endif
