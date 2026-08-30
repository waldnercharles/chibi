#include "../core/xalloc.h"
#include "../editor/ui_shell.h"
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
typedef struct
{
    float values[6];
} CF_M3x2;
CF_M3x2 cf_ortho_2d(float x, float y, float scale_x, float scale_y);
void cf_draw_projection(CF_M3x2 projection);
void cf_draw_push(void);
void cf_draw_pop(void);
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
enum
{
    ImGuiFocusedFlags_ChildWindows = 1 << 0,
};
bool ImGui_IsWindowFocused(int flags);
void ImGui_SetNextWindowFocus(void);
void ImGui_SetWindowFocusStr(const char *name);
ImVec2 ImGui_GetContentRegionAvail(void);
void ImGui_Image(ImTextureRef texture, ImVec2 size);
void ImGui_TextUnformatted(const char *text);
#endif

#include <stdint.h>
#include <stdlib.h>

struct engine_viewport
{
    CF_Canvas canvas;
    int width;
    int height;
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

static void draw_viewport_canvas(engine_viewport *viewport)
{
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(
        (float)viewport->width * 0.5f,
        (float)viewport->height * 0.5f,
        (float)viewport->width,
        (float)viewport->height
    ));
    if (viewport->render) {
        viewport->render(viewport->render_user);
    }
    cf_render_to(viewport->canvas, true);
    cf_draw_pop();
}

int engine_viewport_draw(engine_viewport *viewport)
{
    if (!viewport || !viewport->canvas.id) {
        return -1;
    }
    /* cf_render_to flushes the queued draw commands into the canvas, so the
     * scene must be queued before it, under a projection sized to this target
     * rather than the app window's. cf_draw_push/pop keep both off the
     * editor's own draw state. */
    draw_viewport_canvas(viewport);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("Scene", NULL, 0)) {
        viewport->focused = 0;
        ImGui_End();
        return 0;
    }
    /* ENG-3: the panel's own focus decides who consumes host input, read here
     * and acted on by the host next frame. */
    viewport->focused = ImGui_IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ? 1 : 0;
    ImGui_TextUnformatted(
        viewport->focused ? "Input: scene. Escape releases."
                          : "Input: graph. Click here to control the scene."
    );
    ImVec2 size = ImGui_GetContentRegionAvail();
    int width = (int)size.x;
    int height = (int)size.y;
    if (width > 0 && height > 0) {
        engine_viewport_request_resize(viewport, width, height);
        if (viewport->stable_resize_frames >= 2) {
            int resized = viewport->resize_pending;
            if (engine_viewport_commit_resize(viewport) != 0) {
                ImGui_End();
                return -1;
            }
            if (resized) {
                draw_viewport_canvas(viewport);
            }
        }
    }
    CF_Texture target = cf_canvas_get_target(viewport->canvas);
    ImTextureRef texture = {
        ._TexData = NULL,
        ._TexID = (ImTextureID)(uintptr_t)cf_texture_handle(target),
    };
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
