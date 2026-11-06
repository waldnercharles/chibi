#ifndef CHIBI_EVENTS_H
#define CHIBI_EVENTS_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "runtime/events.h"

extern node_desc NODE_CallEvent;
extern const registry_pack CORE_EVENT_PACK;

pin_spec graph_event_payload_pin(const event_field *);

/* Event-definition variables declare payload fields. Existing connected fields
 * retain IDs; incompatible/removal edits are rejected atomically. */
int graph_event_apply(graph *g, node *n, const behavior_asset *definition);
/* Gives an Event node named for a host event the payload pins it lacks. */
int graph_host_event_apply(graph *g, node *n);
bool graph_event_matches(const node *n, const behavior_asset *definition);

#endif
