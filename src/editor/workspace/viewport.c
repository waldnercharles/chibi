#include "editor/ui/input_feel.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/arena3d.h"
#include "render/audio.h"
#include "render/ui.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct engine_viewport
{
    const engine *runtime; /* plays its audio while a session runs */
    CF_Canvas canvas;
    int width;
    int height;
    float center_x;
    float center_y;
    float view_width;
    float view_height;
    float focal_distance;
    bool panning;
    bool pointer_active;
    bool perspective;
    bool arena_showcase;
    float arena_showcase_time;
    CF_V2 image_min, image_size; /* image_min is only set by engine_viewport_image */
    engine_viewport_pointer pointer;
    int requested_width;
    int requested_height;
    int resize_pending;
    int stable_resize_frames;
    int focused;
    bool host;
    engine_viewport_host_input host_input;
    CF_V2 host_delta, press_origin;
    float press_distance_sq;
    render_audio *audio;
    ui_world ui_world; /* no documents while its ecs is NULL */
    render_ui *ui;
    bool ui_capture; /* the documents took the pointer last frame */
    bool ui_typing;  /* they had the keyboard and a text field had focus */
    engine_viewport_render_fn render;
    void *render_user;
};

/* One frame of pointer input in logical pixels relative to the image. */
typedef struct
{
    CF_V2 mouse, delta, size;
    bool hovered, popup, escape, focused, alt;
    bool down[3], clicked[3], released[3];
    float wheel;
    float drag_sq, drag_threshold;
} viewport_io;

static viewport_io host_io(engine_viewport *viewport)
{
    const engine_viewport_host_input *in = &viewport->host_input;
    viewport_io v = {
        .mouse = { in->x, in->y },
        .delta = viewport->host_delta,
        .size = { in->width, in->height },
        .hovered = in->hovered && !in->blocked,
        .popup = in->blocked,
        .escape = in->escape,
        .focused = in->focused,
        .alt = in->alt,
        .wheel = in->wheel,
        .drag_sq = viewport->press_distance_sq,
        .drag_threshold = FEEL_DRAG_DP,
    };
    for (int b = 0; b < 3; b++) {
        v.down[b] = in->down[b];
        v.clicked[b] = in->pressed[b];
        v.released[b] = in->released[b];
    }
    return v;
}

void engine_viewport_set_host_input(engine_viewport *viewport, const engine_viewport_host_input *input)
{
    viewport->host = input != NULL;
    if (!input) {
        return;
    }
    if (input->pressed[0]) {
        viewport->press_origin = (CF_V2){ input->x, input->y };
        viewport->press_distance_sq = 0;
    } else if (input->down[0]) {
        float dx = input->x - viewport->press_origin.x,
              dy = input->y - viewport->press_origin.y;
        viewport->press_distance_sq = fmaxf(viewport->press_distance_sq, dx * dx + dy * dy);
    }
    viewport->host_delta = (CF_V2){ input->x - viewport->host_input.x,
                                    input->y - viewport->host_input.y };
    viewport->host_input = *input;
    /* The canvas takes the image's shape now, so the camera frames it before
       the draw, as a fit does. */
    float resolution = fmaxf(input->resolution, 1);
    engine_viewport_request_resize(
        viewport,
        (int)(input->width * resolution),
        (int)(input->height * resolution)
    );
}

uint64_t engine_viewport_texture(const engine_viewport *viewport)
{
    return viewport && viewport->canvas.id
             ? cf_canvas_get_target(viewport->canvas).id
             : 0;
}

void engine_viewport_set_runtime(engine_viewport *viewport, const engine *runtime)
{
    viewport->runtime = runtime;
}

void engine_viewport_set_ui(engine_viewport *viewport, const ui_world *world, const char *content_root)
{
    viewport->ui_world = *world;
    if (!viewport->ui) {
        viewport->ui = render_ui_new(content_root, NULL);
    }
}

void engine_viewport_adopt_ui(engine_viewport *viewport, render_ui *ui)
{
    assert(!viewport->ui);
    viewport->ui = ui;
}

render_ui *engine_viewport_ui(const engine_viewport *viewport)
{
    assert(viewport->ui);
    return viewport->ui;
}

bool engine_viewport_ui_capture(const engine_viewport *viewport)
{
    return viewport->ui_capture;
}

bool engine_viewport_ui_typing(const engine_viewport *viewport)
{
    return viewport->ui_typing;
}

static CF_Canvas viewport_canvas(int width, int height)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.depth_stencil_enable = true;
    return cf_make_canvas(params);
}

engine_viewport *engine_viewport_new(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    engine_viewport *viewport = (engine_viewport *)xcalloc(1, sizeof *viewport);
    viewport->width = width;
    viewport->height = height;
    engine_viewport_reset_camera(viewport);
    viewport->requested_width = width;
    viewport->requested_height = height;
    viewport->canvas = viewport_canvas(width, height);
    if (!viewport->canvas.id) {
        free(viewport);
        return NULL;
    }
    viewport->audio = render_audio_new();
    return viewport;
}

void engine_viewport_free(engine_viewport *viewport)
{
    if (!viewport) {
        return;
    }
    if (viewport->canvas.id) {
        cf_destroy_canvas(viewport->canvas);
    }
    render_audio_free(viewport->audio);
    render_ui_free(viewport->ui);
    free(viewport);
}

int engine_viewport_request_resize(engine_viewport *viewport, int width, int height)
{
    if (!viewport || width <= 0 || height <= 0) {
        return -1;
    }
    int same_request = width == viewport->requested_width &&
                       height == viewport->requested_height;
    viewport->requested_width = width;
    viewport->requested_height = height;
    if (same_request && viewport->resize_pending) {
        viewport->stable_resize_frames++;
    } else {
        viewport->stable_resize_frames = 0;
    }
    viewport->resize_pending = width != viewport->width || height != viewport->height;
    return 0;
}

static int commit_resize(engine_viewport *viewport)
{
    if (!viewport->resize_pending) {
        return 0;
    }
    CF_Canvas replacement = viewport_canvas(viewport->requested_width, viewport->requested_height);
    if (!replacement.id) {
        return -1;
    }
    cf_destroy_canvas(viewport->canvas);
    viewport->canvas = replacement;
    viewport->width = viewport->requested_width;
    viewport->height = viewport->requested_height;
    viewport->resize_pending = 0;
    viewport->stable_resize_frames = 0;
    return 0;
}

int engine_viewport_focused(const engine_viewport *viewport)
{
    return viewport ? viewport->focused : 0;
}

void engine_viewport_reset_camera(engine_viewport *viewport)
{
    viewport->center_x = 320.0f;
    viewport->center_y = 180.0f;
    viewport->view_width = 640.0f;
    viewport->view_height = 360.0f;
    viewport->panning = false;
}

void engine_viewport_fit(engine_viewport *viewport, float min_x, float min_y, float max_x, float max_y)
{
    assert(isfinite(min_x) && isfinite(min_y) && isfinite(max_x) && isfinite(max_y));
    assert(min_x <= max_x && min_y <= max_y);
    viewport->center_x = min_x + (max_x - min_x) * 0.5f;
    viewport->center_y = min_y + (max_y - min_y) * 0.5f;
    viewport->view_width = fmaxf(1.0f, max_x - min_x) * 1.2f;
    viewport->view_height = fmaxf(1.0f, max_y - min_y) * 1.2f;
}

void engine_viewport_get_view(const engine_viewport *viewport, engine_viewport_view *view)
{
    *view = (engine_viewport_view){ viewport->center_x,
                                    viewport->center_y,
                                    viewport->view_width,
                                    viewport->view_height,
                                    viewport->focal_distance };
}

void engine_viewport_set_view(engine_viewport *viewport, const engine_viewport_view *view)
{
    assert(isfinite(view->center_x) && isfinite(view->center_y));
    assert(isfinite(view->width) && isfinite(view->height));
    assert(view->width > 0.0f && view->height > 0.0f);
    viewport->center_x = view->center_x;
    viewport->center_y = view->center_y;
    viewport->view_width = view->width;
    viewport->view_height = view->height;
    assert(isfinite(view->focal_distance) && view->focal_distance >= 0);
    assert(view->focal_distance == 0 || view->focal_distance > 1);
    viewport->focal_distance = view->focal_distance;
    viewport->panning = false;
}

void engine_viewport_pixel_size(const engine_viewport *viewport, int *width, int *height)
{
    *width = viewport->width;
    *height = viewport->height;
}

void engine_viewport_get_camera(const engine_viewport *viewport, engine_viewport_camera *camera)
{
    float aspect = (float)viewport->requested_width / (float)viewport->requested_height;
    float width = viewport->view_width;
    float height = viewport->view_height;
    if (width / height > aspect) {
        height = width / aspect;
    } else {
        width = height * aspect;
    }
    *camera = (engine_viewport_camera){ viewport->center_x,
                                        viewport->center_y,
                                        width,
                                        height,
                                        viewport->focal_distance };
}

void engine_viewport_set_arena_showcase(engine_viewport *viewport, float time)
{
    assert(isfinite(time));
    viewport->arena_showcase = true;
    viewport->arena_showcase_time = time;
}

void engine_viewport_clear_arena_camera(engine_viewport *viewport)
{
    viewport->arena_showcase = false;
}

void engine_viewport_get_arena_camera(const engine_viewport *viewport, render_arena_camera *camera)
{
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    *camera = viewport->arena_showcase ? render_arena_camera_make_showcase(
                                             frame.center_x,
                                             frame.center_y,
                                             frame.span_width,
                                             frame.span_height,
                                             viewport->arena_showcase_time
                                         )
                                       : render_arena_camera_make(
                                             frame.center_x,
                                             frame.center_y,
                                             frame.span_width,
                                             frame.span_height
                                         );
}

void engine_viewport_set_perspective(engine_viewport *viewport, bool perspective)
{
    viewport->perspective = perspective;
}

bool engine_viewport_perspective(const engine_viewport *viewport)
{
    return viewport->perspective;
}

static bool pointer_world(const engine_viewport *viewport, CF_V2 ndc, CF_V2 *world)
{
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    if (!viewport->perspective) {
        *world = (CF_V2){ frame.center_x + ndc.x * frame.span_width * 0.5f,
                          frame.center_y + ndc.y * frame.span_height * 0.5f };
        return true;
    }
    render_arena_camera camera;
    engine_viewport_get_arena_camera(viewport, &camera);
    if (!render_arena_ground(&camera, ndc, world)) {
        return false;
    }
    world->y = -world->y;
    return true;
}

bool engine_viewport_mouse_world(const engine_viewport *viewport, float *x, float *y)
{
    CF_V2 mouse = { viewport->host_input.x, viewport->host_input.y };
    CF_V2 size = viewport->image_size;
    if (size.x <= 0 || size.y <= 0 || mouse.x < 0 || mouse.y < 0 ||
        mouse.x >= size.x || mouse.y >= size.y) {
        return false;
    }
    CF_V2 world;
    if (!pointer_world(
            viewport,
            (CF_V2){ 2 * mouse.x / size.x - 1, 1 - 2 * mouse.y / size.y },
            &world
        )) {
        return false;
    }
    *x = world.x;
    *y = world.y;
    return true;
}

static void draw_viewport_canvas(engine_viewport *viewport)
{
    cf_draw_push();
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    cf_draw_projection(
        cf_ortho_2d(camera.center_x, camera.center_y, camera.span_width, camera.span_height)
    );
    if (viewport->render) {
        viewport->render(viewport->render_user);
    }
    cf_render_to(viewport->canvas, true);
    cf_draw_pop();
    if (viewport->ui && viewport->ui_world.ecs) {
        render_ui_update(
            viewport->ui,
            &viewport->ui_world,
            viewport->width,
            viewport->height,
            CF_DELTA_TIME
        );
        render_ui_composite(viewport->ui, viewport->canvas);
    }
}

/* Replays a pointer over the displayed image into the game documents in
   canvas pixels. `at` is relative to the image, `size` its size. */
static void ui_pointer(
    engine_viewport *viewport,
    CF_V2 at,
    CF_V2 size,
    bool hovered,
    bool pressed,
    bool released,
    bool active
)
{
    viewport->ui_capture = false;
    if (!viewport->ui || !active || size.x <= 0 || size.y <= 0) {
        return;
    }
    float x = at.x * (float)viewport->width / size.x;
    float y = at.y * (float)viewport->height / size.y;
    rml_input_type move = hovered ? RML_INPUT_POINTER_MOVE : RML_INPUT_POINTER_LEAVE;
    unsigned captured = render_ui_input(viewport->ui, &(rml_input){ .type = move, .x = x, .y = y });
    if (pressed) {
        captured |= render_ui_input(
            viewport->ui,
            &(rml_input){ .type = RML_INPUT_POINTER_DOWN, .x = x, .y = y }
        );
    }
    if (released) {
        captured |= render_ui_input(
            viewport->ui,
            &(rml_input){ .type = RML_INPUT_POINTER_UP, .x = x, .y = y }
        );
    }
    viewport->ui_capture = (captured & RML_INPUT_CAPTURE_POINTER) != 0;
}

static bool ui_wheel(engine_viewport *viewport, CF_V2 at, CF_V2 size, bool hovered, float wheel, bool active)
{
    if (!viewport->ui || !active || !hovered || size.x <= 0 || size.y <= 0 ||
        wheel == 0) {
        return false;
    }
    unsigned captured = render_ui_input(
        viewport->ui,
        &(rml_input){ .type = RML_INPUT_POINTER_WHEEL,
                      .x = at.x * (float)viewport->width / size.x,
                      .y = at.y * (float)viewport->height / size.y,
                      .wheel_y = -wheel }
    );
    bool scrolled = (captured & RML_INPUT_CAPTURE_POINTER) != 0;
    viewport->ui_capture |= scrolled;
    return scrolled;
}

/* Sends the host's keys and text to the game documents while the host's
   keyboard is the image's and `active`. */
static void ui_keys(engine_viewport *viewport, const engine_viewport_host_input *in, bool active)
{
    bool keyboard = viewport->ui && active && in->focused;
    for (int i = 0; keyboard && i < in->key_count; i++) {
        render_ui_input(viewport->ui, &in->keys[i]);
    }
    viewport->ui_typing = keyboard && render_ui_wants_text(viewport->ui);
}

static bool navigate_viewport(engine_viewport *viewport, const viewport_io *io, bool playing)
{
    CF_V2 size = io->size;
    if (playing && !io->alt) {
        viewport->panning = false;
        return false;
    }
    bool hover = io->hovered;
    if (!io->down[2]) {
        viewport->panning = false;
    }
    if (io->popup) {
        viewport->panning = false;
        return false;
    }
    bool pan_started = hover && io->clicked[2];
    if (pan_started) {
        viewport->panning = true;
    }
    bool zoom = hover && io->wheel != 0.0f;
    if (!viewport->panning && !zoom) {
        return false;
    }
    engine_viewport_camera before;
    engine_viewport_get_camera(viewport, &before);
    if (zoom) {
        float x = io->mouse.x / size.x - 0.5f;
        float y = 0.5f - io->mouse.y / size.y;
        /* The rate every canvas zooms; the host clamped each notch. */
        float scale = 1 / feel_zoom_scale(io->wheel);
        scale = fmaxf(1.0f / before.span_width, fminf(100000.0f / before.span_width, scale));
        CF_V2 anchor;
        bool anchored = pointer_world(viewport, (CF_V2){ 2 * x, 2 * y }, &anchor);
        viewport->view_width *= scale;
        viewport->view_height *= scale;
        CF_V2 after;
        if (anchored && pointer_world(viewport, (CF_V2){ 2 * x, 2 * y }, &after)) {
            viewport->center_x += anchor.x - after.x;
            viewport->center_y += anchor.y - after.y;
        }
    }
    /* The press-frame delta precedes the gesture and may come from another pane. */
    if (viewport->panning && !pan_started) {
        CF_V2 origin, delta;
        if (pointer_world(viewport, (CF_V2){ 0, 0 }, &origin) &&
            pointer_world(
                viewport,
                (CF_V2){ 2 * io->delta.x / size.x, -2 * io->delta.y / size.y },
                &delta
            )) {
            viewport->center_x -= delta.x - origin.x;
            viewport->center_y -= delta.y - origin.y;
        }
    }
    return true;
}

void engine_viewport_get_pointer(const engine_viewport *viewport, engine_viewport_pointer *pointer)
{
    *pointer = viewport->pointer;
}

static void cancel_viewport_pointer(engine_viewport *viewport)
{
    viewport->pointer.cancel = viewport->pointer_active;
    viewport->pointer_active = false;
}

static void capture_viewport_pointer(engine_viewport *viewport, const viewport_io *io, bool blocked)
{
    CF_V2 size = io->size;
    if (blocked || io->popup || io->escape || size.x <= 0 || size.y <= 0) {
        cancel_viewport_pointer(viewport);
        return;
    }
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    engine_viewport_pointer *pointer = &viewport->pointer;
    pointer->world_per_pixel = camera.span_width / size.x;
    pointer->ndc_x = 2 * io->mouse.x / size.x - 1;
    pointer->ndc_y = 1 - 2 * io->mouse.y / size.y;
    CF_V2 world;
    if (!pointer_world(viewport, (CF_V2){ pointer->ndc_x, pointer->ndc_y }, &world)) {
        cancel_viewport_pointer(viewport);
        return;
    }
    pointer->world_x = world.x;
    pointer->world_y = world.y;
    pointer->hovered = io->hovered && io->mouse.x >= 0 && io->mouse.y >= 0 &&
                       io->mouse.x < size.x && io->mouse.y < size.y;
    snprintf(
        pointer->dropped_prototype,
        sizeof pointer->dropped_prototype,
        "%s",
        viewport->host_input.dropped_prototype
    );
    pointer->pressed = pointer->hovered && io->clicked[0];
    if (pointer->pressed) {
        viewport->pointer_active = true;
    }
    if (!viewport->pointer_active) {
        return;
    }
    pointer->down = io->down[0];
    pointer->released = io->released[0];
    pointer->dragging = io->drag_sq >= io->drag_threshold * io->drag_threshold;
    if (pointer->released) {
        viewport->pointer_active = false;
    } else if (!pointer->down) {
        cancel_viewport_pointer(viewport);
    }
}

int engine_viewport_image(engine_viewport *viewport, float width, float height)
{
    if (width <= 0 || height <= 0) {
        return -1;
    }
    engine_viewport_request_resize(viewport, (int)width, (int)height);
    if (viewport->stable_resize_frames >= 2 && commit_resize(viewport) != 0) {
        return -1;
    }
    draw_viewport_canvas(viewport);
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
    ImGui_Image(texture, (ImVec2){ width, height });
    ImVec2 min = ImGui_GetItemRectMin();
    viewport->image_min = (CF_V2){ min.x, min.y };
    viewport->image_size = (CF_V2){ width, height };
    return 0;
}

void engine_viewport_ui_input(engine_viewport *viewport, bool active)
{
    ImGuiIO *io = ImGui_GetIO();
    CF_V2 at = { io->MousePos.x - viewport->image_min.x,
                 io->MousePos.y - viewport->image_min.y };
    CF_V2 size = viewport->image_size;
    bool inside = at.x >= 0 && at.y >= 0 && at.x < size.x && at.y < size.y;
    ui_pointer(viewport, at, size, inside, io->MouseClicked[0], io->MouseReleased[0], active);
    ui_wheel(viewport, at, size, inside, io->MouseWheel, active);
}

/* The keys game documents bind with data-keys or edit text with, besides
   letters and digits. */
static const struct
{
    ImGuiKey imgui;
    rml_input_key rml;
} UI_KEYS[] = {
    { ImGuiKey_Space, RML_INPUT_KEY_SPACE },
    { ImGuiKey_Enter, RML_INPUT_KEY_ENTER },
    { ImGuiKey_KeypadEnter, RML_INPUT_KEY_NUMPAD_ENTER },
    { ImGuiKey_Escape, RML_INPUT_KEY_ESCAPE },
    { ImGuiKey_Tab, RML_INPUT_KEY_TAB },
    { ImGuiKey_Backspace, RML_INPUT_KEY_BACKSPACE },
    { ImGuiKey_Delete, RML_INPUT_KEY_DELETE },
    { ImGuiKey_LeftArrow, RML_INPUT_KEY_LEFT },
    { ImGuiKey_RightArrow, RML_INPUT_KEY_RIGHT },
    { ImGuiKey_UpArrow, RML_INPUT_KEY_UP },
    { ImGuiKey_DownArrow, RML_INPUT_KEY_DOWN },
    { ImGuiKey_Home, RML_INPUT_KEY_HOME },
    { ImGuiKey_End, RML_INPUT_KEY_END },
    { ImGuiKey_PageUp, RML_INPUT_KEY_PAGE_UP },
    { ImGuiKey_PageDown, RML_INPUT_KEY_PAGE_DOWN },
};

static rml_input_key ui_key(ImGuiKey key)
{
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
        return (rml_input_key)(RML_INPUT_KEY_A + (key - ImGuiKey_A));
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
        return (rml_input_key)(RML_INPUT_KEY_0 + (key - ImGuiKey_0));
    }
    for (size_t i = 0; i < sizeof UI_KEYS / sizeof *UI_KEYS; i++) {
        if (UI_KEYS[i].imgui == key) {
            return UI_KEYS[i].rml;
        }
    }
    return RML_INPUT_KEY_UNKNOWN;
}

/* ImGui's modifiers as RmlUi's. On macOS Command edits text (select all,
   copy, paste) where RmlUi expects Control, so the two trade places. */
static unsigned ui_modifiers(const ImGuiIO *io)
{
#ifdef __APPLE__
    bool control = io->KeySuper, meta = io->KeyCtrl;
#else
    bool control = io->KeyCtrl, meta = io->KeySuper;
#endif
    return (control ? RML_INPUT_MOD_CONTROL : 0u) |
           (io->KeyShift ? RML_INPUT_MOD_SHIFT : 0u) |
           (io->KeyAlt ? RML_INPUT_MOD_ALT : 0u) | (meta ? RML_INPUT_MOD_META : 0u);
}

/* Appends code point `c` to `text` as UTF-8. */
static void append_utf8(char **text, unsigned c)
{
    static const unsigned char LEAD[] = { 0x00, 0xC0, 0xE0, 0xF0 };
    int extra = c < 0x80 ? 0 : c < 0x800 ? 1 : c < 0x10000 ? 2 : 3;
    apush(*text, (char)(LEAD[extra] | c >> (6 * extra)));
    for (int i = extra - 1; i >= 0; i--) {
        apush(*text, (char)(0x80 | (c >> (6 * i) & 0x3F)));
    }
}

/* This frame's typed text, taken from Cute's buffer, as an array without a
   terminator; NULL when there is none. The buffer is read in order: its pop
   takes the last code point. */
static char *typed_text()
{
    char *text = NULL;
    CF_InputTextBuffer buffer;
    cf_input_text_get_buffer(&buffer);
    for (int i = 0; i < buffer.len; i++) {
        append_utf8(&text, (unsigned)buffer.codepoints[i]);
    }
    cf_input_text_clear();
    return text;
}

void engine_viewport_ui_keys(engine_viewport *viewport, bool active)
{
    assert(viewport->ui);
    ImGuiIO *io = ImGui_GetIO();
    char *text = typed_text();
    unsigned modifiers = ui_modifiers(io);
    for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
        rml_input_key rml = ui_key(key);
        if (rml == RML_INPUT_KEY_UNKNOWN) {
            continue;
        }
        if (active && ImGui_IsKeyPressedEx(key, false)) {
            render_ui_input(
                viewport->ui,
                &(rml_input){ .type = RML_INPUT_KEY_PRESS, .key = rml, .modifiers = modifiers }
            );
        }
        if (ImGui_IsKeyReleased(key)) {
            render_ui_input(
                viewport->ui,
                &(rml_input){ .type = RML_INPUT_KEY_RELEASE, .key = rml, .modifiers = modifiers }
            );
        }
    }
    if (active && text) {
        render_ui_input(
            viewport->ui,
            &(rml_input){ .type = RML_INPUT_TEXT_INPUT,
                          .modifiers = modifiers,
                          .text = text,
                          .text_size = (size_t)acount(text) }
        );
    }
    afree(text);
    viewport->ui_typing = active && render_ui_wants_text(viewport->ui);
}

int engine_viewport_draw(engine_viewport *viewport, const engine_viewport_controls *controls)
{
    assert(viewport->host);
    viewport->pointer = (engine_viewport_pointer){ 0 };
    const engine_viewport_host_input *in = &viewport->host_input;
    viewport->image_size = (CF_V2){ in->width, in->height };
    viewport->focused = in->focused && !controls->editing;
    float resolution = fmaxf(in->resolution, 1);
    int width = (int)(in->width * resolution), height = (int)(in->height * resolution);
    if (width <= 0 || height <= 0) {
        cancel_viewport_pointer(viewport);
        return 0;
    }
    if (viewport->stable_resize_frames >= 2 && commit_resize(viewport) != 0) {
        return -1;
    }
    draw_viewport_canvas(viewport);
    ui_pointer(
        viewport,
        (CF_V2){ in->x, in->y },
        viewport->image_size,
        in->hovered,
        in->pressed[0],
        in->released[0],
        controls->playing
    );
    bool scrolled = ui_wheel(
        viewport,
        (CF_V2){ in->x, in->y },
        viewport->image_size,
        in->hovered,
        in->wheel,
        controls->playing
    );
    ui_keys(viewport, in, controls->playing);
    viewport_io io = host_io(viewport);
    if (scrolled) {
        io.wheel = 0;
    }
    bool navigating = navigate_viewport(viewport, &io, controls->playing);
    capture_viewport_pointer(viewport, &io, navigating || viewport->ui_capture);
    if (viewport->runtime && controls->session) {
        render_audio_update(
            viewport->audio,
            engine_audio(viewport->runtime),
            controls->playing
        );
    }
    return 0;
}

void engine_viewport_set_render_callback(
    engine_viewport *viewport,
    engine_viewport_render_fn fn,
    void *user
)
{
    viewport->render = fn;
    viewport->render_user = user;
}
