#ifndef CBLUEPRINT_EVENTS_H
#define CBLUEPRINT_EVENTS_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

extern node_desc NODE_CallEvent;
extern const registry_pack CORE_EVENT_PACK;

/* Event-definition variables declare payload fields. Existing connected fields
 * retain IDs; incompatible/removal edits are rejected atomically. */
int graph_event_apply(graph *g, node *n, const behavior_asset *definition);
bool graph_event_host_name(const char *name);
bool graph_event_matches(const node *n, const behavior_asset *definition);

#endif
