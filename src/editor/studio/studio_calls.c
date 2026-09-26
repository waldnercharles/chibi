/* Call Hierarchy (VS Code) in Output's Calls view: a function's or event's
 * callers or callees as a tree, from the shared call hierarchy. A handler's
 * row stands for its event among callers. A row opens the call, send or
 * Event node that joins it to its parent; a row whose children the trace
 * left out ends with More, which traces again from it. */
#include "assets/call_hierarchy.h"
#include "editor/studio/studio_graph_internal.h"
#include "graph/document/events.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Among callers a handler and its event take two levels, so this shows six
   hops through events. */
enum
{
    CALLS_DEPTH = 12,
};

static const char *asset_name(const reference_index *index, int asset)
{
    const reference_asset *found = reference_index_asset_at(index, asset);
    return found ? found->name : "";
}

static const char *item_label(const reference_index *index, const call_item *item)
{
    switch (item->kind) {
        case CALL_FUNCTION: return item->name ? item->name : "Unresolved";
        case CALL_GRAPH: return item->name ? item->name : "Event Graph";
        case CALL_PLACE:
            return item->name ? item->name : asset_name(index, item->asset);
        case CALL_HANDLER:
        case CALL_EVENT: return item->name;
    }
    return "";
}

static uint32_t item_icon(const reference_index *index, const call_item *item)
{
    switch (item->kind) {
        case CALL_FUNCTION: return KIT_ICON_FUNCTION;
        case CALL_GRAPH: return KIT_ICON_GRAPH;
        case CALL_PLACE:
            return studio_asset_kind_icon(
                (int)reference_index_asset_at(index, item->asset)->kind
            );
        case CALL_HANDLER:
        case CALL_EVENT: return KIT_ICON_EVENT;
    }
    return KIT_ICON_GRAPH;
}

/* Events are named, host events marked; everything else sits in an asset. */
static const char *item_meta(const reference_index *index, const call_item *item)
{
    if (item->kind != CALL_EVENT) {
        return asset_name(index, item->asset);
    }
    return graph_event_host_name(item->name) ? "Host" : "";
}

/* A function or an event as a root to trace, by name; a handler traces as
   its event. */
static studio_call_root root_of(const reference_index *index, const call_item *item)
{
    assert(item->kind == CALL_FUNCTION || item->kind == CALL_HANDLER || item->kind == CALL_EVENT);
    bool function = item->kind == CALL_FUNCTION;
    const reference_asset *asset = function
                                     ? reference_index_asset_at(index, item->asset)
                                     : NULL;
    studio_call_root root = { .kind = function ? REFERENCE_SYMBOL_GRAPH
                                               : REFERENCE_SYMBOL_EVENT,
                              .asset_kind = asset ? (int)asset->kind : 0,
                              .graph_id = item->graph_id };
    snprintf(root.title, sizeof root.title, "%s", item_label(index, item));
    snprintf(root.asset, sizeof root.asset, "%s", asset ? asset->name : "");
    return root;
}

/* Appends at's children as rows at depth, each followed by its own. Among
   callers a handler's only child is its event, so the handler row stands for
   it and shows the event's senders. */
static void add_rows(studio *s, const reference_index *index, const call_item *items, int at, int depth)
{
    for (int i = items[at].first; i < items[at].first + items[at].count; i++) {
        const call_item *item = &items[i];
        int below = !s->calls.callees && item->kind == CALL_HANDLER && item->count == 1
                      ? item->first
                      : i;
        studio_call row = { .icon = item_icon(index, item),
                            .at = studio_reference_of(index, item->sites[0]),
                            .depth = depth,
                            .open = depth == 0,
                            .parent = items[below].count > 0 || items[below].more,
                            .cycle = item->cycle || items[below].cycle,
                            .more = items[below].more };
        if (row.more) {
            row.from = root_of(index, &items[below]);
        }
        snprintf(row.label, sizeof row.label, "%s", item_label(index, item));
        snprintf(row.meta, sizeof row.meta, "%s", item_meta(index, item));
        apush(s->calls.rows, row);
        add_rows(s, index, items, below, depth + 1);
    }
}

static reference_symbol root_symbol(const studio_call_root *root, const reference_index *index)
{
    if (root->kind == REFERENCE_SYMBOL_EVENT) {
        return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                                   .asset = -1,
                                   .name = root->title };
    }
    return (reference_symbol){
        .kind = REFERENCE_SYMBOL_GRAPH,
        .asset = reference_index_find_name(index, (reference_asset_kind)root->asset_kind, root->asset),
        .id = root->graph_id,
    };
}

static void trace(studio *s)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    call_item *items = call_hierarchy(
        index,
        root_symbol(&s->calls.root, index),
        s->calls.callees,
        CALLS_DEPTH
    );
    asetlen(s->calls.rows, 0);
    add_rows(s, index, items, 0, 0);
    call_hierarchy_free(items);
    studio_output_calls(s);
}

/* Traces the callers of a function or event the index names. */
static void trace_symbol(studio *s, reference_symbol symbol, const char *title)
{
    const reference_asset *asset = reference_index_asset_at(
        app_scene_authoring_asset_index(s->scene),
        symbol.asset
    );
    if (symbol.kind != REFERENCE_SYMBOL_EVENT &&
        (symbol.kind != REFERENCE_SYMBOL_GRAPH || !asset)) {
        studio_report(s, "Not in the project");
        return;
    }
    studio_call_root *root = &s->calls.root;
    *root = (studio_call_root){ .kind = symbol.kind,
                                .asset_kind = asset ? (int)asset->kind : 0,
                                .graph_id = symbol.id };
    snprintf(root->asset, sizeof root->asset, "%s", asset ? asset->name : "");
    snprintf(root->title, sizeof root->title, "%s", title);
    s->calls.callees = 0;
    trace(s);
}

void studio_calls_node(studio *s, const node *n)
{
    const char *title;
    reference_symbol symbol = studio_node_symbol(s, n, &title);
    trace_symbol(s, symbol, title);
}

void studio_calls_graph(studio *s, const graph *gr)
{
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_GRAPH,
                                .asset = studio_refresh_document_asset(s),
                                .id = gr->graph_id };
    trace_symbol(s, symbol, gr->name);
}

void studio_calls_clear(studio *s)
{
    asetlen(s->calls.rows, 0);
    s->calls.root.title[0] = '\0';
}

/* Ends the subtrees of rows at depth or deeper with their More rows,
   written as -1 - row. */
static void end_subtrees(const studio_call *rows, int **more, int depth, int **shown)
{
    while (acount(*more) && rows[alast(*more)].depth >= depth) {
        apush(*shown, -1 - apop(*more));
    }
}

/* The rows under open rows as positions, and a More after an open row's
   subtree when the trace left some of its children out. */
static int *shown_rows(const studio *s)
{
    const studio_call *rows = s->calls.rows;
    int *shown = NULL, *more = NULL;
    int closed = -1; /* depth of the closed row being skipped past */
    for (int i = 0; i < acount(rows); i++) {
        if (closed >= 0 && rows[i].depth > closed) {
            continue;
        }
        end_subtrees(rows, &more, rows[i].depth, &shown);
        closed = rows[i].open ? -1 : rows[i].depth;
        if (!studio_matches(s->output_filter, rows[i].label) &&
            !studio_matches(s->output_filter, rows[i].meta)) {
            continue;
        }
        apush(shown, i);
        if (rows[i].open && rows[i].more) {
            apush(more, i);
        }
    }
    end_subtrees(rows, &more, 0, &shown);
    afree(more);
    return shown;
}

/* A More row: tracing from its row lists the children left out. True when
   it traced, which replaces the rows. */
static bool more_row(studio *s, int row)
{
    char key[16];
    snprintf(key, sizeof key, "m%d", row);
    kit_row_desc d = { .label = "More",
                       .icon = KIT_ICON_MORE,
                       .depth = s->calls.rows[row].depth + 1,
                       .twisty = -1 };
    unsigned events = kit_row_begin(s->kit, key, &d);
    kit_row_end(s->kit);
    if (!(events & KIT_ROW_CLICK)) {
        return false;
    }
    s->calls.root = s->calls.rows[row].from;
    trace(s);
    return true;
}

static void head(studio *s)
{
    kit *k = s->kit;
    static const char *const DIRECTIONS[] = { "Callers", "Callees" };
    kit_push(k, "head", NULL, "find-head");
    kit_icon(k, "icon", NULL, s->calls.callees ? KIT_ICON_CALLEES : KIT_ICON_CALLERS);
    kit_label(k, "title", "find-title", s->calls.root.title);
    kit_leaf(k, "gap", NULL, "spacer");
    kit_push(k, "direction", NULL, "output-scope");
    if (kit_toggle_group(k, "direction", &s->calls.callees, DIRECTIONS, 2, 0)) {
        trace(s);
    }
    kit_pop(k);
    kit_pop(k);
}

void studio_calls_view(studio *s, float row_height)
{
    kit *k = s->kit;
    if (s->calls.root.title[0]) {
        head(s);
    }
    int *shown = shown_rows(s);
    kit_list_desc list = { .count = acount(shown),
                           .row_height = row_height,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "calls", "output-list find-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        if (shown[r] < 0 && more_row(s, -1 - shown[r])) {
            break;
        }
        if (shown[r] < 0) {
            continue;
        }
        studio_call *row = &s->calls.rows[shown[r]];
        char key[16], meta[160];
        snprintf(key, sizeof key, "c%d", shown[r]);
        snprintf(meta, sizeof meta, "%s%s", row->meta, row->cycle ? " \xe2\x86\xbb" : "");
        kit_row_desc d = { .label = row->label,
                           .icon = row->icon,
                           .depth = row->depth,
                           .twisty = row->parent ? row->open : -1,
                           .meta = meta };
        unsigned events = kit_row_begin(k, key, &d);
        kit_row_end(k);
        if (events & KIT_ROW_TWISTY) {
            row->open = !row->open;
        } else if (events & KIT_ROW_CLICK) {
            studio_open_reference(s, &row->at);
        }
    }
    kit_list_end(k);
    if (!acount(shown)) {
        kit_label(k, "empty", "output-empty", acount(s->calls.rows) ? "No matches" : "No calls");
    }
    afree(shown);
}
