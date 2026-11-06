#include "assets/call_hierarchy.h"

#include "support/array.h"

#include <assert.h>
#include <string.h>

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

static bool same_item(const call_item *a, const call_item *b)
{
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
        case CALL_EVENT: return same_text(a->name, b->name);
        case CALL_PLACE:
            return a->asset == b->asset && same_text(a->name, b->name);
        case CALL_HANDLER:
            return a->asset == b->asset && a->graph_id == b->graph_id &&
                   a->node_id == b->node_id;
        case CALL_FUNCTION:
        case CALL_GRAPH:
            return a->asset == b->asset && a->graph_id == b->graph_id;
    }
    return false;
}

static reference_symbol graph_symbol(int asset, uint64_t id)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_GRAPH, .asset = asset, .id = id };
}

static reference_symbol event_symbol(const char *name)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT, .asset = -1, .name = name };
}

static call_item function_item(const reference_index *index, int asset, uint64_t id)
{
    const reference *definition = reference_index_definition(index, graph_symbol(asset, id));
    return (call_item){ .kind = CALL_FUNCTION,
                        .asset = asset,
                        .graph_id = id,
                        .name = definition ? definition->symbol.name : NULL };
}

static call_item event_item(const reference_index *index, const char *name)
{
    return (call_item){ .kind = CALL_EVENT,
                        .asset = reference_index_event_asset(index, name),
                        .name = name };
}

static call_item handler_item(const reference *handle)
{
    return (call_item){ .kind = CALL_HANDLER,
                        .asset = handle->source,
                        .graph_id = handle->graph_id,
                        .node_id = handle->node_id,
                        .name = handle->symbol.name,
                        .handle = handle };
}

/* Adds child with the site joining it to its parent, or the site to the
 * same child found before. */
static void found(call_item **children, call_item child, const reference *site)
{
    for (int i = 0; i < acount(*children); i++) {
        if (same_item(&(*children)[i], &child)) {
            apush((*children)[i].sites, site);
            return;
        }
    }
    child.sites = NULL;
    apush(child.sites, site);
    apush(*children, child);
}

/* The items holding a call or send: its function, the handlers whose runs
 * reach it, else its graph; outside graphs, its place. */
static void holders(const reference_index *index, const reference *site, call_item **children)
{
    if (!site->node_id) {
        const char *place = site->object ? site->object : site->component;
        found(children, (call_item){ .kind = CALL_PLACE, .asset = site->source, .name = place }, site);
        return;
    }
    if (reference_index_definition(index, graph_symbol(site->source, site->graph_id))) {
        found(children, function_item(index, site->source, site->graph_id), site);
        return;
    }
    bool run = false;
    for (int i = 0; i < reference_index_count(index); i++) {
        const reference *handle = reference_index_at(index, i);
        if (handle->role == REFERENCE_HANDLE && handle->source == site->source &&
            handle->graph_id == site->graph_id &&
            reference_index_reaches(index, handle, site->node_id)) {
            found(children, handler_item(handle), site);
            run = true;
        }
    }
    if (!run) {
        call_item part = { .kind = CALL_GRAPH,
                           .asset = site->source,
                           .graph_id = site->graph_id,
                           .name = site->graph };
        found(children, part, site);
    }
}

static void callers(const reference_index *index, const call_item *item, call_item **children)
{
    if (item->kind == CALL_HANDLER) {
        found(children, event_item(index, item->name), item->handle);
        return;
    }
    if (item->kind != CALL_FUNCTION && item->kind != CALL_EVENT) {
        return;
    }
    bool function = item->kind == CALL_FUNCTION;
    const reference **uses = NULL;
    reference_index_find_uses(
        index,
        function ? graph_symbol(item->asset, item->graph_id) : event_symbol(item->name),
        &uses
    );
    for (int i = 0; i < acount(uses); i++) {
        if (uses[i]->role == (function ? REFERENCE_CALL : REFERENCE_SEND)) {
            holders(index, uses[i], children);
        }
    }
    afree(uses);
}

/* Whether a function's graph, or a handler's run, holds a node reference. */
static bool runs(const reference_index *index, const call_item *item, const reference *r)
{
    return r->source == item->asset && r->graph_id == item->graph_id && r->node_id &&
           (item->kind == CALL_FUNCTION ||
            reference_index_reaches(index, item->handle, r->node_id));
}

static void callees(const reference_index *index, const call_item *item, call_item **children)
{
    if (item->kind == CALL_EVENT) {
        const reference **uses = NULL;
        reference_index_find_uses(index, event_symbol(item->name), &uses);
        for (int i = 0; i < acount(uses); i++) {
            if (uses[i]->role == REFERENCE_HANDLE) {
                found(children, handler_item(uses[i]), uses[i]);
            }
        }
        afree(uses);
        return;
    }
    if (item->kind != CALL_FUNCTION && item->kind != CALL_HANDLER) {
        return;
    }
    for (int i = 0; i < reference_index_count(index); i++) {
        const reference *r = reference_index_at(index, i);
        bool call = r->role == REFERENCE_CALL;
        if ((call || r->role == REFERENCE_SEND) && runs(index, item, r)) {
            found(
                children,
                call ? function_item(index, r->symbol.asset, r->symbol.id)
                     : event_item(index, r->symbol.name),
                r
            );
        }
    }
}

static bool repeats(const call_item *items, int parent, const call_item *item)
{
    for (int at = parent; at >= 0; at = items[at].parent) {
        if (same_item(&items[at], item)) {
            return true;
        }
    }
    return false;
}

/* Frees the sites of items from `first` on. */
static void free_sites(call_item *items, int first)
{
    for (int i = first; i < acount(items); i++) {
        afree(items[i].sites);
    }
}

call_item *call_hierarchy(const reference_index *index, reference_symbol root, bool want_callees, int depth)
{
    assert(root.kind == REFERENCE_SYMBOL_GRAPH || root.kind == REFERENCE_SYMBOL_EVENT);
    call_item *items = NULL;
    apush(
        items,
        root.kind == REFERENCE_SYMBOL_EVENT ? event_item(index, root.name)
                                            : function_item(index, root.asset, root.id)
    );
    items[0].parent = -1;
    call_item *children = NULL;
    for (int at = 0; at < acount(items); at++) {
        if (items[at].cycle) {
            continue;
        }
        if (want_callees) {
            callees(index, &items[at], &children);
        } else {
            callers(index, &items[at], &children);
        }
        /* Children past the depth or the item limit are left out. */
        int room = items[at].depth < depth ? CALL_HIERARCHY_ITEMS - acount(items) : 0;
        int kept = acount(children) < room ? acount(children) : room;
        items[at].more = kept < acount(children);
        items[at].first = acount(items);
        items[at].count = kept;
        for (int i = 0; i < kept; i++) {
            call_item child = children[i];
            child.parent = at;
            child.depth = items[at].depth + 1;
            child.cycle = repeats(items, at, &child);
            apush(items, child);
        }
        free_sites(children, kept);
        asetlen(children, 0);
    }
    afree(children);
    return items;
}

void call_hierarchy_free(call_item *items)
{
    free_sites(items, 0);
    afree(items);
}
