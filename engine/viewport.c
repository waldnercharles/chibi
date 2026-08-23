#include "../core/xalloc.h"
#include "engine.h"

#if ENGINE_CUTE_AVAILABLE
#include <cute.h>
#else
#include <stdbool.h>
typedef struct CF_Canvas
{
    uint64_t id;
} CF_Canvas;
typedef struct CF_Texture
{
    uint64_t id;
} CF_Texture;
typedef struct
{
    unsigned char bytes[512];
} CF_CanvasParams;
CF_CanvasParams cf_canvas_defaults(int width, int height);
CF_Canvas cf_make_canvas(CF_CanvasParams params);
void cf_destroy_canvas(CF_Canvas canvas);
CF_Texture cf_canvas_get_target(CF_Canvas canvas);
uint64_t cf_texture_binding_handle(CF_Texture texture);
uint64_t cf_texture_handle(CF_Texture texture);
void cf_render_to(CF_Canvas canvas, bool clear);
#endif
#if ENGINE_IMGUI_AVAILABLE
#include <imgui/dcimgui.h>
#else
#include <stdbool.h>
typedef uint64_t ImTextureID;
typedef struct ImTextureData ImTextureData;
typedef struct ImTextureRef_t
{
    ImTextureData *data;
    ImTextureID id;
} ImTextureRef;
typedef struct ImVec2_t
{
    float x;
    float y;
} ImVec2;
bool ImGui_Begin(const char *name, bool *open, int flags);
void ImGui_End(void);
ImVec2 ImGui_GetContentRegionAvail(void);
void ImGui_Image(ImTextureRef texture, ImVec2 size);
#endif

#include <stdlib.h>
#include <string.h>

struct engine_viewport
{
    CF_Canvas canvas;
    int width;
    int height;
    int requested_width;
    int requested_height;
    int resize_pending;
    int stable_resize_frames;
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

int engine_viewport_draw(engine_viewport *viewport)
{
    if (!viewport || !viewport->canvas.id) {
        return -1;
    }
    cf_render_to(viewport->canvas, true);
    if (viewport->render) {
        viewport->render(viewport->render_user);
    }
    if (!ImGui_Begin("Scene", NULL, 0)) {
        ImGui_End();
        return 0;
    }
    ImVec2 size = ImGui_GetContentRegionAvail();
    int width = (int)size.x;
    int height = (int)size.y;
    if (width > 0 && height > 0) {
        engine_viewport_request_resize(viewport, width, height);
        if (viewport->stable_resize_frames >= 2) {
            engine_viewport_commit_resize(viewport);
        }
    }
    ImTextureRef texture = { 0 };
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    uint64_t binding_handle = cf_texture_binding_handle(target);
    (void)binding_handle;
    ImTextureID texture_id = (ImTextureID)cf_texture_handle(target);
    memcpy((unsigned char *)&texture + sizeof(void *), &texture_id, sizeof texture_id);
    ImGui_Image(texture, size);
    ImGui_End();
    return 0;
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
