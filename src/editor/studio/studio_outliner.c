/* The Outliner, authored (projects/studio Outliner.rml and OutlinerHeader.rml,
 * docs/studio-rml-migration.md W2 P2): the scene's objects as rows, with
 * rename in place, Hide in Editor, multi-selection, drops and a row menu.
 * C keeps the selection, the filter, rename and the drops; the `outliner`
 * panel source publishes each area's window of rows (ui_list_window) and
 * takes the panel's intents. Blender's Outliner and Unity's Hierarchy are
 * the references: their click, Mod and Shift selection, F2 rename and
 * restriction toggles, and Unity's focusable list for the keyboard. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "ui/drag.h"
#include "ui/list.h"
#include "ui/popover.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The List of each area's Outliner panel, in its host `outliner-view-<area>`. */
#define LIST_SELECTOR "#outliner-list"

/* ---------- Header ---------- */

void studio_outliner_header(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Hierarchy. */
    if (studio_designer_active(s)) {
        studio_designer_hierarchy_header(s, area);
        return;
    }
    rml_element *host = studio_ui_area_controls(s, area, "outliner", "OutlinerHeader");
    rml_element *field = host && s->find_request == STUDIO_EDITOR_OUTLINER + 1
                           ? rml_element_query_selector(host, "#outliner-filter")
                           : NULL;
    if (field && rml_element_focus(field, true)) {
        rml_element_select(field);
        s->find_request = 0;
    }
}

/* ---------- Rename ---------- */

/* The object authoring `entity`, or -1. */
static int actor_index(const studio *s, uint64_t entity)
{
    for (int i = 0; entity && i < acount(s->scene->actors); i++) {
        if (ecs_entity_to_u64(s->scene->actors[i].entity) == entity) {
            return i;
        }
    }
    return -1;
}

static bool editor_shown(const studio *s, studio_editor editor)
{
    if (s->maximized >= 0) {
        return s->prefs.layouts[s->prefs.workspace].editors[s->maximized] == editor;
    }
    return studio_editor_shown(s, editor);
}

void studio_rename_object(studio *s)
{
    scene_actor *actor = selected_actor(s->scene);
    bool outliner = editor_shown(s, STUDIO_EDITOR_OUTLINER);
    s->rename.entity = ecs_entity_to_u64(actor->entity);
    s->rename.outliner = outliner;
    s->rename.focus = true;
    snprintf(s->rename.name, sizeof s->rename.name, "%s", actor->name);
    if (!outliner) {
        s->tab = STUDIO_TAB_OBJECT;
        studio_show_editor(s, STUDIO_EDITOR_PROPERTIES);
        return;
    }
    /* Its row shows, scrolled into view. */
    s->outliner_collapsed = false;
    s->outliner_reveal = true;
    if (!studio_matches(s->outliner_filter, actor->name) &&
        !studio_matches(s->outliner_filter, actor->prototype_name)) {
        s->outliner_filter[0] = '\0';
    }
}

void studio_rename_commit(studio *s, uint64_t entity, const char *name)
{
    int index = actor_index(s, entity);
    scene_actor *actor = index >= 0 ? &s->scene->actors[index] : NULL;
    if (!actor || !studio_editable(s) || !strcmp(actor->name, name)) {
        return;
    }
    char report[544];
    snprintf(report, sizeof report, "Renamed %s to %s", actor->name, name);
    finish_transform_edit(s->scene, false);
    if (app_scene_authoring_rename_instance(s->scene, actor->entity, name) != 0) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't rename",
                               .description = s->scene->message }
        );
        return;
    }
    studio_report(s, report);
}

/* The row's field takes the keyboard once it is built, a frame after its
   row starts renaming; Enter or leaving it renames (Blender's F2). Whether
   the keyboard held the list decides how it gets it back. */
static void focus_rename(studio *s, int area, rml_element *host)
{
    if (!s->rename.focus || !s->rename.outliner) {
        return;
    }
    rml_element *field = rml_element_query_selector(host, ".object-rename input");
    s->outliner.keyboard = studio_ui_keyboard_area(s) == area;
    if (field && rml_element_focus(field, true)) {
        rml_element_select(field);
        s->rename.focus = false;
        return;
    }
    s->ui_schedule.wake = true;
}

/* ---------- Hide in Editor ---------- */

static unsigned actor_view(studio *s, int actor)
{
    return app_scene_authoring_actor_view(s->scene, actor);
}

static void set_hidden(studio *s, int actor, bool hidden)
{
    unsigned view = actor_view(s, actor) & ~APP_SCENE_VIEW_HIDDEN;
    app_scene_authoring_set_actor_view(s->scene, actor, view | (hidden ? APP_SCENE_VIEW_HIDDEN : 0));
}

void studio_hide(studio *s, bool selected)
{
    app_scene_authoring *scene = s->scene;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (scene_instance_selected(scene, i) == selected) {
            set_hidden(s, i, true);
        }
    }
}

void studio_show_all(studio *s)
{
    for (int i = 0; i < acount(s->scene->actors); i++) {
        set_hidden(s, i, false);
    }
}

int studio_hidden_count(studio *s)
{
    int count = 0;
    for (int i = 0; i < acount(s->scene->actors); i++) {
        count += (actor_view(s, i) & APP_SCENE_VIEW_HIDDEN) != 0;
    }
    return count;
}

/* Alt+click on an eye shows its object alone, or every object once it is the
   only one shown, as Blender's Ctrl+click on the eye isolates. */
static void isolate(studio *s, int actor)
{
    bool alone = true;
    for (int i = 0; i < acount(s->scene->actors); i++) {
        alone &= (i == actor) != ((actor_view(s, i) & APP_SCENE_VIEW_HIDDEN) != 0);
    }
    for (int i = 0; i < acount(s->scene->actors); i++) {
        set_hidden(s, i, !alone && i != actor);
    }
}

/* ---------- Multi-selection ---------- */

static int actor_by_identity(const studio *s, uint64_t identity)
{
    for (int i = 0; identity && i < acount(s->scene->actors); i++) {
        if (s->scene->actors[i].identity == identity) {
            return i;
        }
    }
    return -1;
}

/* A range extends from the anchor, which the last plain or Mod+click left:
   while the active object is the one the last range left, the anchor holds,
   else the active object becomes it (Finder's and VS Code's lists). */
void studio_select_range(studio *s, const int *rows, int count, int row)
{
    app_scene_authoring *scene = s->scene;
    scene_actor *active = selected_actor(scene);
    if (!active || active->identity != s->range.active) {
        s->range.anchor = active ? active->identity : 0;
    }
    int anchor = actor_by_identity(s, s->range.anchor), from = row;
    for (int r = 0; r < count; r++) {
        from = rows[r] == anchor ? r : from;
    }
    scene_select_range(scene, rows, from, row);
    s->range.active = scene->actors[rows[row]].identity;
}

/* Actors shown by the Outliner filter, in scene order. The caller frees the array. */
int *studio_outliner_rows(studio *s)
{
    int *rows = NULL;
    for (int i = 0; i < acount(s->scene->actors); i++) {
        const scene_actor *actor = &s->scene->actors[i];
        if (studio_matches(s->outliner_filter, actor->name) ||
            studio_matches(s->outliner_filter, actor->prototype_name)) {
            apush(rows, i);
        }
    }
    return rows;
}

/* Left folds the scene's root and Right unfolds it, as a tree view's
   keys do. */
bool studio_outliner_key(studio *s, SDL_Keycode key, SDL_Keymod mod)
{
    if ((key != SDLK_LEFT && key != SDLK_RIGHT) ||
        (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT | SDL_KMOD_SHIFT))) {
        return false;
    }
    s->outliner_collapsed = key == SDLK_LEFT;
    s->ui_schedule.wake = true;
    return true;
}

/* ---------- The panel's data ---------- */

static const char *const ROW_TEXTS[] = { "id",        "label", "icon", "meta",
                                         "behaviors", "drop",  NULL };
static const char *const ROW_FLAGS[] = { "parent", "open",     "selected",
                                         "active", "marked",   "hidden",
                                         "locked", "renaming", NULL };
static const char *const MENU_ITEM_TEXTS[] = { "label", "icon", "shortcut", NULL };

static yyjson_mut_val *member(yyjson_mut_val *object, const char *name)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, name);
    assert(value);
    return value;
}

static void add_members(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *const *texts,
    const char *const *flags
)
{
    for (int i = 0; texts && texts[i]; i++) {
        yyjson_mut_obj_add_strcpy(doc, object, texts[i], "");
    }
    for (int i = 0; flags && flags[i]; i++) {
        yyjson_mut_obj_add_bool(doc, object, flags[i], false);
    }
}

/* A TreeRow's item: an object's row, or the scene's root at depth 0. */
static yyjson_mut_val *row_new(yyjson_mut_doc *doc, int depth)
{
    yyjson_mut_val *row = yyjson_mut_obj(doc);
    add_members(doc, row, ROW_TEXTS, ROW_FLAGS);
    yyjson_mut_obj_add_int(doc, row, "depth", depth);
    return row;
}

/* A MenuItem's item. */
static void add_menu_item(yyjson_mut_doc *doc, yyjson_mut_val *menu, const char *name, const char *label)
{
    yyjson_mut_val *item = yyjson_mut_obj_add_obj(doc, menu, name);
    static const char *const FLAGS[] = { "checked", "disabled", NULL };
    add_members(doc, item, MENU_ITEM_TEXTS, FLAGS);
    ui_data_set_text(doc, yyjson_mut_obj_get(item, "label"), label);
}

void studio_outliner_add(yyjson_mut_doc *doc)
{
    yyjson_mut_val *outliner = yyjson_mut_obj_add_obj(doc, yyjson_mut_doc_get_root(doc), "outliner");
    static const char *const TEXTS[] = { "count",  "empty",   "query",
                                         "rename", "isolate", NULL };
    static const char *const FLAGS[] = { "editable", NULL };
    add_members(doc, outliner, TEXTS, FLAGS);
    yyjson_mut_obj_add_val(doc, outliner, "root", row_new(doc, 0));
    yyjson_mut_obj_add_arr(doc, outliner, "prototypes");
    yyjson_mut_val *menu = yyjson_mut_obj_add_obj(doc, outliner, "menu");
    static const char *const MENU_FLAGS[] = { "actor", "live", NULL };
    add_members(doc, menu, (const char *const[]){ "label", NULL }, MENU_FLAGS);
    add_menu_item(doc, menu, "prototype", "Open Prototype");
    add_menu_item(doc, menu, "spawnBreak", "Break When Spawned");
    yyjson_mut_val *views = yyjson_mut_obj_add_arr(doc, outliner, "views");
    for (int area = 0; area < STUDIO_AREA_COUNT; area++) {
        yyjson_mut_val *view = yyjson_mut_arr_add_obj(doc, views);
        yyjson_mut_obj_add_bool(doc, view, "root", false);
        yyjson_mut_obj_add_arr(doc, view, "rows");
    }
}

/* ---------- Values, written in place ---------- */

static bool put_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *name, const char *text)
{
    yyjson_mut_val *value = member(object, name);
    if (!strcmp(yyjson_mut_get_str(value), text)) {
        return false;
    }
    ui_data_set_text(doc, value, text);
    return true;
}

static bool put_bool(yyjson_mut_val *object, const char *name, bool on)
{
    yyjson_mut_val *value = member(object, name);
    if (yyjson_mut_get_bool(value) == on) {
        return false;
    }
    yyjson_mut_set_bool(value, on);
    return true;
}

static bool put_glyph(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *name, uint32_t icon)
{
    char glyph[5] = "";
    if (icon) {
        studio_glyph(glyph, icon);
    }
    return put_text(doc, object, name, glyph);
}

/* Gives `rows` `length` items, adding and removing at its end, so rows
 * already shown keep their elements; true when its length changed. */
static bool fit(yyjson_mut_doc *doc, yyjson_mut_val *rows, int length)
{
    int had = (int)yyjson_mut_arr_size(rows);
    for (int i = had; i < length; i++) {
        yyjson_mut_arr_append(rows, row_new(doc, 1));
    }
    for (int i = had; i > length; i--) {
        yyjson_mut_arr_remove_last(rows);
    }
    return had != length;
}

/* ---------- Header, root and menu ---------- */

/* What the panel wrote since C last showed it: the filter typed in its
   header and the root's twisty. */
static void take_view_state(studio *s, yyjson_mut_val *outliner)
{
    const char *query = yyjson_mut_get_str(member(outliner, "query"));
    if (strcmp(query, s->outliner.query)) {
        snprintf(s->outliner_filter, sizeof s->outliner_filter, "%s", query);
    }
    bool open = yyjson_mut_get_bool(member(member(outliner, "root"), "open"));
    if (open != s->outliner.open) {
        s->outliner_collapsed = !open;
    }
}

/* The Add menu's prototypes, as the scene loaded them. */
static bool show_prototypes(studio *s, yyjson_mut_doc *doc, yyjson_mut_val *list)
{
    int count = 0;
    for (int i = 0; i < acount(s->scene->prototypes); i++) {
        const game_prototype *prototype = s->scene->prototypes[i].prototype;
        count += prototype && prototype->name;
    }
    bool changed = fit(doc, list, count);
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(list);
    for (int i = 0; i < acount(s->scene->prototypes); i++) {
        const game_prototype *prototype = s->scene->prototypes[i].prototype;
        if (!prototype || !prototype->name) {
            continue;
        }
        yyjson_mut_val *option = yyjson_mut_arr_iter_next(&it);
        changed |= put_text(doc, option, "label", prototype->name);
        changed |= put_glyph(
            doc,
            option,
            "icon",
            studio_prototype_icon(s, prototype->name)
        );
    }
    return changed;
}

/* The row menu acts on the selection, named in its heading. */
static bool show_menu(studio *s, yyjson_mut_doc *doc, yyjson_mut_val *menu)
{
    scene_actor *actor = selected_actor(s->scene);
    int count = scene_selection_count(s->scene);
    char group[64];
    snprintf(group, sizeof group, "%d objects", count);
    yyjson_mut_val *prototype = member(menu, "prototype");
    yyjson_mut_val *spawn = member(menu, "spawnBreak");
    bool changed = put_text(
        doc,
        menu,
        "label",
        count > 1 || !actor ? group : actor->name
    );
    changed |= put_bool(menu, "actor", actor != NULL);
    changed |= put_bool(menu, "live", studio_live(s));
    changed |= put_glyph(doc, prototype, "icon", KIT_ICON_PROTOTYPE);
    changed |= put_bool(prototype, "disabled", !actor);
    bool spawning = actor &&
                    app_scene_authoring_spawn_break(s->scene, actor->prototype_name);
    changed |= put_bool(spawn, "checked", spawning);
    return changed;
}

/* The scene's root row, the count of rows the filter shows, and the notice
   an empty list gives. */
static bool show_header(studio *s, yyjson_mut_doc *doc, yyjson_mut_val *outliner, int shown)
{
    const char *filter = s->outliner_filter;
    const char *empty = !shown && filter[0] ? "No matches"
                      : !acount(s->scene->actors) && !filter[0]
                          ? "No objects. Add a prototype to start."
                          : "";
    char count[16], title[128];
    snprintf(count, sizeof count, "%d", shown);
    yyjson_mut_val *root = member(outliner, "root");
    bool changed = put_text(doc, outliner, "count", count);
    changed |= put_text(doc, outliner, "empty", empty);
    changed |= put_text(doc, outliner, "query", filter);
    changed |= put_text(doc, outliner, "rename", s->rename.name);
    changed |= put_text(
        doc,
        outliner,
        "isolate",
        s->mac ? "\xe2\x8c\xa5-click shows only this object" : "Alt+click shows only this object"
    );
    changed |= put_bool(outliner, "editable", studio_editable(s));
    changed |= put_text(doc, root, "label", studio_scene_title(s->scene, title, sizeof title));
    changed |= put_glyph(doc, root, "icon", KIT_ICON_SCENE);
    changed |= put_bool(root, "parent", true);
    changed |= put_bool(root, "open", !s->outliner_collapsed);
    snprintf(s->outliner.query, sizeof s->outliner.query, "%s", filter);
    s->outliner.open = !s->outliner_collapsed;
    return changed;
}

/* ---------- Rows ---------- */

/* Object `index` as a row: its name, prototype icon, behavior count, the
   drags it takes, and its selection, view and rename states. */
static bool show_row(studio *s, yyjson_mut_doc *doc, yyjson_mut_val *row, int index)
{
    app_scene_authoring *scene = s->scene;
    const scene_actor *actor = &scene->actors[index];
    uint64_t entity = ecs_entity_to_u64(actor->entity);
    char id[24], behaviors[16] = "", drop[160];
    snprintf(id, sizeof id, "%llu", (unsigned long long)entity);
    char **names = instance_behavior_names(scene, actor->entity);
    if (acount(names)) {
        snprintf(behaviors, sizeof behaviors, "%d", acount(names));
    }
    free_behavior_names(names);
    studio_drop_types(s, index, drop, sizeof drop);
    unsigned view = actor_view(s, index);
    /* Every selected row shows; the active one stands out (Blender). */
    bool selected = scene_instance_selected(scene, index),
         active = scene->selected == index;
    bool changed = put_text(doc, row, "id", id);
    changed |= put_text(doc, row, "label", actor->name);
    changed |= put_glyph(doc, row, "icon", studio_prototype_icon(s, actor->prototype_name));
    changed |= put_text(doc, row, "behaviors", behaviors);
    changed |= put_text(doc, row, "drop", drop);
    changed |= put_bool(row, "selected", selected);
    changed |= put_bool(row, "active", active);
    changed |= put_bool(row, "marked", selected && !active);
    changed |= put_bool(row, "hidden", view & APP_SCENE_VIEW_HIDDEN);
    changed |= put_bool(row, "locked", view & APP_SCENE_VIEW_LOCKED);
    changed |= put_bool(row, "renaming", s->rename.outliner && s->rename.entity == entity);
    return changed;
}

/* The element with `id` in Studio's documents, or NULL. */
static rml_element *by_id(studio *s, const char *id)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int i = 0; i < rml_context_document_count(context); i++) {
        rml_element *found = rml_element_by_id(rml_context_document(context, i), id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static void host_id(char *out, size_t size, int area)
{
    snprintf(out, size, "outliner-view-%d", area);
}

/* The List of area `area`'s Outliner, or NULL while the area shows none. */
static rml_element *area_list(studio *s, int area)
{
    if (studio_active_layout(s)->editors[area] != STUDIO_EDITOR_OUTLINER ||
        studio_designer_active(s)) {
        return NULL;
    }
    char id[32];
    host_id(id, sizeof id, area);
    rml_element *host = by_id(s, id);
    return host ? rml_element_query_selector(host, LIST_SELECTOR) : NULL;
}

/* Area `area`'s window of the root and the rows `rows` lists, scrolled to
   the active row when it is to be revealed; true when it shows one. */
static bool show_view(
    studio *s,
    yyjson_mut_doc *doc,
    int area,
    yyjson_mut_val *view,
    const int *rows,
    bool *changed
)
{
    rml_element *list = area_list(s, area);
    int count = acount(rows), first = 0, length = 0;
    if (list) {
        int reveal = -1;
        for (int r = 0; s->outliner_reveal && r < count; r++) {
            reveal = rows[r] == s->scene->selected ? r + 1 : reveal;
        }
        ui_list window = { .count = 1 + (s->outliner_collapsed ? 0 : count),
                           .reveal = reveal };
        length = ui_list_window(list, &window, &s->outliner.lists[area], &first);
    }
    /* The root is the list's first row; the rows follow it. */
    bool root = length > 0 && first == 0;
    int start = first > 0 ? first - 1 : 0;
    yyjson_mut_val *shown = member(view, "rows");
    *changed |= put_bool(view, "root", root);
    *changed |= fit(doc, shown, length - root);
    yyjson_mut_val *row;
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(shown);
    for (int i = start; (row = yyjson_mut_arr_iter_next(&it)); i++) {
        *changed |= show_row(s, doc, row, rows[i]);
    }
    return list != NULL;
}

bool studio_outliner_sync(studio *s, const studio_panel_sync *at)
{
    if (at->preview) {
        return false;
    }
    yyjson_mut_val *outliner = at->variable;
    take_view_state(s, outliner);
    int *rows = studio_outliner_rows(s);
    bool changed = show_header(s, at->doc, outliner, acount(rows));
    changed |= show_prototypes(s, at->doc, member(outliner, "prototypes"));
    changed |= show_menu(s, at->doc, member(outliner, "menu"));
    yyjson_mut_val *views = member(outliner, "views");
    bool shown = false;
    for (int area = 0; area < STUDIO_AREA_COUNT; area++) {
        yyjson_mut_val *view = yyjson_mut_arr_get(views, (size_t)area);
        shown |= show_view(s, at->doc, area, view, rows, &changed);
    }
    afree(rows);
    s->outliner_reveal &= !shown;
    return changed;
}

/* ---------- Intents ---------- */

/* The object a row's id, its entity, names; -1 when it is gone. */
static int actor_named(const studio *s, const char *id)
{
    return actor_index(s, strtoull(id, NULL, 10));
}

/* A row's click: alone, Mod toggles it in the selection and Shift selects
   the rows from the anchor through it (Unity's and Godot's lists). It comes
   with the release, and never after a press that became a drag. */
static void click(studio *s, int actor, unsigned modifiers)
{
    app_scene_authoring *scene = s->scene;
    int *rows = studio_outliner_rows(s), row = -1;
    for (int r = 0; r < acount(rows); r++) {
        row = rows[r] == actor ? r : row;
    }
    if (row < 0) {
        afree(rows);
        return;
    }
    finish_transform_edit(scene, false);
    if (modifiers & RML_KM_SHIFT) {
        studio_select_range(s, rows, acount(rows), row);
    } else if (modifiers & (s->mac ? RML_KM_META : RML_KM_CTRL)) {
        scene_toggle_instance(scene, actor);
        s->range.active = 0;
    } else {
        select_instance(scene, actor);
        s->range.active = 0;
    }
    afree(rows);
}

/* A right click keeps a selection it lands in, as Unity's does, and opens
   the row menu at the pointer. */
static void open_menu(studio *s, int actor, const ui_activation *a)
{
    if (!scene_instance_selected(s->scene, actor)) {
        finish_transform_edit(s->scene, false);
        select_instance(s->scene, actor);
    }
    rml_element *row = a->anchor ? rml_element_ref_get(a->anchor) : NULL;
    rml_element *menu = row
                          ? rml_element_query_selector(rml_element_owner_document(row), "#outliner-menu")
                          : NULL;
    if (menu) {
        popover_show(menu, &(popover_anchor){ .side = POPOVER_POINT, .box = { a->x, a->y, 0, 0 } });
    }
}

static void toggle_view(studio *s, int actor, const ui_activation *a, bool lock)
{
    unsigned view = actor_view(s, actor);
    if (lock) {
        app_scene_authoring_set_actor_view(s->scene, actor, view ^ APP_SCENE_VIEW_LOCKED);
    } else if (a->modifiers & RML_KM_ALT) {
        isolate(s, actor);
    } else {
        set_hidden(s, actor, !(view & APP_SCENE_VIEW_HIDDEN));
    }
}

/* A drop on the list places a prototype; one on a row attaches a behavior
   or assigns an asset its fields name. A row's drop bubbles to the list,
   which takes only prototypes. */
static void drop(studio *s, const ui_activation *a)
{
    const char *type = a->args[1], *name = a->args[2];
    if (!a->args[0][0]) {
        if (!strcmp(type, "prototype")) {
            studio_add_object(s, name);
        }
        return;
    }
    int actor = actor_named(s, a->args[0]);
    if (actor >= 0 && studio_editable(s)) {
        studio_drop_on(s, actor, type, name);
    }
}

/* Ends the rename in place; with `name`, renames as one Undo step. */
static void end_rename(studio *s, const char *name)
{
    uint64_t entity = s->rename.outliner ? s->rename.entity : 0;
    if (!entity) {
        return;
    }
    s->rename.entity = 0;
    if (name) {
        studio_rename_commit(s, entity, name);
    }
}

/* Enter and Escape give the keyboard back to the list, which ends the
   rename: Enter's commits what the field holds as it loses focus; Escape
   ends it first, so nothing commits. */
static void leave_rename(studio *s, bool cancel)
{
    if (cancel) {
        end_rename(s, NULL);
    }
    rml_element *field = rml_context_focus_element(rml_backend_context(s->backend));
    rml_element *list = field
                          ? rml_element_query_selector(rml_element_owner_document(field), LIST_SELECTOR)
                          : NULL;
    if (!list || !rml_element_focus(list, s->outliner.keyboard)) {
        end_rename(s, NULL);
    }
}

/* What the selection's menu does besides commands. */
static void menu_action(studio *s, const char *action)
{
    scene_actor *actor = selected_actor(s->scene);
    if (!actor) {
        return;
    }
    if (!strcmp(action, "openPrototype")) {
        app_scene_authoring_open_prototype(s->scene, actor->prototype_name);
        return;
    }
    studio_toggle_spawn_break(s, actor->prototype_name);
}

/* A row's intents name it by id first. */
static void row_action(studio *s, const char *action, const ui_activation *a)
{
    int actor = actor_named(s, a->args[0]);
    if (actor < 0) {
        return;
    }
    if (!strcmp(action, "click")) {
        click(s, actor, a->modifiers);
    } else if (!strcmp(action, "menu")) {
        open_menu(s, actor, a);
    } else {
        toggle_view(s, actor, a, !strcmp(action, "lock"));
    }
}

void studio_outliner_action(studio *s, const char *action, const ui_activation *a)
{
    /* A scroll only wakes the source, which reads where each List is. */
    if (!strcmp(action, "click") || !strcmp(action, "menu") ||
        !strcmp(action, "eye") || !strcmp(action, "lock")) {
        row_action(s, action, a);
    } else if (!strcmp(action, "drop") && a->argc == 3) {
        drop(s, a);
    } else if (!strcmp(action, "rename") && a->argc == 1) {
        end_rename(s, a->args[0]);
    } else if (!strcmp(action, "renameEnter") || !strcmp(action, "renameCancel")) {
        leave_rename(s, !strcmp(action, "renameCancel"));
    } else if (!strcmp(action, "collapse")) {
        s->outliner_collapsed = !s->outliner_collapsed;
    } else if (!strcmp(action, "add") && a->argc == 1) {
        studio_add_object(s, a->args[0]);
    } else if (!strcmp(action, "openPrototype") || !strcmp(action, "spawnBreak")) {
        menu_action(s, action);
    }
}

/* ---------- Body ---------- */

/* Leaving the field ends the rename, after whatever it committed as it
   lost focus, which the panel's intents brought first. */
static void left_rename(studio *s, rml_element *host)
{
    if (!s->rename.entity || !s->rename.outliner || s->rename.focus ||
        !rml_element_query_selector(host, ".object-rename input")) {
        return;
    }
    rml_element *focus = rml_context_focus_element(rml_backend_context(s->backend));
    rml_element *field = focus ? rml_element_parent(focus) : NULL;
    rml_element *row = field ? rml_element_parent(field) : NULL;
    if (!row || !rml_element_is_class_set(row, "object-rename")) {
        end_rename(s, NULL);
    }
}

/* While an asset is dragged over a row, the object shows it in the Scene
   and the overlay says what a drop does. */
static void preview_drop(studio *s, rml_element *host)
{
    rml_element *over = rml_element_query_selector(host, "[data-drop-over][data-id]");
    ui_drag_item item;
    if (!over || !ui_drag_over(over, &item)) {
        return;
    }
    char id[24] = "";
    rml_element_attribute(over, "data-id", id, sizeof id);
    int actor = actor_named(s, id);
    if (actor >= 0) {
        studio_drop_preview(s, actor, item.type, item.payload);
    }
}

void studio_outliner_body(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Hierarchy. */
    if (studio_designer_active(s)) {
        studio_designer_hierarchy(s, area);
        return;
    }
    rml_element *host = studio_ui_area_view(s, area, "Outliner", "outliner.views");
    if (!host) {
        return;
    }
    char id[32];
    host_id(id, sizeof id, area);
    kit_attr(s->kit, "id", id);
    preview_drop(s, host);
    left_rename(s, host);
    focus_rename(s, area, host);
}
