#ifndef CHIBI_EVENTS_H
#define CHIBI_EVENTS_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

extern node_desc NODE_CallEvent;
extern const registry_pack CORE_EVENT_PACK;

typedef enum
{
    GRAPH_HOST_EVENT_BEGIN,
    GRAPH_HOST_EVENT_TICK,
    GRAPH_HOST_EVENT_FIRE,
    GRAPH_HOST_EVENT_DASH,
    GRAPH_HOST_EVENT_INPUT,
    GRAPH_HOST_EVENT_CONTACT_BEGIN,
    GRAPH_HOST_EVENT_COUNT,
} graph_host_event;

/* An event the engine dispatches: its name, words for and a sentence on when
 * it fires, and the payload its Event node outputs after Then. */
typedef struct
{
    const char *name;
    const char *keywords;
    const char *description;
    const pin_spec *payload;
    int payload_count;
} graph_host_event_desc;

extern const graph_host_event_desc GRAPH_HOST_EVENTS[GRAPH_HOST_EVENT_COUNT];
const char *graph_host_event_name(graph_host_event event);
bool graph_host_event_is(const char *name, graph_host_event event);

/* Event-definition variables declare payload fields. Existing connected fields
 * retain IDs; incompatible/removal edits are rejected atomically. */
int graph_event_apply(graph *g, node *n, const behavior_asset *definition);
bool graph_event_host_name(const char *name);
/* Gives an Event node named for a host event the payload pins it lacks. */
int graph_host_event_apply(graph *g, node *n);
bool graph_event_matches(const node *n, const behavior_asset *definition);

#endif
