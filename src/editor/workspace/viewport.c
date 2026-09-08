#include "editor/ui/ui_shell.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
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
    CF_Canvas canvas;
    int width;
    int height;
    float center_x;
    float center_y;
    float view_width;
    float view_height;
    bool panning;
    bool pointer_active;
    engine_viewport_pointer pointer;
    int requested_width;
    int requested_height;
    int resize_pending;
    int stable_resize_frames;
    int focused;
    uint32_t recreate_count;
    engine_viewport_render_fn render;
    void *render_user;
};

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
    viewport->canvas = cf_make_canvas(cf_canvas_defaults(width, height));
    if (!viewport->canvas.id) {
        free(viewport);
        return NULL;
    }
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

int engine_viewport_commit_resize(engine_viewport *viewport)
{
    if (!viewport || !viewport->resize_pending) {
        return 0;
    }
    CF_Canvas replacement = cf_make_canvas(
        cf_canvas_defaults(viewport->requested_width, viewport->requested_height)
    );
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
    ImGui_SetWindowFocusStr(NULL);
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

static bool navigate_viewport(engine_viewport *viewport, ImVec2 size, bool playing)
{
#if ENGINE_IMGUI_AVAILABLE
    ImGuiIO *io = ImGui_GetIO();
    if (playing && !io->KeyAlt) {
        viewport->panning = false;
        return false;
    }
    bool hover = ImGui_IsItemHovered(0);
    if (!io->MouseDown[2]) {
        viewport->panning = false;
    }
    if (ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopup)) {
        viewport->panning = false;
        return false;
    }
    bool pan_started = hover && io->MouseClicked[2];
    if (pan_started) {
        viewport->panning = true;
    }
    bool zoom = hover && io->MouseWheel != 0.0f;
    if (!viewport->panning && !zoom) {
        return false;
    }
    engine_viewport_camera before;
    engine_viewport_get_camera(viewport, &before);
    if (zoom) {
        ImVec2 min = ImGui_GetItemRectMin();
        /* ImGui's item rect includes its image border; projection coordinates
         * start inside that border, at the first texture pixel. */
        float border = ImGui_GetStyle()->ImageBorderSize;
        min.x += border;
        min.y += border;
        float x = (io->MousePos.x - min.x) / size.x - 0.5f;
        float y = 0.5f - (io->MousePos.y - min.y) / size.y;
        float scale = powf(1.1f, -io->MouseWheel);
        scale = fmaxf(1.0f / before.span_width, fminf(100000.0f / before.span_width, scale));
        viewport->view_width *= scale;
        viewport->view_height *= scale;
        viewport->center_x += x * before.span_width * (1.0f - scale);
        viewport->center_y += y * before.span_height * (1.0f - scale);
    }
    /* The press-frame delta precedes the gesture and may come from another pane. */
    if (viewport->panning && !pan_started) {
        viewport->center_x -= io->MouseDelta.x * before.span_width / size.x;
        viewport->center_y += io->MouseDelta.y * before.span_height / size.y;
    }
    return true;
#else
    (void)viewport;
    (void)size;
    (void)playing;
    return false;
#endif
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

static void capture_viewport_pointer(engine_viewport *viewport, ImVec2 size, bool blocked)
{
#if ENGINE_IMGUI_AVAILABLE
    ImGuiIO *io = ImGui_GetIO();
    bool popup = ImGui_IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopup);
    if (blocked || popup || io->MouseWheelH != 0 ||
        ImGui_IsKeyPressed(ImGuiKey_Escape) || size.x <= 0 || size.y <= 0) {
        cancel_viewport_pointer(viewport);
        return;
    }
    ImVec2 min = ImGui_GetItemRectMin();
    float border = ImGui_GetStyle()->ImageBorderSize;
    min.x += border;
    min.y += border;
    engine_viewport_camera camera;
    engine_viewport_get_camera(viewport, &camera);
    engine_viewport_pointer *pointer = &viewport->pointer;
    pointer->world_per_pixel = camera.span_width / size.x;
    pointer->world_x = camera.center_x + ((io->MousePos.x - min.x) / size.x - 0.5f) *
                                             camera.span_width;
    pointer->world_y = camera.center_y + (0.5f - (io->MousePos.y - min.y) / size.y) *
                                             camera.span_height;
    pointer->hovered = ImGui_IsItemHovered(0) && io->MousePos.x >= min.x &&
                       io->MousePos.y >= min.y && io->MousePos.x < min.x + size.x &&
                       io->MousePos.y < min.y + size.y;
    pointer->delete_pressed = ImGui_IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
                              !io->WantTextInput &&
                              app_input_pressed(APP_INPUT_DELETE);
    if (ImGui_BeginDragDropTarget()) {
        const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CBLUEPRINT_PROTOTYPE", 0);
        if (payload && payload->DataSize > 0 &&
            payload->DataSize <= (int)sizeof pointer->dropped_prototype) {
            memcpy(pointer->dropped_prototype, payload->Data, (size_t)payload->DataSize);
            pointer->dropped_prototype[sizeof pointer->dropped_prototype - 1] = 0;
        }
        ImGui_EndDragDropTarget();
    }
    pointer->pressed = pointer->hovered && io->MouseClicked[0];
    if (pointer->pressed) {
        viewport->pointer_active = true;
    }
    if (!viewport->pointer_active) {
        return;
    }
    pointer->down = io->MouseDown[0];
    pointer->released = io->MouseReleased[0];
    pointer->dragging = io->MouseDragMaxDistanceSqr[0] >=
                        io->MouseDragThreshold * io->MouseDragThreshold;
    if (pointer->released) {
        viewport->pointer_active = false;
    } else if (!pointer->down) {
        cancel_viewport_pointer(viewport);
    }
#else
    (void)viewport;
    (void)size;
    (void)blocked;
#endif
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
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
    ImGui_Image(texture, (ImVec2){ width, height });
    draw_viewport_canvas(viewport);
    return 0;
}

engine_viewport_action engine_viewport_draw(engine_viewport *viewport, const engine_viewport_controls *controls)
{
    if (!viewport || !viewport->canvas.id) {
        return ENGINE_VIEWPORT_ERROR;
    }
    viewport->pointer = (engine_viewport_pointer){ 0 };
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
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
    ImGui_Image(texture, size);
    editor_ui_track_item("viewport.image");
    bool navigating = navigate_viewport(viewport, size, controls && controls->playing);
    capture_viewport_pointer(viewport, size, controls_blocked || navigating);
#if ENGINE_IMGUI_AVAILABLE
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
#else
    if (controls && (scene_hovered || scene_focused)) {
        action = draw_controls(viewport, controls, false, &controls_blocked);
    }
#endif
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
    /* Match projection to the displayed image immediately. Canvas allocation
     * may settle over several frames, but its temporary scaling must not change
     * object proportions or pointer coordinates. */
    draw_viewport_canvas(viewport);
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
