#include "editor/ui/ui_shell.h"
#include "editor/workspace/input.h"
#include "platform/cute.h"
#include "render/screenshot.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void frame(float scale)
{
    cf_app_update(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DeltaTime = 1.0f / 60.0f;
    io->DisplayFramebufferScale = (ImVec2){ scale, scale };
    io->DisplaySize = (ImVec2){ cf_app_get_canvas_width() / scale,
                                cf_app_get_canvas_height() / scale };
    app_input_frame();
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    editor_ui_prepare();
    editor_ui_render();
    cf_app_draw_onto_screen(true);
}

static void frames(int count, float scale)
{
    for (int i = 0; i < count; i++) {
        frame(scale);
    }
}

static bool item(const char *id, editor_ui_rect *rect)
{
    return editor_ui_item_rect(id, rect) && rect->width > 0 && rect->height > 0;
}

static bool hover(const char *id)
{
    editor_ui_rect rect;
    if (!item(id, &rect)) {
        return false;
    }
    ImGuiIO *io = ImGui_GetIO();
    float x = rect.x + rect.width * .5f;
    float y = rect.y + rect.height * .5f;
    ImGuiIO_AddMousePosEvent(io, x, y);
    frame(cf_app_get_pixel_scale());
    return true;
}

static bool click(const char *id)
{
    if (!hover(id)) {
        return false;
    }
    ImGuiIO *io = ImGui_GetIO();
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    frame(cf_app_get_pixel_scale());
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    frame(cf_app_get_pixel_scale());
    return true;
}

static void key(ImGuiKey key)
{
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), key, true);
    frame(cf_app_get_pixel_scale());
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), key, false);
    frame(cf_app_get_pixel_scale());
}

static void capture(const char *name)
{
    const char *directory = getenv("CHIBI_RMLUI_CAPTURE_DIR");
    if (!directory || !*directory) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", directory, name);
    assert(app_save_screenshot(path) == 0);
}

static bool has_magenta(CF_Canvas canvas)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    assert(readback.id);
    int size = cf_readback_size(readback);
    assert(size > 0 && size % (int)sizeof(CF_Pixel) == 0);
    CF_Pixel *pixels = malloc((size_t)size);
    assert(pixels);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    assert(cf_readback_data(readback, pixels, size) == size);
    bool found = false;
    for (int i = 0; i < size / (int)sizeof(CF_Pixel); i++) {
        CF_Pixel p = pixels[i];
        if (p.colors.r > 220 && p.colors.b > 220 && p.colors.g < 40 && p.colors.a > 220) {
            found = true;
            break;
        }
    }
    free(pixels);
    cf_destroy_readback(readback);
    return found;
}

static void queue_witness_geometry(void)
{
    cf_draw_push_layer(0);
    cf_draw_push_color(cf_make_color_rgba(255, 0, 255, 255));
    cf_draw_quad_fill2(
        (CF_V2){ -16, -16 },
        (CF_V2){ 16, -16 },
        (CF_V2){ 16, 16 },
        (CF_V2){ -16, 16 },
        0
    );
    cf_draw_pop_color();
    cf_draw_pop_layer();
}

static void render_witness_frame(CF_Canvas witness)
{
    cf_app_update(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DeltaTime = 1.0f / 60.0f;
    io->DisplayFramebufferScale = (ImVec2){ 1, 1 };
    io->DisplaySize = (ImVec2){ cf_app_get_canvas_width(), cf_app_get_canvas_height() };
    app_input_frame();
    editor_ui_set_framebuffer_target(true);
    editor_ui_set_framebuffer_size(cf_app_get_canvas_width(), cf_app_get_canvas_height());
    editor_ui_prepare();
    queue_witness_geometry();
    editor_ui_render();

    /* Rendering the retained UI must leave ordinary Cute layer-0 work queued. */
    cf_render_layers_to(witness, 0, 0, true);
    assert(has_magenta(witness));
    cf_app_draw_onto_screen(true);
}

static int menu_command(editor_ui_command, void *)
{
    return 0;
}

int rmlui_render_run(const char *program)
{
    CF_Result result = cf_make_app(
        "RmlUi menu render smoke",
        0,
        0,
        0,
        1024,
        768,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT | CF_APP_OPTIONS_RESIZABLE_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "RmlUi render app: %s\n", result.details);
        return 1;
    }
    cf_app_init_imgui();
    app_input_initialize();
    editor_ui_initialize(false);
    editor_ui_set_command_handler(menu_command, NULL);
    ImGui_GetIO()->IniFilename = NULL;
    cf_app_set_imgui_render_target(cf_app_get_canvas());
    editor_ui_set_framebuffer_target(true);

    float scale = cf_app_get_pixel_scale();
    frames(2, scale);
    assert(click("file.menu"));
    frames(12, scale);
    assert(item("file.save", &(editor_ui_rect){ 0 }));
    capture("file-popup");

    assert(hover("view.menu"));
    frames(12, scale);
    assert(item("view.navigator", &(editor_ui_rect){ 0 }));
    capture("view-popup");

    bool reduced_motion = editor_ui_reduced_motion();
    assert(click("view.reduced_motion"));
    assert(editor_ui_reduced_motion() != reduced_motion);
    editor_ui_set_reduced_motion(reduced_motion);
    frames(12, scale);

    key(ImGuiKey_F10);
    key(ImGuiKey_DownArrow);
    assert(item("file.save", &(editor_ui_rect){ 0 }));
    capture("keyboard-popup");
    key(ImGuiKey_Escape);
    key(ImGuiKey_Escape);

    CF_Canvas witness = cf_make_canvas(cf_canvas_defaults(128, 128));
    assert(witness.id);
    render_witness_frame(witness);
    cf_destroy_canvas(witness);

    frames(3, scale == 1.0f ? 2.0f : 1.0f);
    editor_ui_rect scaled;
    assert(item("file.menu", &scaled));
    assert(scaled.x >= 0 && scaled.y >= 0);
    assert(scaled.x + scaled.width <= ImGui_GetIO()->DisplaySize.x + .5f);
    assert(scaled.y + scaled.height <= ImGui_GetIO()->DisplaySize.y + .5f);

    cf_app_set_size(1280, 720);
    frames(5, scale);
    assert(cf_app_get_canvas_width() > 0 && cf_app_get_canvas_height() > 0);
    assert(item("file.menu", &(editor_ui_rect){ 0 }));

    editor_ui_deactivate();
    app_input_shutdown();
    cf_destroy_app();
    puts("RmlUi menu GPU smoke passed");
    return 0;
}
