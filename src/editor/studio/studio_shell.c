#include "editor/studio/studio_internal.h"
#include "editor/workspace/asset_index.h"
#include "support/array.h"
#include "support/text_file.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* Present only when the browser keeps projects itself (no local server). */
EM_JS(bool, web_projects_available, (), {
    return !!Module['chibiExportProject'];
})
EM_JS(void, web_export_project, (), { Module['chibiExportProject'](); })
EM_JS(void, web_import_project, (), { Module['chibiImportProject'](); })
#endif

static const char *const EDITOR_LABELS[STUDIO_EDITOR_COUNT] = {
    "Scene",  "Behavior",    "Outliner", "Properties", "Output",
    "Assets", "Performance", "Members",  "Panel"
};
static const uint32_t EDITOR_ICONS[STUDIO_EDITOR_COUNT] = {
    KIT_ICON_VIEWPORT,    KIT_ICON_GRAPH,    KIT_ICON_OUTLINER,
    KIT_ICON_PROPERTIES,  KIT_ICON_OUTPUT,   KIT_ICON_ASSETS,
    KIT_ICON_PERFORMANCE, KIT_ICON_VARIABLE, KIT_ICON_LAYOUT,
};

static void dp(char *out, size_t size, float value)
{
    snprintf(out, size, "%.0fdp", value);
}

void studio_report(studio *s, const char *message)
{
    snprintf(s->status, sizeof s->status, "%s", message);
    s->status_time = s->time;
    studio_record(s, KIT_TOAST_INFO, message);
}

const char *studio_scene_title(const app_scene_authoring *scene, char *out, size_t size)
{
    const char *path = scene->scene_path;
    const char *base = path && path[0] ? content_basename(path) : NULL;
    snprintf(out, size, "%s", base ? base : "Scene");
    /* The document name without its storage format (.json, .yaml). */
    char *extension = strrchr(out, '.');
    if (extension && extension != out) {
        *extension = '\0';
    }
    return out;
}

/* Kit icons by component_icon. */
static const uint32_t PROTOTYPE_ICONS[COMPONENT_ICON_COUNT] = {
    KIT_ICON_CAMERA,     KIT_ICON_SYSTEM,  KIT_ICON_LIGHT,   KIT_ICON_EMITTER,
    KIT_ICON_WATER,      KIT_ICON_TERRAIN, KIT_ICON_GAMEPAD, KIT_ICON_KEYBOARD,
    KIT_ICON_PROJECTILE, KIT_ICON_HEALTH,  KIT_ICON_DIAMOND, KIT_ICON_SHAPE,
};

/* A prototype's icon follows from what it is made of, never from its name: of
   the icons its components name in editor/Components.yaml, the first in
   component_icon order decides. */
uint32_t studio_prototype_icon(const studio *s, const char *prototype)
{
    scene_prototype_asset *asset = prototype ? find_prototype(s->scene, prototype)
                                             : NULL;
    if (!asset || !asset->prototype) {
        return KIT_ICON_PROTOTYPE;
    }
    const game_prototype *p = asset->prototype;
    component_icon first = COMPONENT_ICON_COUNT;
    for (int c = 0; c < acount(p->components); c++) {
        component_icon icon = component_presentation_icon(
            &s->presentation.hints,
            p->components[c].desc
        );
        if (icon != COMPONENT_ICON_NONE && icon < first) {
            first = icon;
        }
    }
    return first < COMPONENT_ICON_COUNT ? PROTOTYPE_ICONS[first] : KIT_ICON_PROTOTYPE;
}

/* ---------- Commands ---------- */

/* The graph canvas owns history and saving while it has focus (Blender: the
   area under the pointer or last clicked), and so do the editors of its
   members and nodes. */
static bool graph_focused(const studio *s)
{
    if (!studio_graph_active(s) || s->focus_area < 0) {
        return false;
    }
    studio_editor editor = s->prefs.editors[s->focus_area];
    return s->focus_area == s->graph.area || editor == STUDIO_EDITOR_MEMBERS ||
           (editor == STUDIO_EDITOR_PROPERTIES &&
            (s->tab == STUDIO_TAB_NODE || s->tab == STUDIO_TAB_MEMBER));
}

static void toast(studio *s, kit_toast_type type, const char *title, const char *description)
{
    char text[256];
    snprintf(text, sizeof text, description ? "%s: %s" : "%s", title, description);
    studio_record(s, type, text);
    kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .type = type,
                           .title = title,
                           .description = description,
                           .duration = type == KIT_TOAST_ERROR ? -1 : 0 }
    );
}

void studio_error(studio *s, const char *title, const char *message)
{
    toast(s, KIT_TOAST_ERROR, title, message);
}

static void studio_save(studio *s)
{
    app_scene_authoring *scene = s->scene;
    if (!studio_editable(s) || runtime_session(scene)) {
        toast(s, KIT_TOAST_WARNING, "Stop the scene to save", NULL);
        return;
    }
    finish_transform_edit(scene, false);
    s->problems_checked = false;
    const char *failure = scene->message[0] ? scene->message : NULL;
    if (graph_focused(s) && scene->graph_document_kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        char title[160];
        snprintf(title, sizeof title, "Saved %s", scene->graph_document_id);
        if (save_behavior_document(scene) == 0) {
            toast(s, KIT_TOAST_SUCCESS, title, NULL);
        } else {
            toast(
                s,
                KIT_TOAST_ERROR,
                "Could not save",
                scene->message[0] ? scene->message : failure
            );
        }
        return;
    }
    if (scene->document.kind != APP_EDITOR_DOCUMENT_SCENE) {
        const char *label = scene->active_tab >= 0 &&
                                    scene->active_tab < acount(scene->tabs)
                              ? scene->tabs[scene->active_tab].label
                              : NULL;
        char title[160];
        snprintf(title, sizeof title, "Saved %s", label ? label : "document");
        if (save_active_document(scene) == 0) {
            toast(s, KIT_TOAST_SUCCESS, title, NULL);
        } else {
            toast(
                s,
                KIT_TOAST_ERROR,
                "Could not save",
                scene->message[0] ? scene->message : failure
            );
        }
        return;
    }
    if (!scene->scene_path || !scene->scene_path[0]) {
        toast(s, KIT_TOAST_WARNING, "Scene has no file", NULL);
        return;
    }
    char name[128];
    char title[160];
    if (app_scene_authoring_save(scene, scene->scene_path) == 0) {
        snprintf(title, sizeof title, "Saved %s", studio_scene_title(scene, name, sizeof name));
        toast(s, KIT_TOAST_SUCCESS, title, content_basename(scene->scene_path));
    } else {
        toast(s, KIT_TOAST_ERROR, "Could not save", scene->message[0] ? scene->message : NULL);
    }
}

/* History and dirty state belong to the active document, not always the scene. */
static bool can_replay(studio *s, bool redo)
{
    app_scene_authoring *scene = s->scene;
    if (graph_focused(s)) {
        return studio_graph_can_replay(s, redo);
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = selected_prototype(scene);
        return asset && acount(redo ? asset->redo : asset->undo) > 0;
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SCENE) {
        return redo ? scene_history_can_redo(scene->history)
                    : scene_history_can_undo(scene->history);
    }
    return false;
}

static bool active_dirty(studio *s)
{
    app_scene_authoring *scene = s->scene;
    const scene_document_tab *tab = scene->active_tab >= 0 &&
                                            scene->active_tab < acount(scene->tabs)
                                      ? &scene->tabs[scene->active_tab]
                                      : NULL;
    return tab ? scene_tabs_dirty(scene, tab) : scene->scene_dirty;
}

bool studio_unsaved(studio *s)
{
    app_scene_authoring *scene = s->scene;
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene_tabs_dirty(scene, &scene->tabs[i])) {
            return true;
        }
    }
    return scene->scene_dirty;
}

/* The active scene or prototype document's history. */
static void document_history(studio *s, bool redo)
{
    finish_transform_edit(s->scene, false);
    int result = s->scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                   ? (redo ? prototype_redo(s->scene) : prototype_undo(s->scene))
                   : replay_scene_edit(s->scene, redo);
    if (result == 0) {
        studio_report(s, redo ? "Redo" : "Undo");
    }
}

static void studio_history(studio *s, bool redo)
{
    if (!studio_editable(s) || !can_replay(s, redo)) {
        return;
    }
    if (graph_focused(s)) {
        studio_graph_history(s, redo);
        studio_report(s, redo ? "Redo" : "Undo");
        return;
    }
    document_history(s, redo);
}

static void studio_runtime(studio *s, app_runtime_command command)
{
    /* Play resumes a run paused at a breakpoint (Unreal). */
    if (command == APP_RUNTIME_PLAY && studio_debug(s, STUDIO_RESUME)) {
        return;
    }
    finish_transform_edit(s->scene, false);
    app_runtime_state state = app_scene_authoring_runtime_state(s->scene);
    if (command == APP_RUNTIME_PLAY && state.playing) {
        command = APP_RUNTIME_PAUSE;
    }
    app_scene_authoring_runtime_command(s->scene, state.target, command);
}

void studio_frame_selection(studio *s)
{
    ecs_entity entity = selected_scene_entity(s->scene);
    if (!entity.slot || !ecs_entity_is_alive(s->scene->world, entity)) {
        return;
    }
    fit_scene_view(s->viewport, s->scene->world, entity);
    engine_viewport_get_view(s->viewport, &s->scene->camera.view);
    s->scene->camera.saved = true;
}

void studio_duplicate(studio *s)
{
    if (studio_editable(s) && selected_actor(s->scene)) {
        finish_transform_edit(s->scene, false);
        app_scene_authoring_duplicate_instance(s->scene);
    }
}

/* Destructive edits offer Undo in a toast while they are still the latest edit. */
void studio_delete(studio *s)
{
    scene_actor *actor = selected_actor(s->scene);
    if (!studio_editable(s) || !actor) {
        return;
    }
    char title[160];
    snprintf(title, sizeof title, "Deleted %s", actor->name);
    finish_transform_edit(s->scene, false);
    scene_history_begin_group(s->scene->history);
    int result = app_scene_authoring_delete_instance(s->scene);
    scene_history_end_group(s->scene->history);
    if (result != 0) {
        toast(
            s,
            KIT_TOAST_ERROR,
            "Could not delete",
            s->scene->message[0] ? s->scene->message : NULL
        );
        return;
    }
    kit_toast_dismiss(s->kit, s->undo_toast);
    s->undo_toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .title = title, .action = "Undo", .duration = 6 }
    );
    studio_record(s, KIT_TOAST_DEFAULT, title);
    s->undo_toast_group = scene_history_group_id(s->scene->history, false);
}

static void undo_toast(studio *s)
{
    if (!s->undo_toast) {
        return;
    }
    bool current = s->scene->document.kind == APP_EDITOR_DOCUMENT_SCENE &&
                   scene_history_group_id(s->scene->history, false) == s->undo_toast_group;
    if (kit_toast_action(s->kit, s->undo_toast)) {
        s->undo_toast = 0;
        /* The deletion, even when a graph under the toast has focus. */
        if (current && studio_editable(s)) {
            document_history(s, false);
        }
        return;
    }
    if (!current) {
        kit_toast_dismiss(s->kit, s->undo_toast);
        s->undo_toast = 0;
    }
}

void studio_add_object(studio *s, const char *prototype)
{
    if (!studio_editable(s)) {
        return;
    }
    finish_transform_edit(s->scene, false);
    if (app_scene_authoring_add_instance(s->scene, prototype) != 0) {
        studio_report(s, s->scene->message[0] ? s->scene->message : "Could not add object");
    }
}

/* ---------- Menus ---------- */

static bool item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut, bool disabled)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1,
                             .disabled = disabled };
    return kit_menu_item(k, key, &d);
}

static void add_menu_items(studio *s)
{
    kit *k = s->kit;
    for (int i = 0; i < acount(s->scene->prototypes); i++) {
        const game_prototype *prototype = s->scene->prototypes[i].prototype;
        if (!prototype || !prototype->name) {
            continue;
        }
        char key[32];
        snprintf(key, sizeof key, "p%d", i);
        if (item(k, key, prototype->name, studio_prototype_icon(s, prototype->name), NULL, !studio_editable(s))) {
            studio_add_object(s, prototype->name);
        }
    }
}

void studio_toggle_drawer(studio *s, studio_editor editor)
{
    bool showing = s->prefs.drawer_open &&
                   s->prefs.editors[STUDIO_AREA_DRAWER] == editor;
    s->prefs.editors[STUDIO_AREA_DRAWER] = editor;
    s->prefs.drawer_open = !showing;
}

/* Fills the workspace with one area, or restores the layout; -1 is no area. */
static void studio_toggle_maximized(studio *s, int area)
{
    s->maximized = s->maximized >= 0 ? -1 : area;
}

/* UI Scale's steps, as browsers step a page's zoom, and the menus' choices. */
static const float UI_ZOOMS[] = { 0.5f, 0.67f, 0.75f, 0.8f,  0.9f, 1,
                                  1.1f, 1.25f, 1.5f,  1.75f, 2 };
static const float UI_SCALES[] = { 0.5f, 0.75f, 1, 1.25f, 1.5f, 2 };
enum
{
    UI_ZOOM_COUNT = sizeof UI_ZOOMS / sizeof *UI_ZOOMS,
    UI_SCALE_COUNT = sizeof UI_SCALES / sizeof *UI_SCALES,
};

void studio_zoom(studio *s, int step)
{
    float scale = s->prefs.ui_scale, next = step ? scale : 1;
    /* The last match is the nearest step: up from the top, down from the bottom. */
    for (int i = 0; i < UI_ZOOM_COUNT; i++) {
        float up = UI_ZOOMS[UI_ZOOM_COUNT - 1 - i], down = UI_ZOOMS[i];
        if (step > 0 && up > scale + 0.001f) {
            next = up;
        }
        if (step < 0 && down < scale - 0.001f) {
            next = down;
        }
    }
    s->prefs.ui_scale = next;
}

static void zoom_items(studio *s)
{
    kit *k = s->kit;
    float scale = s->prefs.ui_scale;
    kit_menu_group(k, "scale", "UI Scale");
    if (item(
            k,
            "zoom-in",
            "Zoom In",
            KIT_ICON_ZOOM_IN,
            studio_chord(s, "Mod+="),
            scale >= UI_ZOOMS[UI_ZOOM_COUNT - 1]
        )) {
        studio_zoom(s, 1);
    }
    if (item(k, "zoom-out", "Zoom Out", KIT_ICON_ZOOM_OUT, studio_chord(s, "Mod+-"), scale <= UI_ZOOMS[0])) {
        studio_zoom(s, -1);
    }
    if (item(k, "zoom-reset", "Reset Zoom", 0, studio_chord(s, "Mod+0"), scale == 1)) {
        studio_zoom(s, 0);
    }
}

#ifdef __EMSCRIPTEN__
/* Export archives saved files and import reloads the page, so both need saved work. */
static void project_menu_items(studio *s)
{
    kit *k = s->kit;
    kit_menu_separator(k, "s2");
    bool importing = item(k, "import", "Import Project", 0, NULL, false);
    bool exporting = item(k, "export", "Export Project", 0, NULL, false);
    if ((importing || exporting) && studio_unsaved(s)) {
        toast(s, KIT_TOAST_WARNING, "Save changes first", NULL);
        return;
    }
    if (importing) {
        web_import_project();
    }
    if (exporting) {
        web_export_project();
    }
}
#endif

static void menus(studio *s)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    bool can_edit = studio_editable(s);
    bool selection = selected_actor(scene) != NULL;
    app_runtime_state runtime = app_scene_authoring_runtime_state(scene);
    kit_push(k, "menus", "nav", "menus");
    if (kit_menubar_trigger(k, "file", "File")) {
        if (item(k, "search", "Search", KIT_ICON_SEARCH, studio_chord(s, "Mod+K"), false)) {
            kit_command_open(k, "Search objects, assets and commands");
        }
        studio_projects_menu(s);
        kit_menu_separator(k, "s1");
        if (item(k, "save", "Save", KIT_ICON_SAVE, studio_chord(s, "Mod+S"), !active_dirty(s))) {
            studio_save(s);
        }
        if (item(k, "reload", "Reload", 0, NULL, !can_edit)) {
            app_scene_authoring_reload(scene);
        }
#ifdef __EMSCRIPTEN__
        if (web_projects_available()) {
            project_menu_items(s);
        }
#endif
        kit_popup_end(k);
    }
    if (kit_menubar_trigger(k, "edit", "Edit")) {
        if (item(k, "undo", "Undo", KIT_ICON_UNDO, studio_chord(s, "Mod+Z"), !can_edit || !can_replay(s, false))) {
            studio_history(s, false);
        }
        if (item(
                k,
                "redo",
                "Redo",
                KIT_ICON_REDO,
                studio_chord(s, "Mod+Shift+Z"),
                !can_edit || !can_replay(s, true)
            )) {
            studio_history(s, true);
        }
        kit_menu_separator(k, "s1");
        if (item(k, "duplicate", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), !can_edit || !selection)) {
            studio_duplicate(s);
        }
        if (item(k, "delete", "Delete", KIT_ICON_TRASH, "X", !can_edit || !selection)) {
            studio_delete(s);
        }
        kit_popup_end(k);
    }
    if (kit_menubar_trigger(k, "view", "View")) {
        if (item(k, "frame", "Frame Selected", KIT_ICON_FRAME, "F", !selection)) {
            studio_frame_selection(s);
        }
        if (item(k, "reset_camera", "Reset Camera", 0, NULL, false)) {
            engine_viewport_reset_camera(s->viewport);
        }
        kit_menu_separator(k, "s1");
        kit_menu_group(k, "drawers", "Drawers");
        static const studio_editor drawers[] = { STUDIO_EDITOR_ASSETS,
                                                 STUDIO_EDITOR_OUTPUT,
                                                 STUDIO_EDITOR_PERFORMANCE };
        const char *const drawer_keys[] = { studio_chord(s, "Mod+Shift+A"),
                                            studio_chord(s, "Mod+Shift+U"),
                                            NULL };
        for (int i = 0; i < 3; i++) {
            char key[16];
            snprintf(key, sizeof key, "drawer%d", i);
            kit_menu_item_desc d = {
                .label = EDITOR_LABELS[drawers[i]],
                .shortcut = drawer_keys[i],
                .checked = s->prefs.drawer_open &&
                           s->prefs.editors[STUDIO_AREA_DRAWER] == drawers[i],
            };
            if (kit_menu_item(k, key, &d)) {
                studio_toggle_drawer(s, drawers[i]);
            }
        }
        kit_menu_separator(k, "s2");
        kit_menu_item_desc maximize = { .label = "Maximize Area",
                                        .shortcut = "Ctrl+Space",
                                        .checked = s->maximized >= 0 };
        if (kit_menu_item(k, "maximize", &maximize)) {
            studio_toggle_maximized(s, s->focus_area);
        }
        if (item(k, "reset_layout", "Reset Layout", KIT_ICON_LAYOUT, NULL, false)) {
            studio_prefs layout = studio_prefs_default();
            s->prefs.side_width = layout.side_width;
            s->prefs.main_split = layout.main_split;
            s->prefs.side_split = layout.side_split;
            memcpy(s->prefs.editors, layout.editors, sizeof layout.editors);
            memcpy(s->prefs.panels, layout.panels, sizeof layout.panels);
        }
        kit_menu_separator(k, "s3");
        zoom_items(s);
        kit_popup_end(k);
    }
    if (kit_menubar_trigger(k, "object", "Object")) {
        if (kit_submenu_begin(k, "add", "Add", KIT_ICON_ADD)) {
            add_menu_items(s);
            kit_submenu_end(k);
        }
        if (item(k, "duplicate", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), !can_edit || !selection)) {
            studio_duplicate(s);
        }
        if (item(k, "delete", "Delete", KIT_ICON_TRASH, "X", !can_edit || !selection)) {
            studio_delete(s);
        }
        kit_menu_separator(k, "s1");
        scene_actor *actor = selected_actor(scene);
        if (item(k, "prototype", "Open Prototype", KIT_ICON_PROTOTYPE, NULL, !actor)) {
            app_scene_authoring_open_prototype(scene, actor->prototype_name);
        }
        kit_popup_end(k);
    }
    if (kit_menubar_trigger(k, "play", "Play")) {
        if (item(
                k,
                "play",
                runtime.playing ? "Pause" : "Play",
                runtime.playing ? KIT_ICON_PAUSE : KIT_ICON_PLAY,
                "Space",
                false
            )) {
            studio_runtime(s, APP_RUNTIME_PLAY);
        }
        if (item(k, "stop", "Stop", KIT_ICON_STOP, NULL, !runtime_session(scene))) {
            studio_runtime(s, APP_RUNTIME_STOP);
        }
        if (item(k, "step", "Step", KIT_ICON_STEP, NULL, s->pause.active)) {
            studio_runtime(s, APP_RUNTIME_STEP);
        }
        kit_menu_separator(k, "s1");
        studio_debug_menu(s);
        kit_popup_end(k);
    }
    kit_pop(k);
}

static void document_tabs(studio *s)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    kit_push(k, "docs", "nav", "doc-tabs");
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        bool active = i == scene->active_tab;
        bool dirty = scene_tabs_dirty(scene, tab);
        uint32_t icon = tab->kind == APP_EDITOR_DOCUMENT_SCENE ? KIT_ICON_SCENE
                      : tab->kind == APP_EDITOR_DOCUMENT_BEHAVIOR ? KIT_ICON_GRAPH
                      : tab->kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                          ? KIT_ICON_PROTOTYPE
                          : KIT_ICON_SYSTEM;
        char key[24];
        snprintf(key, sizeof key, "t%d", i);
        kit_push(k, key, "button", "btn doc-tab");
        if (active) {
            kit_attr(k, "data-state", "on");
        } else {
            kit_flag(k, "data-state", false);
        }
        kit_tip(k, tab->path, NULL, NULL);
        kit_icon(k, "icon", NULL, icon);
        kit_label(k, "label", "label", tab->label ? tab->label : "Untitled");
        if (dirty) {
            kit_leaf(k, "dirty", NULL, "dirty-dot");
        }
        kit_pop(k);
        if (kit_item_clicked(k) && !active) {
            finish_transform_edit(scene, false);
            scene_tabs_activate(scene, i);
        }
    }
    kit_pop(k);
}

/* What Play runs: the scene, or a preview of the selected prototype. Fixed
   while a session runs; the label morphs when the target changes. */
static void runtime_target(studio *s, app_runtime_state runtime, bool session)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    scene_prototype_asset *previewed = prototype_at(scene, scene->runtime_preview_prototype);
    /* The open prototype document, else the selected object's prototype. */
    scene_actor *actor = selected_actor(scene);
    scene_prototype_asset *candidate = scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                                         ? selected_prototype(scene)
                                     : actor ? find_prototype(scene, actor->prototype_name)
                                             : NULL;
    bool preview = runtime.target == APP_RUNTIME_PREVIEW && previewed;
    char title[128], scene_label[160], preview_label[160];
    snprintf(
        scene_label,
        sizeof scene_label,
        "Scene \xc2\xb7 %s",
        studio_scene_title(scene, title, sizeof title)
    );
    const scene_prototype_asset *shown = candidate ? candidate : previewed;
    snprintf(
        preview_label,
        sizeof preview_label,
        "Preview \xc2\xb7 %s",
        shown ? shown->prototype->name : "Prototype"
    );
    char current[160];
    snprintf(
        current,
        sizeof current,
        "Preview \xc2\xb7 %s",
        preview ? previewed->prototype->name : ""
    );
    kit_button_desc d = {
        .label = preview ? current : scene_label,
        .icon = preview ? KIT_ICON_PROTOTYPE : KIT_ICON_SCENE,
        .variant = KIT_SUBTLE,
        .cls = "target",
        .morph = true,
        .tip = "Play target",
    };
    if (kit_button(k, "target", &d)) {
        kit_popup_open(k, "target");
    }
    if (kit_popup_begin(k, "target", "menu")) {
        kit_menu_group(k, "group", "Play");
        kit_menu_item_desc run_scene = { .label = scene_label,
                                         .checked = !preview,
                                         .disabled = session };
        if (kit_menu_item(k, "scene", &run_scene) && preview) {
            app_scene_authoring_set_runtime_target(scene, APP_RUNTIME_SCENE);
        }
        kit_menu_item_desc run_preview = { .label = preview_label,
                                           .checked = preview && shown == previewed,
                                           .disabled = session || !candidate };
        if (kit_menu_item(k, "preview", &run_preview) && candidate) {
            scene->selected_prototype = (int)(candidate - scene->prototypes);
            app_scene_authoring_set_runtime_target(scene, APP_RUNTIME_PREVIEW);
        }
        kit_popup_end(k);
    }
}

static void transport(studio *s)
{
    kit *k = s->kit;
    app_runtime_state runtime = app_scene_authoring_runtime_state(s->scene);
    bool session = runtime_session(s->scene);
    kit_push(k, "transport", NULL, "transport");
    runtime_target(s, runtime, session);
    kit_push(k, "group", NULL, "group");
    /* A run paused at a breakpoint trades Pause for Resume and the frame step
       for the debugger's steps (Unreal). */
    bool paused = s->pause.active;
    kit_button_desc play = {
        .icon = paused          ? KIT_ICON_CONTINUE
              : runtime.playing ? KIT_ICON_PAUSE
                                : KIT_ICON_PLAY,
        .tip = paused          ? "Resume"
             : runtime.playing ? "Pause"
                               : "Play",
        .shortcut = paused ? "F5" : "Space",
        .flags = runtime.playing && !paused ? KIT_ON : 0,
    };
    if (kit_button(k, "play", &play)) {
        studio_runtime(s, APP_RUNTIME_PLAY);
    }
    kit_button_desc stop = { .icon = KIT_ICON_STOP,
                             .tip = "Stop",
                             .flags = session ? 0 : KIT_DISABLED };
    if (kit_button(k, "stop", &stop)) {
        studio_runtime(s, APP_RUNTIME_STOP);
    }
    kit_button_desc step = { .icon = KIT_ICON_STEP, .tip = "Step" };
    if (paused) {
        studio_debug_steps(s);
    } else if (kit_button(k, "step", &step)) {
        studio_runtime(s, APP_RUNTIME_STEP);
    }
    kit_pop(k);
    kit_pop(k);
}

/* Settings is an authored dialog in the Studio project. */
static void settings(studio *s)
{
    kit_button_desc d = { .icon = KIT_ICON_SETTINGS,
                          .tip = "Settings",
                          .shortcut = studio_chord(s, "Mod+,") };
    if (kit_button(s->kit, "settings", &d)) {
        studio_ui_show(s, "Settings");
    }
    studio_ui_modal(s, "Settings");
}

static void topbar(studio *s)
{
    kit *k = s->kit;
    kit_push(k, "topbar", "header", "topbar");
    kit_push(k, "brand", NULL, "brand");
    kit_leaf(k, "mark", NULL, "mark");
    kit_pop(k);
    menus(s);
    kit_leaf(k, "sep", NULL, "sep");
    document_tabs(s);
    kit_leaf(k, "spacer", NULL, "spacer");
    transport(s);
    kit_leaf(k, "spacer2", NULL, "spacer");
    settings(s);
    kit_button_desc search = { .label = "Search",
                               .icon = KIT_ICON_SEARCH,
                               .cls = "search-trigger",
                               .shortcut = studio_chord(s, "Mod+K") };
    if (kit_button(k, "search", &search)) {
        kit_command_open(k, "Search objects, assets and commands");
    }
    kit_pop(k);
}

/* ---------- Areas ---------- */

/* Drawers hold panels, never a second Scene or graph. */
static bool drawer_editor(studio_editor editor)
{
    return editor == STUDIO_EDITOR_OUTPUT || editor == STUDIO_EDITOR_ASSETS ||
           editor == STUDIO_EDITOR_PERFORMANCE || editor == STUDIO_EDITOR_PANEL;
}

/* The Studio project's panels, after the built-in editors. */
static void panel_types(studio *s, int area)
{
    kit *k = s->kit;
    int count = studio_ui_panel_count(s);
    if (count) {
        kit_menu_separator(k, "panels-sep");
        kit_menu_group(k, "panels", "Studio");
    }
    for (int i = 0; i < count; i++) {
        const char *name = studio_ui_panel_name(s, i);
        char item_key[16];
        snprintf(item_key, sizeof item_key, "p%d", i);
        kit_menu_item_desc item = {
            .label = name,
            .icon = EDITOR_ICONS[STUDIO_EDITOR_PANEL],
            .checked = -1,
            .current = s->prefs.editors[area] == STUDIO_EDITOR_PANEL &&
                       !strcmp(s->prefs.panels[area], name),
        };
        if (kit_menu_item(k, item_key, &item)) {
            s->prefs.editors[area] = STUDIO_EDITOR_PANEL;
            snprintf(s->prefs.panels[area], sizeof s->prefs.panels[area], "%s", name);
        }
    }
}

void studio_maximize_graph(studio *s)
{
    int area = STUDIO_AREA_TOP;
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        area = s->prefs.editors[i] == STUDIO_EDITOR_BEHAVIOR ? i : area;
    }
    s->prefs.editors[area] = STUDIO_EDITOR_BEHAVIOR;
    s->applied.editors[area] = STUDIO_EDITOR_BEHAVIOR;
    s->maximized = area;
}

static void editor_type_menu(studio *s, int area)
{
    kit *k = s->kit;
    studio_editor *editor = &s->prefs.editors[area];
    kit_button_desc d = { .icon = EDITOR_ICONS[*editor],
                          .cls = "editor-type",
                          .tip = *editor == STUDIO_EDITOR_PANEL
                                   ? s->prefs.panels[area]
                                   : EDITOR_LABELS[*editor] };
    char key[24];
    snprintf(key, sizeof key, "type:%d", area);
    if (kit_button(k, "type", &d)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_group(k, "group", "Editor");
        for (int e = 0; e < STUDIO_EDITOR_PANEL; e++) {
            if (area == STUDIO_AREA_DRAWER && !drawer_editor((studio_editor)e)) {
                continue;
            }
            char item_key[16];
            snprintf(item_key, sizeof item_key, "e%d", e);
            kit_menu_item_desc item = { .label = EDITOR_LABELS[e],
                                        .icon = EDITOR_ICONS[e],
                                        .checked = -1,
                                        .current = *editor == (studio_editor)e };
            if (kit_menu_item(k, item_key, &item)) {
                *editor = (studio_editor)e;
            }
        }
        panel_types(s, area);
        kit_popup_end(k);
    }
}

static void area(studio *s, int index, const char *key, float size, bool fill)
{
    kit *k = s->kit;
    studio_editor editor = s->prefs.editors[index];
    kit_push(k, key, NULL, KIT_CX(k, "area", { "fill", fill }, { "drawer", index == STUDIO_AREA_DRAWER }));
    if (!fill) {
        char height[32];
        dp(height, sizeof height, size);
        kit_style(k, "height", height);
    }
    kit_push(k, "head", "header", "area-head");
    editor_type_menu(s, index);
    switch (editor) {
        case STUDIO_EDITOR_SCENE: studio_scene_header(s, index); break;
        case STUDIO_EDITOR_BEHAVIOR: studio_behavior_header(s, index); break;
        case STUDIO_EDITOR_OUTLINER: studio_outliner_header(s, index); break;
        case STUDIO_EDITOR_PROPERTIES:
            studio_properties_header(s, index);
            break;
        case STUDIO_EDITOR_OUTPUT: studio_output_header(s, index); break;
        case STUDIO_EDITOR_ASSETS: studio_assets_header(s, index); break;
        case STUDIO_EDITOR_PERFORMANCE:
            studio_performance_header(s, index);
            break;
        case STUDIO_EDITOR_MEMBERS: studio_members_header(s, index); break;
        case STUDIO_EDITOR_PANEL:
            kit_label(k, "title", NULL, s->prefs.panels[index]);
            break;
        default: break;
    }
    if (index == STUDIO_AREA_DRAWER) {
        kit_button_desc close = { .icon = KIT_ICON_CLOSE,
                                  .tip = "Close",
                                  .cls = "drawer-close" };
        if (kit_button(k, "close", &close)) {
            s->prefs.drawer_open = false;
        }
    }
    kit_pop(k);
    kit_push(k, "body", NULL, "area-body");
    switch (editor) {
        case STUDIO_EDITOR_SCENE: studio_scene_body(s, index); break;
        case STUDIO_EDITOR_BEHAVIOR: studio_behavior_body(s, index); break;
        case STUDIO_EDITOR_OUTLINER: studio_outliner_body(s, index); break;
        case STUDIO_EDITOR_PROPERTIES: studio_properties_body(s, index); break;
        case STUDIO_EDITOR_OUTPUT: studio_output_body(s, index); break;
        case STUDIO_EDITOR_ASSETS: studio_assets_body(s, index); break;
        case STUDIO_EDITOR_PERFORMANCE:
            studio_performance_body(s, index);
            break;
        case STUDIO_EDITOR_MEMBERS: studio_members_body(s, index); break;
        case STUDIO_EDITOR_PANEL:
            studio_ui_area(s, index, s->prefs.panels[index]);
            break;
        default: break;
    }
    kit_pop(k);
    kit_pop(k);
    s->areas[index].rect = kit_item_rect(k);
}

static void workspace(studio *s)
{
    static const char *const AREA_KEYS[STUDIO_AREA_COUNT] = { "top",
                                                              "bottom",
                                                              "outliner",
                                                              "properties",
                                                              "drawer" };
    kit *k = s->kit;
    kit_push(k, "workspace", "main", "workspace");
    kit_push(k, "main", NULL, "column main");
    if (s->maximized >= 0) {
        memset(s->areas, 0, sizeof s->areas);
        area(s, s->maximized, AREA_KEYS[s->maximized], 0, true);
        kit_pop(k);
        s->main_rect = kit_item_rect(k);
        kit_pop(k);
        return;
    }
    float column_height = s->main_rect.h > 0 ? s->main_rect.h : s->in.height - 60;
    /* The drawer eases open in 200 ms and closed in 100 ms, and follows its
       splitter directly while dragged. */
    float drawer_target = s->prefs.drawer_open
                            ? fminf(s->prefs.drawer_height, fmaxf(80, column_height - 200))
                            : 0;
    float drawer = kit_spring(
        k,
        "drawer",
        drawer_target,
        s->drawer_dragging     ? 0
        : s->prefs.drawer_open ? 0.2f
                               : 0.1f
    );
    bool drawer_shown = drawer > 0.5f;
    float main_height = column_height - (drawer_shown ? drawer + 4 : 0);
    float top = floorf((main_height - 4) * s->prefs.main_split);
    area(s, STUDIO_AREA_TOP, "top", top, false);
    if (kit_splitter(k, "split", 1, &top, 80, fmaxf(80, main_height - 80), false)) {
        s->prefs.main_split = top / fmaxf(1, main_height - 4);
    }
    area(s, STUDIO_AREA_BOTTOM, "bottom", 0, true);
    if (drawer_shown) {
        float height = s->prefs.drawer_height;
        if (kit_splitter(k, "drawer-split", 1, &height, 80, fmaxf(80, column_height - 200), true)) {
            s->prefs.drawer_height = height;
        }
        s->drawer_dragging = kit_item_active(k);
        area(s, STUDIO_AREA_DRAWER, "drawer", drawer, false);
    } else {
        s->areas[STUDIO_AREA_DRAWER].rect = (kit_rect){ 0 };
        s->drawer_dragging = false;
    }
    kit_pop(k);
    s->main_rect = kit_item_rect(k);
    kit_splitter(k, "split", 0, &s->prefs.side_width, 260, fmaxf(260, s->in.width - 320), true);
    kit_push(k, "side", NULL, "column side");
    char width[32];
    dp(width, sizeof width, s->prefs.side_width);
    kit_style(k, "width", width);
    float side_height = s->side_rect.h > 0 ? s->side_rect.h : s->in.height - 60;
    float outliner = floorf((side_height - 4) * s->prefs.side_split);
    area(s, STUDIO_AREA_OUTLINER, "outliner", outliner, false);
    if (kit_splitter(k, "split", 1, &outliner, 80, fmaxf(80, side_height - 120), false)) {
        s->prefs.side_split = outliner / fmaxf(1, side_height - 4);
    }
    area(s, STUDIO_AREA_PROPERTIES, "properties", 0, true);
    kit_pop(k);
    s->side_rect = kit_item_rect(k);
    kit_pop(k);
}

/* ---------- Status bar ---------- */

/* UI Scale stays in reach, as Office keeps its zoom in the status bar. */
static void scale_control(studio *s)
{
    kit *k = s->kit;
    char label[16];
    snprintf(label, sizeof label, "%d%%", (int)lroundf(s->prefs.ui_scale * 100));
    kit_button_desc d = { .label = label, .size = KIT_SM, .cls = "ui-scale", .tip = "UI Scale" };
    if (kit_button(k, "scale", &d)) {
        kit_popup_open(k, "scale-menu");
    }
    if (!kit_popup_begin(k, "scale-menu", "menu")) {
        return;
    }
    zoom_items(s);
    kit_menu_separator(k, "s1");
    for (int i = 0; i < UI_SCALE_COUNT; i++) {
        char key[8], text[8];
        snprintf(key, sizeof key, "p%d", i);
        snprintf(text, sizeof text, "%d%%", (int)lroundf(UI_SCALES[i] * 100));
        kit_menu_item_desc preset = {
            .label = text,
            .checked = fabsf(s->prefs.ui_scale - UI_SCALES[i]) < 0.001f
        };
        if (kit_menu_item(k, key, &preset)) {
            s->prefs.ui_scale = UI_SCALES[i];
        }
    }
    kit_popup_end(k);
}

static void hint(kit *k, const char *key, const char *keys, uint32_t icon, const char *label)
{
    kit_push(k, key, NULL, "hint");
    if (icon) {
        kit_icon(k, "i", NULL, icon);
    } else {
        kit_label(k, "k", "kbd-key", keys);
    }
    kit_label(k, "l", NULL, label);
    kit_pop(k);
}

static void statusbar(studio *s)
{
    kit *k = s->kit;
    kit_push(k, "status", "footer", "statusbar");
    kit_push(k, "hints", NULL, "hints");
    studio_editor editor = s->hover_area >= 0 ? s->prefs.editors[s->hover_area]
                                              : STUDIO_EDITOR_COUNT;
    switch (editor) {
        case STUDIO_EDITOR_SCENE:
            hint(k, "select", NULL, KIT_ICON_MOUSE, "Select");
            hint(k, "pan", NULL, KIT_ICON_PAN, "Pan (middle)");
            hint(k, "frame", "F", 0, "Frame");
            break;
        case STUDIO_EDITOR_PROPERTIES:
            hint(k, "scrub", NULL, KIT_ICON_MOUSE, "Drag to scrub");
            hint(k, "fine", studio_chord(s, "Alt"), 0, "Fine");
            hint(k, "coarse", studio_chord(s, "Shift"), 0, "Coarse");
            hint(k, "snap", studio_chord(s, "Ctrl"), 0, "Snap");
            break;
        case STUDIO_EDITOR_OUTLINER:
            hint(k, "select", NULL, KIT_ICON_MOUSE, "Select");
            hint(k, "frame", "F", 0, "Frame");
            hint(k, "delete", "X", 0, "Delete");
            break;
        case STUDIO_EDITOR_MEMBERS:
            hint(k, "drag", NULL, KIT_ICON_MOUSE, "Drag to graph");
            hint(k, "set", studio_chord(s, "Alt"), 0, "Set");
            hint(k, "rename", "F2", 0, "Rename");
            hint(k, "delete", "X", 0, "Delete");
            break;
        case STUDIO_EDITOR_BEHAVIOR:
            if (s->graph.gesture == STUDIO_GRAPH_MOVE) {
                hint(k, "snap", studio_chord(s, "Ctrl"), 0, "Snap");
                break;
            }
            hint(k, "select", NULL, KIT_ICON_MOUSE, "Select");
            hint(k, "add", studio_chord(s, "Shift+A"), 0, "Add");
            hint(k, "straighten", "Q", 0, "Straighten");
            break;
        default:
            hint(k, "select", NULL, KIT_ICON_MOUSE, "Select");
            hint(k, "search", studio_chord(s, "Mod+K"), 0, "Search");
            break;
    }
    kit_pop(k);
    kit_leaf(k, "spacer", NULL, "spacer");
    /* Authoring messages report like any other operation: shown for three seconds. */
    if (strcmp(s->scene->message, s->seen_message) != 0) {
        snprintf(s->seen_message, sizeof s->seen_message, "%s", s->scene->message);
        if (s->seen_message[0]) {
            studio_report(s, s->seen_message);
        }
    }
    const char *message = s->time - s->status_time < 3 ? s->status : "";
    if (message[0]) {
        kit_push(k, "report", NULL, "report");
        kit_presence(k);
        kit_icon(k, "i", NULL, KIT_ICON_CHECK);
        kit_morph(k, "l", NULL, message);
        kit_pop(k);
    }
    kit_leaf(k, "spacer2", NULL, "spacer");
    kit_push(k, "drawers", NULL, "drawers");
    const struct
    {
        studio_editor editor;
        const char *shortcut;
    } toggles[] = { { STUDIO_EDITOR_ASSETS, studio_chord(s, "Mod+Shift+A") },
                    { STUDIO_EDITOR_OUTPUT, studio_chord(s, "Mod+Shift+U") },
                    { STUDIO_EDITOR_PERFORMANCE, NULL } };
    for (size_t i = 0; i < sizeof toggles / sizeof *toggles; i++) {
        studio_editor e = toggles[i].editor;
        bool on = s->prefs.drawer_open && s->prefs.editors[STUDIO_AREA_DRAWER] == e;
        char key[16];
        snprintf(key, sizeof key, "d%d", (int)e);
        kit_button_desc d = {
            .label = EDITOR_LABELS[e],
            .icon = EDITOR_ICONS[e],
            .size = KIT_SM,
            .cls = "toggle-drawer",
            .tip = EDITOR_LABELS[e],
            .shortcut = toggles[i].shortcut,
            .flags = on ? KIT_ON : 0,
        };
        if (kit_button(k, key, &d)) {
            studio_toggle_drawer(s, e);
        }
    }
    kit_pop(k);
    kit_push(k, "stats", NULL, "stats");
    char title[128];
    kit_label(k, "scene", "strong", studio_scene_title(s->scene, title, sizeof title));
    scene_actor *actor = selected_actor(s->scene);
    kit_label(k, "selection", actor ? "strong" : NULL, actor ? actor->name : "No selection");
    kit_push(k, "objects", NULL, "count");
    kit_label(k, "l", NULL, "Objects ");
    const kit_number_flow_desc count = { 0 };
    kit_number_flow(k, "selected", NULL, actor ? 1 : 0, &count);
    kit_label(k, "of", NULL, "/");
    kit_number_flow(k, "total", NULL, acount(s->scene->actors), &count);
    kit_pop(k);
    kit_pop(k);
    scale_control(s);
    kit_pop(k);
}

/* ---------- Command palette (cmdk) ---------- */

enum
{
    COMMAND_SAVE,
    COMMAND_UNDO,
    COMMAND_REDO,
    COMMAND_PLAY,
    COMMAND_STOP,
    COMMAND_FRAME,
    COMMAND_DUPLICATE,
    COMMAND_DELETE,
    COMMAND_DENSITY,
    COMMAND_MOTION,
    COMMAND_THEME,
    COMMAND_ZOOM_IN,
    COMMAND_ZOOM_OUT,
    COMMAND_ZOOM_RESET,
    COMMAND_ASSETS,
    COMMAND_OUTPUT,
    COMMAND_PERFORMANCE,
    COMMAND_COUNT,
};

static void palette(studio *s)
{
    if (studio_graph_palette(s) || studio_member_palette(s) ||
        !kit_command_is_open(s->kit)) {
        return;
    }
    app_scene_authoring *scene = s->scene;
    kit_command_item *items = NULL;
    const kit_command_item commands[COMMAND_COUNT] = {
        { "Save", "Commands", NULL, studio_chord(s, "Mod+S"), NULL, KIT_ICON_SAVE },
        { "Undo", "Commands", NULL, studio_chord(s, "Mod+Z"), NULL, KIT_ICON_UNDO },
        { "Redo", "Commands", NULL, studio_chord(s, "Mod+Shift+Z"), NULL, KIT_ICON_REDO },
        { "Play", "Commands", NULL, "Space", "run start preview pause", KIT_ICON_PLAY },
        { "Stop", "Commands", NULL, NULL, NULL, KIT_ICON_STOP },
        { "Frame Selected", "Commands", NULL, "F", "focus zoom", KIT_ICON_FRAME },
        { "Duplicate", "Commands", NULL, studio_chord(s, "Shift+D"), "copy", KIT_ICON_COPY },
        { "Delete", "Commands", NULL, "X", "remove", KIT_ICON_TRASH },
        { s->prefs.comfortable ? "Compact Density" : "Comfortable Density",
          "Commands",
          NULL,
          NULL,
          "spacing size settings",
          KIT_ICON_LAYOUT },
        { s->prefs.reduced_motion ? "Full Motion" : "Reduce Motion",
          "Commands",
          NULL,
          NULL,
          "animation settings",
          KIT_ICON_SETTINGS },
        { s->light ? "Dark Theme" : "Light Theme",
          "Commands",
          NULL,
          NULL,
          "appearance color mode settings",
          s->light ? KIT_ICON_MOON : KIT_ICON_SUN },
        { "Zoom In", "Commands", NULL, studio_chord(s, "Mod+="), "ui scale larger bigger", KIT_ICON_ZOOM_IN },
        { "Zoom Out", "Commands", NULL, studio_chord(s, "Mod+-"), "ui scale smaller", KIT_ICON_ZOOM_OUT },
        { "Reset Zoom", "Commands", NULL, studio_chord(s, "Mod+0"), "ui scale actual size 100%", 0 },
        { "Toggle Assets",
          "Commands",
          NULL,
          studio_chord(s, "Mod+Shift+A"),
          "drawer browser content",
          KIT_ICON_ASSETS },
        { "Toggle Output",
          "Commands",
          NULL,
          studio_chord(s, "Mod+Shift+U"),
          "drawer log console problems diagnostics",
          KIT_ICON_OUTPUT },
        { "Toggle Performance", "Commands", NULL, NULL, "drawer profiler frame time fps", KIT_ICON_PERFORMANCE },
    };
    for (int i = 0; i < COMMAND_COUNT; i++) {
        apush(items, commands[i]);
    }
    int objects = acount(items);
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        apush(
            items,
            ((kit_command_item){ actor->name,
                                 "Objects",
                                 actor->prototype_name,
                                 NULL,
                                 actor->prototype_name,
                                 studio_prototype_icon(s, actor->prototype_name) })
        );
    }
    int assets = acount(items);
    const reference_index *index = app_scene_authoring_asset_index(scene);
    int asset_count = index ? reference_index_asset_count(index) : 0;
    for (int i = 0; i < asset_count; i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        if (!asset->name || asset->kind < 0 || asset->kind >= REFERENCE_ASSET_KIND_COUNT) {
            continue;
        }
        const char *kind = studio_asset_kind_name(asset->kind);
        apush(
            items,
            ((kit_command_item){ asset->name,
                                 "Assets",
                                 kind,
                                 NULL,
                                 kind,
                                 studio_asset_kind_icon(asset->kind) })
        );
    }
    int projects = acount(items);
    for (int i = 0; i < acount(s->projects); i++) {
        if (!studio_project_current(s, i)) {
            apush(
                items,
                ((kit_command_item){ s->projects[i].name, "Projects", NULL, NULL, "open switch", KIT_ICON_FOLDER })
            );
        }
    }
    int chosen = kit_command(s->kit, items, acount(items));
    if (chosen >= 0 && chosen < objects) {
        switch (chosen) {
            case COMMAND_SAVE: studio_save(s); break;
            case COMMAND_UNDO: studio_history(s, false); break;
            case COMMAND_REDO: studio_history(s, true); break;
            case COMMAND_PLAY: studio_runtime(s, APP_RUNTIME_PLAY); break;
            case COMMAND_STOP: studio_runtime(s, APP_RUNTIME_STOP); break;
            case COMMAND_FRAME: studio_frame_selection(s); break;
            case COMMAND_DUPLICATE: studio_duplicate(s); break;
            case COMMAND_DELETE: studio_delete(s); break;
            case COMMAND_DENSITY:
                s->prefs.comfortable = !s->prefs.comfortable;
                break;
            case COMMAND_MOTION:
                s->prefs.reduced_motion = !s->prefs.reduced_motion;
                break;
            case COMMAND_THEME:
                s->prefs.theme = s->light ? STUDIO_THEME_DARK : STUDIO_THEME_LIGHT;
                break;
            case COMMAND_ZOOM_IN: studio_zoom(s, 1); break;
            case COMMAND_ZOOM_OUT: studio_zoom(s, -1); break;
            case COMMAND_ZOOM_RESET: studio_zoom(s, 0); break;
            case COMMAND_ASSETS:
                studio_toggle_drawer(s, STUDIO_EDITOR_ASSETS);
                break;
            case COMMAND_OUTPUT:
                studio_toggle_drawer(s, STUDIO_EDITOR_OUTPUT);
                break;
            case COMMAND_PERFORMANCE:
                studio_toggle_drawer(s, STUDIO_EDITOR_PERFORMANCE);
                break;
        }
    } else if (chosen >= objects && chosen < assets) {
        finish_transform_edit(scene, false);
        select_instance(scene, chosen - objects);
        studio_frame_selection(s);
    } else if (chosen >= projects) {
        int position = projects;
        for (int i = 0; i < acount(s->projects); i++) {
            if (!studio_project_current(s, i) && position++ == chosen) {
                studio_open_project(s, i);
            }
        }
    } else if (chosen >= assets) {
        int position = 0;
        for (int i = 0; i < asset_count; i++) {
            const reference_asset *asset = reference_index_asset_at(index, i);
            if (asset->name && asset->kind >= 0 &&
                asset->kind < REFERENCE_ASSET_KIND_COUNT &&
                position++ == chosen - assets) {
                app_scene_authoring_open_asset(scene, i);
                break;
            }
        }
    }
    afree(items);
}

/* ---------- Frame ---------- */

static void track_pointer(studio *s)
{
    s->hover_area = -1;
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        kit_rect r = s->areas[i].rect;
        if (s->in.mouse_x >= r.x && s->in.mouse_y >= r.y &&
            s->in.mouse_x < r.x + r.w && s->in.mouse_y < r.y + r.h) {
            s->hover_area = i;
        }
    }
    if (s->in.pressed[0] || s->in.pressed[1] || s->in.pressed[2]) {
        s->focus_area = s->hover_area;
    }
}

/* editor/Components.yaml applies without a restart. An invalid edit keeps the
   last valid file; components present generically while it is missing. */
static void sync_presentation(studio *s)
{
    studio_presentation *p = &s->presentation;
    char path[sizeof p->watch.path];
    snprintf(path, sizeof path, "%s/" COMPONENT_PRESENTATION_PATH, s->scene->content_root);
    if (!file_watch_changed(&p->watch, path, s->time)) {
        return;
    }
    if (!file_watch_exists(&p->watch)) {
        component_presentation_free(&p->hints);
        return;
    }
    char *text = text_file_read(path);
    char error[256] = "could not be read";
    if (!text ||
        !component_presentation_read(text, &s->scene->registry, &p->hints, error, sizeof error)) {
        toast(s, KIT_TOAST_ERROR, COMPONENT_PRESENTATION_PATH, error);
    }
    free(text);
}

void studio_emit(studio *s)
{
    sync_presentation(s);
    studio_disk_frame(s);
    track_pointer(s);
    kit_root(s->kit, "app");
    topbar(s);
    workspace(s);
    statusbar(s);
    palette(s);
    undo_toast(s);
    studio_projects_frame(s);
    studio_rewire_toast(s);
    studio_members_toast(s);
    studio_refactor_frame(s);
}

/* Up and Down walk the Outliner's visible rows (Blender). */
static void step_selection(studio *s, int direction)
{
    int *rows = studio_outliner_rows(s);
    int count = acount(rows), at = -1;
    for (int r = 0; r < count; r++) {
        at = rows[r] == s->scene->selected ? r : at;
    }
    int next = at < 0 ? (direction > 0 ? 0 : count - 1) : at + direction;
    if (count && next >= 0 && next < count) {
        finish_transform_edit(s->scene, false);
        select_instance(s->scene, rows[next]);
        s->outliner_reveal = true;
    }
    afree(rows);
}

/* Mod = and Mod - step UI Scale and Mod 0 restores it, as browsers and VS
   Code zoom. */
static bool zoom_key(SDL_Keycode key, int *step)
{
    switch (key) {
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS: *step = 1; return true;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: *step = -1; return true;
        case SDLK_0:
        case SDLK_KP_0: *step = 0; return true;
        default: return false;
    }
}

/* Held keys repeat to walk the Outliner, to undo or redo and to zoom, never
   to toggle, delete or reset. */
static bool repeats(SDL_Keycode key, bool mod)
{
    int step = 0;
    return mod ? key == SDLK_Z || key == SDLK_Y || (zoom_key(key, &step) && step)
               : key == SDLK_UP || key == SDLK_DOWN;
}

void studio_shortcuts(studio *s)
{
    studio_editor editor = s->hover_area >= 0 ? s->prefs.editors[s->hover_area]
                                              : STUDIO_EDITOR_COUNT;
    /* A game playing in the focused viewport owns keys without a modifier,
       such as Space for a dash. */
    bool gameplay = engine_viewport_focused(s->viewport);
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        bool mod = studio_primary(s, s->keys[i].mod);
        bool shift = s->keys[i].mod & SDL_KMOD_SHIFT;
        int step;
        if (studio_debug_key(s, key, shift)) {
            continue;
        }
        if ((gameplay && !mod) || (s->keys[i].repeat && !repeats(key, mod))) {
            continue;
        }
        if (mod && shift && (key == SDLK_A || key == SDLK_U)) {
            studio_toggle_drawer(s, key == SDLK_A ? STUDIO_EDITOR_ASSETS : STUDIO_EDITOR_OUTPUT);
        } else if (mod && (key == SDLK_K || key == SDLK_P)) {
            kit_command_open(s->kit, "Search objects, assets and commands");
        } else if (mod && key == SDLK_S) {
            studio_save(s);
        } else if (mod && key == SDLK_COMMA) {
            studio_ui_show(s, "Settings");
        } else if (mod && zoom_key(key, &step)) {
            studio_zoom(s, step);
        } else if (mod && key == SDLK_Z) {
            studio_history(s, shift);
        } else if (mod && key == SDLK_Y) {
            studio_history(s, true);
        } else if ((s->keys[i].mod & SDL_KMOD_CTRL) && key == SDLK_SPACE) {
            studio_toggle_maximized(s, s->hover_area);
        } else if (!mod && key == SDLK_SPACE && editor != STUDIO_EDITOR_BEHAVIOR) {
            studio_runtime(s, APP_RUNTIME_PLAY);
        } else if (
            editor == STUDIO_EDITOR_MEMBERS &&
            studio_members_key(s, key, s->keys[i].mod)
        ) {
            continue;
        } else if (
            editor == STUDIO_EDITOR_ASSETS &&
            studio_assets_key(s, key, s->keys[i].mod)
        ) {
            continue;
        } else if (!mod && key == SDLK_F && (editor == STUDIO_EDITOR_SCENE || editor == STUDIO_EDITOR_OUTLINER)) {
            studio_frame_selection(s);
        } else if (!mod && shift && key == SDLK_D && editor != STUDIO_EDITOR_PROPERTIES && editor != STUDIO_EDITOR_BEHAVIOR) {
            studio_duplicate(s);
        } else if (!mod && editor == STUDIO_EDITOR_OUTLINER && (key == SDLK_X || key == SDLK_DELETE)) {
            studio_delete(s);
        } else if (!mod && editor == STUDIO_EDITOR_OUTLINER && (key == SDLK_UP || key == SDLK_DOWN)) {
            step_selection(s, key == SDLK_DOWN ? 1 : -1);
        }
    }
}
