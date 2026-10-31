# Include Dear ImGui in canvas captures

Keep this patch. Upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc` still blits the application canvas before drawing ImGui into the swapchain, so a readback of the application canvas omits ImGui.

The patch adds `cf_app_set_imgui_render_target`. With the application canvas selected, Cute draws ImGui into that canvas before blitting it to the window, so captures contain the overlay. The target follows replacement of the default canvas after a resize.

Studio calls the setter in `src/editor/workspace/application.c` for game mode, so its captures read the composed frame. Studio's RmlUi interface is a different rendering path.

```c
CF_Canvas capture = cf_app_get_canvas();
if (!cf_app_set_imgui_render_target(capture)) return;
cf_app_update(NULL);
/* Build the frame and its ImGui overlay here. */
cf_app_draw_onto_screen(false);
/* Read back cf_app_get_canvas() now to include the overlay. */
```

Setting a zero canvas restores drawing into the swapchain. A custom canvas must stay alive until frame drawing returns.

The patch supports native GLES and SDL_GPU. It rejects a nonzero target under Emscripten and on the WebGPU backend, which draws ImGui through its own path. On SDL_GPU, upstream initializes the ImGui pipeline with the swapchain format and one sample, and the setter does not validate the target's format or sample count. The patch replaces ImGui's display size with the target's pixels without changing the framebuffer scale.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc). Run these commands from a checkout of that commit.

This patch can be applied independently.

```sh
git apply --check cute-imgui-render-target.patch
git apply cute-imgui-render-target.patch
```

Patch SHA-256: `fc5b8c72ac1e083206f4c5499966f3a2f0de07fb7b264a702940a0717970e957`.
