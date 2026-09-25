/* Graph navigation in the Behavior header (Unreal): Back and Forward through
 * the graphs visited, across behaviors, with Alt+Left and Alt+Right or mouse
 * buttons 4 and 5; and a breadcrumb of the behavior and the calls entered to
 * reach the open graph, each of which returns to its graph. */
#include "editor/studio/studio_graph_internal.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

enum
{
    VISIT_LIMIT = 64,
};

static studio_visit here(const studio *s)
{
    const app_scene_authoring *scene = s->scene;
    studio_visit visit = { .kind = scene->graph_document_kind,
                           .graph_id = sg_active_graph(s)->graph_id };
    snprintf(visit.document, sizeof visit.document, "%s", scene->graph_document_id);
    if (visit.kind == APP_EDITOR_DOCUMENT_SYSTEM && scene->system_path) {
        snprintf(visit.path, sizeof visit.path, "%s", scene->system_path);
    }
    return visit;
}

static bool same_visit(const studio_visit *a, const studio_visit *b)
{
    return a->kind == b->kind && a->graph_id == b->graph_id &&
           !strcmp(a->document, b->document);
}

/* Records the open graph when it differs from the current visit, dropping
   the visits ahead of it (browser history). */
static void track(studio *s)
{
    studio_graph *g = &s->graph;
    studio_visit visit = here(s);
    if (acount(g->visits) && same_visit(&g->visits[g->visit], &visit)) {
        return;
    }
    asetlen(g->visits, acount(g->visits) ? g->visit + 1 : 0);
    apush(g->visits, visit);
    if (acount(g->visits) > VISIT_LIMIT) {
        adel(g->visits, 0);
    }
    g->visit = acount(g->visits) - 1;
}

static bool open_visit(studio *s, const studio_visit *visit)
{
    app_scene_authoring *scene = s->scene;
    bool open = scene->graph_document_id &&
                scene->graph_document_kind == (app_editor_document_kind)visit->kind &&
                !strcmp(scene->graph_document_id, visit->document);
    if (!open) {
        finish_transform_edit(scene, false);
        int opened = visit->kind == APP_EDITOR_DOCUMENT_SYSTEM
                       ? app_scene_authoring_open_system(scene, visit->path)
                       : app_scene_authoring_open_behavior(scene, visit->document);
        if (opened != 0) {
            return false;
        }
    }
    return studio_graph_active(s) &&
           editor_open_graph(sg_graph_editor(s), visit->graph_id) == 0;
}

static bool can_step(const studio *s, int direction)
{
    int next = s->graph.visit + direction;
    return next >= 0 && next < acount(s->graph.visits);
}

/* Visits that can no longer open, such as a deleted function, drop out. */
static void step(studio *s, int direction)
{
    studio_graph *g = &s->graph;
    track(s);
    int at = g->visit + direction;
    while (at >= 0 && at < acount(g->visits)) {
        if (open_visit(s, &g->visits[at])) {
            g->visit = at;
            return;
        }
        adel(g->visits, at);
        if (at < g->visit) {
            g->visit--;
        }
        at += direction < 0 ? -1 : 0;
    }
}

/* Alt+Left and Alt+Right, or mouse buttons 4 and 5, over the Behavior area. */
void sg_nav_keys(studio *s)
{
    studio_graph *g = &s->graph;
    bool over = s->hover_area == g->area ||
                (s->hover_area < 0 && s->focus_area == g->area);
    if (!over || kit_wants_keyboard(s->kit) || kit_wants_keyboard(g->kit)) {
        s->nav_button = 0;
        return;
    }
    if (s->nav_button) {
        step(s, s->nav_button);
        s->nav_button = 0;
    }
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        if ((s->keys[i].mod & SDL_KMOD_ALT) && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
            step(s, key == SDLK_RIGHT ? 1 : -1);
        }
    }
}

static void history_buttons(studio *s)
{
    kit *k = s->kit;
    kit_button_desc back = { .icon = KIT_ICON_BACK,
                             .tip = "Back",
                             .shortcut = studio_chord(s, "Alt+\xe2\x86\x90"),
                             .flags = can_step(s, -1) ? 0 : KIT_DISABLED };
    if (kit_button(k, "back", &back)) {
        step(s, -1);
    }
    kit_button_desc forward = { .icon = KIT_ICON_FORWARD,
                                .tip = "Forward",
                                .shortcut = studio_chord(s, "Alt+\xe2\x86\x92"),
                                .flags = can_step(s, 1) ? 0 : KIT_DISABLED };
    if (kit_button(k, "forward", &forward)) {
        step(s, 1);
    }
}

static const char *graph_label(const graph *gr)
{
    return gr->name && gr->name[0] ? gr->name : "Event Graph";
}

/* The behavior's crumb finds its references and switches to another asset
   of its kind. */
static void asset_crumb(studio *s)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    const asset_index *index = app_scene_authoring_asset_index(scene);
    asset_index_kind kind = scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM
                              ? ASSET_INDEX_SYSTEM
                              : asset_index_domain_kind(
                                    sg_graph_editor(s)->asset->domain
                                );
    kit_button_desc crumb = { .label = scene->graph_document_id,
                              .icon = studio_asset_kind_icon(kind),
                              .cls = "crumb" };
    if (kit_button(k, "asset", &crumb)) {
        kit_popup_open(k, "crumb-asset");
    }
    if (!kit_popup_begin(k, "crumb-asset", "menu")) {
        return;
    }
    kit_menu_item_desc refs = { .label = "Find References",
                                .icon = KIT_ICON_REFERENCES,
                                .checked = -1 };
    if (kit_menu_item(k, "refs", &refs)) {
        studio_find_document_references(s);
    }
    /* The other assets of its kind, to switch to (the prototype's All
       Behaviors). */
    if (kind != ASSET_INDEX_SYSTEM &&
        kit_submenu_begin(k, "all", studio_asset_kind_label(kind), studio_asset_kind_icon(kind))) {
        for (int i = 0; index && i < asset_index_asset_count(index); i++) {
            const asset_index_asset *asset = asset_index_asset_at(index, i);
            if (asset->kind != kind || asset->unresolved) {
                continue;
            }
            char key[16];
            snprintf(key, sizeof key, "a%d", i);
            kit_menu_item_desc d = {
                .label = asset->name,
                .icon = studio_asset_kind_icon(kind),
                .checked = -1,
                .current = !strcmp(asset->name, scene->graph_document_id)
            };
            if (kit_menu_item(k, key, &d) && !d.current) {
                finish_transform_edit(scene, false);
                app_scene_authoring_open_asset(scene, i);
            }
        }
        kit_submenu_end(k);
    }
    kit_popup_end(k);
}

static void separator(kit *k, int index)
{
    char key[16];
    snprintf(key, sizeof key, "sep%d", index);
    kit_icon(k, key, "crumb-sep", KIT_ICON_RIGHT);
}

/* Back and Forward, then the behavior and the calls entered to reach this
   graph; the open graph's own crumb follows (studio_behavior_header). */
void sg_nav_header(studio *s)
{
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    track(s);
    history_buttons(s);
    kit_push(k, "crumbs", NULL, "crumbs");
    asset_crumb(s);
    separator(k, 0);
    for (int i = 0; i < editor_graph_history_count(e); i++) {
        const graph *caller = behavior_asset_find_graph(e->asset, editor_graph_history_id(e, i));
        if (!caller) {
            continue;
        }
        char key[16];
        snprintf(key, sizeof key, "caller%d", i);
        kit_button_desc crumb = { .label = graph_label(caller), .cls = "crumb" };
        if (kit_button(k, key, &crumb)) {
            editor_graph_ancestor(e, i);
        }
        separator(k, i + 1);
    }
}
