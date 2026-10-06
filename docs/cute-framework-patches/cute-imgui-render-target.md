# Include Dear ImGui in canvas captures

Keep this patch. Upstream commit `7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d` still blits the application canvas before drawing ImGui. On SDL GPU, ImGui renders directly into the swapchain texture. A readback of the application canvas therefore omits ImGui. See the immutable [frame ordering](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_app.cpp#L573) and [ImGui draw implementation](https://github.com/RandyGaul/cute_framework/blob/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d/src/cute_imgui.cpp#L52).

The patch adds `cf_app_set_imgui_render_target`. With the application canvas selected, Cute draws ImGui into that canvas before blitting it to the window. Captures then contain the overlay. It also follows replacement of the default canvas after a resize, rather than retaining a destroyed canvas handle.

Chibi calls the setter in `src/automation/scene_preview.c`, `src/editor/workspace/application.c` for game mode, and `test/arena_play.c`. The preview controller reads the composed frame after drawing. Studio's RmlUi interface is a different rendering path.

An illustrative C excerpt, assuming an initialized app and ImGui context:

```c
CF_Canvas capture = cf_app_get_canvas();
bool selected = cf_app_set_imgui_render_target(capture);
if (!selected) return;
cf_app_update(NULL);
/* Build the frame and its ImGui overlay here. */
cf_app_draw_onto_screen(false);
/* Read back cf_app_get_canvas() now to include the overlay. */
```

Use `cf_app_get_canvas()` again after a resize. Setting a zero canvas restores drawing into the swapchain. A custom canvas must stay alive until frame drawing returns, and selecting another canvas does not automatically composite it into the app canvas.

The patch supports native GLES and SDL GPU. It rejects a nonzero target under Emscripten. On SDL GPU, upstream initializes the ImGui pipeline with the swapchain format and one sample. The setter does not validate target format, sample count, or MRT compatibility, so its return value does not prove an arbitrary canvas is compatible. The patch replaces ImGui's display size with target pixels without changing framebuffer scale. Chibi's preview explicitly sets framebuffer scale to one. Other HiDPI callers need to keep these values consistent.

The rebase changes context around the new `app_push_event` wrapper and the moved resize notification. It preserves the patch's behavior. Source inspection supports retention; this document does not claim a fresh rendered capture or HiDPI verification.

## Apply the patch

This diff targets Cute Framework commit [`7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d`](https://github.com/RandyGaul/cute_framework/commit/7c4e7a3aed3b2f9526a49b7ed3bfa6db18aa5c5d). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-imgui-render-target.patch
git apply cute-imgui-render-target.patch
```

Patch SHA-256: `9efc4b0a150700e205da1cf2850e6bacd8e96607ee82f68dfd0485cd667b94cc`.
