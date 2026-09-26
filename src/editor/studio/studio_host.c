#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SDL keys forwarded to RmlUi; values are RmlUi 6.3 key identifiers. */
static rml_input_key input_key(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) {
        return (rml_input_key)(RML_INPUT_KEY_A + (int)(key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return (rml_input_key)(RML_INPUT_KEY_0 + (int)(key - SDLK_0));
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return (rml_input_key)(RML_INPUT_KEY_F1 + (int)(key - SDLK_F1));
    }
    switch (key) {
        case SDLK_RETURN: return RML_INPUT_KEY_ENTER;
        case SDLK_KP_ENTER: return RML_INPUT_KEY_NUMPAD_ENTER;
        case SDLK_ESCAPE: return RML_INPUT_KEY_ESCAPE;
        case SDLK_BACKSPACE: return RML_INPUT_KEY_BACKSPACE;
        case SDLK_TAB: return RML_INPUT_KEY_TAB;
        case SDLK_SPACE: return RML_INPUT_KEY_SPACE;
        case SDLK_LEFT: return RML_INPUT_KEY_LEFT;
        case SDLK_RIGHT: return RML_INPUT_KEY_RIGHT;
        case SDLK_UP: return RML_INPUT_KEY_UP;
        case SDLK_DOWN: return RML_INPUT_KEY_DOWN;
        case SDLK_HOME: return RML_INPUT_KEY_HOME;
        case SDLK_END: return RML_INPUT_KEY_END;
        case SDLK_PAGEUP: return RML_INPUT_KEY_PAGE_UP;
        case SDLK_PAGEDOWN: return RML_INPUT_KEY_PAGE_DOWN;
        case SDLK_DELETE: return RML_INPUT_KEY_DELETE;
        case SDLK_MINUS: return RML_INPUT_KEY_OEM_MINUS;
        case SDLK_PERIOD: return RML_INPUT_KEY_OEM_PERIOD;
        case SDLK_EQUALS: return RML_INPUT_KEY_OEM_PLUS;
        case SDLK_KP_MINUS: return RML_INPUT_KEY_SUBTRACT;
        case SDLK_LSHIFT: return RML_INPUT_KEY_LEFT_SHIFT;
        case SDLK_RSHIFT: return RML_INPUT_KEY_RIGHT_SHIFT;
        case SDLK_LCTRL: return RML_INPUT_KEY_LEFT_CONTROL;
        case SDLK_RCTRL: return RML_INPUT_KEY_RIGHT_CONTROL;
        case SDLK_LALT: return RML_INPUT_KEY_LEFT_ALT;
        case SDLK_RALT: return RML_INPUT_KEY_RIGHT_ALT;
        case SDLK_LGUI: return RML_INPUT_KEY_LEFT_META;
        case SDLK_RGUI: return RML_INPUT_KEY_RIGHT_META;
        default: return RML_INPUT_KEY_UNKNOWN;
    }
}

static unsigned rml_modifiers(SDL_Keymod mod);

/* On macOS, Command edits text (copy, paste, select all) where RmlUi expects
   Control, so key events trade the two there. Pointer events keep them. */
static unsigned rml_key_modifiers(const studio *s, SDL_Keymod mod)
{
    if (s->mac) {
        mod = (SDL_Keymod)((mod & ~(SDL_KMOD_CTRL | SDL_KMOD_GUI)) |
                           ((mod & SDL_KMOD_GUI) ? SDL_KMOD_CTRL : 0) |
                           ((mod & SDL_KMOD_CTRL) ? SDL_KMOD_GUI : 0));
    }
    return rml_modifiers(mod);
}

static unsigned rml_modifiers(SDL_Keymod mod)
{
    return ((mod & SDL_KMOD_CTRL) ? RML_INPUT_MOD_CONTROL : 0u) |
           ((mod & SDL_KMOD_SHIFT) ? RML_INPUT_MOD_SHIFT : 0u) |
           ((mod & SDL_KMOD_ALT) ? RML_INPUT_MOD_ALT : 0u) |
           ((mod & SDL_KMOD_GUI) ? RML_INPUT_MOD_META : 0u);
}

/* SDL mouse buttons are 1 left, 2 middle, 3 right; the kit uses 0 left, 1 right, 2 middle. */
static int kit_button_index(Uint8 button)
{
    return button == SDL_BUTTON_LEFT   ? 0
         : button == SDL_BUTTON_RIGHT  ? 1
         : button == SDL_BUTTON_MIDDLE ? 2
                                       : -1;
}

/* Polled state loses a press and release within one frame; queue events instead. */
static bool SDLCALL queue_event(void *user, SDL_Event *event)
{
    studio *s = user;
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: apush(s->events, *event); break;
        case SDL_EVENT_SYSTEM_THEME_CHANGED:
            s->system_light = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT;
            break;
        default: break;
    }
    return true;
}

static bool studio_key_pressed(const studio *s, SDL_Keycode key)
{
    for (int i = 0; i < acount(s->keys); i++) {
        if (s->keys[i].key == key) {
            return true;
        }
    }
    return false;
}

static void forward_input(studio *s)
{
    kit_frame_input *in = &s->in;
    for (int b = 0; b < 3; b++) {
        in->pressed[b] = in->released[b] = false;
    }
    in->wheel = 0;
    asetlen(s->keys, 0);
    s->nav_button = 0;
    for (int i = 0; i < acount(s->events); i++) {
        const SDL_Event *e = &s->events[i];
        rml_input input = { .x = in->mouse_x,
                            .y = in->mouse_y,
                            .modifiers = rml_modifiers(s->mods) };
        switch (e->type) {
            case SDL_EVENT_MOUSE_MOTION:
                in->mouse_x = input.x = e->motion.x;
                in->mouse_y = input.y = e->motion.y;
                input.type = RML_INPUT_POINTER_MOVE;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                int b = kit_button_index(e->button.button);
                bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (down && (e->button.button == SDL_BUTTON_X1 ||
                             e->button.button == SDL_BUTTON_X2)) {
                    s->nav_button = e->button.button == SDL_BUTTON_X1 ? -1 : 1;
                }
                if (b < 0) {
                    continue;
                }
                in->mouse_x = input.x = e->button.x;
                in->mouse_y = input.y = e->button.y;
                rml_input move = input;
                move.type = RML_INPUT_POINTER_MOVE;
                rml_backend_process_input(s->backend, &move);
                apush(s->graph.events, move);
                in->down[b] = down;
                in->pressed[b] |= down;
                in->released[b] |= !down;
                input.type = down ? RML_INPUT_POINTER_DOWN : RML_INPUT_POINTER_UP;
                input.button = b;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                /* Zoom anchors where the wheel turned, which a motion event
                   may not have reported yet. */
                in->mouse_x = input.x = e->wheel.mouse_x;
                in->mouse_y = input.y = e->wheel.mouse_y;
                in->wheel += e->wheel.y;
                input.type = RML_INPUT_POINTER_WHEEL;
                input.wheel_x = -e->wheel.x;
                input.wheel_y = -e->wheel.y;
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                input.type = RML_INPUT_POINTER_LEAVE;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                input.type = e->type == SDL_EVENT_KEY_DOWN ? RML_INPUT_KEY_PRESS
                                                           : RML_INPUT_KEY_RELEASE;
                input.key = input_key(e->key.key);
                s->mods = e->key.mod;
                input.modifiers = rml_key_modifiers(s, e->key.mod);
                if (e->type == SDL_EVENT_KEY_DOWN) {
                    apush(s->keys, ((studio_key){ e->key.key, e->key.mod, e->key.repeat }));
                }
                if (input.key == RML_INPUT_KEY_UNKNOWN) {
                    continue;
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                input.type = RML_INPUT_TEXT_INPUT;
                input.modifiers = rml_key_modifiers(s, s->mods);
                input.text = e->text.text;
                input.text_size = strlen(e->text.text);
                break;
            default: continue;
        }
        rml_backend_process_input(s->backend, &input);
        apush(s->graph.events, input);
    }
    asetlen(s->events, 0);
    SDL_Keymod mod = s->mods;
    in->modifiers = ((mod & SDL_KMOD_CTRL) ? KIT_MOD_CTRL : 0u) |
                    ((mod & SDL_KMOD_SHIFT) ? KIT_MOD_SHIFT : 0u) |
                    ((mod & SDL_KMOD_ALT) ? KIT_MOD_ALT : 0u) |
                    ((mod & SDL_KMOD_GUI) ? KIT_MOD_META : 0u);
    /* Text input consumed by the watch must not reach Cute's gameplay buffer twice. */
    cf_input_text_clear();
}

/* RCSS `cursor` names to system cursors: resize on splitters and scrubbed
   fields, text in inputs, a crosshair in the color plane. */
static void sync_cursor(studio *s)
{
    static const struct
    {
        const char *name;
        SDL_SystemCursor cursor;
    } names[] = {
        { "text", SDL_SYSTEM_CURSOR_TEXT },
        { "ew-resize", SDL_SYSTEM_CURSOR_EW_RESIZE },
        { "col-resize", SDL_SYSTEM_CURSOR_EW_RESIZE },
        { "ns-resize", SDL_SYSTEM_CURSOR_NS_RESIZE },
        { "nwse-resize", SDL_SYSTEM_CURSOR_NWSE_RESIZE },
        { "nesw-resize", SDL_SYSTEM_CURSOR_NESW_RESIZE },
        { "row-resize", SDL_SYSTEM_CURSOR_NS_RESIZE },
        { "crosshair", SDL_SYSTEM_CURSOR_CROSSHAIR },
        { "pointer", SDL_SYSTEM_CURSOR_POINTER },
        { "move", SDL_SYSTEM_CURSOR_MOVE },
        { "grab", SDL_SYSTEM_CURSOR_MOVE },
        { "not-allowed", SDL_SYSTEM_CURSOR_NOT_ALLOWED },
    };
    const char *graph = studio_graph_cursor(s);
    const char *name = kit_dragging(s->kit, NULL) ? "move"
                     : graph ? graph
                             : rml_backend_cursor(s->backend);
    SDL_SystemCursor wanted = SDL_SYSTEM_CURSOR_DEFAULT;
    for (size_t i = 0; name && i < sizeof names / sizeof *names; i++) {
        if (strcmp(names[i].name, name) == 0) {
            wanted = names[i].cursor;
        }
    }
    if ((int)wanted == s->cursor_shown) {
        return;
    }
    if (!s->cursors[wanted]) {
        s->cursors[wanted] = SDL_CreateSystemCursor(wanted);
    }
    SDL_SetCursor(s->cursors[wanted]);
    s->cursor_shown = (int)wanted;
}

/* SDL delivers text only while text input is started; follow RmlUi keyboard focus. */
static void sync_text_input(studio *s)
{
    kit_rect caret;
    bool wants = kit_accepts_text(s->kit) || studio_graph_accepts_text(s);
    if (wants != s->text_input) {
        s->text_input = wants;
        if (wants) {
            cf_input_enable_ime();
        } else {
            cf_input_disable_ime();
        }
    }
    if (kit_wants_text(s->kit, &caret)) {
        cf_input_set_ime_rect(
            (int)caret.x,
            (int)caret.y,
            (int)caret.w,
            (int)caret.h
        );
    }
}

static int area_showing(const studio *s, studio_editor editor)
{
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        if (s->prefs.editors[i] == editor) {
            return i;
        }
    }
    return -1;
}

static void draw_viewport(studio *s)
{
    int area = area_showing(s, STUDIO_EDITOR_SCENE);
    if (area < 0 || s->surface.rect.w <= 0 || s->surface.rect.h <= 0) {
        engine_viewport_set_host_input(s->viewport, NULL);
        return;
    }
    engine_viewport_host_input input = {
        .width = s->surface.rect.w,
        .height = s->surface.rect.h,
        .x = s->surface.x,
        .y = s->surface.y,
        .hovered = s->surface.hovered,
        .focused = s->focus_area == area && !kit_wants_keyboard(s->kit),
        .blocked = kit_command_is_open(s->kit),
        .wheel = s->surface.wheel,
        .alt = (s->mods & SDL_KMOD_ALT) != 0,
        .escape = studio_key_pressed(s, SDLK_ESCAPE) && !kit_wants_keyboard(s->kit),
    };
    snprintf(input.dropped_prototype, sizeof input.dropped_prototype, "%s", s->dropped_prototype);
    s->dropped_prototype[0] = '\0';
    for (int b = 0; b < 3; b++) {
        input.down[b] = s->surface.down[b];
        input.pressed[b] = s->surface.pressed[b];
        input.released[b] = s->surface.released[b];
    }
    engine_viewport_set_host_input(s->viewport, &input);
    app_scene_authoring_draw_viewport(s->scene, s->viewport);
    rml_backend_set_texture(s->backend, "viewport", engine_viewport_texture(s->viewport));
}

/* Preferences apply by comparison with the last applied copy, like a zustand
 * selector, and save half a second after the last change. */
static void apply_prefs(studio *s, bool initial)
{
    const studio_prefs *p = &s->prefs;
    bool light = p->theme == STUDIO_THEME_LIGHT ||
                 (p->theme == STUDIO_THEME_SYSTEM && s->system_light);
    if (s->theme_switching && !--s->theme_switching) {
        kit_set_body_class(s->kit, "theme-switching", false);
    }
    if (initial || light != s->light) {
        /* next-themes: no transitions while the palette changes, and no flash
           of the wrong theme because this runs before the first frame. */
        s->light = light;
        kit_set_body_class(s->kit, "theme-switching", !initial);
        s->theme_switching = initial ? 0 : 1;
        kit_set_body_class(s->kit, "dark", !light);
    }
    if (initial || p->comfortable != s->applied.comfortable) {
        kit_set_body_class(s->kit, "comfortable", p->comfortable);
    }
    if (initial || p->reduced_motion != s->applied.reduced_motion) {
        kit_set_theme(s->kit, "reduced-motion", p->reduced_motion);
    }
    if (initial) {
        s->applied = *p;
        return;
    }
    if (!studio_prefs_equal(p, &s->applied)) {
        s->applied = *p;
        s->prefs_changed_at = s->time;
    }
    if (s->prefs_changed_at > 0 && s->time - s->prefs_changed_at >= 0.5) {
        studio_prefs_save(p);
        s->prefs_changed_at = 0;
    }
}

/* With the Studio project open, its scene and previews are the editor's own UI,
 * so their settings are these preferences, as the editor's panels' are. */
static void share_project_prefs(studio *s)
{
    if (!s->own_project) {
        return;
    }
    game_behavior_world *previews = game_prototype_preview_behaviors(s->scene->prototype_preview);
    studio_prefs_share(&s->prefs, game_behavior_save(s->scene->live->behaviors), &s->scene_prefs_seen);
    studio_prefs_share(&s->prefs, game_behavior_save(previews), &s->preview_prefs_seen);
}

/* The editor looks the way its own UI project's theme says; an edit to it
 * restyles every Studio document. */
static void sync_theme(studio *s)
{
    if (!ui_theme_sync(&s->theme, STUDIO_PROJECT "/content", s->time)) {
        return;
    }
    kit_theme(s->kit, s->theme.rcss);
    if (s->graph.kit) {
        kit_theme(s->graph.kit, s->theme.rcss);
    }
    if (s->theme.error[0]) {
        studio_error(s, "Theme", s->theme.error);
    }
}

studio *studio_new(app_scene_authoring *scene, engine_viewport *viewport, const char *project_root)
{
    studio *s = xcalloc(1, sizeof *s);
    s->scene = scene;
    s->viewport = viewport;
    s->project_root = project_root;
    char studio_root[PATH_MAX];
    s->own_project = realpath(STUDIO_PROJECT, studio_root) &&
                     !strcmp(studio_root, project_root);
    s->backend = rml_backend_create(cf_app_get_width(), cf_app_get_height(), cf_app_get_pixel_scale());
    assert(s->backend);
    s->kit = kit_create(rml_backend_context(s->backend), "studio.rml");
    assert(s->kit);
    sync_theme(s);
    s->prefs = studio_prefs_load();
    char error[256];
    s->ui = studio_ui_new(rml_backend_context(s->backend), STUDIO_PROJECT, error, sizeof error);
    if (!s->ui) {
        fprintf(stderr, "Studio UI: %s\n", error);
    }
    s->system_light = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT;
    s->mac = studio_detect_mac();
    apply_prefs(s, true);
    studio_projects_load(s);
    s->mods = SDL_GetModState();
    s->tab = STUDIO_TAB_OBJECT;
    s->assets_kind = -1;
    s->cursor_shown = -1;
    s->hover_area = s->focus_area = s->maximized = -1;
    s->graph.area = -1;
    s->in.mouse_x = cf_mouse_x();
    s->in.mouse_y = cf_mouse_y();
    SDL_AddEventWatch(queue_event, s);
    return s;
}

void studio_free(studio *s)
{
    if (!s) {
        return;
    }
    if (s->text_input) {
        cf_input_disable_ime();
    }
    SDL_RemoveEventWatch(queue_event, s);
    if (s->prefs_changed_at > 0) {
        studio_prefs_save(&s->prefs);
    }
    SDL_SetCursor(SDL_GetDefaultCursor());
    for (int i = 0; i < SDL_SYSTEM_CURSOR_COUNT; i++) {
        if (s->cursors[i]) {
            SDL_DestroyCursor(s->cursors[i]);
        }
    }
    afree(s->events);
    afree(s->records);
    afree(s->runtime_errors);
    afree(s->perf);
    afree(s->keys);
    studio_projects_free(s);
    studio_disk_free(s);
    engine_viewport_set_host_input(s->viewport, NULL);
    editor_set_retained_keyboard(false);
    studio_graph_free(s);
    component_presentation_free(&s->presentation.hints);
    kit_destroy(s->kit);
    studio_ui_free(s->ui);
    ui_theme_free(&s->theme);
    rml_backend_destroy(s->backend);
    free(s);
}

void studio_frame(studio *s, double delta_seconds)
{
    int width = cf_app_get_width(), height = cf_app_get_height();
    float scale = cf_app_get_pixel_scale();
    rml_backend_resize(s->backend, width, height, scale);
    s->time += delta_seconds;
    s->in.time = s->time;
    s->in.dp_ratio = scale;
    s->in.width = (float)width;
    s->in.height = (float)height;
    forward_input(s);
    studio_performance_sample(s, delta_seconds);
    app_scene_authoring_update(s->scene, s->in.down[0] || kit_wants_keyboard(s->kit));
    sync_theme(s);
    studio_ui_frame(s);
    share_project_prefs(s);
    studio_debug_frame(s);
    kit_begin(s->kit, &s->in);
    studio_emit(s);
    kit_end(s->kit);
    bool typing = kit_wants_keyboard(s->kit) || studio_graph_wants_keyboard(s);
    if (!typing) {
        studio_shortcuts(s);
    }
    editor_set_retained_keyboard(typing);
    apply_prefs(s, false);
    draw_viewport(s);
    studio_graph_frame(s, delta_seconds);
    sync_text_input(s);
    sync_cursor(s);
    rml_backend_update(s->backend, delta_seconds);
    rml_backend_render(s->backend);
    rml_backend_present(s->backend);
}
