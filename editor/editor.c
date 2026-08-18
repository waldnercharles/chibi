#include "editor.h"

#include "core/registry.h"
#include "core/serialize.h"
#include "eval_view.h"
#include "ne/ne.h"
#include "third_party/ckit/ckit.h"

#include <ctype.h>
#include <dcimgui.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ED_HIST_MAX 64

/* Pin id encoding: 48 bits of node pointer, 8 bits of pin index, 8 bits of
 * kind. Decoding never trusts the pointer bits: it iterates the graph and
 * recomputes the encoding. */
#define ED_PIN_PTR_MASK 0x0000FFFFFFFFFFFFull

typedef struct ed_link
{
    ne_link_id id;
    node *from;
    int from_pin;
    node *to;
    int to_pin;
    int used; /* matched a graph edge this frame */
} ed_link;

/* A graph retired by undo/redo/load, kept alive until the editor's queued
 * deletions for it have been consumed. ne frees its objects one frame after
 * the delete action accepts them; freeing this memory earlier could hand a
 * reused pointer back to ne as a "new" node id while the old object is still
 * alive in its maps. */
typedef struct ed_grave
{
    graph g;
    ne_node_id *node_ids;
    int node_id_count;
    ne_link_id *link_ids;
    int link_id_count;
} ed_grave;

struct editor
{
    graph *g;
    ne_editor_context *ctx;
    eval_view *ev;
    Map seen; /* node_id -> 1 once its position has been applied */
    ed_link *links;
    int link_count, link_cap;
    ne_link_id next_link_id;
    ed_grave *graves;
    int grave_count, grave_cap;
    int in_frame;     /* ne_begin...ne_end is active */
    int frames_drawn; /* ne context has been cycled at least once */
    float spawn_x, spawn_y;
    char search[64];            /* spawn menu filter */
    char *clipboard;            /* JSON from copy/cut; NULL if empty */
    ImVec2 view_pos, view_size; /* editor view rect, for the mini-map */
    ne_node_id menu_node;
    ne_link_id menu_link;
    ne_pin_id menu_pin;
    char **undo; /* JSON snapshots, oldest first */
    int undo_count, undo_cap;
    char **redo;
    int redo_count, redo_cap;
};

/* --- colors --- */

static ImU32 ed_pin_color_u32(pin_type t)
{
    switch (t) {
        case PIN_F32:
        case PIN_F64: return IM_COL32(110, 180, 255, 255);
        case PIN_I32:
        case PIN_U32:
        case PIN_ENUM: return IM_COL32(110, 220, 160, 255);
        case PIN_BOOL: return IM_COL32(255, 160, 120, 255);
        case PIN_STR: return IM_COL32(220, 160, 255, 255);
        case PIN_STRUCT: return IM_COL32(255, 220, 120, 255);
        case PIN_FLOW:
        default: return IM_COL32(220, 220, 220, 255);
    }
}

static ImVec4 ed_u32_to_v4(ImU32 c)
{
    return (ImVec4){ (float)(c & 255) / 255.0f,
                     (float)((c >> 8) & 255) / 255.0f,
                     (float)((c >> 16) & 255) / 255.0f,
                     (float)((c >> 24) & 255) / 255.0f };
}

static ImVec4 ed_pin_color(pin_type t)
{ return ed_u32_to_v4(ed_pin_color_u32(t)); }

static ImVec4 ed_dim(ImVec4 c, float f)
{
    c.x *= f;
    c.y *= f;
    c.z *= f;
    return c;
}

static const ImVec4 ED_VAL_COLOR = { 0.47f, 0.78f, 1.0f, 1.0f };
static const ImVec4 ED_TITLE_COLOR = { 0.92f, 0.92f, 0.94f, 1.0f };

/* --- ids --- */

static ne_pin_id ed_pin_id(const node *n, int kind, int index)
{
    return ((ne_pin_id)(uintptr_t)n & ED_PIN_PTR_MASK) |
           ((uint64_t)(uint32_t)index << 48) | ((uint64_t)(uint32_t)kind << 56);
}

static int ed_pin_decode(const graph *g, ne_pin_id id, node **n, int *kind, int *index)
{
    for (int i = 0; i < g->node_count; i++) {
        node *nd = g->nodes[i];
        for (int p = 0; p < nd->desc->input_count; p++)
            if (ed_pin_id(nd, ne_pin_kind_input, p) == id) {
                *n = nd;
                *kind = ne_pin_kind_input;
                *index = p;
                return 1;
            }
        for (int p = 0; p < nd->desc->output_count; p++)
            if (ed_pin_id(nd, ne_pin_kind_output, p) == id) {
                *n = nd;
                *kind = ne_pin_kind_output;
                *index = p;
                return 1;
            }
    }
    return 0;
}

static node *ed_node_by_id(const graph *g, ne_node_id id)
{
    for (int i = 0; i < g->node_count; i++)
        if ((ne_node_id)(uintptr_t)g->nodes[i] == id) return g->nodes[i];
    return NULL;
}

/* --- link table --- */

static ed_link *ed_entry_find_unused(const editor *e, const node *from, int from_pin, const node *to, int to_pin)
{
    for (int i = 0; i < e->link_count; i++) {
        ed_link *l = &e->links[i];
        if (!l->used && l->from == from && l->from_pin == from_pin &&
            l->to == to && l->to_pin == to_pin)
            return l;
    }
    return NULL;
}

static ed_link *ed_entry_find_by_id(const editor *e, ne_link_id id)
{
    for (int i = 0; i < e->link_count; i++)
        if (e->links[i].id == id) return &e->links[i];
    return NULL;
}

static ed_link *ed_entry_new(editor *e)
{
    if (e->link_count == e->link_cap) {
        int nc = e->link_cap ? e->link_cap * 2 : 8;
        ed_link *l = (ed_link *)realloc(e->links, (size_t)nc * sizeof *l);
        if (!l) return NULL;
        e->links = l;
        e->link_cap = nc;
    }
    ed_link *l = &e->links[e->link_count++];
    memset(l, 0, sizeof *l);
    l->id = ++e->next_link_id;
    return l;
}

static void ed_entry_drop(editor *e, ed_link *l)
{
    *l = e->links[e->link_count - 1];
    e->link_count--;
}

static void ed_entries_drop_node(editor *e, const node *n)
{
    for (int i = e->link_count - 1; i >= 0; i--)
        if (e->links[i].from == n || e->links[i].to == n)
            ed_entry_drop(e, &e->links[i]);
}

/* --- graveyard: retired graphs kept until ne consumed their deletions --- */

static void ed_grave_queue_node(ed_grave *gv, ne_node_id id)
{
    ne_node_id *ns = (ne_node_id *)
        realloc(gv->node_ids, (size_t)(gv->node_id_count + 1) * sizeof *ns);
    if (!ns) return;
    gv->node_ids = ns;
    gv->node_ids[gv->node_id_count++] = id;
}

static void ed_grave_queue_link(ed_grave *gv, ne_link_id id)
{
    ne_link_id *ls = (ne_link_id *)
        realloc(gv->link_ids, (size_t)(gv->link_id_count + 1) * sizeof *ls);
    if (!ls) return;
    gv->link_ids = ls;
    gv->link_ids[gv->link_id_count++] = id;
}

static void ed_grave_take_node(editor *e, ne_node_id id)
{
    for (int gi = 0; gi < e->grave_count; gi++) {
        ed_grave *gv = &e->graves[gi];
        for (int i = 0; i < gv->node_id_count; i++)
            if (gv->node_ids[i] == id) {
                gv->node_ids[i] = gv->node_ids[gv->node_id_count - 1];
                gv->node_id_count--;
                return;
            }
    }
}

static void ed_grave_take_link(editor *e, ne_link_id id)
{
    for (int gi = 0; gi < e->grave_count; gi++) {
        ed_grave *gv = &e->graves[gi];
        for (int i = 0; i < gv->link_id_count; i++)
            if (gv->link_ids[i] == id) {
                gv->link_ids[i] = gv->link_ids[gv->link_id_count - 1];
                gv->link_id_count--;
                return;
            }
    }
}

static void ed_grave_sweep(editor *e)
{
    int w = 0;
    for (int i = 0; i < e->grave_count; i++) {
        ed_grave *gv = &e->graves[i];
        if (gv->node_id_count == 0 && gv->link_id_count == 0) {
            graph_free(&gv->g);
            free(gv->node_ids);
            free(gv->link_ids);
        } else {
            e->graves[w++] = *gv;
        }
    }
    e->grave_count = w;
}

/* --- undo/redo: full-graph JSON snapshots --- */

static void ed_hist_push(char ***st, int *n, int *cap, char *json)
{
    if (*n == ED_HIST_MAX) { /* at cap: drop the oldest snapshot */
        free((*st)[0]);
        memmove(*st, *st + 1, (size_t)(ED_HIST_MAX - 1) * sizeof(char *));
        (*st)[ED_HIST_MAX - 1] = json;
        return;
    }
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 16;
        if (nc > ED_HIST_MAX) nc = ED_HIST_MAX;
        char **s = (char **)realloc(*st, (size_t)nc * sizeof(char *));
        if (!s) {
            free(json);
            return;
        }
        *st = s;
        *cap = nc;
    }
    (*st)[(*n)++] = json;
}

static void ed_hist_clear(char **st, int *n)
{
    for (int i = 0; i < *n; i++) free(st[i]);
    *n = 0;
}

/* Copy ne's node positions back into the graph so snapshots and saves carry
 * drags made since the last edit. Only valid inside ne_begin...ne_end. */
static void ed_sync_positions(const editor *e)
{
    if (!e->in_frame) return;
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        ImVec2 p = ne_get_node_position((ne_node_id)(uintptr_t)n);
        if (p.x == FLT_MAX && p.y == FLT_MAX)
            continue; /* not yet in the editor */
        n->x = p.x;
        n->y = p.y;
    }
}

static void ed_snapshot(editor *e)
{
    ed_sync_positions(e);
    char *json = graph_save_json(e->g);
    if (json) ed_hist_push(&e->undo, &e->undo_count, &e->undo_cap, json);
    ed_hist_clear(e->redo, &e->redo_count);
}

/* Replace the graph with the JSON snapshot. The old graph is retired: its
 * ne-side objects are queued for deletion and its memory is held until the
 * editor consumes those deletions, so a reused pointer can never alias a
 * still-alive ne object. */
static int ed_adopt(editor *e, const char *json)
{
    graph *fresh = graph_load_json(json);
    if (!fresh) return -1;

    if (e->frames_drawn > 0) {
        ed_grave *gv = NULL;
        if (e->grave_count == e->grave_cap) {
            int nc = e->grave_cap ? e->grave_cap * 2 : 4;
            ed_grave *gs = (ed_grave *)realloc(e->graves, (size_t)nc * sizeof *gs);
            if (gs) {
                e->graves = gs;
                e->grave_cap = nc;
            }
        }
        if (e->grave_count < e->grave_cap) {
            gv = &e->graves[e->grave_count++];
            memset(gv, 0, sizeof *gv);
            gv->g = *e->g;
        }
        for (int i = 0; i < e->g->node_count; i++) {
            ne_node_id id = (ne_node_id)(uintptr_t)e->g->nodes[i];
            ne_delete_node(id);
            if (gv) ed_grave_queue_node(gv, id);
        }
        for (int i = 0; i < e->link_count; i++) {
            ne_link_id id = e->links[i].id;
            ne_delete_link(id);
            if (gv) ed_grave_queue_link(gv, id);
        }
        if (gv) {
            memset(e->g, 0, sizeof *e->g);
        } else {
            graph_free(e->g); /* OOM: no pointer protection, delete now */
        }
    }
    map_clear(e->seen);
    e->link_count = 0;

    *e->g = *fresh;
    free(fresh);
    return 0;
}

static void ed_undo(editor *e)
{
    if (e->undo_count == 0) return;
    char *prev = e->undo[--e->undo_count];
    char *cur = graph_save_json(e->g);
    if (cur) ed_hist_push(&e->redo, &e->redo_count, &e->redo_cap, cur);
    ed_adopt(e, prev);
    free(prev);
}

static void ed_redo(editor *e)
{
    if (e->redo_count == 0) return;
    char *next = e->redo[--e->redo_count];
    char *cur = graph_save_json(e->g);
    if (cur) ed_hist_push(&e->undo, &e->undo_count, &e->undo_cap, cur);
    ed_adopt(e, next);
    free(next);
}

/* --- frame sections --- */

static void ed_draw_links(editor *e)
{
    const graph *g = e->g;
    ImVec4 col = ed_u32_to_v4(IM_COL32(160, 170, 200, 255));

    for (int i = 0; i < e->link_count; i++) e->links[i].used = 0;

    for (int i = 0; i < g->edge_count; i++) {
        const edge *ed = &g->edges[i];
        ed_link *l = ed_entry_find_unused(e, ed->from, ed->from_pin, ed->to, ed->to_pin);
        if (!l) l = ed_entry_new(e);
        if (!l) continue;
        l->from = ed->from;
        l->from_pin = ed->from_pin;
        l->to = ed->to;
        l->to_pin = ed->to_pin;
        l->used = 1;
        ne_link(
            l->id,
            ed_pin_id(ed->from, ne_pin_kind_output, ed->from_pin),
            ed_pin_id(ed->to, ne_pin_kind_input, ed->to_pin),
            col,
            3.0f
        );
    }

    /* Entries whose edge vanished this frame (node removed, undo, load) are
     * pruned; ne_delete_link queues the editor-side removal, which the delete
     * handler consumes on a later frame. */
    for (int i = e->link_count - 1; i >= 0; i--) {
        if (!e->links[i].used) {
            ne_link_id id = e->links[i].id;
            ed_entry_drop(e, &e->links[i]);
            ne_delete_link(id);
        }
    }
}

static void ed_handle_create(editor *e)
{
    ImVec4 col = ed_u32_to_v4(IM_COL32(120, 200, 255, 255));
    ne_begin_create(col, 3.0f);

    ne_pin_id start, end;
    while (ne_query_new_link(&start, &end)) {
        node *from = NULL, *to = NULL;
        int fkind = 0, fpin = 0, tkind = 0, tpin = 0;
        int ok = ed_pin_decode(e->g, start, &from, &fkind, &fpin) &&
                 ed_pin_decode(e->g, end, &to, &tkind, &tpin) &&
                 fkind == ne_pin_kind_output && tkind == ne_pin_kind_input &&
                 pin_types_compatible(&from->desc->outputs[fpin], &to->desc->inputs[tpin]);
        if (ok) {
            ed_snapshot(e);
            graph_connect(e->g, from, fpin, to, tpin);
            ne_accept_new_item();
        } else {
            ne_reject_new_item();
        }
    }

    ne_end_create();
}

static void ed_handle_delete(editor *e)
{
    ne_begin_delete();

    ne_node_id nid;
    while (ne_query_deleted_node(&nid)) {
        node *n = ed_node_by_id(e->g, nid);
        if (n) {
            ed_snapshot(e);
            ed_entries_drop_node(e, n); /* ne marks its links via dependencies */
            graph_remove_node(e->g, n);
            map_del(e->seen, nid);
        } else {
            ed_grave_take_node(e, nid); /* retired by undo/redo/load */
        }
        ne_accept_deleted_item(true);
    }

    ne_link_id lid;
    ne_pin_id start, end;
    while (ne_query_deleted_link(&lid, &start, &end)) {
        ed_link *l = ed_entry_find_by_id(e, lid);
        if (l) {
            ed_snapshot(e);
            graph_remove_edge(e->g, l->from, l->from_pin, l->to, l->to_pin);
            ed_entry_drop(e, l);
        } else {
            ed_grave_take_link(e, lid); /* retired by undo/redo/load */
        }
        ne_accept_deleted_item(false);
    }

    ne_end_delete();
    ed_grave_sweep(e);
}

static int ed_name_matches(const char *name, const char *filter)
{
    if (!filter[0]) return 1;
    while (*filter) {
        const char *at = strchr(name, (int)tolower((unsigned char)*filter));
        const char *at2 = strchr(name, (int)toupper((unsigned char)*filter));
        if (!at && !at2) return 0;
        const char *next = at && (!at2 || at < at2) ? at : at2;
        name = next + 1;
        filter++;
    }
    return 1;
}

static void ed_handle_spawn_menu(editor *e)
{
    if (ne_show_background_context_menu()) {
        ImVec2 p = ne_screen_to_canvas(ImGui_GetMousePos());
        e->spawn_x = p.x;
        e->spawn_y = p.y;
        ImGui_OpenPopup("spawn", 0);
    }
    if (ImGui_BeginPopup("spawn", 0)) {
        ImGui_InputText("Filter", e->search, sizeof e->search, 0);
        ImGui_Separator();
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *d = registry_node(i);
            if (!ed_name_matches(d->name, e->search)) continue;
            if (ImGui_MenuItem(d->name))
                editor_spawn(e, d, e->spawn_x, e->spawn_y);
        }
        ImGui_EndPopup();
    }
}

static void ed_handle_context_menus(editor *e)
{
    if (ne_show_node_context_menu(&e->menu_node))
        ImGui_OpenPopup("node_menu", 0);
    if (ne_show_link_context_menu(&e->menu_link))
        ImGui_OpenPopup("link_menu", 0);
    if (ne_show_pin_context_menu(&e->menu_pin)) ImGui_OpenPopup("pin_menu", 0);

    if (ImGui_BeginPopup("node_menu", 0)) {
        if (ImGui_MenuItem("Delete")) {
            node *n = ed_node_by_id(e->g, e->menu_node);
            if (n) {
                ed_snapshot(e);
                /* queue the ne-side links before the graph forgets them */
                for (int i = e->link_count - 1; i >= 0; i--) {
                    if (e->links[i].from == n || e->links[i].to == n) {
                        ne_delete_link(e->links[i].id);
                        ed_entry_drop(e, &e->links[i]);
                    }
                }
                graph_remove_node(e->g, n);
                ne_delete_node(e->menu_node);
                map_del(e->seen, e->menu_node);
            }
        }
        ImGui_EndPopup();
    }

    if (ImGui_BeginPopup("link_menu", 0)) {
        if (ImGui_MenuItem("Delete Link")) {
            ed_link *l = ed_entry_find_by_id(e, e->menu_link);
            if (l) {
                ed_snapshot(e);
                graph_remove_edge(e->g, l->from, l->from_pin, l->to, l->to_pin);
                ed_entry_drop(e, l);
                ne_delete_link(e->menu_link);
            }
        }
        ImGui_EndPopup();
    }

    if (ImGui_BeginPopup("pin_menu", 0)) {
        node *n = NULL;
        int kind = 0, pin = 0;
        int decode_ok = ed_pin_decode(e->g, e->menu_pin, &n, &kind, &pin);
        int edge_count = 0;
        if (decode_ok) {
            for (int i = 0; i < e->g->edge_count; i++) {
                const edge *ed = &e->g->edges[i];
                int hit = kind == ne_pin_kind_output
                            ? (ed->from == n && ed->from_pin == pin)
                            : (ed->to == n && ed->to_pin == pin);
                if (hit) edge_count++;
            }
        }
        if (ImGui_MenuItem("Break Links")) {
            if (decode_ok && edge_count > 0) {
                ed_snapshot(e);
                ne_break_links_pin(e->menu_pin); /* queues the ne-side links */
                for (int i = e->g->edge_count - 1; i >= 0; i--) {
                    const edge *ed = &e->g->edges[i];
                    int hit = kind == ne_pin_kind_output
                                ? (ed->from == n && ed->from_pin == pin)
                                : (ed->to == n && ed->to_pin == pin);
                    if (hit)
                        graph_remove_edge(e->g, ed->from, ed->from_pin, ed->to, ed->to_pin);
                }
                for (int i = e->link_count - 1; i >= 0; i--) {
                    int hit = kind == ne_pin_kind_output
                                ? (e->links[i].from == n && e->links[i].from_pin == pin)
                                : (e->links[i].to == n && e->links[i].to_pin == pin);
                    if (hit) ed_entry_drop(e, &e->links[i]);
                }
            }
        }
        ImGui_EndPopup();
    }
}

/* --- clipboard (graph subgraph JSON) --- */

static void ed_copy_selected(editor *e)
{
    ne_node_id ids[64];
    int count = ne_get_action_context_nodes(ids, 64);
    if (count <= 0) return;
    node *subset[64];
    for (int i = 0; i < count && i < 64; i++)
        subset[i] = ed_node_by_id(e->g, ids[i]);
    char *json = graph_save_nodes_json(e->g, subset, count);
    if (!json) return;
    free(e->clipboard);
    e->clipboard = json;
}

static void ed_cut_selected(editor *e)
{
    ed_copy_selected(e);

    ne_link_id link_ids[64];
    int lc = ne_get_action_context_links(link_ids, 64);
    ne_node_id node_ids[64];
    int nc = ne_get_action_context_nodes(node_ids, 64);
    if (lc <= 0 && nc <= 0) return;

    ed_snapshot(e);

    for (int i = 0; i < lc && i < 64; i++) {
        ed_link *l = ed_entry_find_by_id(e, link_ids[i]);
        if (l) {
            graph_remove_edge(e->g, l->from, l->from_pin, l->to, l->to_pin);
            ed_entry_drop(e, l);
        }
        ne_delete_link(link_ids[i]);
    }
    for (int i = 0; i < nc && i < 64; i++) {
        node *n = ed_node_by_id(e->g, node_ids[i]);
        if (!n) {
            ne_delete_node(node_ids[i]);
            continue;
        }
        for (int j = e->link_count - 1; j >= 0; j--) {
            if (e->links[j].from == n || e->links[j].to == n) {
                ne_delete_link(e->links[j].id);
                ed_entry_drop(e, &e->links[j]);
            }
        }
        graph_remove_node(e->g, n);
        ne_delete_node(node_ids[i]);
        map_del(e->seen, node_ids[i]);
    }
}

static void ed_paste(editor *e)
{
    if (!e->clipboard) return;
    ed_snapshot(e);
    graph_append_json(e->g, e->clipboard, 24.0f, 24.0f);
}

static void ed_duplicate_selected(editor *e)
{
    ne_node_id ids[64];
    int count = ne_get_action_context_nodes(ids, 64);
    if (count <= 0) return;
    node *subset[64];
    for (int i = 0; i < count && i < 64; i++)
        subset[i] = ed_node_by_id(e->g, ids[i]);
    char *json = graph_save_nodes_json(e->g, subset, count);
    if (!json) return;
    ed_snapshot(e);
    graph_append_json(e->g, json, 24.0f, 24.0f);
    free(json);
}

static void ed_handle_shortcuts(editor *e)
{
    /* accept_* asserts m_in_action, so only call them while the shortcut
     * action is active */
    if (ne_begin_shortcut()) {
        if (ne_accept_copy()) ed_copy_selected(e);
        if (ne_accept_cut()) ed_cut_selected(e);
        if (ne_accept_duplicate()) ed_duplicate_selected(e);
        if (ne_accept_paste()) ed_paste(e);
    }
    ne_end_shortcut();
}

static void ed_draw_minimap(editor *e)
{
    if (e->g->node_count == 0) return;

    ImVec2 cmin = { FLT_MAX, FLT_MAX };
    ImVec2 cmax = { -FLT_MAX, -FLT_MAX };
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        ImVec2 s = ne_get_node_size((ne_node_id)(uintptr_t)n);
        if (s.x == FLT_MAX && s.y == FLT_MAX) continue;
        ImVec2 p = { n->x, n->y };
        if (p.x < cmin.x) cmin.x = p.x;
        if (p.y < cmin.y) cmin.y = p.y;
        if (p.x + s.x > cmax.x) cmax.x = p.x + s.x;
        if (p.y + s.y > cmax.y) cmax.y = p.y + s.y;
    }
    if (cmax.x <= cmin.x || cmax.y <= cmin.y) return;

    ImVec2 content = { cmax.x - cmin.x, cmax.y - cmin.y };
    const float mm_w = 200.0f, mm_h = 140.0f, pad = 8.0f;
    float scale = mm_w / content.x < mm_h / content.y ? mm_w / content.x
                                                      : mm_h / content.y;
    ImVec2 mm_min = { e->view_pos.x + e->view_size.x - mm_w - pad, e->view_pos.y + pad };

    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_AddRectFilled(
        dl,
        mm_min,
        (ImVec2){ mm_min.x + mm_w, mm_min.y + mm_h },
        IM_COL32(20, 22, 28, 200)
    );
    ImDrawList_AddRect(
        dl,
        mm_min,
        (ImVec2){ mm_min.x + mm_w, mm_min.y + mm_h },
        IM_COL32(90, 92, 100, 255)
    );

    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        ImVec2 s = ne_get_node_size((ne_node_id)(uintptr_t)n);
        if (s.x == FLT_MAX && s.y == FLT_MAX) continue;
        ImVec2 a = { mm_min.x + (n->x - cmin.x) * scale,
                     mm_min.y + (n->y - cmin.y) * scale };
        ImVec2 b = { a.x + s.x * scale, a.y + s.y * scale };
        if (b.x - a.x < 3.0f) b.x = a.x + 3.0f;
        if (b.y - a.y < 3.0f) b.y = a.y + 3.0f;
        ImDrawList_AddRectFilled(dl, a, b, IM_COL32(70, 90, 140, 220));
    }

    /* current view rect: map the screen corners back to canvas space */
    ImVec2 v0 = ne_screen_to_canvas(e->view_pos);
    ImVec2 v1 = ne_screen_to_canvas((ImVec2){ e->view_pos.x + e->view_size.x,
                                              e->view_pos.y + e->view_size.y });
    ImVec2 va = { mm_min.x + (v0.x - cmin.x) * scale,
                  mm_min.y + (v0.y - cmin.y) * scale };
    ImVec2 vb = { mm_min.x + (v1.x - cmin.x) * scale,
                  mm_min.y + (v1.y - cmin.y) * scale };
    ImDrawList_AddRect(dl, va, vb, IM_COL32(120, 180, 255, 255));
}

static void ed_handle_keys(editor *e)
{
    ImGuiIO *io = ImGui_GetIO();
    if (!ImGui_IsWindowFocused(0) || !(io->KeyCtrl || io->KeySuper)) return;
    if (ImGui_IsKeyPressed(ImGuiKey_S)) editor_save(e, "graph.json");
    if (ImGui_IsKeyPressed(ImGuiKey_O)) editor_load(e, "graph.json");
    if (ImGui_IsKeyPressed(ImGuiKey_Z)) io->KeyShift ? ed_redo(e) : ed_undo(e);
    if (ImGui_IsKeyPressed(ImGuiKey_Y)) ed_redo(e);
}

/* --- lifecycle --- */

editor *editor_new(graph *g)
{
    editor *e = (editor *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->g = g;
    e->ev = eval_view_new();

    ne_config cfg;
    ne_config_default(&cfg);
    cfg.settings_file = NULL;      /* our persistence is the graph JSON */
    cfg.navigate_button_index = 2; /* middle-drag pans; right-click is menus */
    e->ctx = ne_create_editor(&cfg);
    if (!e->ctx) {
        eval_view_free(e->ev);
        free(e);
        return NULL;
    }
    return e;
}

void editor_free(editor *e)
{
    if (!e) return;
    ed_hist_clear(e->undo, &e->undo_count);
    ed_hist_clear(e->redo, &e->redo_count);
    free(e->undo);
    free(e->redo);
    ne_destroy_editor(e->ctx);
    for (int i = 0; i < e->grave_count; i++) {
        graph_free(&e->graves[i].g);
        free(e->graves[i].node_ids);
        free(e->graves[i].link_ids);
    }
    free(e->graves);
    map_free(e->seen);
    free(e->links);
    free(e->clipboard);
    eval_view_free(e->ev);
    free(e);
}

node *editor_spawn(editor *e, const node_desc *desc, float x, float y)
{
    ed_snapshot(e);
    node *n = graph_add_node(e->g, desc);
    if (!n) return NULL;
    n->x = x;
    n->y = y;
    return n; /* the next frame applies the position via the seen set */
}

int editor_save(const editor *e, const char *path)
{
    ed_sync_positions(e);
    char *json = graph_save_json(e->g);
    if (!json) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(json);
        return -1;
    }
    int ok = fputs(json, f) >= 0;
    fclose(f);
    free(json);
    return ok ? 0 : -1;
}

static int ed_read_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return -1;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = '\0';
    *out = buf;
    return 0;
}

int editor_load(editor *e, const char *path)
{
    char *buf = NULL;
    if (ed_read_file(path, &buf)) return -1;
    int rc = ed_adopt(e, buf); /* clears the seen set, positions re-apply */
    free(buf);
    return rc;
}

/* --- frame --- */

void editor_draw(editor *e)
{
    eval_view_refresh(e->ev, e->g);

    ImGui_SetNextWindowSize((ImVec2){ 960.0f, 640.0f }, ImGuiCond_FirstUseEver);
    ImGui_Begin("Node Canvas", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    e->view_pos = ImGui_GetCursorScreenPos();
    ImVec2 avail = ImGui_GetContentRegionAvail();
    e->view_size = avail;

    ne_set_current_editor(e->ctx);
    e->in_frame = 1;
    ne_begin("cblueprints", avail);

    /* Apply graph positions to nodes the editor has not seen yet (first
     * frame, spawn, undo/redo, load). */
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        ne_node_id id = (ne_node_id)(uintptr_t)n;
        if (map_get(e->seen, id) != 0) continue;
        ne_set_node_position(id, (ImVec2){ n->x, n->y });
        map_add(e->seen, id, 1);
    }

    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        const node_desc *d = n->desc;
        ne_begin_node((ne_node_id)(uintptr_t)n);
        ImGui_TextColored(ED_TITLE_COLOR, "%s", d->name);
        for (int p = 0; p < d->input_count; p++) {
            ImVec4 col = ed_pin_color(d->inputs[p].type);
            ImVec4 bcol = ed_dim(col, 0.6f);
            ne_push_style_color(ne_style_color_pin_rect, &col);
            ne_push_style_color(ne_style_color_pin_rect_border, &bcol);
            ne_begin_pin(ed_pin_id(n, ne_pin_kind_input, p), ne_pin_kind_input);
            ImGui_Text("%s", d->inputs[p].name);
            const char *val = eval_view_input(e->g, n, p);
            if (val) {
                ImGui_SameLineEx(0.0f, 4.0f);
                ImGui_TextColored(ED_VAL_COLOR, "%s", val);
            }
            ne_end_pin();
            ne_pop_style_color(2);
        }
        for (int p = 0; p < d->output_count; p++) {
            ImVec4 col = ed_pin_color(d->outputs[p].type);
            ImVec4 bcol = ed_dim(col, 0.6f);
            ne_push_style_color(ne_style_color_pin_rect, &col);
            ne_push_style_color(ne_style_color_pin_rect_border, &bcol);
            ne_begin_pin(ed_pin_id(n, ne_pin_kind_output, p), ne_pin_kind_output);
            ImGui_Text("%s", d->outputs[p].name);
            const char *val = eval_view_output(n, p);
            if (val && val[0]) {
                ImGui_SameLineEx(0.0f, 4.0f);
                ImGui_TextColored(ED_VAL_COLOR, "%s", val);
            }
            ne_end_pin();
            ne_pop_style_color(2);
        }
        ne_end_node();
    }

    ed_draw_links(e);
    ed_handle_create(e);
    ed_handle_delete(e);
    ed_handle_spawn_menu(e);
    ed_handle_context_menus(e);
    ed_handle_shortcuts(e);
    ed_handle_keys(e);

    if (e->frames_drawn == 0)
        ne_navigate_to_content(0.0f); /* first frame: bring seeded content into view */

    ne_end();
    e->in_frame = 0;
    e->frames_drawn++;

    ed_draw_minimap(e); /* after ne_end: the canvas splitter is swapped back */
    ImGui_End();
}
