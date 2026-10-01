/* Find in graph (Unreal's Find Results, Blender's Find Node): a field over the
 * canvas lists the nodes of the open behavior whose title, pins, values,
 * bindings, expression or comment contain the query, graph by graph. Up and
 * Down step through them in the graph; Enter or a click jumps to one. */
#include "editor/studio/studio_graph_internal.h"
#include "graph/authoring/find.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

enum
{
    VISIBLE_ROWS = 8,
};

void sg_find_open(studio *s)
{
    s->graph.find.open = true;
    s->graph.find.focus = true;
}

static void close_find(studio *s)
{
    s->graph.find.open = false;
    asetlen(s->graph.find.hits, 0);
}

/* A match on an inline node points at the node that draws it. */
static void add_hits(studio *s, graph *gr)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        char title[96];
        graph_find_match match;
        if (!n->desc || !graph_find_node(
                            e->asset,
                            gr,
                            n,
                            ed_node_title(e, n, title, sizeof title),
                            g->find.query,
                            &match
                        )) {
            continue;
        }
        node *owner = graph_inline_owner(gr, n);
        uint32_t icon = sg_node_icon(s, owner);
        studio_find_hit hit = { .graph_id = gr->graph_id,
                                .node_id = owner->id,
                                .icon = icon ? icon : KIT_ICON_HEXAGON };
        ed_node_title(e, owner, hit.title, sizeof hit.title);
        snprintf(hit.text, sizeof hit.text, "%s", match.text);
        apush(g->find.hits, hit);
    }
}

static bool same_hit(const studio_find_hit *a, const studio_find_hit *b)
{
    return a->graph_id == b->graph_id && a->node_id == b->node_id &&
           !strcmp(a->text, b->text);
}

/* Results keep the behavior's graph order and follow the graph as it
   changes, the current one by identity; a new query starts before the first
   without moving the view (cmdk). */
static void collect(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    studio_find_hit shown = { 0 };
    bool showing = g->find.current >= 0 && g->find.current < acount(g->find.hits);
    if (showing) {
        shown = g->find.hits[g->find.current];
    }
    asetlen(g->find.hits, 0);
    for (int i = 0; i < acount(e->asset->graphs); i++) {
        if (!e->asset->graphs[i]->math_expression) {
            add_hits(s, e->asset->graphs[i]);
        }
    }
    g->find.current = -1;
    for (int i = 0; showing && i < acount(g->find.hits); i++) {
        g->find.current = same_hit(&g->find.hits[i], &shown) ? i : g->find.current;
    }
    if (strcmp(g->find.seen, g->find.query)) {
        snprintf(g->find.seen, sizeof g->find.seen, "%s", g->find.query);
        g->find.current = -1;
    }
}

static void jump(studio *s, int index)
{
    const studio_find_hit *hit = &s->graph.find.hits[index];
    s->graph.find.current = index;
    studio_reveal_node(s, hit->graph_id, hit->node_id);
}

/* Steps through the results in the graph, wrapping at either end. */
static void step(studio *s, int direction)
{
    int count = acount(s->graph.find.hits), current = s->graph.find.current;
    if (!count) {
        return;
    }
    int next = current < 0 ? (direction > 0 ? 0 : count - 1) : current + direction;
    jump(s, (next + count) % count);
}

static void find_keys(studio *s)
{
    studio_graph *g = &s->graph;
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        if (key == SDLK_ESCAPE) {
            close_find(s);
            return;
        }
        if (key == SDLK_UP || key == SDLK_DOWN) {
            step(s, key == SDLK_DOWN ? 1 : -1);
        }
        if (key == SDLK_RETURN && acount(g->find.hits)) {
            jump(s, g->find.current < 0 ? 0 : g->find.current);
            close_find(s);
            s->focus_area = g->area;
            return;
        }
    }
}

static void emit_bar(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    kit_push(k, "bar", NULL, "find-widget-bar");
    kit_search(k, "query", g->find.query, sizeof g->find.query, "Find");
    /* Escape clears and blurs the field in the frame it arrives, so the
       field's focus a frame ago counts too. */
    bool typing = kit_item_active(k) || g->find.typing;
    g->find.typing = kit_item_active(k);
    if (g->find.focus) {
        kit_focus(k);
        g->find.focus = false;
    }
    int count = acount(g->find.hits);
    char label[32] = "";
    if (g->find.query[0] && !count) {
        snprintf(label, sizeof label, "No results");
    } else if (g->find.current >= 0) {
        snprintf(label, sizeof label, "%d/%d", g->find.current + 1, count);
    } else if (count) {
        snprintf(label, sizeof label, "%d", count);
    }
    kit_label(k, "count", "find-widget-count", label);
    kit_button_desc previous = { .icon = KIT_ICON_UP,
                                 .tip = "Previous",
                                 .shortcut = "\xe2\x86\x91",
                                 .flags = count ? 0 : KIT_DISABLED };
    if (kit_button(k, "previous", &previous)) {
        step(s, -1);
    }
    kit_button_desc next = { .icon = KIT_ICON_DOWN,
                             .tip = "Next",
                             .shortcut = "\xe2\x86\x93",
                             .flags = count ? 0 : KIT_DISABLED };
    if (kit_button(k, "next", &next)) {
        step(s, 1);
    }
    kit_button_desc close = { .icon = KIT_ICON_CLOSE, .tip = "Close", .shortcut = "Esc" };
    if (kit_button(k, "close", &close)) {
        close_find(s);
    }
    kit_pop(k);
    if (typing && g->find.open) {
        find_keys(s);
    }
}

static const char *graph_label(const graph *gr)
{
    return gr->name && gr->name[0] ? gr->name : "Event Graph";
}

static void emit_results(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    int count = acount(g->find.hits);
    if (!count) {
        return;
    }
    float row = kit_metric(s->kit, "--row");
    kit_list_desc list = { .count = count, .row_height = row, .reveal = g->find.current };
    int first, last;
    kit_list_begin(k, "results", "graph-find-list", &list, &first, &last);
    char height[32];
    snprintf(height, sizeof height, "%.0fdp", row * (count < VISIBLE_ROWS ? count : VISIBLE_ROWS));
    kit_style(k, "height", height);
    uint64_t active = sg_active_graph(s)->graph_id;
    for (int r = first; r <= last && r < count; r++) {
        const studio_find_hit *hit = &g->find.hits[r];
        const graph *gr = behavior_asset_find_graph(e->asset, hit->graph_id);
        char key[16], meta[256];
        snprintf(key, sizeof key, "r%d", r);
        /* A match in another graph names it. */
        snprintf(
            meta,
            sizeof meta,
            "%s%s%s",
            strcmp(hit->text, hit->title) ? hit->text : "",
            hit->graph_id != active && strcmp(hit->text, hit->title)
                ? " \xc2\xb7 "
                : "",
            hit->graph_id != active && gr ? graph_label(gr) : ""
        );
        kit_row_desc d = { .label = hit->title,
                           .icon = hit->icon,
                           .twisty = -1,
                           .meta = meta,
                           .flags = r == g->find.current ? KIT_ON : 0 };
        if (kit_row_begin(k, key, &d) & KIT_ROW_CLICK) {
            jump(s, r);
        }
        kit_row_end(k);
    }
    kit_list_end(k);
}

/* The panel floats over the canvas's top right, in the Studio document so it
   stays unscaled. */
void sg_find_panel(studio *s)
{
    studio_graph *g = &s->graph;
    if (!g->find.open) {
        return;
    }
    collect(s);
    kit *k = s->kit;
    kit_push(k, "find", NULL, "find-widget");
    emit_bar(s);
    if (g->find.open) {
        emit_results(s);
    }
    kit_pop(k);
}

/* Framing waits for the frame's layout, so a node in a graph just opened
   frames too. */
void studio_reveal_node(studio *s, uint64_t graph_id, uint64_t node_id)
{
    editor *e = sg_graph_editor(s);
    if (!studio_graph_active(s) || editor_open_graph(e, graph_id) != 0) {
        return;
    }
    node *n = sg_node_by_id(s, node_id);
    if (!n) {
        return;
    }
    n = graph_inline_owner(sg_active_graph(s), n);
    sg_select_only(e, n);
    e->front_node = n;
    s->graph.reveal = n->id;
    s->graph.reveal_frame = true;
}
