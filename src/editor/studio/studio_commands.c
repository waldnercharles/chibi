/* Studio's commands: one table the menus, the command palette, shortcuts and
 * authored UI share, as VS Code's command registry and Blender's operators.
 * Authored UI runs one with emit('studio.<id>'), or on a tab or an area with
 * emit('studio.<id>', index), as VS Code's commands take arguments. Ids are
 * camelCase, so `commands.<id>` binds the same name. */
#include "editor/studio/studio_internal.h"

#include <string.h>

typedef struct
{
    const char *id;
    const char *label;
    const char *menu;  /* the label in a menu, when it differs */
    const char *chord; /* studio_chord notation; Ctrl stays Ctrl on macOS */
    const char *keywords;
    uint32_t icon;
    bool indexed; /* runs only on the tab its argument names */
} command_entry;

static const command_entry COMMANDS[STUDIO_COMMAND_COUNT] = {
    [STUDIO_COMMAND_SEARCH] = { "search", "Search", NULL, "Mod+K", "find go to", KIT_ICON_SEARCH },
    [STUDIO_COMMAND_SETTINGS] = { "settings", "Settings", NULL, "Mod+,", "preferences options", KIT_ICON_SETTINGS },
    [STUDIO_COMMAND_SAVE] = { "save", "Save", NULL, "Mod+S", NULL, KIT_ICON_SAVE },
    [STUDIO_COMMAND_RELOAD] = { "reload", "Reload", NULL, NULL, "revert disk", 0 },
    [STUDIO_COMMAND_UNDO] = { "undo", "Undo", NULL, "Mod+Z", NULL, KIT_ICON_UNDO },
    [STUDIO_COMMAND_REDO] = { "redo", "Redo", NULL, "Mod+Shift+Z", NULL, KIT_ICON_REDO },
    [STUDIO_COMMAND_DUPLICATE] = { "duplicate", "Duplicate", NULL, "Shift+D", "copy", KIT_ICON_COPY },
    [STUDIO_COMMAND_DELETE] = { "delete", "Delete", NULL, "X", "remove", KIT_ICON_TRASH },
    [STUDIO_COMMAND_OPEN_PROTOTYPE] = { "openPrototype", "Open Prototype", NULL, NULL, "edit", KIT_ICON_PROTOTYPE },
    [STUDIO_COMMAND_FRAME] = { "frame", "Frame Selected", NULL, "F", "focus zoom", KIT_ICON_FRAME },
    [STUDIO_COMMAND_RESET_CAMERA] = { "resetCamera", "Reset Camera", NULL, NULL, "view", 0 },
    [STUDIO_COMMAND_TOGGLE_ASSETS] = { "toggleAssets",
                                       "Toggle Assets",
                                       "Assets",
                                       "Mod+Shift+A",
                                       "drawer browser content",
                                       KIT_ICON_ASSETS },
    [STUDIO_COMMAND_TOGGLE_OUTPUT] = { "toggleOutput", "Toggle Output", "Output", "Mod+Shift+U", "drawer log console problems diagnostics", KIT_ICON_OUTPUT },
    [STUDIO_COMMAND_TOGGLE_PERFORMANCE] = { "togglePerformance",
                                            "Toggle Performance",
                                            "Performance",
                                            NULL,
                                            "drawer profiler frame time fps",
                                            KIT_ICON_PERFORMANCE },
    [STUDIO_COMMAND_MAXIMIZE] = { "maximize", "Maximize Area", NULL, "Ctrl+Space", "fullscreen", KIT_ICON_MAXIMIZE },
    [STUDIO_COMMAND_RESET_LAYOUT] = { "resetLayout", "Reset Layout", NULL, NULL, "areas default", KIT_ICON_LAYOUT },
    [STUDIO_COMMAND_ZOOM_IN] = { "zoomIn", "Zoom In", NULL, "Mod+=", "ui scale larger bigger", KIT_ICON_ZOOM_IN },
    [STUDIO_COMMAND_ZOOM_OUT] = { "zoomOut", "Zoom Out", NULL, "Mod+-", "ui scale smaller", KIT_ICON_ZOOM_OUT },
    [STUDIO_COMMAND_ZOOM_RESET] = { "zoomReset", "Reset Zoom", NULL, "Mod+0", "ui scale actual size 100%", 0 },
    [STUDIO_COMMAND_PLAY] = { "play", "Play", NULL, "Space", "run start preview pause", KIT_ICON_PLAY },
    [STUDIO_COMMAND_STOP] = { "stop", "Stop", NULL, NULL, NULL, KIT_ICON_STOP },
    [STUDIO_COMMAND_STEP] = { "step", "Step", NULL, NULL, "frame advance", KIT_ICON_STEP },
    [STUDIO_COMMAND_DENSITY] = { "density",
                                 "Comfortable Density",
                                 NULL,
                                 NULL,
                                 "spacing size settings",
                                 KIT_ICON_LAYOUT },
    [STUDIO_COMMAND_MOTION] = { "motion", "Reduce Motion", NULL, NULL, "animation settings", KIT_ICON_SETTINGS },
    [STUDIO_COMMAND_THEME] = { "theme", "Light Theme", NULL, NULL, "appearance color mode settings", KIT_ICON_SUN },
    /* UI Inspector (studio_inspect.c) */
    [STUDIO_COMMAND_INSPECT] = { "inspectElement", "Inspect Element", NULL, "Mod+Shift+C", "ui inspector devtools debugger pick styles rml rcss", KIT_ICON_SELECT },
    /* UI wiring (workstream K) */
    [STUDIO_COMMAND_PREVIEW_UI] = { "previewUi", "Preview UI Asset\xe2\x80\xa6", NULL, NULL, "canvas storybook widget document screen size", KIT_ICON_LAYOUT },
    /* Authored top bar and area headers (workstream T): the document tabs'
       commands take the tab's index, emit('studio.selectTab', i). */
    [STUDIO_COMMAND_SELECT_TAB] = { "selectTab", "Select Tab", NULL, NULL, "document switch", KIT_ICON_FILE, true },
    [STUDIO_COMMAND_CLOSE_TAB] = { "closeTab", "Close Tab", NULL, NULL, "document", KIT_ICON_CLOSE },
    [STUDIO_COMMAND_CLOSE_DRAWER] = { "closeDrawer", "Close Drawer", NULL, NULL, "hide panel", KIT_ICON_CLOSE },
};

studio_command_info studio_command_describe(studio *s, studio_command command)
{
    const command_entry *e = &COMMANDS[command];
    studio_command_info info = {
        .id = e->id,
        .label = e->label,
        .menu = e->menu ? e->menu : e->label,
        .shortcut = e->chord ? studio_chord(s, e->chord) : NULL,
        .keywords = e->keywords,
        .icon = e->icon,
        .indexed = e->indexed,
    };
    switch (command) {
        case STUDIO_COMMAND_PLAY:
            /* Play resumes a run paused at a breakpoint (Unreal). */
            if (s->pause.active) {
                info.label = info.menu = "Resume";
                info.icon = KIT_ICON_CONTINUE;
                info.shortcut = "F5";
            } else if (s->guest) {
                info.label = info.menu = "Stop Studio";
            } else if (app_scene_authoring_runtime_state(s->scene).playing) {
                info.label = info.menu = "Pause";
                info.icon = KIT_ICON_PAUSE;
            }
            break;
        case STUDIO_COMMAND_DENSITY:
            info.label = info.menu = s->prefs.comfortable ? "Compact Density"
                                                          : e->label;
            break;
        case STUDIO_COMMAND_MOTION:
            info.label = info.menu = s->prefs.reduced_motion ? "Full Motion" : e->label;
            break;
        case STUDIO_COMMAND_THEME:
            info.label = info.menu = s->light ? "Dark Theme" : e->label;
            info.icon = s->light ? KIT_ICON_MOON : e->icon;
            break;
        default: break;
    }
    return info;
}

static bool drawer_showing(const studio *s, studio_editor editor)
{
    return s->prefs.drawer_open && s->prefs.editors[STUDIO_AREA_DRAWER] == editor;
}

int studio_command_checked(studio *s, studio_command command)
{
    switch (command) {
        case STUDIO_COMMAND_TOGGLE_ASSETS:
            return drawer_showing(s, STUDIO_EDITOR_ASSETS);
        case STUDIO_COMMAND_TOGGLE_OUTPUT:
            return drawer_showing(s, STUDIO_EDITOR_OUTPUT);
        case STUDIO_COMMAND_TOGGLE_PERFORMANCE:
            return drawer_showing(s, STUDIO_EDITOR_PERFORMANCE);
        case STUDIO_COMMAND_MAXIMIZE: return s->maximized >= 0;
        case STUDIO_COMMAND_INSPECT: return studio_inspect_picking(s);
        default: return -1;
    }
}

/* The tab `argument` names, else the active one. */
static int tab_at(const studio *s, int argument)
{
    return argument >= 0 ? argument : s->scene->active_tab;
}

/* Whether the command runs on `argument`, a tab or an area, or on the active
 * one for -1. */
static bool enabled_with(studio *s, studio_command command, int argument)
{
    bool editable = studio_editable(s);
    bool selection = selected_actor(s->scene) != NULL;
    switch (command) {
        case STUDIO_COMMAND_MAXIMIZE: return argument < STUDIO_AREA_COUNT;
        case STUDIO_COMMAND_SELECT_TAB:
            return argument >= 0 && argument < acount(s->scene->tabs) &&
                   argument != s->scene->active_tab;
        case STUDIO_COMMAND_CLOSE_TAB:
            return studio_tab_closes(s, tab_at(s, argument));
        case STUDIO_COMMAND_CLOSE_DRAWER: return s->prefs.drawer_open;
        case STUDIO_COMMAND_SAVE: return studio_active_dirty(s);
        case STUDIO_COMMAND_RELOAD: return editable;
        case STUDIO_COMMAND_UNDO:
            return editable && studio_can_replay(s, false);
        case STUDIO_COMMAND_REDO: return editable && studio_can_replay(s, true);
        case STUDIO_COMMAND_DUPLICATE:
        case STUDIO_COMMAND_DELETE: return editable && selection;
        case STUDIO_COMMAND_OPEN_PROTOTYPE:
        case STUDIO_COMMAND_FRAME: return selection;
        case STUDIO_COMMAND_ZOOM_IN:
            return s->prefs.ui_scale < STUDIO_UI_SCALE_MAX;
        case STUDIO_COMMAND_ZOOM_OUT:
            return s->prefs.ui_scale > STUDIO_UI_SCALE_MIN;
        case STUDIO_COMMAND_ZOOM_RESET: return s->prefs.ui_scale != 1;
        /* A guest Studio runs no scene of its own. */
        case STUDIO_COMMAND_PLAY: return !s->hosted;
        case STUDIO_COMMAND_STOP: return !s->hosted && studio_session(s);
        case STUDIO_COMMAND_STEP:
            return !s->hosted && !s->guest && !s->pause.active;
        default: return true;
    }
}

bool studio_command_enabled(studio *s, studio_command command)
{
    return enabled_with(s, command, -1);
}

void studio_command_run(studio *s, studio_command command)
{
    studio_command_run_with(s, command, -1);
}

void studio_command_run_with(studio *s, studio_command command, int argument)
{
    if (!enabled_with(s, command, argument)) {
        return;
    }
    switch (command) {
        case STUDIO_COMMAND_SEARCH:
            kit_command_open(s->kit, "Search objects, assets and commands");
            break;
        case STUDIO_COMMAND_SETTINGS: studio_ui_show(s, "Settings"); break;
        case STUDIO_COMMAND_SAVE: studio_save(s); break;
        case STUDIO_COMMAND_RELOAD: app_scene_authoring_reload(s->scene); break;
        case STUDIO_COMMAND_UNDO: studio_history(s, false); break;
        case STUDIO_COMMAND_REDO: studio_history(s, true); break;
        case STUDIO_COMMAND_DUPLICATE: studio_duplicate(s); break;
        case STUDIO_COMMAND_DELETE: studio_delete(s); break;
        case STUDIO_COMMAND_OPEN_PROTOTYPE:
            app_scene_authoring_open_prototype(
                s->scene,
                selected_actor(s->scene)->prototype_name
            );
            break;
        case STUDIO_COMMAND_FRAME: studio_frame_selection(s); break;
        case STUDIO_COMMAND_RESET_CAMERA:
            engine_viewport_reset_camera(s->viewport);
            break;
        case STUDIO_COMMAND_TOGGLE_ASSETS:
            studio_toggle_drawer(s, STUDIO_EDITOR_ASSETS);
            break;
        case STUDIO_COMMAND_TOGGLE_OUTPUT:
            studio_toggle_drawer(s, STUDIO_EDITOR_OUTPUT);
            break;
        case STUDIO_COMMAND_TOGGLE_PERFORMANCE:
            studio_toggle_drawer(s, STUDIO_EDITOR_PERFORMANCE);
            break;
        case STUDIO_COMMAND_MAXIMIZE:
            studio_toggle_maximized(s, argument >= 0 ? argument : s->focus_area);
            break;
        case STUDIO_COMMAND_RESET_LAYOUT: studio_reset_layout(s); break;
        case STUDIO_COMMAND_ZOOM_IN: studio_zoom(s, 1); break;
        case STUDIO_COMMAND_ZOOM_OUT: studio_zoom(s, -1); break;
        case STUDIO_COMMAND_ZOOM_RESET: studio_zoom(s, 0); break;
        case STUDIO_COMMAND_PLAY: studio_runtime(s, APP_RUNTIME_PLAY); break;
        case STUDIO_COMMAND_STOP: studio_runtime(s, APP_RUNTIME_STOP); break;
        case STUDIO_COMMAND_STEP: studio_runtime(s, APP_RUNTIME_STEP); break;
        case STUDIO_COMMAND_DENSITY:
            s->prefs.comfortable = !s->prefs.comfortable;
            break;
        case STUDIO_COMMAND_MOTION:
            s->prefs.reduced_motion = !s->prefs.reduced_motion;
            break;
        case STUDIO_COMMAND_THEME:
            s->prefs.theme = s->light ? STUDIO_THEME_DARK : STUDIO_THEME_LIGHT;
            break;
        case STUDIO_COMMAND_INSPECT: studio_inspect_toggle(s); break;
        case STUDIO_COMMAND_PREVIEW_UI:
            studio_pick_open(
                s,
                STUDIO_PICK_PREVIEW,
                studio_previewed_ui(s),
                &(studio_pick_desc){ .kind = REFERENCE_ASSET_UI }
            );
            break;
        case STUDIO_COMMAND_SELECT_TAB:
            finish_transform_edit(s->scene, false);
            scene_tabs_activate(s->scene, argument);
            break;
        case STUDIO_COMMAND_CLOSE_TAB:
            studio_code_close(s, tab_at(s, argument));
            break;
        case STUDIO_COMMAND_CLOSE_DRAWER: s->prefs.drawer_open = false; break;
        case STUDIO_COMMAND_COUNT: break;
    }
}

const char *studio_command_id(studio_command command)
{
    return COMMANDS[command].id;
}

int studio_command_named(const char *id)
{
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        if (!strcmp(COMMANDS[c].id, id)) {
            return c;
        }
    }
    return -1;
}

bool studio_command_item(studio *s, studio_command command)
{
    studio_command_info info = studio_command_describe(s, command);
    kit_menu_item_desc d = { .label = info.menu,
                             .icon = info.icon,
                             .shortcut = info.shortcut,
                             .checked = studio_command_checked(s, command),
                             .disabled = !studio_command_enabled(s, command) };
    if (!kit_menu_item(s->kit, info.id, &d)) {
        return false;
    }
    studio_command_run(s, command);
    return true;
}
