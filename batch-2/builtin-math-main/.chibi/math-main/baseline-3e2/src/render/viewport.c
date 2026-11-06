#include "render/viewport.h"

#include "platform/cute.h"
#include "render/audio.h"
#include "render/scene3d.h"
#include "render/ui.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "ui/input_feel.h"

#include <assert.h>
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
    float yaw, pitch, target_elevation, roll;
    engine_viewport_lens lens;
    bool panning;
    bool pointer_active;
    bool perspective;
    CF_V2 image_min, image_size;
    render_scene3d_camera image_camera;
    bool image_perspective;
    engine_viewport_pointer pointer;
    int requested_width;
    int requested_height;
    int resize_pending;
    int stable_resize_frames;
    bool sized; /* the canvas has taken a size the host asked for */
    int focused;
    bool host;
    engine_viewport_host_input host_input;
    CF_V2 host_delta, press_origin;
    float press_distance_sq;
    render_audio *audio;
    ui_world ui_world; /* no documents while its ecs is NULL */
    render_ui *ui;
    double ui_time;  /* engine_viewport_set_time's */
    double ui_drawn; /* ui_time at the documents' last draw, -1 before it */
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
        viewport->press_origin = input->press_position
                                   ? (CF_V2){ input->press_x, input->press_y }
                                   : (CF_V2){ input->x, input->y };
        viewport->press_distance_sq = 0;
    }
    if (input->pressed[0] || input->down[0] || input->released[0]) {
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

void engine_viewport_set_time(engine_viewport *viewport, double seconds)
{
    assert(seconds >= 0);
    viewport->ui_time = seconds;
}

/* The host's time since the documents last drew. Their first draw steps
   none, as their context joins RmlUi's shared clock where it stands. */
static double ui_step(engine_viewport *viewport)
{
    double drawn = viewport->ui_drawn;
    viewport->ui_drawn = viewport->ui_time;
    return drawn < 0 ? 0 : fmax(0, viewport->ui_time - drawn);
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
    viewport->ui_drawn = -1;
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
    viewport->sized |= !viewport->resize_pending;
    return 0;
}

/* A new size replaces the canvas once the host has asked for it two frames
   running, so a drag does not reallocate it every frame. The first replaces
   the placeholder at once, which the image would otherwise stretch. */
static int settle_resize(engine_viewport *viewport)
{
    if (!viewport->resize_pending ||
        (viewport->sized && viewport->stable_resize_frames < 2)) {
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
    viewport->sized = true;
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
    viewport->lens = (engine_viewport_lens){ 0 };
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
    viewport->lens = (engine_viewport_lens){ 0 };
}

void engine_viewport_get_view(const engine_viewport *viewport, engine_viewport_view *view)
{
    *view = (engine_viewport_view){
        viewport->center_x,    viewport->center_y,         viewport->view_width,
        viewport->view_height, viewport->focal_distance,   viewport->yaw,
        viewport->pitch,       viewport->target_elevation, viewport->roll,
        viewport->lens
    };
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
    assert(
        isfinite(view->yaw) && isfinite(view->pitch) && isfinite(view->roll) &&
        isfinite(view->target_elevation)
    );
    viewport->yaw = view->yaw;
    viewport->pitch = view->pitch;
    viewport->roll = view->roll;
    viewport->target_elevation = view->target_elevation;
    viewport->lens = view->lens;
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
    engine_viewport_lens lens = viewport->lens;
    if (!lens.orthographic && height > viewport->view_height &&
        cf_dot_v3(lens.forward, lens.forward) > 0) {
        float fov = lens.fov > 0 ? lens.fov : ENGINE_VIEWPORT_FOV_DEFAULT;
        float tangent = tanf(fov * 0.5f) * height / viewport->view_height;
        lens.fov = fminf(2 * atanf(tangent), nextafterf(CF_PI, 0));
    }
    *camera = (engine_viewport_camera){ viewport->center_x,
                                        viewport->center_y,
                                        width,
                                        height,
                                        viewport->focal_distance,
                                        viewport->yaw,
                                        viewport->pitch,
                                        viewport->target_elevation,
                                        viewport->roll,
                                        lens };
}

void engine_viewport_get_camera3d(const engine_viewport *viewport, render_scene3d_camera *camera)
{
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    *camera = render_scene3d_camera_frame(&frame);
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
    render_scene3d_camera camera;
    engine_viewport_get_camera3d(viewport, &camera);
    if (!render_scene3d_ground(&camera, ndc, world)) {
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

bool engine_viewport_mouse_ray(const engine_viewport *viewport, CF_Ray3 *ray)
{
    CF_V2 mouse = { viewport->host_input.x, viewport->host_input.y };
    CF_V2 size = viewport->image_size;
    if (!viewport->perspective || !viewport->image_perspective || size.x <= 0 ||
        size.y <= 0 || mouse.x < 0 || mouse.y < 0 || mouse.x >= size.x ||
        mouse.y >= size.y) {
        return false;
    }
    *ray = render_scene3d_ray(
        &viewport->image_camera,
        (CF_V2){ 2 * mouse.x / size.x - 1, 1 - 2 * mouse.y / size.y }
    );
    return true;
}

static void draw_viewport_canvas(engine_viewport *viewport)
{
    cf_draw_push();
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    viewport->image_perspective = viewport->perspective;
    if (viewport->image_perspective) {
        viewport->image_camera = render_scene3d_camera_frame(&camera);
    }
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
            ui_step(viewport)
        );
        render_ui_composite(viewport->ui, viewport->canvas);
    }
}

/* Replays a pointer over the displayed image into the game documents in
   canvas pixels, the modifier keys held (RML_INPUT_MOD_*) with it, which
   their events carry. `at` is relative to the image, `size` its size. */
static void ui_pointer(
    engine_viewport *viewport,
    CF_V2 at,
    CF_V2 size,
    bool hovered,
    bool pressed,
    bool released,
    bool active,
    unsigned modifiers
)
{
    viewport->ui_capture = false;
    if (!viewport->ui || !active || size.x <= 0 || size.y <= 0) {
        return;
    }
    float x = at.x * (float)viewport->width / size.x;
    float y = at.y * (float)viewport->height / size.y;
    rml_input_type move = hovered ? RML_INPUT_POINTER_MOVE : RML_INPUT_POINTER_LEAVE;
    unsigned captured = render_ui_input(
        viewport->ui,
        &(rml_input){ .type = move, .x = x, .y = y, .modifiers = modifiers }
    );
    if (pressed) {
        captured |= render_ui_input(
            viewport->ui,
            &(rml_input){ .type = RML_INPUT_POINTER_DOWN, .x = x, .y = y, .modifiers = modifiers }
        );
    }
    if (released) {
        captured |= render_ui_input(
            viewport->ui,
            &(rml_input){ .type = RML_INPUT_POINTER_UP, .x = x, .y = y, .modifiers = modifiers }
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
    /* Gestures move the overhead framing, so they take the view off a lens. */
    viewport->lens = (engine_viewport_lens){ 0 };
    engine_viewport_camera before;
    engine_viewport_get_camera(viewport, &before);
    if (zoom) {
        float x = io->mouse.x / size.x - 0.5f;
        float y = 0.5f - io->mouse.y / size.y;
        /* The rate every canvas zooms; the host clamped each notch. */
        float scale = 1 / feel_zoom_scale(io->wheel);
        scale = fmaxf(.0001f / before.span_width, fminf(100000.0f / before.span_width, scale));
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
    pointer->press_ndc_x = 2 * viewport->press_origin.x / size.x - 1;
    pointer->press_ndc_y = 1 - 2 * viewport->press_origin.y / size.y;
    /* A perspective pointer above the horizon still picks and drags by its
       ray; only its point on the ground is missing. */
    CF_V2 world;
    pointer->grounded = pointer_world(viewport, (CF_V2){ pointer->ndc_x, pointer->ndc_y }, &world);
    if (pointer->grounded) {
        pointer->world_x = world.x;
        pointer->world_y = world.y;
    }
    pointer->hovered = io->hovered && io->mouse.x >= 0 && io->mouse.y >= 0 &&
                       io->mouse.x < size.x && io->mouse.y < size.y;
    snprintf(
        pointer->dropped_prototype,
        sizeof pointer->dropped_prototype,
        "%s",
        viewport->host_input.dropped_prototype
    );
    bool press_inside = viewport->press_origin.x >= 0 &&
                        viewport->press_origin.y >= 0 &&
                        viewport->press_origin.x < size.x &&
                        viewport->press_origin.y < size.y;
    pointer->pressed = press_inside && io->clicked[0];
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

int engine_viewport_render(engine_viewport *viewport, float width, float height)
{
    if (width <= 0 || height <= 0) {
        return -1;
    }
    engine_viewport_request_resize(viewport, (int)width, (int)height);
    if (settle_resize(viewport) != 0) {
        return -1;
    }
    draw_viewport_canvas(viewport);
    viewport->image_size = (CF_V2){ width, height };
    return 0;
}

void engine_viewport_present(engine_viewport *viewport)
{
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_canvas(viewport->canvas, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_draw_pop();
}

void engine_viewport_set_image_origin(engine_viewport *viewport, float x, float y)
{
    viewport->image_min = (CF_V2){ x, y };
}

void engine_viewport_image_pointer(
    engine_viewport *viewport,
    float x,
    float y,
    bool pressed,
    bool released,
    float wheel,
    unsigned modifiers,
    bool active
)
{
    CF_V2 at = { x - viewport->image_min.x, y - viewport->image_min.y };
    CF_V2 size = viewport->image_size;
    bool inside = at.x >= 0 && at.y >= 0 && at.x < size.x && at.y < size.y;
    ui_pointer(viewport, at, size, inside, pressed, released, active, modifiers);
    ui_wheel(viewport, at, size, inside, wheel, active);
}

void engine_viewport_ui_event(engine_viewport *viewport, const rml_input *input)
{
    assert(viewport->ui);
    render_ui_input(viewport->ui, input);
}

void engine_viewport_ui_keyboard(engine_viewport *viewport, bool active)
{
    viewport->ui_typing = active && viewport->ui &&
                          render_ui_wants_text(viewport->ui);
}

int engine_viewport_draw(engine_viewport *viewport, const engine_viewport_controls *controls)
{
    assert(viewport->host);
    viewport->pointer = (engine_viewport_pointer){ 0 };
    /* Sound follows the session even while the viewport draws nothing. */
    bool session = viewport->runtime && controls->session;
    render_audio_update(
        viewport->audio,
        session ? engine_audio(viewport->runtime) : NULL,
        controls->playing
    );
    const engine_viewport_host_input *in = &viewport->host_input;
    viewport->image_size = (CF_V2){ in->width, in->height };
    viewport->focused = in->focused && !controls->editing;
    float resolution = fmaxf(in->resolution, 1);
    int width = (int)(in->width * resolution), height = (int)(in->height * resolution);
    if (width <= 0 || height <= 0) {
        cancel_viewport_pointer(viewport);
        return 0;
    }
    if (settle_resize(viewport) != 0) {
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
        controls->playing,
        in->modifiers
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
