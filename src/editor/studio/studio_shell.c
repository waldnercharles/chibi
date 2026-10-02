#include "editor/studio/studio_internal.h"
#include "editor/workspace/asset_index.h"
#include "support/array.h"
#include "support/text_file.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>
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
    "Scene",  "Behavior",    "Outliner", "Properties",  "Output",
    "Assets", "Performance", "Members",  "Text Editor", "Panel"
};
static const uint32_t EDITOR_ICONS[STUDIO_EDITOR_COUNT] = {
    KIT_ICON_VIEWPORT,    KIT_ICON_GRAPH,    KIT_ICON_OUTLINER,
    KIT_ICON_PROPERTIES,  KIT_ICON_OUTPUT,   KIT_ICON_ASSETS,
    KIT_ICON_PERFORMANCE, KIT_ICON_VARIABLE, KIT_ICON_EXPRESSION,
    KIT_ICON_LAYOUT,
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
    studio_editor editor = s->prefs.layouts[s->prefs.workspace].editors[s->focus_area];
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

void studio_save(studio *s)
{
    app_scene_authoring *scene = s->scene;
    /* Text saves while the scene runs, as it applies live. */
    /* Selecting a document tab changes the active document but does not
       necessarily move focus away from the last editor area. The selected UI
       document owns Save in that state; graph focus owns Save only while a
       different document is active. */
    if (studio_ui_buffer(s) &&
        (!graph_focused(s) || scene->document.kind == APP_EDITOR_DOCUMENT_UI)) {
        studio_code_save(s);
        return;
    }
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
bool studio_can_replay(studio *s, bool redo)
{
    app_scene_authoring *scene = s->scene;
    if (graph_focused(s)) {
        return studio_graph_can_replay(s, redo);
    }
    ui_buffer *text = studio_ui_buffer(s);
    if (text) {
        return redo ? ui_buffer_can_redo(text) : ui_buffer_can_undo(text);
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = selected_prototype(scene);
        return asset && acount(redo ? asset->redo : asset->undo) > 0;
    }
    return redo ? scene_history_can_redo(scene->history)
                : scene_history_can_undo(scene->history);
}

bool studio_active_dirty(studio *s)
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

void studio_history(studio *s, bool redo)
{
    ui_buffer *text = graph_focused(s) ? NULL : studio_ui_buffer(s);
    /* Text replays while the scene runs, as it applies live. */
    if (text && studio_can_replay(s, redo)) {
        if (redo) {
            ui_buffer_redo(text);
        } else {
            ui_buffer_undo(text);
        }
        studio_report(s, redo ? "Redo" : "Undo");
        return;
    }
    if (!studio_editable(s) || !studio_can_replay(s, redo)) {
        return;
    }
    if (graph_focused(s)) {
        studio_graph_history(s, redo);
        studio_report(s, redo ? "Redo" : "Undo");
        return;
    }
    document_history(s, redo);
}

bool studio_session(const studio *s)
{
    return runtime_session(s->scene) || s->guest != NULL;
}

/* The Studio project's game is Studio: Play there runs a guest Studio in
   the Scene area, and Play again or Stop ends it, as Unity's Play toggles. */
static bool runs_guest(const studio *s, app_runtime_state state)
{
    return s->own_project && state.target == APP_RUNTIME_SCENE &&
           !state.playing && !runtime_session(s->scene);
}

void studio_record_gameplay(studio *s)
{
    if (!app_scenario_recording(s->scene)) {
        if (app_scenario_record(s->scene) != 0) {
            toast(s, KIT_TOAST_ERROR, "Could not record gameplay", app_scene_authoring_message(s->scene));
        }
        return;
    }
    char name[128];
    if (app_scenario_save(s->scene, name, sizeof name) != 0) {
        toast(s, KIT_TOAST_ERROR, "Could not save the recording", NULL);
        return;
    }
    toast(s, KIT_TOAST_SUCCESS, "Saved gameplay recording", name);
}

void studio_replay_scenario(studio *s, const char *name)
{
    if (app_scenario_replay(s->scene, name) != 0) {
        toast(s, KIT_TOAST_ERROR, "Could not replay scenario", app_scene_authoring_message(s->scene));
    }
}

void studio_runtime(studio *s, app_runtime_command command)
{
    /* Play resumes a run paused at a breakpoint (Unreal). */
    if (command == APP_RUNTIME_PLAY && studio_debug(s, STUDIO_RESUME)) {
        return;
    }
    /* Stop keeps a recording and ends a replay. */
    if (command == APP_RUNTIME_STOP) {
        if (app_scenario_recording(s->scene)) {
            studio_record_gameplay(s);
        }
        app_scenario_stop(s->scene);
    }
    finish_transform_edit(s->scene, false);
    app_runtime_state state = app_scene_authoring_runtime_state(s->scene);
    if (s->guest && command != APP_RUNTIME_STEP) {
        studio_guest_stop(s);
        return;
    }
    if (command == APP_RUNTIME_PLAY && runs_guest(s, state)) {
        studio_guest_start(s);
        return;
    }
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
    studio_layout *layout = studio_active_layout(s);
    bool showing = layout->drawer_open && layout->editors[STUDIO_AREA_DRAWER] == editor;
    layout->editors[STUDIO_AREA_DRAWER] = editor;
    layout->drawer_open = !showing;
}

void studio_toggle_maximized(studio *s, int area)
{
    s->maximized = s->maximized >= 0 ? -1 : area;
}

/* A new arrangement starts unmaximized, with no area focused or hovered
   until the pointer finds one. */
static void forget_areas(studio *s)
{
    s->maximized = s->focus_area = s->hover_area = -1;
    memset(s->areas, 0, sizeof s->areas);
    s->main_rect = s->side_rect = (kit_rect){ 0 };
    s->drawer_dragging = s->side_dragging = false;
}

void studio_reset_layout(studio *s)
{
    *studio_active_layout(s) = studio_layout_default(s->prefs.workspace);
    forget_areas(s);
}

void studio_set_workspace(studio *s, studio_workspace workspace)
{
    assert(workspace >= 0 && workspace < STUDIO_WORKSPACE_COUNT);
    if (s->prefs.workspace == workspace) {
        return;
    }
    s->prefs.workspace = workspace;
    forget_areas(s);
}

void studio_toggle_side_pane(studio *s)
{
    studio_layout *layout = studio_active_layout(s);
    layout->side_open = !layout->side_open;
    s->maximized = -1;
}

/* The menus' UI Scale choices. */
static const float UI_SCALES[] = { 0.5f, 0.75f, 1, 1.25f, 1.5f, 2 };
enum
{
    UI_SCALE_COUNT = sizeof UI_SCALES / sizeof *UI_SCALES,
};

/* The settings table's UI Scale row, whose presets are its steps. */
static const studio_setting *ui_scale_setting()
{
    for (int i = 0; i < studio_setting_count; i++) {
        if (studio_settings[i].offset == offsetof(studio_prefs, ui_scale)) {
            return &studio_settings[i];
        }
    }
    assert(!"the settings table has UI Scale");
    return NULL;
}

void studio_zoom(studio *s, int step)
{
    const studio_setting *setting = ui_scale_setting();
    int count = setting->choice_count;
    float scale = s->prefs.ui_scale, next = step ? scale : setting->fallback;
    /* The last match is the nearest step: up from the top, down from the bottom. */
    for (int i = 0; i < count; i++) {
        float up = strtof(setting->choices[count - 1 - i].value, NULL),
              down = strtof(setting->choices[i].value, NULL);
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
    kit_menu_group(s->kit, "scale", "UI Scale");
    studio_command_item(s, STUDIO_COMMAND_ZOOM_IN);
    studio_command_item(s, STUDIO_COMMAND_ZOOM_OUT);
    studio_command_item(s, STUDIO_COMMAND_ZOOM_RESET);
}

/* Export archives saved files and import reloads the page, so both need
   saved work; only the browser without a local server offers them. */
static void web_project_items(studio *s)
{
#ifdef __EMSCRIPTEN__
    kit *k = s->kit;
    if (!web_projects_available()) {
        return;
    }
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
#else
    (void)s;
#endif
}

/* ---------- Menubar ---------- */

/* An entry the menu names but Studio lacks, shown so its author sees it. */
static void unknown_entry(kit *k, const char *key, const char *what, const char *name)
{
    char label[160];
    snprintf(label, sizeof label, "No %s \"%s\"", what, name);
    item(k, key, label, KIT_ICON_WARNING, NULL, true);
}

static void add_submenu(studio *s)
{
    if (kit_submenu_begin(s->kit, "add", "Add", KIT_ICON_ADD)) {
        add_menu_items(s);
        kit_submenu_end(s->kit);
    }
}

/* The lists Studio fills where a menu names them with <li list="...">. */
static void menu_list(studio *s, const char *key, const char *name)
{
    kit *k = s->kit;
    if (!strcmp(name, "projects")) {
        studio_projects_menu(s);
    } else if (!strcmp(name, "newProject")) {
        studio_new_project_menu(s);
    } else if (!strcmp(name, "webProjects")) {
        web_project_items(s);
    } else if (!strcmp(name, "add")) {
        add_submenu(s);
    } else if (!strcmp(name, "uiScale")) {
        zoom_items(s);
    } else if (!strcmp(name, "debugger")) {
        studio_debug_menu(s);
    } else {
        unknown_entry(k, key, "list", name);
    }
}

static bool entry_attribute(rml_element *entry, const char *name, char *value, size_t size)
{
    return rml_element_attribute(entry, name, value, size) > 0;
}

/* A menu's entries in order: <li command="id"> runs a command by its id,
   <li list="name"> is a list Studio fills, <li label="..."> heads a group,
   <hr/> separates and a <menu label> inside opens a submenu. */
static void menu_entries(studio *s, rml_element *menu)
{
    kit *k = s->kit;
    for (int i = 0; i < rml_element_child_count(menu); i++) {
        rml_element *entry = rml_element_child(menu, i);
        const char *tag = rml_element_tag_name(entry);
        char key[16], value[128];
        snprintf(key, sizeof key, "e%d", i);
        if (!strcmp(tag, "hr")) {
            kit_menu_separator(k, key);
        } else if (!strcmp(tag, "menu") && entry_attribute(entry, "label", value, sizeof value)) {
            if (kit_submenu_begin(k, key, value, 0)) {
                menu_entries(s, entry);
                kit_submenu_end(k);
            }
        } else if (entry_attribute(entry, "command", value, sizeof value)) {
            int command = studio_command_named(value);
            if (command < 0) {
                unknown_entry(k, key, "command", value);
            } else {
                studio_command_item(s, (studio_command)command);
            }
        } else if (entry_attribute(entry, "list", value, sizeof value)) {
            menu_list(s, key, value);
        } else if (entry_attribute(entry, "label", value, sizeof value)) {
            kit_menu_group(k, key, value);
        }
    }
}

/* The menubar is authored: the top bar's #menus holds a <menu label> per
   menu, whose entries name commands by id, as VS Code's menus contribute
   them; the menus themselves are native, in the #menubar slot. */
static void menus(studio *s, rml_element *host)
{
    rml_element *bar = rml_element_query_selector(host, "#menus");
    for (int i = 0; bar && i < rml_element_child_count(bar); i++) {
        rml_element *menu = rml_element_child(bar, i);
        char key[64], label[64];
        if (strcmp(rml_element_tag_name(menu), "menu") ||
            !entry_attribute(menu, "label", label, sizeof label)) {
            continue;
        }
        /* Keyed by label, "file" for File, so paths name the menu. */
        for (size_t c = 0; c < sizeof key; c++) {
            key[c] = (char)tolower((unsigned char)label[c]);
            if (!label[c]) {
                break;
            }
        }
        if (kit_menubar_trigger(s->kit, key, label)) {
            menu_entries(s, menu);
            kit_popup_end(s->kit);
        }
    }
}

/* ---------- Document tabs ---------- */

static uint32_t tab_icon(const scene_document_tab *tab)
{
    switch (tab->kind) {
        case APP_EDITOR_DOCUMENT_SCENE: return KIT_ICON_SCENE;
        case APP_EDITOR_DOCUMENT_PROTOTYPE: return KIT_ICON_PROTOTYPE;
        case APP_EDITOR_DOCUMENT_BEHAVIOR: return KIT_ICON_GRAPH;
        case APP_EDITOR_DOCUMENT_SYSTEM: return KIT_ICON_SYSTEM;
        case APP_EDITOR_DOCUMENT_UI:
            return tab->ui->stylesheet ? KIT_ICON_FILE : KIT_ICON_LAYOUT;
    }
    return KIT_ICON_FILE;
}

/* Every document but a scene closes from its tab, as in VS Code; a scene is
   the root the other documents open over. */
bool studio_tab_closes(const studio *s, int tab)
{
    return tab >= 0 && tab < acount(s->scene->tabs) &&
           s->scene->tabs[tab].kind != APP_EDITOR_DOCUMENT_SCENE;
}

/* The top bar's TopBar lists these from `editor.tabs`; a tab runs
   selectTab and its close button closeTab, with the tab's index. */
studio_tab_view *studio_document_tabs(studio *s)
{
    app_scene_authoring *scene = s->scene;
    studio_tab_view *tabs = NULL;
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        studio_tab_view shown = {
            .label = tab->label ? tab->label : "Untitled",
            .path = tab->path ? tab->path : "",
            .icon = tab_icon(tab),
            .dirty = scene_tabs_dirty(scene, tab),
            .active = i == scene->active_tab,
            .closable = studio_tab_closes(s, i),
        };
        apush(tabs, shown);
    }
    return tabs;
}

/* ---------- Top bar ---------- */

/* What Play runs: the scene, or a preview of the selected prototype. Fixed
   while a session runs; the label morphs when the target changes. Native in
   the top bar's #play-target slot. */
static void runtime_target(studio *s)
{
    kit *k = s->kit;
    app_scene_authoring *scene = s->scene;
    app_runtime_state runtime = app_scene_authoring_runtime_state(scene);
    bool session = studio_session(s);
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

static void layout_control(studio *s)
{
    kit *k = s->kit;
    kit_button_desc button = {
        .icon = KIT_ICON_LAYOUT,
        .tip = "Layout",
    };
    if (kit_button(k, "trigger", &button)) {
        if (kit_popup_is_open(k, "layouts")) {
            kit_popup_close(k, "layouts");
        } else {
            kit_popup_open(k, "layouts");
        }
    }
    if (!kit_popup_begin(k, "layouts", "menu")) {
        return;
    }
    kit_menu_group(k, "title", "Layout");
    const studio_command commands[] = {
        STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT,
        STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT,
    };
    for (int i = 0; i < 2; i++) {
        studio_command_info command = studio_command_describe(s, commands[i]);
        kit_menu_item_desc choice = {
            .label = command.menu,
            .icon = command.icon,
            .checked = studio_command_checked(s, commands[i]),
        };
        if (kit_menu_item(k, command.id, &choice)) {
            studio_command_run(s, commands[i]);
        }
    }
    kit_popup_end(k);
}

/* The top bar is authored: projects/studio's TopBar binds `editor` and
   `commands` (studio_sources.c) for its document tabs, transport, gear and
   search, which run commands. The menubar, the play target's menu and a
   paused run's steps stay native in its slots; a run paused at a breakpoint
   trades the frame step for the debugger's steps (Unreal). Chrome controls
   retain focus while the host routes global shortcuts. */
static void topbar(studio *s)
{
    kit *k = s->kit;
    /* The gear's Settings page is an authored dialog. */
    studio_ui_modal(s, "Settings");
    rml_element *host = studio_ui_panel(s, "topbar", "TopBar");
    if (!host) {
        return;
    }
    if (kit_slot_begin(k, "menus", host, "#menubar")) {
        menus(s, host);
        kit_slot_end(k);
    }
    if (kit_slot_begin(k, "layout", host, "#layout")) {
        layout_control(s);
        kit_slot_end(k);
    }
    if (kit_slot_begin(k, "target", host, "#play-target")) {
        runtime_target(s);
        kit_slot_end(k);
    }
    if (s->pause.active && kit_slot_begin(k, "steps", host, "#debug-steps")) {
        studio_debug_steps(s);
        kit_slot_end(k);
    }
}

/* ---------- Areas ---------- */

/* Drawers hold panels, never a second Scene or graph. */
static bool drawer_editor(studio_editor editor)
{
    return editor == STUDIO_EDITOR_OUTPUT || editor == STUDIO_EDITOR_ASSETS ||
           editor == STUDIO_EDITOR_PERFORMANCE ||
           editor == STUDIO_EDITOR_PANEL || editor == STUDIO_EDITOR_CODE;
}

/* The Studio project's panels, after the built-in editors. */
static void panel_types(studio *s, int area)
{
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
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
            .current = layout->editors[area] == STUDIO_EDITOR_PANEL &&
                       !strcmp(layout->panels[area], name),
        };
        if (kit_menu_item(k, item_key, &item)) {
            layout->editors[area] = STUDIO_EDITOR_PANEL;
            snprintf(layout->panels[area], sizeof layout->panels[area], "%s", name);
        }
    }
}

void studio_maximize_graph(studio *s)
{
    studio_layout *layout = studio_active_layout(s);
    int area = STUDIO_AREA_TOP;
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        area = layout->editors[i] == STUDIO_EDITOR_BEHAVIOR ? i : area;
    }
    layout->editors[area] = STUDIO_EDITOR_BEHAVIOR;
    s->applied.layouts[s->prefs.workspace].editors[area] = STUDIO_EDITOR_BEHAVIOR;
    s->maximized = area;
}

static void editor_type_menu(studio *s, int area)
{
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
    studio_editor *editor = &layout->editors[area];
    kit_button_desc d = { .icon = EDITOR_ICONS[*editor],
                          .cls = "editor-type",
                          .tip = *editor == STUDIO_EDITOR_PANEL
                                   ? layout->panels[area]
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

/* The editor's own header controls. */
static void editor_header(studio *s, int index)
{
    switch (studio_active_layout(s)->editors[index]) {
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
        case STUDIO_EDITOR_CODE: studio_code_header(s, index); break;
        case STUDIO_EDITOR_PANEL:
            kit_label(s->kit, "title", NULL, studio_active_layout(s)->panels[index]);
            break;
        default: break;
    }
}

/* The area header is authored: projects/studio's AreaHeader, one per area,
   binds its `area` (studio_sources.c) to maximize it and to close the
   drawer. The editor type menu and the editor's own controls stay native in
   its slots. Fields keep their keys while chrome controls allow global
   shortcuts through the host. */
static void area_header(studio *s, int index)
{
    kit *k = s->kit;
    rml_element *host = studio_ui_area_header(s, index, "AreaHeader");
    if (!host) {
        return;
    }
    if (kit_slot_begin(k, "type", host, "#editor-type")) {
        editor_type_menu(s, index);
        kit_slot_end(k);
    }
    if (kit_slot_begin(k, "controls", host, "#controls")) {
        editor_header(s, index);
        kit_slot_end(k);
    }
}

/* An area `size` dp along `axis` (0 across, 1 down), or filling what its
   row or column leaves. */
static void area(studio *s, int index, const char *key, int axis, float size, bool fill)
{
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
    studio_editor editor = layout->editors[index];
    kit_push(k, key, NULL, KIT_CX(k, "area", { "fill", fill }, { "drawer", index == STUDIO_AREA_DRAWER }));
    /* Styles persist across frames, so the other axis and a filling area
       reset theirs as a workspace changes. */
    char extent[32];
    dp(extent, sizeof extent, size);
    kit_style(k, "width", !fill && !axis ? extent : "auto");
    kit_style(k, "height", !fill && axis ? extent : "auto");
    area_header(s, index);
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
        case STUDIO_EDITOR_CODE: studio_code_body(s, index); break;
        case STUDIO_EDITOR_PANEL:
            studio_ui_area(s, index, layout->panels[index]);
            break;
        default: break;
    }
    kit_pop(k);
    kit_pop(k);
    s->areas[index].rect = kit_item_rect(k);
}

/* The top and bottom areas: stacked vertically or arranged side by side. */
static void main_areas(studio *s, float size)
{
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
    int axis = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT ? 0 : 1;
    float minimum = axis ? 80 : 160;
    kit_push(k, "editors", NULL, KIT_CX(k, "main-editors", { "across", !axis }));
    float first = floorf((size - 4) * layout->main_split);
    area(s, STUDIO_AREA_TOP, "top", axis, first, false);
    if (kit_splitter(k, "split", axis, &first, minimum, fmaxf(minimum, size - minimum - 4), false)) {
        layout->main_split = first / fmaxf(1, size - 4);
    }
    area(s, STUDIO_AREA_BOTTOM, "bottom", axis, 0, true);
    kit_pop(k);
}

/* The right column eases closed and open, at its last width, and follows
   its splitter directly while dragged. */
static void side_areas(studio *s)
{
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
    float target = layout->side_open ? layout->side_width : 0;
    float width = kit_spring(k, "side-width", target, s->side_dragging ? 0 : 0.2f);
    if (width <= 0.5f) {
        studio_ui_area_detach(s, STUDIO_AREA_OUTLINER);
        studio_ui_area_detach(s, STUDIO_AREA_PROPERTIES);
        s->side_rect = s->areas[STUDIO_AREA_OUTLINER]
                           .rect = s->areas[STUDIO_AREA_PROPERTIES].rect = (kit_rect){ 0 };
        s->side_dragging = false;
        return;
    }
    kit_splitter(k, "split", 0, &layout->side_width, 260, fmaxf(260, s->in.width - 320), true);
    s->side_dragging = kit_item_active(k);
    kit_push(k, "side", NULL, "column side");
    char extent[32];
    dp(extent, sizeof extent, width);
    kit_style(k, "width", extent);
    float height = s->side_rect.h > 0 ? s->side_rect.h : s->in.height - 60;
    float outliner = floorf((height - 4) * layout->side_split);
    area(s, STUDIO_AREA_OUTLINER, "outliner", 1, outliner, false);
    if (kit_splitter(k, "split", 1, &outliner, 80, fmaxf(80, height - 120), false)) {
        layout->side_split = outliner / fmaxf(1, height - 4);
    }
    area(s, STUDIO_AREA_PROPERTIES, "properties", 1, 0, true);
    kit_pop(k);
    s->side_rect = kit_item_rect(k);
}

static void workspace(studio *s)
{
    static const char *const AREA_KEYS[STUDIO_AREA_COUNT] = { "top",
                                                              "bottom",
                                                              "outliner",
                                                              "properties",
                                                              "drawer" };
    kit *k = s->kit;
    studio_layout *layout = studio_active_layout(s);
    kit_push(k, "workspace", "main", "workspace");
    kit_push(k, "main", NULL, "column main");
    if (s->maximized >= 0) {
        memset(s->areas, 0, sizeof s->areas);
        for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
            if (i != s->maximized) {
                studio_ui_area_detach(s, i);
            }
        }
        area(s, s->maximized, AREA_KEYS[s->maximized], 1, 0, true);
        kit_pop(k);
        s->main_rect = kit_item_rect(k);
        kit_pop(k);
        return;
    }
    float column_height = s->main_rect.h > 0 ? s->main_rect.h : s->in.height - 60;
    /* The drawer eases open in 200 ms and closed in 100 ms, and follows its
       splitter directly while dragged. */
    float drawer_target = layout->drawer_open
                            ? fminf(layout->drawer_height, fmaxf(80, column_height - 200))
                            : 0;
    float drawer = kit_spring(
        k,
        "drawer",
        drawer_target,
        s->drawer_dragging    ? 0
        : layout->drawer_open ? 0.2f
                              : 0.1f
    );
    bool drawer_shown = drawer > 0.5f;
    float main_width = s->main_rect.w > 0 ? s->main_rect.w : s->in.width;
    main_areas(
        s,
        s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT
            ? main_width
            : column_height - (drawer_shown ? drawer + 4 : 0)
    );
    if (drawer_shown) {
        float height = layout->drawer_height;
        if (kit_splitter(k, "drawer-split", 1, &height, 80, fmaxf(80, column_height - 200), true)) {
            layout->drawer_height = height;
        }
        s->drawer_dragging = kit_item_active(k);
        area(s, STUDIO_AREA_DRAWER, "drawer", 1, drawer, false);
    } else {
        studio_ui_area_detach(s, STUDIO_AREA_DRAWER);
        s->areas[STUDIO_AREA_DRAWER].rect = (kit_rect){ 0 };
        s->drawer_dragging = false;
    }
    kit_pop(k);
    s->main_rect = kit_item_rect(k);
    side_areas(s);
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

/* Authoring messages report like any other operation. */
static void follow_messages(studio *s)
{
    if (!strcmp(s->scene->message, s->seen_message)) {
        return;
    }
    snprintf(s->seen_message, sizeof s->seen_message, "%s", s->scene->message);
    if (s->seen_message[0]) {
        studio_report(s, s->seen_message);
    }
}

/* The status bar is authored: projects/studio's StatusBar binds `editor`
   (studio_sources.c) and runs commands. UI Scale stays native, with its
   menu, in the bar's #ui-scale slot. Controls retain focus while the host
   routes global shortcuts. */
static void statusbar(studio *s)
{
    follow_messages(s);
    rml_element *host = studio_ui_panel(s, "status", "StatusBar");
    if (!host) {
        return;
    }
    if (kit_slot_begin(s->kit, "scale", host, "#ui-scale")) {
        scale_control(s);
        kit_slot_end(s->kit);
    }
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
        /* Shell controls act on the last editor. A popup can overlap another
           pane geometrically without transferring its command context. */
        const char *path = kit_hovered_path(s->kit);
        bool overlay = path && !strncmp(path, "layer/", 6);
        if (s->hover_area >= 0 && !overlay && !kit_command_is_open(s->kit)) {
            s->focus_area = s->hover_area;
        }
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
    studio_code_frame(s);
    studio_canvas_follow(s);
    track_pointer(s);
    kit_root(s->kit, "app");
    topbar(s);
    workspace(s);
    /* Visual designer (workstream H): the Text Editor's cursor and the Canvas. */
    studio_designer_frame(s);
    statusbar(s);
    studio_palette(s);
    studio_new_asset_frame(s);
    undo_toast(s);
    studio_projects_frame(s);
    studio_rewire_toast(s);
    studio_members_toast(s);
    studio_refactor_frame(s);
    studio_component_toast(s);
    studio_new_project_frame(s);
}
