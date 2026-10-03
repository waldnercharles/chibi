/* Studio editors for the authored scene: viewport and Outliner. */
#include "editor/studio/studio_internal.h"
#include "editor/ui/input_feel.h"
#include "support/array.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool studio_editable(const studio *s)
{
    return scene_can_move(s->scene) && !s->scene->runtime_snapshot_valid;
}

bool studio_live(const studio *s)
{
    return s->scene->runtime_snapshot_valid;
}

bool studio_matches(const char *query, const char *text)
{
    if (!query[0]) {
        return true;
    }
    for (const char *h = text; *h; h++) {
        size_t i = 0;
        while (query[i] && h[i] &&
               tolower((unsigned char)query[i]) == tolower((unsigned char)h[i])) {
            i++;
        }
        if (!query[i]) {
            return true;
        }
    }
    return false;
}

/* ---------- View zoom ---------- */

/* The view zoom magnifies the Scene's rendered frame, game UI included, as
 * Unity's Game view Scale does; the wheel's camera zoom moves through the
 * world instead. The frame renders sharper up to VIEW_SHARP_MAX, and beyond
 * it its pixels magnify, so the canvas stays bounded. */
#define VIEW_ZOOM_MAX 8.0f
#define VIEW_SHARP_MAX 2.0f

static const float VIEW_ZOOMS[] = { 1, 2, 4, 8 };

/* Shows the frame at `zoom` around (x, y), kept inside the frame. */
static void place_view(studio *s, float zoom, float x, float y)
{
    float half = 0.5f / zoom;
    s->view.zoom = zoom;
    s->view.x = fminf(fmaxf(x, half), 1 - half);
    s->view.y = fminf(fmaxf(y, half), 1 - half);
}

/* Zooms keeping the frame point at (u, v), fractions of the area, in place. */
static void zoom_view(studio *s, float zoom, float u, float v)
{
    float next = fminf(fmaxf(zoom, 1), VIEW_ZOOM_MAX);
    float shift = 1 / s->view.zoom - 1 / next;
    place_view(s, next, s->view.x + (u - 0.5f) * shift, s->view.y + (v - 0.5f) * shift);
}

/* A pinch, or Mod or Ctrl with the wheel, zooms the view toward the pointer,
 * as browsers zoom a page, and while the view magnifies the middle button
 * pans it. */
void studio_scene_view(studio *s, engine_viewport_host_input *input)
{
    float u = s->surface.x / s->surface.rect.w,
          v = s->surface.y / s->surface.rect.h;
    if (s->surface.hovered && (s->zoom_wheel != 0 || s->pinch != 0)) {
        zoom_view(s, s->view.zoom * feel_zoom_scale(s->zoom_wheel) * expf(s->pinch), u, v);
        input->wheel -= s->zoom_wheel;
    }
    float zoom = s->view.zoom;
    s->view.panning = zoom > 1 && input->down[2] &&
                      (s->view.panning || input->pressed[2]);
    if (s->view.panning && !input->pressed[2]) {
        place_view(
            s,
            zoom,
            s->view.x - (u - s->view.u) / zoom,
            s->view.y - (v - s->view.v) / zoom
        );
    }
    s->view.u = u;
    s->view.v = v;
    if (zoom > 1) {
        input->down[2] = input->pressed[2] = input->released[2] = false;
    }
    input->x = (s->view.x + (u - 0.5f) / zoom) * input->width;
    input->y = (s->view.y + (v - 0.5f) / zoom) * input->height;
    input->resolution = fminf(zoom, VIEW_SHARP_MAX);
}

/* The View Zoom choices, in a menu the caller has open. */
static void view_zoom_items(studio *s)
{
    kit *k = s->kit;
    kit_menu_group(k, "zoom-group", "View Zoom");
    for (size_t i = 0; i < sizeof VIEW_ZOOMS / sizeof *VIEW_ZOOMS; i++) {
        char item_key[8], item_label[8];
        snprintf(item_key, sizeof item_key, "z%zu", i);
        snprintf(item_label, sizeof item_label, "%d%%", (int)(VIEW_ZOOMS[i] * 100));
        kit_menu_item_desc item = { .label = item_label,
                                    .checked = fabsf(s->view.zoom - VIEW_ZOOMS[i]) <
                                               0.005f };
        if (kit_menu_item(k, item_key, &item)) {
            zoom_view(s, VIEW_ZOOMS[i], 0.5f, 0.5f);
        }
    }
}

static void view_zoom_menu(studio *s, int area)
{
    kit *k = s->kit;
    char key[24], label[16];
    snprintf(key, sizeof key, "zoom:%d", area);
    snprintf(label, sizeof label, "%d%%", (int)lroundf(s->view.zoom * 100));
    kit_button_desc zoom = { .label = label,
                             .cls = "menu-btn",
                             .tip = "View Zoom",
                             .shortcut = studio_chord(s, "Mod+Scroll") };
    if (kit_button(k, "zoom", &zoom)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        view_zoom_items(s);
        kit_popup_end(k);
    }
}

/* ---------- Scene viewport ---------- */

/* View › Show: what the edited scene shows of the game (Unreal's Show
   flags), each a command over a preference. */
static void show_items(studio *s)
{
    kit_menu_group(s->kit, "show-group", "Show");
    studio_command_item(s, STUDIO_COMMAND_TOGGLE_GRID);
    studio_command_item(s, STUDIO_COMMAND_SHOW_WORLD);
    studio_command_item(s, STUDIO_COMMAND_SHOW_LIGHTING);
    studio_command_item(s, STUDIO_COMMAND_SHOW_FOG);
    studio_command_item(s, STUDIO_COMMAND_SHOW_CAMERAS);
}

static bool canvas_header(studio *s, int area);
static bool canvas_body(studio *s);

/* The settings table's Grid Increment row, whose presets are its choices. */
static const studio_setting *increment_setting()
{
    for (int i = 0; i < studio_setting_count; i++) {
        if (studio_settings[i].offset == offsetof(studio_prefs, scene_increment)) {
            return &studio_settings[i];
        }
    }
    assert(!"the settings table has Grid Increment");
    return NULL;
}

float studio_grid_increment(const studio *s, int step)
{
    const studio_setting *setting = increment_setting();
    float current = s->prefs.scene_increment, next = current;
    for (int i = 0; i < setting->choice_count; i++) {
        float up = strtof(setting->choices[setting->choice_count - 1 - i].value, NULL);
        float down = strtof(setting->choices[i].value, NULL);
        next = step > 0 && up > current ? up : next;
        next = step < 0 && down < current ? down : next;
    }
    return next;
}

/* Snapping and the grid increment, as Unity's grid snapping toolbar: the
   magnet toggles it, and the increment beside it chooses the step. */
static void snap_controls(studio *s, int area)
{
    kit *k = s->kit;
    kit_push(k, "snapping", NULL, "snap-controls");
    studio_command_info snap = studio_command_describe(s, STUDIO_COMMAND_TOGGLE_SNAP);
    kit_button_desc toggle = { .icon = snap.icon,
                               .tip = snap.label,
                               .shortcut = snap.shortcut,
                               .flags = s->prefs.scene_snap ? KIT_ON : 0 };
    if (kit_button(k, "snap", &toggle)) {
        studio_command_run(s, STUDIO_COMMAND_TOGGLE_SNAP);
    }
    char key[24], label[16];
    snprintf(key, sizeof key, "increment:%d", area);
    snprintf(label, sizeof label, "%g", s->prefs.scene_increment);
    kit_button_desc increment = { .label = label, .cls = "menu-btn", .tip = "Grid Increment" };
    if (kit_button(k, "increment", &increment)) {
        kit_popup_open(k, key);
    }
    kit_pop(k);
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    const studio_setting *setting = increment_setting();
    kit_menu_group(k, "group", "Grid Increment");
    for (int i = 0; i < setting->choice_count; i++) {
        float value = strtof(setting->choices[i].value, NULL);
        kit_menu_item_desc item = { .label = setting->choices[i].label,
                                    .checked = value == s->prefs.scene_increment };
        if (kit_menu_item(k, setting->choices[i].value, &item)) {
            s->prefs.scene_increment = value;
        }
    }
    kit_popup_end(k);
}

/* A tool in the header's group: pressed while active, its name and key in
   its tip. */
static void tool_button(studio *s, studio_command command)
{
    studio_command_info info = studio_command_describe(s, command);
    kit_button_desc button = { .icon = info.icon,
                               .tip = info.menu,
                               .shortcut = info.shortcut,
                               .flags = studio_command_checked(s, command) > 0 ? KIT_ON
                                                                               : 0 };
    if (kit_button(s->kit, info.id, &button)) {
        studio_command_run(s, command);
    }
}

/* The transform tools sit in the header, as Unreal's and Godot's viewport
   toolbars hold them, so nothing covers the game's HUD in the view. */
static void tool_group(studio *s)
{
    kit_push(s->kit, "tools", NULL, "tool-group");
    for (int tool = 0; tool < SCENE_TOOL_COUNT; tool++) {
        tool_button(s, STUDIO_COMMAND_SELECT_TOOL + tool);
    }
    kit_pop(s->kit);
}

void studio_scene_header(studio *s, int area)
{
    kit *k = s->kit;
    if (s->guest) {
        kit_label(k, "guest", NULL, "Studio");
        kit_label(k, "working", "dim", "Running this project's working copy");
        return;
    }
    if (canvas_header(s, area)) {
        return;
    }
    char key[24];
    snprintf(key, sizeof key, "view:%d", area);
    kit_button_desc view = { .label = "View", .cls = "menu-btn" };
    if (kit_button(k, "view", &view)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_item_desc frame = { .label = "Frame Selected",
                                     .icon = KIT_ICON_FRAME,
                                     .shortcut = "F",
                                     .checked = -1,
                                     .disabled = !selected_actor(s->scene) };
        if (kit_menu_item(k, "frame", &frame)) {
            studio_frame_selection(s);
        }
        kit_menu_item_desc reset = { .label = "Reset Camera", .checked = -1 };
        if (kit_menu_item(k, "reset", &reset)) {
            engine_viewport_reset_camera(s->viewport);
        }
        studio_command_item(s, STUDIO_COMMAND_VIEW_THROUGH_CAMERA);
        kit_menu_separator(k, "s1");
        show_items(s);
        kit_menu_separator(k, "s2");
        view_zoom_items(s);
        kit_popup_end(k);
    }
    tool_group(s);
    kit_leaf(k, "spacer", NULL, "spacer");
    /* View Zoom magnifies the frame, the game's UI too, apart from the
       camera the readout shows; it shows here only while it magnifies, as a
       browser shows its zoom, so one zoom reads at a time. */
    if (fabsf(s->view.zoom - 1) > 0.005f) {
        view_zoom_menu(s, area);
    }
    snap_controls(s, area);
    kit_button_desc frame = { .icon = KIT_ICON_FRAME,
                              .tip = "Frame Selected",
                              .shortcut = "F",
                              .flags = selected_actor(s->scene) ? 0 : KIT_DISABLED };
    if (kit_button(k, "frame", &frame)) {
        studio_frame_selection(s);
    }
}

/* Shows the part of the frame the view zoom does, as fractions of it, on
   the surface emitted last. */
static void crop_view(studio *s)
{
    float half = 0.5f / s->view.zoom;
    char crop[64];
    snprintf(crop, sizeof crop, "%g %g %g %g", s->view.x - half, s->view.y - half, 2 * half, 2 * half);
    kit_attr(s->kit, "rect", crop);
}

#define CAMERA_LABELS_MAX 16

/* Each camera frame's name over its top-left corner, in the frame the view
   zoom shows. */
static void camera_labels(studio *s)
{
    kit *k = s->kit;
    app_scene_camera_label labels[CAMERA_LABELS_MAX];
    int count = app_scene_authoring_camera_labels(s->scene, labels, CAMERA_LABELS_MAX);
    float half = 0.5f / s->view.zoom;
    for (int i = 0; i < count; i++) {
        float x = (labels[i].x - (s->view.x - half)) / (2 * half);
        float y = (labels[i].y - (s->view.y - half)) / (2 * half);
        if (x < 0 || x >= 1 || y < 0 || y >= 1) {
            continue;
        }
        char key[16], value[32];
        snprintf(key, sizeof key, "camera%d", i);
        kit_label(k, key, "camera-label", labels[i].name);
        kit_flag(k, "data-active", labels[i].active);
        kit_flag(k, "data-disabled", labels[i].disabled);
        kit_flag(k, "data-selected", labels[i].selected);
        snprintf(value, sizeof value, "%.1fdp", x * s->surface.rect.w);
        kit_style(k, "left", value);
        snprintf(value, sizeof value, "%.1fdp", y * s->surface.rect.h);
        kit_style(k, "top", value);
    }
}

/* The label naming the Scene's view and selection. */
static void view_overlay(studio *s)
{
    kit *k = s->kit;
    kit_push(k, "label", NULL, "view-label");
    kit_presence(k);
    char title[128], context[256];
    scene_actor *actor = selected_actor(s->scene);
    snprintf(
        context,
        sizeof context,
        "(%s) %s",
        studio_scene_title(s->scene, title, sizeof title),
        actor ? actor->name : "-"
    );
    kit_label(k, "context", "dim", context);
    kit_pop(k);
}

void studio_scene_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    /* A guest Studio fills the area as the Studio project's game, whatever
       document is active. */
    if (s->guest) {
        kit_push(k, "viewport", NULL, "viewport");
        kit_surface(k, "guest", "surface", "guest", &s->surface);
        kit_pop(k);
        return;
    }
    if (canvas_body(s)) {
        return;
    }
    app_scene_authoring *scene = s->scene;
    kit_push(k, "viewport", NULL, "viewport");
    kit_surface(k, "surface", "surface", "viewport", &s->surface);
    crop_view(s);
    /* A prototype dropped here is placed under the pointer by the viewport. */
    kit_drop_target(k, "prototype", s->dropped_prototype, sizeof s->dropped_prototype);
    /* While a session runs the Scene is the game's view, so its tools and
       label leave the top-left corner to the game's HUD. */
    bool session = runtime_session(scene);
    if (!session) {
        camera_labels(s);
        view_overlay(s);
    }
    engine_viewport_camera camera;
    engine_viewport_get_camera(s->viewport, &camera);
    /* Only once the surface is laid out and the scene has drawn: its first
       draw restores or fits the camera after this readout is emitted, so an
       earlier value would be the reset camera's, rolling to the real one. */
    if (s->surface.rect.w > 0 && scene->camera_target >= 0) {
        /* The camera widens the framed span to the area's aspect, so its
           span, not the framed width, sets the pixels per world unit. */
        double zoom = roundf(s->surface.rect.w / fmaxf(camera.span_width, 1) * 100);
        kit_number_flow(
            k,
            "zoom",
            "readout",
            zoom,
            &(kit_number_flow_desc){ .grouping = true, .suffix = "%" }
        );
    }
    if (session) {
        kit_label(k, "live", "badge accent live-badge", "Live");
    }
    if (!session && app_scene_authoring_world_pending(scene)) {
        kit_push(k, "generating", NULL, "world-pending");
        kit_presence(k);
        kit_icon(k, "spin", "spin", KIT_ICON_LOADING);
        kit_label(k, "text", NULL, "Generating world");
        kit_pop(k);
    }
    /* The renderer's error first, then the generated world's, then the game
       UI's (render_ui_error). */
    const char *error = app_scene_renderer_error(scene->renderer);
    const char *world = app_scene_authoring_world_error(scene);
    error = error && error[0] ? error : world[0] ? world : s->ui_error;
    const char *warning = app_scene_renderer_warning(scene->renderer);
    if (error[0]) {
        kit_label(k, "error", "notice", error);
    } else if (warning && warning[0]) {
        kit_label(k, "warning", "notice", "Lighting adjusted");
        kit_tip(k, "Lighting adjusted", NULL, warning);
    }
    kit_pop(k);
}

void studio_scene_tools(studio *s)
{
    scene_gizmo *g = &s->scene->gizmo;
    bool gesture = g->active != SCENE_GIZMO_NONE;
    g->in = (scene_tool_input){
        .tool = s->prefs.tool,
        .grid = s->prefs.scene_grid,
        .snap = s->prefs.scene_snap,
        .increment = s->prefs.scene_increment,
        .shift = (s->mods & SDL_KMOD_SHIFT) != 0,
        .extend = (s->mods & SDL_KMOD_SHIFT) || studio_primary(s, s->mods),
        .ctrl = (s->mods & SDL_KMOD_CTRL) != 0,
        .alt = (s->mods & SDL_KMOD_ALT) != 0,
        .axis_x = g->modal && studio_key_pressed(s, SDLK_X),
        .axis_y = g->modal && studio_key_pressed(s, SDLK_Y),
        .confirm = g->modal && (studio_key_pressed(s, SDLK_RETURN) ||
                                studio_key_pressed(s, SDLK_KP_ENTER)),
        .cancel = gesture &&
                  (studio_key_pressed(s, SDLK_ESCAPE) || s->surface.pressed[1]),
    };
    /* A gesture's change shows live and is reported once it is recorded. */
    if (gesture && g->status[0]) {
        snprintf(s->status, sizeof s->status, "%s", g->status);
        s->status_time = s->time;
        s->scene_status = true;
    }
    if (g->report[0]) {
        studio_report(s, g->report);
        g->report[0] = '\0';
        s->scene_status = false;
    }
    if (!gesture && s->scene_status) {
        s->status[0] = '\0';
        s->scene_status = false;
    }
}

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
            finish_transform_edit(scene, false);
            select_instance(scene, i);
            if (app_scene_authoring_set_instance_behavior(scene, dropped, true) == 0) {
                char message[320];
                snprintf(
                    message,
                    sizeof message,
                    "Attached %s to %s",
                    dropped,
                    actor->name
                );
                studio_report(s, message);
            }
        }
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
        }
        studio_command_item(s, STUDIO_COMMAND_HIDE_SELECTED);
        kit_menu_separator(k, "s1");
        studio_command_item(s, STUDIO_COMMAND_DELETE);
        kit_popup_end(k);
    }
}

/* ---------- Canvas preview ---------- */

/* Where a ui asset is used: GameUi fields and <use ui> from the index, and
   the scene's and the project's root document. A root document's row opens
   the Scene tab of Properties, where it is chosen. */
typedef struct
{
    char label[288];
    uint32_t icon;
    bool page;
    studio_reference at;
} canvas_user;

static canvas_user *canvas_users(studio *s, const char *asset)
{
    app_scene_authoring *scene = s->scene;
    canvas_user *users = NULL;
    char title[128];
    if (scene->ui && !strcmp(scene->ui, asset)) {
        canvas_user user = { .icon = KIT_ICON_SCENE, .page = true };
        snprintf(
            user.label,
            sizeof user.label,
            "%s \xe2\x80\xba ui",
            studio_scene_title(scene, title, sizeof title)
        );
        apush(users, user);
    }
    if (scene->default_ui && !strcmp(scene->default_ui, asset)) {
        apush(
            users,
            ((canvas_user){ .label = "Project \xe2\x80\xba ui",
                            .icon = KIT_ICON_FOLDER,
                            .page = true })
        );
    }
    const reference_index *index = app_scene_authoring_asset_index(scene);
    int at = index ? reference_index_find_name(index, REFERENCE_ASSET_UI, asset) : -1;
    const reference **uses = NULL;
    if (at >= 0) {
        reference_index_find_uses(
            index,
            (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = at },
            &uses
        );
    }
    for (int i = 0; i < acount(uses); i++) {
        canvas_user user = { .at = studio_reference_of(index, uses[i]) };
        user.icon = studio_asset_kind_icon(user.at.kind);
        snprintf(
            user.label,
            sizeof user.label,
            user.at.place[0] ? "%s \xe2\x80\xba %s" : "%s",
            user.at.asset,
            user.at.place
        );
        apush(users, user);
    }
    afree(uses);
    return users;
}

/* A widget's document previews in its place; anything else opens and the
   Scene area returns to the scene. */
static void open_user(studio *s, const canvas_user *user)
{
    if (user->page) {
        s->tab = STUDIO_TAB_SCENE;
        studio_show_editor(s, STUDIO_EDITOR_PROPERTIES);
        return;
    }
    if (user->at.kind == REFERENCE_ASSET_UI) {
        studio_preview_ui(s, user->at.asset);
        return;
    }
    studio_preview_ui(s, NULL);
    studio_open_reference(s, &user->at);
}

/* The previewed asset's crumb: what uses it, and the other ui assets to
   preview instead. */
static void asset_crumb(studio *s, const char *asset)
{
    kit *k = s->kit;
    kit_button_desc crumb = { .label = asset,
                              .icon = studio_asset_kind_icon(REFERENCE_ASSET_UI),
                              .cls = "crumb current" };
    if (kit_button(k, "asset", &crumb)) {
        kit_popup_open(k, "canvas-asset");
    }
    if (!kit_popup_begin(k, "canvas-asset", "menu")) {
        return;
    }
    canvas_user *users = canvas_users(s, asset);
    kit_menu_group(k, "used", "Used by");
    for (int i = 0; i < acount(users); i++) {
        char key[16];
        snprintf(key, sizeof key, "u%d", i);
        kit_menu_item_desc d = { .label = users[i].label,
                                 .icon = users[i].icon,
                                 .checked = -1 };
        if (kit_menu_item(k, key, &d)) {
            open_user(s, &users[i]);
        }
    }
    if (!acount(users)) {
        kit_menu_item_desc none = { .label = "Nothing uses it yet",
                                    .checked = -1,
                                    .disabled = true };
        kit_menu_item(k, "none", &none);
    }
    afree(users);
    kit_menu_separator(k, "s0");
    const char **names = studio_asset_names(app_scene_authoring_asset_index(s->scene), REFERENCE_ASSET_UI);
    if (kit_submenu_begin(k, "all", "Preview", KIT_ICON_LAYOUT)) {
        for (int i = 0; i < acount(names); i++) {
            char key[16];
            snprintf(key, sizeof key, "a%d", i);
            kit_menu_item_desc d = { .label = names[i],
                                     .icon = studio_asset_kind_icon(REFERENCE_ASSET_UI),
                                     .checked = -1,
                                     .current = !strcmp(names[i], asset) };
            if (kit_menu_item(k, key, &d) && !d.current) {
                studio_preview_ui(s, names[i]);
            }
        }
        kit_submenu_end(k);
    }
    afree(names);
    kit_popup_end(k);
}

/* Scene › Asset, the screen size, the backdrop and the view zoom. */
static bool canvas_header(studio *s, int area)
{
    if (!studio_previewed_ui(s)) {
        return false;
    }
    kit *k = s->kit;
    studio_canvas *c = s->canvas;
    char asset[sizeof c->asset];
    snprintf(asset, sizeof asset, "%s", c->asset);
    kit_push(k, "crumbs", NULL, "crumbs");
    kit_button_desc scene = { .label = "Scene",
                              .icon = KIT_ICON_SCENE,
                              .cls = "crumb",
                              .tip = "Back to the scene" };
    if (kit_button(k, "scene", &scene)) {
        studio_preview_ui(s, NULL);
    }
    kit_icon(k, "sep", "crumb-sep", KIT_ICON_RIGHT);
    asset_crumb(s, asset);
    kit_pop(k);
    const char *sizes[STUDIO_CANVAS_SIZE_COUNT];
    for (int i = 0; i < STUDIO_CANVAS_SIZE_COUNT; i++) {
        sizes[i] = studio_canvas_size_of((studio_canvas_size)i).label;
    }
    int size = (int)c->size;
    kit_push(k, "sizes", NULL, "canvas-sizes");
    if (kit_toggle_group(k, "size", &size, sizes, STUDIO_CANVAS_SIZE_COUNT, 0)) {
        c->size = (studio_canvas_size)size;
    }
    kit_pop(k);
    kit_button_desc backdrop = { .icon = c->light ? KIT_ICON_SUN : KIT_ICON_MOON,
                                 .tip = c->light ? "Light backdrop" : "Dark backdrop" };
    if (kit_button(k, "backdrop", &backdrop)) {
        c->light = !c->light;
    }
    studio_layout *layout = studio_active_layout(s);
    kit_push(k, "actions-toggle", NULL, "canvas-actions-toggle");
    kit_button_desc actions = { .label = "Actions",
                                .icon = KIT_ICON_EVENT,
                                .size = KIT_SM,
                                .cls = "canvas-actions-button",
                                .tip = "Show or hide UI events",
                                .flags = layout->canvas_actions_open ? KIT_ON : 0 };
    if (kit_button(k, "actions", &actions)) {
        layout->canvas_actions_open = !layout->canvas_actions_open;
    }
    kit_number_flow(k, "count", "count", acount(c->actions), &(kit_number_flow_desc){ 0 });
    kit_pop(k);
    kit_leaf(k, "spacer", NULL, "spacer");
    view_zoom_menu(s, area);
    return true;
}

/* A screen size's frame, centered in the stage with room around it; Fit
   fills the stage. */
static void place_frame(kit *k, studio_canvas_size_info size, kit_rect stage)
{
    enum
    {
        MARGIN = 16,
    };
    float w = stage.w, h = stage.h;
    if (size.width) {
        float room_w = fmaxf(stage.w - 2 * MARGIN, 1),
              room_h = fmaxf(stage.h - 2 * MARGIN, 1);
        float aspect = (float)size.width / (float)size.height;
        w = fminf(room_w, room_h * aspect);
        h = w / aspect;
    }
    char value[32];
    snprintf(value, sizeof value, "%.1fdp", (stage.w - w) / 2);
    kit_style(k, "left", value);
    snprintf(value, sizeof value, "%.1fdp", (stage.h - h) / 2);
    kit_style(k, "top", value);
    snprintf(value, sizeof value, "%.1fdp", w);
    kit_style(k, "width", value);
    snprintf(value, sizeof value, "%.1fdp", h);
    kit_style(k, "height", value);
}

static void action_row(kit *k, const studio_canvas_action *a, int index)
{
    char key[16], time[16], count[16];
    snprintf(key, sizeof key, "a%d", index);
    int seconds = (int)a->time;
    snprintf(time, sizeof time, "%02d:%02d", seconds / 60 % 100, seconds % 60);
    snprintf(count, sizeof count, "\xc3\x97%d", a->count);
    kit_push(k, key, NULL, "canvas-action");
    kit_label(k, "event", "event", a->event);
    kit_label(k, "element", "element", a->element);
    kit_label(k, "value", "value", a->value);
    kit_label(k, "count", "count", a->count > 1 ? count : "");
    kit_label(k, "time", "time", time);
    kit_pop(k);
}

/* Storybook's Actions: each event the asset emitted, newest last, in a
   column beside the stage so it never covers the asset. */
static void actions_panel(studio *s)
{
    kit *k = s->kit;
    studio_canvas *c = s->canvas;
    int total = acount(c->actions);
    kit_push(k, "actions", NULL, "canvas-actions");
    kit_push(k, "head", NULL, "canvas-actions-head");
    kit_icon(k, "icon", NULL, KIT_ICON_EVENT);
    kit_label(k, "title", "title", "Actions");
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_button_desc clear = { .icon = KIT_ICON_TRASH,
                              .size = KIT_SM,
                              .tip = "Clear",
                              .flags = total ? 0 : KIT_DISABLED };
    if (kit_button(k, "clear", &clear)) {
        asetlen(c->actions, 0);
        total = 0;
    }
    kit_pop(k);
    if (!total) {
        kit_label(k, "empty", "canvas-actions-empty", "No actions yet");
        kit_pop(k);
        return;
    }
    kit_list_desc list = { .count = total,
                           .row_height = kit_metric(s->kit, "--row"),
                           .follow = true,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "log", "canvas-actions-list", &list, &first, &last);
    for (int r = first; r <= last && r < total; r++) {
        action_row(k, &c->actions[r], r);
    }
    kit_list_end(k);
    kit_pop(k);
}

/* The size and users of the previewed asset, over the stage's top left. */
static void canvas_label(studio *s, studio_canvas_size_info size, kit_rect stage)
{
    kit *k = s->kit;
    char line[288];
    if (size.width) {
        snprintf(line, sizeof line, "%s \xc2\xb7 %d \xc3\x97 %d", size.label, size.width, size.height);
    } else {
        float ui = s->prefs.ui_scale;
        snprintf(
            line,
            sizeof line,
            "Fit \xc2\xb7 %d \xc3\x97 %d",
            (int)(stage.w * ui),
            (int)(stage.h * ui)
        );
    }
    kit_push(k, "label", NULL, "view-label canvas-label");
    kit_label(k, "size", NULL, line);
    canvas_user *users = canvas_users(s, s->canvas->asset);
    int used = snprintf(line, sizeof line, "%s", acount(users) ? "Used by " : "Not used yet");
    for (int i = 0; i < acount(users) && used < (int)sizeof line; i++) {
        used += snprintf(
            line + used,
            sizeof line - (size_t)used,
            "%s%s",
            i ? ", " : "",
            users[i].label
        );
    }
    afree(users);
    kit_label(k, "users", "dim", line);
    kit_pop(k);
}

/* The stage, with the asset's frame at its screen size, its size and users
   and the UI error, then the Actions log. Its keys differ from the scene's,
   whose surface keeps no frame placement. */
static bool canvas_body(studio *s)
{
    if (!studio_previewed_ui(s)) {
        return false;
    }
    kit *k = s->kit;
    studio_canvas *c = s->canvas;
    kit_push(k, "canvas", NULL, KIT_CX(k, "viewport canvas", { "light", c->light }));
    kit_push(k, "stage", NULL, "canvas-stage");
    kit_rect stage = kit_item_rect(k);
    studio_canvas_size_info size = studio_canvas_size_of(c->size);
    c->frame = kit_surface(k, "frame", "surface canvas-frame", "canvas", &s->surface);
    crop_view(s);
    place_frame(k, size, stage);
    canvas_label(s, size, stage);
    if (s->ui_error[0]) {
        kit_label(k, "error", "notice", s->ui_error);
    }
    kit_pop(k);
    if (studio_active_layout(s)->canvas_actions_open) {
        actions_panel(s);
    }
    kit_pop(k);
    return true;
}
