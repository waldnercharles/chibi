/* Graph navigation in the Behavior header (Unreal): Back and Forward through
 * the graphs visited and their breadcrumbs, across behaviors, with Alt+Left
 * and Alt+Right or mouse buttons 4 and 5; and a breadcrumb of the behavior
 * and the calls entered to reach the open graph, each of which returns to its
 * graph. The Text Editor's Go to Definition joins the same history, as VS
 * Code's Go Back crosses editors: it records the place it leaves and the
 * one it lands on (workstream Q). */
#include "editor/studio/studio_graph_internal.h"
#include "support/array.h"

#include <assert.h>
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
    if (a->kind == APP_EDITOR_DOCUMENT_UI || b->kind == APP_EDITOR_DOCUMENT_UI) {
        return a->kind == b->kind && !strcmp(a->place.path, b->place.path) &&
               a->place.part == b->place.part && a->place.offset == b->place.offset;
    }
    return a->kind == b->kind && a->graph_id == b->graph_id &&
           !strcmp(a->document, b->document);
}

/* Makes `visit` the current one unless it is, dropping the visits ahead of
   it (browser history). */
static void push(studio_graph *g, const studio_visit *visit)
{
    if (acount(g->visits) && same_visit(&g->visits[g->visit], visit)) {
        return;
    }
    asetlen(g->visits, acount(g->visits) ? g->visit + 1 : 0);
    apush(g->visits, *visit);
    if (acount(g->visits) > VISIT_LIMIT) {
        adel(g->visits, 0);
    }
    g->visit = acount(g->visits) - 1;
}

static void see(studio_graph *g, const studio_visit *visit)
{
    asetlen(g->shown, 0);
    apush(g->shown, *visit);
}

/* Records the open graph and its breadcrumb once it differs from the graph
   last seen, which a Text Editor place does not change. */
static void track(studio *s)
{
    studio_graph *g = &s->graph;
    studio_visit visit = here(s);
    if (acount(g->shown) && same_visit(&g->shown[0], &visit)) {
        return;
    }
    const editor *e = sg_graph_editor(s);
    visit.caller_count = acount(e->graph_callers);
    assert(visit.caller_count <= ED_CALLER_LIMIT);
    if (visit.caller_count) {
        memcpy(
            visit.callers,
            e->graph_callers,
            (size_t)visit.caller_count * sizeof *visit.callers
        );
    }
    see(g, &visit);
    push(g, &visit);
}

static bool open_visit(studio *s, const studio_visit *visit)
{
    if (visit->kind == APP_EDITOR_DOCUMENT_UI) {
        return studio_code_open_place(s, &visit->place);
    }
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
    editor *e = sg_graph_editor(s);
    if (!studio_graph_active(s) || editor_open_graph(e, visit->graph_id) != 0) {
        return false;
    }
    ed_restore_callers(e, visit->callers, visit->caller_count);
    see(&s->graph, visit);
    return true;
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
    if (studio_graph_active(s)) {
        track(s);
    }
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

/* ---------- The Text Editor's places (workstream Q) ---------- */

void studio_nav_jump(studio *s, const ui_code_place *from, const ui_code_place *to)
{
    studio_graph *g = &s->graph;
    if (studio_graph_active(s)) {
        track(s);
    }
    studio_visit visits[2] = { { .kind = APP_EDITOR_DOCUMENT_UI, .place = *from },
                               { .kind = APP_EDITOR_DOCUMENT_UI, .place = *to } };
    push(g, &visits[0]);
    push(g, &visits[1]);
}

bool studio_nav_can_step(const studio *s, int direction)
{
    return can_step(s, direction);
}

void studio_nav_step(studio *s, int direction)
{
    step(s, direction);
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
    const reference_index *index = app_scene_authoring_asset_index(scene);
    reference_asset_kind kind = scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM
                                  ? REFERENCE_ASSET_SYSTEM
                                  : reference_domain_kind(
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
    if (kind != REFERENCE_ASSET_SYSTEM &&
        kit_submenu_begin(k, "all", studio_asset_kind_label(kind), studio_asset_kind_icon(kind))) {
        for (int i = 0; index && i < reference_index_asset_count(index); i++) {
            const reference_asset *asset = reference_index_asset_at(index, i);
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
        uint64_t id = e->graph_callers[i].graph_id;
        const graph *caller = behavior_asset_find_graph(e->asset, id);
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
