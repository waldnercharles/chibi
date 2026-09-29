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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* SDL's browser backend reports no pinch: the page shell turns a browser's
 * pinch, a wheel with Ctrl held or Safari's gesture events, into the pinch
 * SDL's macOS backend reports. */
EMSCRIPTEN_KEEPALIVE void studio_web_pinch(float scale)
{
    SDL_Event event = { .pinch = { .type = SDL_EVENT_PINCH_UPDATE, .scale = scale } };
    SDL_PushEvent(&event);
}
#endif

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
        /* Workstream I: Settings (Mod+,) and Vim's Control+[ from the Text
           Editor. */
        case SDLK_COMMA: return RML_INPUT_KEY_OEM_COMMA;
        case SDLK_SEMICOLON: return RML_INPUT_KEY_OEM_1;
        case SDLK_LEFTBRACKET: return RML_INPUT_KEY_OEM_4;
        case SDLK_RIGHTBRACKET: return RML_INPUT_KEY_OEM_6;
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

/* Records a key press without the primary modifier, which Studio's shortcuts
 * keep, or typed text, in order, as input for the game documents in the Scene
 * area. The text stays SDL's, valid until the next frame's events. */
static void game_input(studio *s, const rml_input *input, const SDL_Event *e)
{
    bool key = input->type == RML_INPUT_KEY_PRESS && !studio_primary(s, e->key.mod);
    if (key || input->type == RML_INPUT_TEXT_INPUT) {
        apush(s->game_input, *input);
    }
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
        case SDL_EVENT_PINCH_UPDATE:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_LOST: apush(s->events, *event); break;
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

float studio_pixel_ratio(const studio *s)
{
    return cf_app_get_pixel_scale() * s->prefs.ui_scale;
}

/* SDL reports window points; the editor lays out in dp, UI Scale apart. */
static float to_dp(const studio *s, float points)
{
    return points / s->prefs.ui_scale;
}

/* How long frames run at full rate after input, for the tooltips and
 * delayed motion it starts. */
#define ACTIVE_AFTER_INPUT 1.0

bool studio_active(const studio *s)
{
    return s->time < s->active_until || s->guest != NULL ||
           (app_scene_authoring_runtime_state(s->scene).playing && !s->pause.active);
}

/* No frame is needed while nothing moves: no input lately, no scene playing
 * (a paused one waits), and the last frame drew what the one before it did,
 * so any animation keeps frames coming without declaring itself. Timers
 * without motion, such as a toast's lifetime, wait for the host's next idle
 * frame. */
double studio_idle_time(studio *s)
{
    bool changed = rml_backend_take_changed(s->backend);
    if (s->graph.backend) {
        changed |= rml_backend_take_changed(s->graph.backend);
    }
    if (changed || studio_active(s)) {
        return 0;
    }
    double idle = kit_idle_time(s->kit);
    return s->graph.kit ? fmin(idle, kit_idle_time(s->graph.kit)) : idle;
}

/* ---------- Studio in Studio ---------- */

/* The guest's window: the Scene area's surface, in the host's window points. */
static kit_rect guest_window(const studio *s)
{
    float ui = s->prefs.ui_scale;
    kit_rect r = s->surface.rect;
    return (kit_rect){ r.x * ui, r.y * ui, r.w * ui, r.h * ui };
}

static bool window_contains(kit_rect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* Keys follow the pointer, as Blender's areas take them, unless a host field
 * has the keyboard; a press in the guest keeps them there until a press
 * elsewhere. */
static bool guest_has_keys(const studio *s)
{
    return s->guest_keys || (s->guest_hover && !s->typing);
}

/* A pointer event at (x, y) in window points reaches the guest while it is
 * over the guest, uncovered by the host's popups, or while a press there is
 * held; the guest hears it in its own window's points. */
static void guest_pointer(studio *s, SDL_Event *moved, float *x, float *y)
{
    kit_rect w = guest_window(s);
    bool inside = window_contains(w, *x, *y) && s->surface.hovered;
    bool press = moved->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    if (press) {
        s->guest_keys = inside;
        s->guest_capture = inside;
    }
    bool reach = inside || s->guest_capture;
    if (moved->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        s->guest_capture = false;
    }
    if (!reach && s->guest_hover) {
        apush(s->guest->events, ((SDL_Event){ .type = SDL_EVENT_WINDOW_MOUSE_LEAVE }));
    }
    s->guest_hover = reach;
    if (!reach) {
        return;
    }
    *x -= w.x;
    *y -= w.y;
    apush(s->guest->events, *moved);
}

static bool guest_keys(studio *s, const SDL_Event *e)
{
    if (!guest_has_keys(s)) {
        return false;
    }
    apush(s->guest->events, *e);
    return true;
}

/* Hands the guest what is its; true when the host must not handle the event. */
static bool to_guest(studio *s, const SDL_Event *e)
{
    SDL_Event moved = *e;
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            guest_pointer(s, &moved, &moved.motion.x, &moved.motion.y);
            return false;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            guest_pointer(s, &moved, &moved.button.x, &moved.button.y);
            return false;
        case SDL_EVENT_MOUSE_WHEEL:
            guest_pointer(s, &moved, &moved.wheel.mouse_x, &moved.wheel.mouse_y);
            return false;
        case SDL_EVENT_PINCH_UPDATE:
            if (s->guest_hover) {
                apush(s->guest->events, *e);
            }
            return s->guest_hover;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            s->guest->mods = e->key.mod;
            return guest_keys(s, e);
        case SDL_EVENT_TEXT_INPUT: return guest_keys(s, e);
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (s->guest_hover) {
                apush(s->guest->events, *e);
            }
            s->guest_hover = false;
            return false;
        default: return false;
    }
}

static void forward_input(studio *s)
{
    kit_frame_input *in = &s->in;
    if (acount(s->events)) {
        s->active_until = s->time + ACTIVE_AFTER_INPUT;
    }
    for (int b = 0; b < 3; b++) {
        in->pressed[b] = in->released[b] = false;
    }
    in->wheel = s->zoom_wheel = s->pinch = 0;
    asetlen(s->keys, 0);
    asetlen(s->game_input, 0);
    s->nav_button = 0;
    bool recording = s->shortcut_recording != 0;
    for (int i = 0; i < acount(s->events); i++) {
        const SDL_Event *e = &s->events[i];
        if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            s->shortcut_recording = 0;
            recording = false;
            s->mods = SDL_KMOD_NONE;
            continue;
        }
        if (recording && (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP ||
                          e->type == SDL_EVENT_TEXT_INPUT)) {
            if (e->type != SDL_EVENT_TEXT_INPUT) {
                s->mods = e->key.mod;
            }
            if (e->type == SDL_EVENT_KEY_DOWN) {
                studio_shortcut_record(
                    &s->prefs,
                    &s->shortcut_recording,
                    (studio_key){ e->key.key, e->key.mod, e->key.repeat },
                    s->mac
                );
            }
            continue;
        }
        if (s->guest && to_guest(s, e)) {
            continue;
        }
        /* The Inspector's pick keys reach no document. */
        if (e->type == SDL_EVENT_KEY_DOWN && studio_inspect_key(s, &e->key)) {
            continue;
        }
        rml_input input = { .x = in->mouse_x,
                            .y = in->mouse_y,
                            .modifiers = rml_modifiers(s->mods) };
        switch (e->type) {
            case SDL_EVENT_MOUSE_MOTION:
                in->mouse_x = input.x = to_dp(s, e->motion.x);
                in->mouse_y = input.y = to_dp(s, e->motion.y);
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
                in->mouse_x = input.x = to_dp(s, e->button.x);
                in->mouse_y = input.y = to_dp(s, e->button.y);
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
                in->mouse_x = input.x = to_dp(s, e->wheel.mouse_x);
                in->mouse_y = input.y = to_dp(s, e->wheel.mouse_y);
                in->wheel += e->wheel.y;
                /* The modifiers as the wheel turned: a key released later in
                   the same frame must not undo the gesture. */
                if (studio_primary(s, s->mods) || (s->mods & SDL_KMOD_CTRL)) {
                    s->zoom_wheel += e->wheel.y;
                }
                input.type = RML_INPUT_POINTER_WHEEL;
                input.wheel_x = -e->wheel.x;
                input.wheel_y = -e->wheel.y;
                break;
            case SDL_EVENT_PINCH_UPDATE:
                /* The Scene view and the graph canvas zoom a pinch at the
                   pointer; scales multiply, so their logs add. */
                s->pinch += logf(e->pinch.scale);
                continue;
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
        game_input(s, &input, e);
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

/* The cursor the element under the pointer asks for, the guest's over it. */
static const char *cursor_name(studio *s)
{
    if (s->guest && s->guest_hover) {
        return cursor_name(s->guest);
    }
    const char *graph = studio_graph_cursor(s);
    return kit_dragging(s->kit, NULL) ? "move"
         : graph                      ? graph
                                      : rml_backend_cursor(s->backend);
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
    const char *name = cursor_name(s);
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

/* Whether a text field has focus in the game documents that hear the Scene
 * area's keys: the Canvas preview's while it shows, else the scene's. */
static bool game_typing(const studio *s)
{
    const studio_canvas *c = studio_previewed_ui(s) ? s->canvas : NULL;
    engine_viewport *viewport = c ? c->viewport : s->viewport;
    return viewport && engine_viewport_ui_typing(viewport);
}

static bool accepts_text(studio *s)
{
    return kit_accepts_text(s->kit) || studio_graph_accepts_text(s) ||
           game_typing(s);
}

/* SDL delivers text only while text input is started; follow RmlUi keyboard
 * focus, the game documents' and a guest's with the keyboard included. */
static void sync_text_input(studio *s)
{
    kit_rect caret;
    bool wants = accepts_text(s) ||
                 (s->guest && guest_has_keys(s) && accepts_text(s->guest));
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
        if (s->prefs.layouts[s->prefs.workspace].editors[i] == editor &&
            s->areas[i].rect.w > 0) {
            return i;
        }
    }
    return -1;
}

static void draw_viewport(studio *s)
{
    /* The guest's own Scene area draws the scene, resizing its target: the
       texture this Studio showed last is gone by the time it shows the
       scene again, so it names none until it draws. */
    if (s->guest) {
        rml_backend_set_texture(s->backend, "viewport", 0);
        return;
    }
    int area = area_showing(s, STUDIO_EDITOR_SCENE);
    if (area < 0 || s->surface.rect.w <= 0 || s->surface.rect.h <= 0) {
        engine_viewport_set_host_input(s->viewport, NULL);
        return;
    }
    /* The Scene renders in window points, as games' UI scales with the view
       and not with the editor's UI Scale. */
    float ui = s->prefs.ui_scale;
    engine_viewport_host_input input = {
        .width = s->surface.rect.w * ui,
        .height = s->surface.rect.h * ui,
        .x = s->surface.x * ui,
        .y = s->surface.y * ui,
        .hovered = s->surface.hovered,
        .focused = s->focus_area == area && !kit_wants_keyboard(s->kit),
        .blocked = kit_command_is_open(s->kit),
        .wheel = s->surface.wheel,
        .alt = (s->mods & SDL_KMOD_ALT) != 0,
        .escape = studio_key_pressed(s, SDLK_ESCAPE) && !kit_wants_keyboard(s->kit),
    };
    input.keys = s->game_input;
    input.key_count = acount(s->game_input);
    snprintf(input.dropped_prototype, sizeof input.dropped_prototype, "%s", s->dropped_prototype);
    s->dropped_prototype[0] = '\0';
    for (int b = 0; b < 3; b++) {
        input.down[b] = s->surface.down[b];
        input.pressed[b] = s->surface.pressed[b];
        input.released[b] = s->surface.released[b];
    }
    studio_scene_view(s, &input);
    if (studio_canvas_draw(s, &input)) {
        engine_viewport_set_host_input(s->viewport, NULL);
        return;
    }
    engine_viewport_set_host_input(s->viewport, &input);
    app_scene_authoring_draw_viewport(s->scene, s->viewport);
    rml_backend_set_texture(s->backend, "viewport", engine_viewport_texture(s->viewport));
    studio_ui_error(s, render_ui_error(engine_viewport_ui(s->viewport)));
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
    /* A guest's preferences are its own and last while it runs. */
    if (!s->hosted && s->prefs_changed_at > 0 && s->time - s->prefs_changed_at >= 0.5) {
        studio_prefs_save(p);
        s->prefs_changed_at = 0;
    }
}

/* With the Studio project open, its scene and previews are the editor's own UI,
 * so their settings are these preferences, as the editor's panels' are. */
static void share_project_prefs(studio *s)
{
    if (!s->own_project || s->hosted) {
        return;
    }
    game_behavior_world *previews = game_prototype_preview_behaviors(s->scene->prototype_preview);
    studio_prefs_share(&s->prefs, game_behavior_save(s->scene->live->behaviors), &s->scene_prefs_seen);
    studio_prefs_share(&s->prefs, game_behavior_save(previews), &s->preview_prefs_seen);
}

/* Studio's chrome is the Studio scene's page: the scene's root document's
 * RCSS styles it, and an edit to it or an import restyles the editor. */
static void sync_theme(studio *s)
{
    const char *name = s->ui ? studio_ui_page(s->ui) : "";
    if (!ui_page_sync(&s->page, STUDIO_PROJECT "/content", name, s->time) &&
        !s->page.error[0]) {
        return;
    }
    const char *rcss = s->page.document.rcss ? s->page.document.rcss : "";
    kit_theme(s->kit, rcss);
    if (s->graph.kit) {
        kit_theme(s->graph.kit, rcss);
    }
    if (s->page.error[0]) {
        studio_error(s, "Studio UI", s->page.error);
        s->page.error[0] = '\0';
    }
}

/* A Studio over the scene authoring: the window's, or with `host` a guest
 * that starts from the host's preferences. */
static studio *create(
    app_scene_authoring *scene,
    engine_viewport *viewport,
    const char *project_root,
    const studio *host
)
{
    studio *s = xcalloc(1, sizeof *s);
    s->scene = scene;
    s->viewport = viewport;
    s->project_root = project_root;
    s->hosted = host != NULL;
    char error[256];
    /* A project that would not open shows empty Project settings. */
    chibi_project_open(&s->project, project_root, error, sizeof error);
    char studio_root[PATH_MAX];
    s->own_project = realpath(STUDIO_PROJECT, studio_root) &&
                     !strcmp(studio_root, project_root);
    s->backend = rml_backend_create(cf_app_get_width(), cf_app_get_height(), cf_app_get_pixel_scale());
    assert(s->backend);
    s->kit = kit_create(rml_backend_context(s->backend), "studio.rml");
    assert(s->kit);
    s->prefs = host ? host->prefs : studio_prefs_load();
    s->ui = studio_ui_new(rml_backend_context(s->backend), STUDIO_PROJECT, error, sizeof error);
    sync_theme(s);
    if (!s->ui) {
        fprintf(stderr, "Studio UI: %s\n", error);
    }
    s->system_light = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT;
    s->mac = studio_detect_mac();
    apply_prefs(s, true);
    s->mods = SDL_GetModState();
    s->tab = STUDIO_TAB_OBJECT;
    s->assets_kind = -1;
    s->assets_hover = -1;
    s->cursor_shown = -1;
    s->hover_area = s->focus_area = s->maximized = -1;
    s->followed_tab = s->followed_kind = -1;
    s->bound.behavior = -2;
    s->view.zoom = 1;
    s->view.x = s->view.y = 0.5f;
    s->graph.area = -1;
    s->in.mouse_x = s->in.mouse_y = -1e9f;
    return s;
}

studio *studio_new(app_scene_authoring *scene, engine_viewport *viewport, const char *project_root)
{
    studio *s = create(scene, viewport, project_root, NULL);
    /* A guest opens no project: the window's Studio owns the workspace. */
    studio_projects_load(s);
    s->in.mouse_x = cf_mouse_x();
    s->in.mouse_y = cf_mouse_y();
    SDL_AddEventWatch(queue_event, s);
    return s;
}

void studio_guest_start(studio *s)
{
    s->guest_request = 1;
}

void studio_guest_stop(studio *s)
{
    s->guest_request = -1;
}

/* Starts or stops the guest between frames, never while either one's
 * documents are being emitted or drawn. */
static void guest_requests(studio *s)
{
    int request = s->guest_request;
    s->guest_request = 0;
    if (request > 0 && !s->guest) {
        s->guest = create(s->scene, s->viewport, s->project_root, s);
        /* The host already reported the authoring's last message. */
        snprintf(s->guest->seen_message, sizeof s->guest->seen_message, "%s", s->seen_message);
        return;
    }
    if (request < 0 && s->guest) {
        rml_backend_set_texture(s->backend, "guest", 0);
        studio_free(s->guest);
        s->guest = NULL;
        s->guest_hover = s->guest_keys = s->guest_capture = false;
    }
}

/* What the window's Studio holds of the window: its event watch, IME,
   cursor and saved preferences. A guest holds none of it. */
static void release_window(studio *s)
{
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
    editor_set_retained_keyboard(false);
}

void studio_free(studio *s)
{
    if (!s) {
        return;
    }
    studio_free(s->guest);
    if (!s->hosted) {
        release_window(s);
    }
    afree(s->events);
    afree(s->bound.bindings);
    afree(s->records);
    afree(s->runtime_errors);
    afree(s->project_problems);
    afree(s->references);
    afree(s->calls.rows);
    afree(s->members.uses.items);
    afree(s->perf);
    afree(s->keys);
    afree(s->game_input);
    studio_projects_free(s);
    chibi_project_free(&s->project);
    studio_disk_free(s);
    studio_refactor_free(s);
    studio_canvas_free(s);
    studio_add_menu_close(s);
    engine_viewport_set_host_input(s->viewport, NULL);
    studio_graph_free(s);
    component_presentation_free(&s->presentation.hints);
    studio_code_free(s);
    /* Visual designer (workstream H). */
    studio_designer_free(s);
    kit_destroy(s->kit);
    studio_ui_free(s->ui);
    /* After the panels' data, which shows the Inspector's strings. */
    studio_inspect_free(s->inspect);
    ui_page_free(&s->page);
    rml_backend_destroy(s->backend);
    free(s);
}

/* One frame of a Studio in a window of `width` by `height` points: the
 * window's, or a guest's in its host's Scene area. */
static void run(studio *s, double delta_seconds, float width, float height)
{
    float scale = cf_app_get_pixel_scale(), ratio = studio_pixel_ratio(s);
    rml_backend_resize_pixels(s->backend, (int)lroundf(width * scale), (int)lroundf(height * scale), ratio);
    s->time += delta_seconds;
    s->in.time = s->time;
    s->in.dp_ratio = ratio;
    s->in.width = to_dp(s, width);
    s->in.height = to_dp(s, height);
    forward_input(s);
    studio_performance_sample(s, delta_seconds);
    if (!s->hosted) {
        app_scene_authoring_update(s->scene, s->in.down[0] || kit_wants_keyboard(s->kit));
    }
    sync_theme(s);
    studio_ui_frame(s);
    share_project_prefs(s);
    studio_debug_frame(s);
    kit_begin(s->kit, &s->in);
    studio_emit(s);
    studio_inspect_emit(s);
    kit_end(s->kit);
    s->typing = kit_wants_keyboard(s->kit) || studio_graph_wants_keyboard(s);
    if (!s->typing) {
        studio_shortcuts(s);
    }
    apply_prefs(s, false);
    draw_viewport(s);
    if (s->guest) {
        kit_rect window = guest_window(s);
        run(s->guest, delta_seconds, fmaxf(window.w, 1), fmaxf(window.h, 1));
        rml_backend_set_texture(
            s->backend,
            "guest",
            rml_backend_texture(s->guest->backend)
        );
    }
    studio_graph_frame(s, delta_seconds);
    rml_backend_update(s->backend, delta_seconds);
    rml_backend_render(s->backend);
}

void studio_frame(studio *s, double delta_seconds)
{
    guest_requests(s);
    run(s, delta_seconds, (float)cf_app_get_width(), (float)cf_app_get_height());
    editor_set_retained_keyboard(s->typing || (s->guest && s->guest->typing));
    sync_text_input(s);
    sync_cursor(s);
    rml_backend_present(s->backend);
}
