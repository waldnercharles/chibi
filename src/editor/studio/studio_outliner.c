/* The Outliner: the scene's objects as rows, with rename in place, Hide in
 * Editor and multi-selection. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Outliner ---------- */

void studio_outliner_header(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Hierarchy. */
    if (studio_designer_active(s)) {
        studio_designer_hierarchy_header(s, area);
        return;
    }
    kit *k = s->kit;
    kit_search(k, "filter", s->outliner_filter, sizeof s->outliner_filter, "Filter");
    studio_find_field(s, STUDIO_EDITOR_OUTLINER);
    char key[24];
    snprintf(key, sizeof key, "add:%d", area);
    kit_button_desc add = { .icon = KIT_ICON_ADD,
                            .tip = "Add object",
                            .flags = studio_editable(s) ? 0 : KIT_DISABLED };
    /* Prototypes are many, so Add searches them, as Unity's Add Component and
       Blender's Shift+A search do. */
    if (kit_button(k, "add", &add)) {
        kit_dropdown_open(k, key, "Add object");
    }
    if (!kit_dropdown_is_open(k, key)) {
        return;
    }
    kit_command_item *items = NULL;
    for (int i = 0; i < acount(s->scene->prototypes); i++) {
        const game_prototype *prototype = s->scene->prototypes[i].prototype;
        if (prototype && prototype->name) {
            apush(
                items,
                ((kit_command_item){ .label = prototype->name,
                                     .icon = studio_prototype_icon(s, prototype->name) })
            );
        }
    }
    int chosen = kit_dropdown(k, key, items, acount(items));
    if (chosen >= 0) {
        studio_add_object(s, items[chosen].label);
    }
    afree(items);
}

/* ---------- Rename ---------- */

static scene_actor *actor_of(studio *s, uint64_t entity)
{
    for (int i = 0; i < acount(s->scene->actors); i++) {
        if (ecs_entity_to_u64(s->scene->actors[i].entity) == entity) {
            return &s->scene->actors[i];
        }
    }
    return NULL;
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
    scene_actor *actor = actor_of(s, entity);
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

/* The name as a field in the row's place (Blender's F2): Enter or leaving
   the field renames, Escape keeps the name. */
static void rename_row(studio *s, const scene_actor *actor)
{
    kit *k = s->kit;
    char key[32];
    snprintf(key, sizeof key, "rename%llu", (unsigned long long)s->rename.entity);
    kit_push(k, key, NULL, "tree-row object-rename");
    /* Where a row at depth 1 shows its icon, past the twisty's room. */
    kit_style(k, "padding-left", "38dp");
    kit_icon(k, "icon", NULL, studio_prototype_icon(s, actor->prototype_name));
    bool committed = kit_text_field(k, "name", s->rename.name, sizeof s->rename.name, NULL, 0);
    bool focus = s->rename.focus;
    if (focus) {
        kit_focus(k);
        s->rename.focus = false;
    }
    bool left = !focus && !kit_item_active(k);
    kit_pop(k);
    if (!committed && !left) {
        return;
    }
    uint64_t entity = s->rename.entity;
    s->rename.entity = 0;
    if (committed) {
        studio_rename_commit(s, entity, s->rename.name);
    }
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

/* The eye and the lock at a row's end, as Blender's Outliner restriction
   toggles: they show while the row is hovered or while they are on. */
static void view_toggles(studio *s, int actor)
{
    kit *k = s->kit;
    unsigned view = actor_view(s, actor);
    bool hidden = view & APP_SCENE_VIEW_HIDDEN, locked = view & APP_SCENE_VIEW_LOCKED;
    studio_command_info keys = studio_command_describe(s, hidden ? STUDIO_COMMAND_SHOW_ALL : STUDIO_COMMAND_HIDE_SELECTED);
    kit_push(k, "view", NULL, "view-toggles");
    kit_button_desc eye = {
        .icon = hidden ? KIT_ICON_EYE_OFF : KIT_ICON_EYE,
        .cls = "toggle",
        .tip = hidden ? "Show in Editor" : "Hide in Editor",
        .shortcut = keys.shortcut,
        .description = s->mac ? "\xe2\x8c\xa5-click shows only this object"
                              : "Alt+click shows only this object",
        .flags = hidden ? KIT_ON : 0
    };
    if (kit_button(k, "eye", &eye)) {
        if (s->in.modifiers & KIT_MOD_ALT) {
            isolate(s, actor);
        } else {
            set_hidden(s, actor, !hidden);
        }
    }
    kit_button_desc lock = {
        .icon = locked ? KIT_ICON_LOCK : KIT_ICON_UNLOCK,
        .cls = "toggle",
        .tip = locked ? "Unlock" : "Lock",
        .description = "A locked object can't be picked in the Scene",
        .flags = locked ? KIT_ON : 0
    };
    if (kit_button(k, "lock", &lock)) {
        app_scene_authoring_set_actor_view(s->scene, actor, view ^ APP_SCENE_VIEW_LOCKED);
    }
    kit_pop(k);
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

/* A row's click: alone, Mod toggles it in the selection and Shift selects
   the rows from the anchor through it (Unity's and Godot's lists). */
static void click_row(studio *s, const int *rows, int count, int row)
{
    app_scene_authoring *scene = s->scene;
    finish_transform_edit(scene, false);
    if (s->mods & SDL_KMOD_SHIFT) {
        studio_select_range(s, rows, count, row);
        return;
    }
    if (studio_primary(s, s->mods)) {
        scene_toggle_instance(scene, rows[row]);
    } else {
        select_instance(scene, rows[row]);
    }
    s->range.active = 0;
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

void studio_outliner_body(studio *s, int area)
{
    /* Visual designer (workstream H): a UI document's Hierarchy. */
    if (studio_designer_active(s)) {
        studio_designer_hierarchy(s, area);
        return;
    }
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    int *rows = studio_outliner_rows(s);
    int shown = acount(rows);
    int reveal = -1;
    for (int r = 0; r < shown; r++) {
        reveal = rows[r] == scene->selected ? r + 1 : reveal;
    }
    kit_list_desc list = {
        .count = 1 + (s->outliner_collapsed ? 0 : shown),
        .row_height = kit_metric(s->kit, "--row"),
        .reveal = s->outliner_reveal ? reveal : -1,
    };
    int first, last;
    kit_list_begin(k, "tree", "outliner", &list, &first, &last);
    char dropped[256];
    if (kit_drop_target(k, "prototype", dropped, sizeof dropped)) {
        studio_add_object(s, dropped);
    }
    char title[128], count[16];
    snprintf(count, sizeof count, "%d", shown);
    if (first == 0) {
        kit_row_desc root = {
            .label = studio_scene_title(scene, title, sizeof title),
            .icon = KIT_ICON_SCENE,
            .twisty = !s->outliner_collapsed,
        };
        unsigned events = kit_row_begin(k, "root", &root);
        /* The count and the toggles' column line up with the rows'. */
        kit_label(k, "count", "meta", count);
        kit_leaf(k, "view", NULL, "view-toggles");
        kit_row_end(k);
        if (events & (KIT_ROW_TWISTY | KIT_ROW_DOUBLE_CLICK)) {
            s->outliner_collapsed = !s->outliner_collapsed;
        }
    }
    char menu_key[24];
    snprintf(menu_key, sizeof menu_key, "actor:%d", area);
    for (int r = first > 0 ? first - 1 : 0;
         !s->outliner_collapsed && r < shown && r + 1 <= last;
         r++) {
        int i = rows[r];
        scene_actor *actor = &scene->actors[i];
        char key[32], behaviors[16] = "";
        snprintf(
            key,
            sizeof key,
            "e%llu",
            (unsigned long long)ecs_entity_to_u64(actor->entity)
        );
        if (s->rename.outliner && s->rename.entity == ecs_entity_to_u64(actor->entity)) {
            rename_row(s, actor);
            continue;
        }
        char **names = instance_behavior_names(scene, actor->entity);
        if (acount(names)) {
            snprintf(behaviors, sizeof behaviors, "%d", acount(names));
        }
        free_behavior_names(names);
        /* Every selected row shows; the active one stands out (Blender). */
        bool selected = scene_instance_selected(scene, i),
             active = scene->selected == i;
        kit_row_desc row = {
            .label = actor->name,
            .icon = studio_prototype_icon(s, actor->prototype_name),
            .depth = 1,
            .twisty = -1,
            .flags = (selected ? KIT_ON : 0) | (active ? KIT_ACTIVE : 0),
        };
        unsigned events = kit_row_begin(k, key, &row);
        if (behaviors[0]) {
            kit_push(k, "behaviors", NULL, "inline row");
            kit_icon(k, "icon", NULL, KIT_ICON_GRAPH);
            kit_label(k, "count", "meta", behaviors);
            kit_pop(k);
        }
        view_toggles(s, i);
        kit_row_end(k);
        kit_flag(k, "data-hidden", actor_view(s, i) & APP_SCENE_VIEW_HIDDEN);
        kit_flag(k, "data-marked", selected && !active);
        if (kit_drop_target(k, "behavior", dropped, sizeof dropped) &&
            studio_editable(s)) {
            studio_attach_behavior(s, i, dropped);
        }
        studio_drop_row(s, i);
        if (events & KIT_ROW_CLICK) {
            click_row(s, rows, shown, r);
        }
        /* A right click keeps a selection it lands in, as Unity's does. */
        if ((events & KIT_ROW_CONTEXT) && !selected) {
            finish_transform_edit(scene, false);
            select_instance(scene, i);
        }
        if (events & KIT_ROW_DOUBLE_CLICK) {
            studio_command_run(s, STUDIO_COMMAND_RENAME);
        }
        if (events & KIT_ROW_CONTEXT) {
            kit_popup_open_at(k, menu_key, s->in.mouse_x, s->in.mouse_y);
        }
    }
    kit_list_end(k);
    afree(rows);
    s->outliner_reveal = false;
    if (!shown && s->outliner_filter[0]) {
        kit_label(k, "empty", "outliner-empty", "No matches");
    }
    if (!acount(scene->actors) && !s->outliner_filter[0]) {
        kit_label(k, "empty", "outliner-empty", "No objects. Add a prototype to start.");
    }
    if (kit_popup_begin(k, menu_key, "menu")) {
        scene_actor *actor = selected_actor(scene);
        int count = scene_selection_count(scene);
        char group[64];
        snprintf(group, sizeof group, "%d objects", count);
        kit_menu_group(k, "group", count > 1 || !actor ? group : actor->name);
        studio_command_item(s, STUDIO_COMMAND_FRAME);
        studio_command_item(s, STUDIO_COMMAND_DUPLICATE);
        studio_command_item(s, STUDIO_COMMAND_RENAME);
        kit_menu_separator(k, "s0");
        studio_command_item(s, STUDIO_COMMAND_CUT);
        studio_command_item(s, STUDIO_COMMAND_COPY);
        studio_command_item(s, STUDIO_COMMAND_PASTE);
        kit_menu_separator(k, "s2");
        kit_menu_item_desc prototype = { .label = "Open Prototype",
                                         .icon = KIT_ICON_PROTOTYPE,
                                         .checked = -1,
                                         .disabled = !actor };
        if (kit_menu_item(k, "prototype", &prototype)) {
            app_scene_authoring_open_prototype(scene, actor->prototype_name);
        }
        if (actor) {
            studio_spawn_break_item(s, actor->prototype_name);
        }
        if (studio_live(s)) {
            studio_command_item(s, STUDIO_COMMAND_KEEP_CHANGES);
        } else {
            studio_command_item(s, STUDIO_COMMAND_APPLY_OVERRIDES);
            studio_command_item(s, STUDIO_COMMAND_SAVE_AS_PROTOTYPE);
        }
        studio_command_item(s, STUDIO_COMMAND_HIDE_SELECTED);
        kit_menu_separator(k, "s1");
        studio_command_item(s, STUDIO_COMMAND_DELETE);
        kit_popup_end(k);
    }
}
