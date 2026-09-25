#include "editor/ui/ui_shell.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/arena3d.h"
#include "render/audio.h"
#include "render/ui.h"
#include "render/widgets.h"
#include "runtime/engine.h"
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
    ecs_world *widget_world;
    game_entities *widget_entities;
    const engine *widget_runtime;
    bool widget_focus;
    CF_Canvas canvas;
    int width;
    int height;
    float center_x;
    float center_y;
    float view_width;
    float view_height;
    bool panning;
    bool pointer_active;
    bool perspective;
    bool arena_showcase;
    float arena_showcase_time;
    ImVec2 image_min, image_size;
    engine_viewport_pointer pointer;
    int requested_width;
    int requested_height;
    int resize_pending;
    int stable_resize_frames;
    int focused;
    uint32_t recreate_count;
    bool host;
    engine_viewport_host_input host_input;
    CF_V2 host_delta, press_origin;
    float press_distance_sq;
    render_audio *audio;
    render_ui *ui;
    game_ui *ui_model;
    engine_viewport_render_fn render;
    void *render_user;
};

/* One frame of pointer input in logical window pixels, from ImGui or a retained host. */
typedef struct
{
    CF_V2 mouse, delta, min, size;
    bool hovered, popup, escape, focused, text_input, alt;
    bool down[3], clicked[3], released[3];
    float wheel, wheel_h;
    float drag_sq, drag_threshold;
} viewport_io;

static viewport_io imgui_io(ImVec2 min, ImVec2 size)
{
    ImGuiIO *io = ImGui_GetIO();
    viewport_io v = {
        .mouse = { io->MousePos.x, io->MousePos.y },
        .delta = { io->MouseDelta.x, io->MouseDelta.y },
        .min = { min.x, min.y },
        .size = { size.x, size.y },
        .hovered = ImGui_IsItemHovered(0),
        .popup = ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopup),
        .escape = ImGui_IsKeyPressed(ImGuiKey_Escape),
        .focused = ImGui_IsWindowFocused(ImGuiFocusedFlags_ChildWindows),
        .text_input = io->WantTextInput,
        .alt = io->KeyAlt,
        .wheel = io->MouseWheel,
        .wheel_h = io->MouseWheelH,
        .drag_sq = io->MouseDragMaxDistanceSqr[0],
        .drag_threshold = io->MouseDragThreshold,
    };
    for (int b = 0; b < 3; b++) {
        v.down[b] = io->MouseDown[b];
        v.clicked[b] = io->MouseClicked[b];
        v.released[b] = io->MouseReleased[b];
    }
    return v;
}

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
        .drag_threshold = 3,
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
}

uint64_t engine_viewport_texture(const engine_viewport *viewport)
{
    return viewport && viewport->canvas.id
             ? cf_canvas_get_target(viewport->canvas).id
             : 0;
}

void engine_viewport_set_widgets(
    engine_viewport *viewport,
    ecs_world *world,
    game_entities *entities,
    const engine *runtime
)
{
    if (world != viewport->widget_world) {
        viewport->widget_focus = false;
    }
    viewport->widget_world = world;
    viewport->widget_entities = entities;
    viewport->widget_runtime = runtime;
}

bool engine_viewport_widgets_capture(const engine_viewport *viewport)
{
    if (viewport->host) {
        return false;
    }
    ImVec2 mouse = ImGui_GetIO()->MousePos;
    return viewport->widget_world &&
           (viewport->widget_focus ||
            game_widgets_capture(
                viewport->widget_world,
                (CF_V2){ viewport->image_min.x, viewport->image_min.y },
                (CF_V2){ viewport->image_size.x, viewport->image_size.y },
                (CF_V2){ mouse.x, mouse.y }
            ));
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
    viewport->recreate_count = 1;
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

void engine_viewport_set_ui(engine_viewport *viewport, game_ui *model, const char *content_root)
{
    viewport->ui_model = model;
    if (!viewport->ui) {
        viewport->ui = render_ui_new(content_root);
    }
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

int engine_viewport_commit_resize(engine_viewport *viewport)
{
    if (!viewport || !viewport->resize_pending) {
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
    viewport->recreate_count++;
    return 0;
}

int engine_viewport_focused(const engine_viewport *viewport)
{
    return viewport ? viewport->focused : 0;
}

void engine_viewport_release_focus(void)
{
    if (ImGui_GetCurrentContext()) {
        ImGui_SetWindowFocusStr(NULL);
    }
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
                                    viewport->view_height };
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
    *camera = (engine_viewport_camera){ viewport->center_x, viewport->center_y, width, height };
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
    ImVec2 mouse = viewport->host
                     ? (ImVec2){ viewport->host_input.x, viewport->host_input.y }
                     : ImGui_GetIO()->MousePos;
    ImVec2 size = viewport->image_size, min = viewport->image_min;
    if (size.x <= 0 || size.y <= 0 || mouse.x < min.x || mouse.y < min.y ||
        mouse.x >= min.x + size.x || mouse.y >= min.y + size.y) {
        return false;
    }
    CF_V2 world;
    if (!pointer_world(
            viewport,
            (CF_V2){ 2 * (mouse.x - min.x) / size.x - 1,
                     1 - 2 * (mouse.y - min.y) / size.y },
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
    if (viewport->ui && viewport->widget_world) {
        render_ui_update(
            viewport->ui,
            viewport->widget_world,
            viewport->ui_model,
            viewport->width,
            viewport->height,
            CF_DELTA_TIME
        );
        render_ui_composite(viewport->ui, viewport->canvas);
    }
}

static engine_viewport_action draw_control(
    engine_viewport *viewport,
    const engine_viewport_controls *controls,
    int index,
    bool menu,
    bool *blocked
)
{
    const char *ids[] = { "viewport.play", "viewport.stop",
                          "viewport.step", "viewport.restart",
                          "viewport.fit",  "viewport.reset_camera" };
    const char *labels[] = { controls->playing ? "Pause simulation" : "Play simulation",
                             "Stop simulation",
                             "Step one frame",
                             "Restart simulation",
                             "Fit Scene",
                             "Reset view camera" };
    editor_icon glyphs[] = { controls->playing ? EDITOR_ICON_FA_PAUSE : EDITOR_ICON_FA_PLAY,
                             EDITOR_ICON_FA_STOP,
                             EDITOR_ICON_FA_FAST_FORWARD,
                             EDITOR_ICON_FA_REFRESH,
                             EDITOR_ICON_FA_EXPAND,
                             EDITOR_ICON_FA_CROSSHAIRS };
    engine_viewport_action actions[] = { controls->playing ? ENGINE_VIEWPORT_PAUSE
                                                           : ENGINE_VIEWPORT_PLAY,
                                         ENGINE_VIEWPORT_STOP,
                                         ENGINE_VIEWPORT_STEP,
                                         ENGINE_VIEWPORT_RESTART,
                                         ENGINE_VIEWPORT_FIT,
                                         ENGINE_VIEWPORT_NONE };
    bool enabled = (index != 1 && index != 3) || controls->session;
    enabled &= index != 2 || !controls->playing;
    bool pressed = menu
                     ? ImGui_MenuItemEx(labels[index], NULL, false, enabled)
                     : editor_ui_icon_button_sized(ids[index], glyphs[index], enabled, 32);
    editor_ui_track_item(ids[index]);
    /* Hovering the overlay is only visual feedback; an active control owns
     * the pointer so a passive hover can still reach the scene. */
    *blocked |= ImGui_IsItemActive();
    if (!pressed) {
        return ENGINE_VIEWPORT_NONE;
    }
    if (index == 5) {
        engine_viewport_reset_camera(viewport);
    }
    return actions[index];
}

static engine_viewport_action draw_controls(
    engine_viewport *viewport,
    const engine_viewport_controls *controls,
    bool compact,
    bool *blocked
)
{
    engine_viewport_action action = draw_control(viewport, controls, 0, false, blocked);
    if (!compact) {
        for (int i = 1; i < 6; i++) {
            ImGui_SameLine();
            engine_viewport_action next = draw_control(viewport, controls, i, false, blocked);
            if (next != ENGINE_VIEWPORT_NONE) {
                action = next;
            }
        }
        return action;
    }
    ImGui_SameLine();
    if (editor_ui_icon_button_sized("viewport.more", EDITOR_ICON_FA_ELLIPSIS_V, true, 32)) {
        editor_ui_open_popup("viewport_more");
    }
    editor_ui_track_item("viewport.more");
    *blocked |= ImGui_IsItemActive();
    if (editor_ui_begin_popup("viewport_more")) {
        *blocked = true;
        for (int i = 1; i < 6; i++) {
            engine_viewport_action next = draw_control(viewport, controls, i, true, blocked);
            if (next != ENGINE_VIEWPORT_NONE) {
                action = next;
            }
        }
        editor_ui_end_popup();
    }
    return action;
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
        CF_V2 min = io->min;
        float x = (io->mouse.x - min.x) / size.x - 0.5f;
        float y = 0.5f - (io->mouse.y - min.y) / size.y;
        float scale = powf(1.1f, -io->wheel);
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
    if (blocked || io->popup || io->wheel_h != 0 || io->escape || size.x <= 0 ||
        size.y <= 0) {
        cancel_viewport_pointer(viewport);
        return;
    }
    CF_V2 min = io->min;
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    engine_viewport_pointer *pointer = &viewport->pointer;
    pointer->world_per_pixel = camera.span_width / size.x;
    pointer->ndc_x = 2 * (io->mouse.x - min.x) / size.x - 1;
    pointer->ndc_y = 1 - 2 * (io->mouse.y - min.y) / size.y;
    CF_V2 world;
    if (!pointer_world(viewport, (CF_V2){ pointer->ndc_x, pointer->ndc_y }, &world)) {
        cancel_viewport_pointer(viewport);
        return;
    }
    pointer->world_x = world.x;
    pointer->world_y = world.y;
    pointer->hovered = io->hovered && io->mouse.x >= min.x &&
                       io->mouse.y >= min.y && io->mouse.x < min.x + size.x &&
                       io->mouse.y < min.y + size.y;
    pointer->delete_pressed = io->focused && !io->text_input &&
                              app_input_pressed(APP_INPUT_DELETE);
    if (viewport->host) {
        snprintf(
            pointer->dropped_prototype,
            sizeof pointer->dropped_prototype,
            "%s",
            viewport->host_input.dropped_prototype
        );
    } else if (ImGui_BeginDragDropTarget()) {
        const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CHIBI_PROTOTYPE", 0);
        if (payload && payload->DataSize > 0 &&
            payload->DataSize <= (int)sizeof pointer->dropped_prototype) {
            memcpy(pointer->dropped_prototype, payload->Data, (size_t)payload->DataSize);
            pointer->dropped_prototype[sizeof pointer->dropped_prototype - 1] = 0;
        }
        ImGui_EndDragDropTarget();
    }
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
    if (viewport->stable_resize_frames >= 2 &&
        engine_viewport_commit_resize(viewport) != 0) {
        return -1;
    }
    draw_viewport_canvas(viewport);
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
    ImGui_Image(texture, (ImVec2){ width, height });
    return 0;
}

engine_viewport_action engine_viewport_draw(engine_viewport *viewport, const engine_viewport_controls *controls)
{
    if (!viewport || !viewport->canvas.id) {
        return ENGINE_VIEWPORT_ERROR;
    }
    viewport->pointer = (engine_viewport_pointer){ 0 };
    viewport->image_size = (ImVec2){ 0 };
    if (viewport->host) {
        const engine_viewport_host_input *in = &viewport->host_input;
        int width = (int)in->width, height = (int)in->height;
        viewport->focused = in->focused && !(controls && controls->editing);
        if (width <= 0 || height <= 0) {
            cancel_viewport_pointer(viewport);
            return ENGINE_VIEWPORT_NONE;
        }
        engine_viewport_request_resize(viewport, width, height);
        if (viewport->stable_resize_frames >= 2 &&
            engine_viewport_commit_resize(viewport) != 0) {
            return ENGINE_VIEWPORT_ERROR;
        }
        draw_viewport_canvas(viewport);
        viewport->image_min = (ImVec2){ 0, 0 };
        viewport->image_size = (ImVec2){ in->width, in->height };
        viewport_io io = host_io(viewport);
        bool navigating = navigate_viewport(viewport, &io, controls && controls->playing);
        capture_viewport_pointer(viewport, &io, navigating);
        if (viewport->widget_world && controls && controls->session) {
            render_audio_update(
                viewport->audio,
                engine_audio(viewport->widget_runtime),
                controls->playing
            );
        }
        return ENGINE_VIEWPORT_NONE;
    }
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_SCENE)) {
        cancel_viewport_pointer(viewport);
        viewport->focused = 0;
        viewport->panning = false;
        return ENGINE_VIEWPORT_NONE;
    }
    if (editor_ui_pane_visible(EDITOR_UI_PANE_SCENE) &&
        editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_SCENE, "Scene", 0)) {
        cancel_viewport_pointer(viewport);
        viewport->focused = 0;
        ImGui_End();
        return ENGINE_VIEWPORT_NONE;
    }
    bool controls_blocked = false;
    engine_viewport_action action = ENGINE_VIEWPORT_NONE;
    bool editing = controls && controls->editing;
    bool scene_focused = ImGui_IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    bool scene_hovered = ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    viewport->focused = scene_focused && !editing;
    ImVec2 size = ImGui_GetContentRegionAvail();
    int width = (int)size.x;
    int height = (int)size.y;
    if (width <= 0 || height <= 0) {
        cancel_viewport_pointer(viewport);
        viewport->focused = 0;
        viewport->panning = false;
        ImGui_End();
        return action;
    }
    engine_viewport_request_resize(viewport, width, height);
    if (viewport->stable_resize_frames >= 2 &&
        engine_viewport_commit_resize(viewport) != 0) {
        ImGui_End();
        return ENGINE_VIEWPORT_ERROR;
    }
    draw_viewport_canvas(viewport);
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
    ImGui_Image(texture, size);
    editor_ui_track_item("viewport.image");
    viewport->image_min = ImGui_GetItemRectMin();
    float border = ImGui_GetStyle()->ImageBorderSize;
    viewport->image_min.x += border;
    viewport->image_min.y += border;
    viewport->image_size = size;
    viewport_io io = imgui_io(viewport->image_min, size);
    io.min = (CF_V2){ viewport->image_min.x, viewport->image_min.y };
    bool navigating = navigate_viewport(viewport, &io, controls && controls->playing);
    capture_viewport_pointer(viewport, &io, controls_blocked || navigating);
    bool show_controls = controls && (scene_hovered || scene_focused) &&
                         size.x >= 110 && size.y >= 100;
    float controls_alpha = editor_ui_controls_opacity("scene.controls.opacity", show_controls);
    if (controls && (show_controls || controls_alpha > 0) && size.x >= 110 &&
        size.y >= 100) {
        ImVec2 image_min = ImGui_GetItemRectMin();
        bool compact = size.x < 262;
        float toolbar_width = compact ? 86 : 238;
        ImGui_SetCursorScreenPos((ImVec2){ image_min.x + (size.x - toolbar_width) * 0.5f,
                                           image_min.y + 12 });
        ImGui_PushStyleVar(ImGuiStyleVar_Alpha, ImGui_GetStyle()->Alpha * fmaxf(1.0f / 255, controls_alpha));
        ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 10);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 8, 6 });
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){ 6, 0 });
        ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg, (ImVec4){ 0.12f, 0.13f, 0.15f, 0.96f });
        if (ImGui_BeginChild(
                "scene_view_controls",
                (ImVec2){ toolbar_width, 44 },
                ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                    (show_controls ? 0 : ImGuiWindowFlags_NoInputs)
            )) {
            engine_viewport_action next = draw_controls(viewport, controls, compact, &controls_blocked);
            if (show_controls) {
                action = next;
            } else {
                controls_blocked = false;
            }
        }
        ImGui_EndChild();
        ImGui_PopStyleColor();
        ImGui_PopStyleVarEx(4);
        ImGui_SetCursorScreenPos(image_min);
    }
    if (controls_blocked) {
        cancel_viewport_pointer(viewport);
        viewport->pointer.pressed = false;
        viewport->pointer.down = false;
        viewport->focused = 0;
        editor_ui_cancel_document_interaction(EDITOR_UI_PANE_SCENE);
    }
    if (navigating) {
        viewport->focused = 0;
        editor_ui_cancel_document_interaction(EDITOR_UI_PANE_SCENE);
    }
    if (viewport->widget_world && controls && controls->session) {
        render_audio_update(
            viewport->audio,
            engine_audio(viewport->widget_runtime),
            controls->playing
        );
        viewport->widget_focus = render_widgets(
            viewport->widget_world,
            viewport->widget_entities,
            engine_visibility(viewport->widget_runtime),
            (CF_V2){ viewport->image_min.x, viewport->image_min.y },
            (CF_V2){ viewport->image_size.x, viewport->image_size.y },
            controls->playing
        );
        if (engine_viewport_widgets_capture(viewport)) {
            cancel_viewport_pointer(viewport);
        }
    }
    if (controls && controls->error && controls->error[0]) {
        ImGui_SetCursorScreenPos((ImVec2){ viewport->image_min.x + 12,
                                           viewport->image_min.y + 52 });
        ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg, (ImVec4){ .08f, .07f, .06f, .96f });
        ImGui_PushStyleColorImVec4(ImGuiCol_Text, (ImVec4){ 1, .68f, .4f, 1 });
        if (ImGui_BeginChild(
                "render_error",
                (ImVec2){ fmaxf(1, viewport->image_size.x - 24), 0 },
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoSavedSettings
            )) {
            ImGui_TextWrapped("%s", controls->error);
            editor_ui_track_item("viewport.error");
        }
        ImGui_EndChild();
        ImGui_PopStyleColorEx(2);
    }
    ImGui_End();
    return action;
}

uint64_t engine_viewport_binding(const engine_viewport *viewport)
{
    if (!viewport || !viewport->canvas.id) {
        return 0;
    }
    return cf_texture_binding_handle(cf_canvas_get_target(viewport->canvas));
}

uint32_t engine_viewport_recreate_count(const engine_viewport *viewport)
{
    return viewport ? viewport->recreate_count : 0;
}

void engine_viewport_set_render_callback(
    engine_viewport *viewport,
    engine_viewport_render_fn fn,
    void *user
)
{
    if (!viewport) {
        return;
    }
    viewport->render = fn;
    viewport->render_user = user;
}
