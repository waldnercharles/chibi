/* Studio's commands: one table the menus, the command palette, shortcuts and
 * authored UI share, as VS Code's command registry and Blender's operators.
 * Authored UI runs one with emit('studio.<id>'). */
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
    [STUDIO_COMMAND_OPEN_PROTOTYPE] = { "open-prototype", "Open Prototype", NULL, NULL, "edit", KIT_ICON_PROTOTYPE },
    [STUDIO_COMMAND_FRAME] = { "frame", "Frame Selected", NULL, "F", "focus zoom", KIT_ICON_FRAME },
    [STUDIO_COMMAND_RESET_CAMERA] = { "reset-camera", "Reset Camera", NULL, NULL, "view", 0 },
    [STUDIO_COMMAND_TOGGLE_ASSETS] = { "toggle-assets",
                                       "Toggle Assets",
                                       "Assets",
                                       "Mod+Shift+A",
                                       "drawer browser content",
                                       KIT_ICON_ASSETS },
    [STUDIO_COMMAND_TOGGLE_OUTPUT] = { "toggle-output", "Toggle Output", "Output", "Mod+Shift+U", "drawer log console problems diagnostics", KIT_ICON_OUTPUT },
    [STUDIO_COMMAND_TOGGLE_PERFORMANCE] = { "toggle-performance",
                                            "Toggle Performance",
                                            "Performance",
                                            NULL,
                                            "drawer profiler frame time fps",
                                            KIT_ICON_PERFORMANCE },
    [STUDIO_COMMAND_MAXIMIZE] = { "maximize", "Maximize Area", NULL, "Ctrl+Space", "fullscreen", KIT_ICON_MAXIMIZE },
    [STUDIO_COMMAND_RESET_LAYOUT] = { "reset-layout", "Reset Layout", NULL, NULL, "areas default", KIT_ICON_LAYOUT },
    [STUDIO_COMMAND_ZOOM_IN] = { "zoom-in", "Zoom In", NULL, "Mod+=", "ui scale larger bigger", KIT_ICON_ZOOM_IN },
    [STUDIO_COMMAND_ZOOM_OUT] = { "zoom-out", "Zoom Out", NULL, "Mod+-", "ui scale smaller", KIT_ICON_ZOOM_OUT },
    [STUDIO_COMMAND_ZOOM_RESET] = { "zoom-reset", "Reset Zoom", NULL, "Mod+0", "ui scale actual size 100%", 0 },
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
    [STUDIO_COMMAND_INSPECT] = { "inspect-element", "Inspect Element", NULL, "Mod+Shift+C", "ui inspector devtools debugger pick styles rml rcss", KIT_ICON_SELECT },
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
    };
    switch (command) {
        case STUDIO_COMMAND_MAXIMIZE: info.shortcut = e->chord; break;
        case STUDIO_COMMAND_PLAY:
            if (app_scene_authoring_runtime_state(s->scene).playing) {
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

bool studio_command_enabled(studio *s, studio_command command)
{
    bool editable = studio_editable(s);
    bool selection = selected_actor(s->scene) != NULL;
    switch (command) {
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
        case STUDIO_COMMAND_STOP: return runtime_session(s->scene);
        case STUDIO_COMMAND_STEP: return !s->pause.active;
        default: return true;
    }
}

void studio_command_run(studio *s, studio_command command)
{
    if (!studio_command_enabled(s, command)) {
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
            studio_toggle_maximized(s, s->focus_area);
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
