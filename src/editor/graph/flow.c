/* What the debug target's wires and nodes did, for flow pulses and node rims.
 * The runtime counts each wire's traversals while it traces the target
 * (behavior_instance_record_activity). Once per editor tick this index reads
 * those counts and notes, on the editor clock, when each one last changed, so
 * pulses ease out while the game is paused as well as when it stops firing. */
#include "editor/graph/flow_visual.h"
#include "editor/workspace/editor_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool ed_inspection_blocked(const editor *e)
{
    return e->inspection_blocked ||
           (e->inspection_instance && (e->edit_pending || e->pending_snapshot));
}

const behavior_instance *ed_display_instance(const editor *e)
{
    return ed_inspection_blocked(e)
             ? NULL
             : (e->inspection_instance ? e->inspection_instance : e->instance);
}

typedef struct
{
    flow_traversal edge; /* the wire's ends and kind */
    uint64_t count;      /* the runtime count last read; zero marks empty */
    double fired;        /* editor time it was last seen to change */
} seen_edge;

typedef struct
{
    uint64_t id;      /* zero marks empty */
    uint64_t entered; /* execution that reached it */
    uint64_t left;    /* the most any of its outputs fired */
    double fired;
} seen_node;

typedef struct ed_flow_activity_index
{
    const behavior_instance *instance;
    uint64_t generation;
    bool primed; /* counts read once; later changes are fires */
    double time;
    seen_edge *edges;
    int edge_capacity, edge_used;
    seen_node *nodes;
    int node_capacity, node_used;
} ed_flow_activity_index;

static uint64_t mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * 1099511628211ull;
}

static uint64_t edge_hash(const flow_traversal *t)
{
    uint64_t hash = 14695981039346656037ull;
    hash = mix(hash, t->from_id);
    hash = mix(hash, t->from_pin_id);
    hash = mix(hash, t->to_id);
    hash = mix(hash, t->to_pin_id);
    return mix(hash, t->kind);
}

static bool same_edge(const flow_traversal *a, const flow_traversal *b)
{
    return a->kind == b->kind && a->from_id == b->from_id &&
           a->from_pin_id == b->from_pin_id && a->to_id == b->to_id &&
           a->to_pin_id == b->to_pin_id;
}

static seen_edge *edge_slot(seen_edge *table, int capacity, const flow_traversal *t)
{
    int mask = capacity - 1;
    int slot = (int)(edge_hash(t) & (uint64_t)mask);
    while (table[slot].count && !same_edge(&table[slot].edge, t)) {
        slot = (slot + 1) & mask;
    }
    return &table[slot];
}

static seen_node *node_slot(seen_node *table, int capacity, uint64_t id)
{
    int mask = capacity - 1;
    int slot = (int)(mix(14695981039346656037ull, id) & (uint64_t)mask);
    while (table[slot].id && table[slot].id != id) {
        slot = (slot + 1) & mask;
    }
    return &table[slot];
}

static void grow_edges(ed_flow_activity_index *index)
{
    int capacity = index->edge_capacity ? index->edge_capacity * 2 : 64;
    seen_edge *table = xcalloc((size_t)capacity, sizeof *table);
    for (int i = 0; i < index->edge_capacity; i++) {
        if (index->edges[i].count) {
            *edge_slot(table, capacity, &index->edges[i].edge) = index->edges[i];
        }
    }
    free(index->edges);
    index->edges = table;
    index->edge_capacity = capacity;
}

static seen_node *node_entry(ed_flow_activity_index *index, uint64_t id)
{
    if ((index->node_used + 1) * 2 > index->node_capacity) {
        int capacity = index->node_capacity ? index->node_capacity * 2 : 64;
        seen_node *table = xcalloc((size_t)capacity, sizeof *table);
        for (int i = 0; i < index->node_capacity; i++) {
            if (index->nodes[i].id) {
                *node_slot(table, capacity, index->nodes[i].id) = index->nodes[i];
            }
        }
        free(index->nodes);
        index->nodes = table;
        index->node_capacity = capacity;
    }
    seen_node *entry = node_slot(index->nodes, index->node_capacity, id);
    if (!entry->id) {
        *entry = (seen_node){ .id = id, .fired = -INFINITY };
        index->node_used++;
    }
    return entry;
}

static void reset(ed_flow_activity_index *index, const behavior_instance *instance, uint64_t generation)
{
    if (index->edge_used) {
        memset(index->edges, 0, (size_t)index->edge_capacity * sizeof *index->edges);
    }
    if (index->node_used) {
        memset(index->nodes, 0, (size_t)index->node_capacity * sizeof *index->nodes);
    }
    index->edge_used = index->node_used = 0;
    index->instance = instance;
    index->generation = generation;
    index->primed = false;
}

/* A wire that fired ran both ends; execution entering a node counts its
   runs, and a node nothing enters (an event, a pure value) counts by its
   busiest output. */
static void note_fire(
    ed_flow_activity_index *index,
    const behavior_instance_activity *record,
    uint64_t added,
    double fired
)
{
    const flow_traversal *t = &record->last;
    seen_node *from = node_entry(index, t->from_id);
    from->left = record->count > from->left ? record->count : from->left;
    from->fired = fired;
    if (t->kind == GRAPH_EDGE_EXEC) {
        seen_node *to = node_entry(index, t->to_id);
        to->entered += added;
        to->fired = fired;
    }
}

static void observe(ed_flow_activity_index *index, const behavior_instance_activity *record, double now)
{
    if ((index->edge_used + 1) * 2 > index->edge_capacity) {
        grow_edges(index);
    }
    seen_edge *seen = edge_slot(index->edges, index->edge_capacity, &record->last);
    if (seen->count == record->count) {
        return;
    }
    if (!seen->count) {
        *seen = (seen_edge){ .edge = record->last, .fired = -INFINITY };
        index->edge_used++;
    }
    uint64_t added = record->count - seen->count;
    seen->count = record->count;
    double fired = index->primed ? now : -INFINITY;
    seen->fired = fired;
    note_fire(index, record, added, fired);
}

/* Queries borrow a lazily derived view, read once per editor tick. */
static ed_flow_activity_index *activity_index(const editor *e)
{
    editor *owner = (editor *)e;
    if (!owner->flow_activity) {
        owner->flow_activity = xcalloc(1, sizeof *owner->flow_activity);
    }
    ed_flow_activity_index *index = owner->flow_activity;
    const behavior_instance *instance = ed_display_instance(e);
    int capacity = 0;
    uint64_t generation = 0;
    const behavior_instance_activity
        *table = instance
                   ? behavior_instance_activity_table(instance, &capacity, &generation)
                   : NULL;
    if (instance != index->instance || generation != index->generation) {
        reset(index, instance, generation);
    }
    if (index->primed && index->time == e->monotonic_time) {
        return index;
    }
    for (int i = 0; i < capacity; i++) {
        if (table[i].count) {
            observe(index, &table[i], e->monotonic_time);
        }
    }
    index->primed = true;
    index->time = e->monotonic_time;
    return index;
}

void ed_flow_activity_free(editor *e)
{
    if (e->flow_activity) {
        free(e->flow_activity->edges);
        free(e->flow_activity->nodes);
        free(e->flow_activity);
        e->flow_activity = NULL;
    }
}

void editor_query_edge_activity(const editor *e, const edge *edge, editor_edge_activity *out)
{
    assert(e && edge && out);
    *out = (editor_edge_activity){ 0 };
    ed_flow_activity_index *index = activity_index(e);
    if (!index->edge_used) {
        return;
    }
    flow_traversal key = {
        .from_id = edge->from_id,
        .from_pin_id = edge->from_pin_id,
        .to_id = edge->to_id,
        .to_pin_id = edge->to_pin_id,
        .kind = edge->kind,
    };
    const seen_edge *seen = edge_slot(index->edges, index->edge_capacity, &key);
    if (!seen->count) {
        return;
    }
    double since = fmax(0.0, e->monotonic_time - seen->fired);
    out->count = seen->count;
    out->pulse = (float)flow_visual_pulse(since);
    out->heat = edge->kind == GRAPH_EDGE_EXEC ? (float)flow_visual_heat(since) : 0;
}

void editor_query_node_activity(const editor *e, uint64_t node_id, editor_node_activity *out)
{
    assert(e && out);
    *out = (editor_node_activity){ .since = INFINITY };
    ed_flow_activity_index *index = activity_index(e);
    if (!index->node_used) {
        return;
    }
    const seen_node *seen = node_slot(index->nodes, index->node_capacity, node_id);
    if (!seen->id) {
        return;
    }
    out->runs = seen->entered ? seen->entered : seen->left;
    out->since = fmax(0.0, e->monotonic_time - seen->fired);
    out->pulse = (float)flow_visual_pulse(out->since);
}
