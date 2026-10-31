#ifndef CHIBI_CALL_HIERARCHY_H
#define CHIBI_CALL_HIERARCHY_H

#include "authoring/reference_index.h"

/* A caller or callee tree over a reference index (VS Code's call hierarchy).
 *
 * Items are functions and macros, the runs Event nodes start (handlers),
 * events, and senders outside graphs: a ui document's emit('Name'), an action
 * binding. A function's callers are the items holding its calls; an event's,
 * the items sending it; a handler's, its event. An item's callees are the
 * functions it calls and the events it sends; an event's, its handlers.
 *
 * In an event graph a call or send belongs to each handler whose run reaches
 * it, and otherwise to the graph, which has no callers or callees. */
typedef enum
{
    CALL_FUNCTION, /* asset, graph_id; a function or macro */
    CALL_HANDLER, /* asset, graph_id, node_id (its Event node); name is its event */
    CALL_GRAPH, /* asset, graph_id: the graph part no handler runs */
    CALL_EVENT, /* name; asset is its event definition, or -1 */
    CALL_PLACE, /* asset; name is the object or component field, or NULL */
} call_kind;

typedef struct
{
    call_kind kind;
    int asset;
    uint64_t graph_id, node_id;
    /* The function's, handler's event's, event's or place's name, borrowed
     * from the index or the root symbol; NULL when unnamed or unresolved. */
    const char *name;
    const reference *handle; /* a handler's Event node reference */
    int parent;              /* -1 for the root */
    int depth;               /* 0 for the root */
    int first, count;        /* its children's positions */
    bool cycle;              /* repeats an ancestor, so it is not expanded */
    bool more; /* has children left out: past the depth or item limit */
    /* The calls, sends or handles joining it to its parent (support/array.h). */
    const reference **sites;
} call_item;

enum
{
    CALL_HIERARCHY_ITEMS = 1024,
};

/* Root's callees, or callers, breadth first to `depth` levels below it, as an
 * array (support/array.h) the caller frees with call_hierarchy_free. root is a
 * GRAPH or EVENT symbol, and the array's first item. */
call_item *call_hierarchy(const reference_index *index, reference_symbol root, bool callees, int depth);
void call_hierarchy_free(call_item *items);

#endif
