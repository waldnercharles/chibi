#include "platform/cute.h"
#include "render/viewport.h"
#include "third_party/pico_headers/pico_unit.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    UI_SIDE = 64
};
static rml_backend *ui;
static CF_Canvas ui_target, ui_texture_view;

static void cleanup_ui()
{
    if (ui_texture_view.id) {
        cf_destroy_canvas(ui_texture_view);
    }
    if (ui_target.id) {
        cf_destroy_canvas(ui_target);
    }
    rml_backend_destroy(ui);
    ui = NULL;
    ui_target = ui_texture_view = (CF_Canvas){ 0 };
}

static bool read_ui(CF_Pixel pixels[UI_SIDE * UI_SIDE])
{
    CF_Readback readback = cf_canvas_readback(ui_target);
    if (!readback.id) {
        return false;
    }
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    int size = UI_SIDE * UI_SIDE * (int)sizeof *pixels;
    int copied = cf_readback_ready(readback)
                   ? cf_readback_data(readback, pixels, size)
                   : 0;
    cf_destroy_readback(readback);
    return copied == size;
}

TEST_CASE(transparent_ui_presents_source_over_and_exports_straight_alpha)
{
    cf_app_update(NULL);
    ui = rml_backend_create(UI_SIDE, UI_SIDE, 1);
    REQUIRE(ui);
    rml_element *document = rml_context_load_document_from_memory(
        rml_backend_context(ui),
        "<rml><head><style>body { margin: 0px; width: 64px; height: 64px; "
        "font-family: Inter; font-size: 26px; color: #ff0000; } "
        "div { position: absolute; left: 0px; top: 0px; width: 32px; "
        "height: 24px; background-color: #ff000080; } "
        "span { position: absolute; left: 4px; top: 32px; }</style></head>"
        "<body><div></div><span>O</span></body></rml>",
        "viewport-alpha.rml"
    );
    REQUIRE(document);
    rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
    rml_backend_update(ui, 0);
    rml_backend_render(ui);
    ui_target = cf_make_canvas(cf_canvas_defaults(UI_SIDE, UI_SIDE));
    REQUIRE(ui_target.id);
    cf_canvas_set_clear_color(ui_target, cf_make_color_rgba_f(0, 0, 1, 1));
    rml_backend_present(ui);
    cf_render_to(ui_target, true);

    /* Copy the public straight-alpha texture into the right half without
     * blending. One readback then compares it with direct presentation on
     * the left, including the font's partial-coverage edge pixels. */
    CF_CanvasParams view = cf_canvas_defaults(UI_SIDE, UI_SIDE);
    view.attach_target = (CF_Texture){ rml_backend_texture(ui) };
    ui_texture_view = cf_make_canvas(view);
    REQUIRE(ui_texture_view.id);
    CF_RenderState replace = cf_render_state_defaults();
    replace.blend.enabled = false;
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, UI_SIDE, UI_SIDE));
    cf_draw_push_scissor((CF_Rect){ UI_SIDE / 2, 0, UI_SIDE / 2, UI_SIDE });
    cf_draw_push_render_state(replace);
    cf_draw_canvas(ui_texture_view, cf_v2(UI_SIDE / 2, 0), cf_v2(UI_SIDE, UI_SIDE));
    cf_draw_pop_render_state();
    cf_draw_pop_scissor();
    cf_render_to(ui_target, false);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    CF_Pixel pixels[UI_SIDE * UI_SIDE];
    REQUIRE(read_ui(pixels));
    int translucent = 0, glyph_edges = 0, opaque = 0;
    bool edge_diagnostic = false;
    for (int y = 0; y < UI_SIDE; y++) {
        for (int x = 0; x < UI_SIDE / 2; x++) {
            CF_Pixel shown = pixels[y * UI_SIDE + x];
            CF_Pixel source = pixels[y * UI_SIDE + x + UI_SIDE / 2];
            float alpha = source.colors.a / 255.0f;
            /* Rectangle interiors avoid filtering between a glyph edge and
             * transparent black after the RGBA8 resolve divides by alpha. */
            if (x >= 4 && x < 28 && y >= 4 && y < 20) {
                if (source.colors.r < 252 || source.colors.g > 3 ||
                    source.colors.b > 3 || abs(source.colors.a - 128) > 1) {
                    fprintf(
                        stderr,
                        "UI straight rectangle (%d,%d): RGBA %u,%u,%u,%u\n",
                        x,
                        y,
                        source.colors.r,
                        source.colors.g,
                        source.colors.b,
                        source.colors.a
                    );
                }
                REQUIRE(
                    source.colors.r >= 252 && source.colors.g <= 3 &&
                    source.colors.b <= 3 && abs(source.colors.a - 128) <= 1
                );
            }
            if (!edge_diagnostic && y >= 32 && source.colors.a > 16 &&
                source.colors.a < 240) {
                fprintf(
                    stderr,
                    "UI glyph edge (%d,%d): source RGBA %u,%u,%u,%u; shown "
                    "RGBA %u,%u,%u,%u\n",
                    x,
                    y,
                    source.colors.r,
                    source.colors.g,
                    source.colors.b,
                    source.colors.a,
                    shown.colors.r,
                    shown.colors.g,
                    shown.colors.b,
                    shown.colors.a
                );
                edge_diagnostic = true;
            }
            float expected[] = { source.colors.r * alpha,
                                 source.colors.g * alpha,
                                 source.colors.b * alpha + 255 * (1 - alpha),
                                 255 };
            unsigned actual[] = { shown.colors.r,
                                  shown.colors.g,
                                  shown.colors.b,
                                  shown.colors.a };
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(actual[channel] - expected[channel]) > 3) {
                    fprintf(
                        stderr,
                        "UI source-over (%d,%d) channel %d: got %u, expected "
                        "%.2f; source RGBA %u,%u,%u,%u\n",
                        x,
                        y,
                        channel,
                        actual[channel],
                        expected[channel],
                        source.colors.r,
                        source.colors.g,
                        source.colors.b,
                        source.colors.a
                    );
                    return false;
                }
            }
            translucent += source.colors.a == 128;
            glyph_edges += y >= 32 && source.colors.a > 16 && source.colors.a < 240;
            opaque += y >= 32 && source.colors.a == 255;
        }
    }
    REQUIRE(translucent >= 32 && glyph_edges >= 8 && opaque >= 8);
    printf(
        "UI alpha coverage: %d translucent rectangle/edge pixels, %d partial "
        "glyph pixels, %d opaque glyph pixels\n",
        translucent,
        glyph_edges,
        opaque
    );
    return true;
}

/* Draws one host frame of the viewport at the requested size and reports the
 * canvas size it drew into. */
static void frame(engine_viewport *viewport, int width, int height, int *drawn_width, int *drawn_height)
{
    cf_app_update(NULL);
    int rendered = engine_viewport_render(viewport, width, height);
    assert(rendered == 0);
    cf_app_draw_onto_screen(false);
    engine_viewport_pixel_size(viewport, drawn_width, drawn_height);
}

TEST_CASE(first_request_replaces_the_placeholder_at_once)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 1530, 570, &width, &height);
    REQUIRE(width == 1530 && height == 570);
    engine_viewport_free(viewport);
    return true;
}

TEST_CASE(later_sizes_wait_until_held_for_two_draws)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 1530, 570, &width, &height);
    for (int draw = 0; draw < 2; draw++) {
        frame(viewport, 800, 600, &width, &height);
        REQUIRE(width == 1530 && height == 570);
    }
    frame(viewport, 800, 600, &width, &height);
    REQUIRE(width == 800 && height == 600);
    engine_viewport_free(viewport);
    return true;
}

TEST_CASE(a_matching_first_request_keeps_later_sizes_waiting)
{
    engine_viewport *viewport = engine_viewport_new(640, 360);
    REQUIRE(viewport);
    int width, height;
    frame(viewport, 640, 360, &width, &height);
    frame(viewport, 800, 600, &width, &height);
    REQUIRE(width == 640 && height == 360);
    engine_viewport_free(viewport);
    return true;
}

/* One host frame of a 160 x 90 image with the left button pressed at (x, y). */
static engine_viewport_pointer press(engine_viewport *viewport, float x, float y)
{
    cf_app_update(NULL);
    engine_viewport_set_host_input(
        viewport,
        &(engine_viewport_host_input){ .width = 160,
                                       .height = 90,
                                       .x = x,
                                       .y = y,
                                       .hovered = true,
                                       .down = { true },
                                       .pressed = { true } }
    );
    int drawn = engine_viewport_draw(viewport, &(engine_viewport_controls){ .editing = true });
    assert(drawn == 0);
    cf_app_draw_onto_screen(false);
    engine_viewport_pointer pointer;
    engine_viewport_get_pointer(viewport, &pointer);
    return pointer;
}

TEST_CASE(a_press_above_the_horizon_is_captured_without_a_ground_point)
{
    engine_viewport *viewport = engine_viewport_new(160, 90);
    REQUIRE(viewport);
    engine_viewport_set_perspective(viewport, true);
    /* Ten units up, looking level along -Z: the image's top half is sky. */
    engine_viewport_set_view(
        viewport,
        &(engine_viewport_view){
            .width = 16,
            .height = 9,
            .lens = { .eye = { 0, 10, 0 }, .forward = { 0, 0, -1 }, .up = { 0, 1, 0 } } }
    );
    engine_viewport_pointer sky = press(viewport, 80, 20);
    REQUIRE(sky.pressed && sky.down && !sky.grounded && !sky.cancel);
    REQUIRE(sky.ndc_y > 0);
    engine_viewport_pointer ground = press(viewport, 80, 80);
    REQUIRE(ground.pressed && ground.grounded);
    /* Straight ahead, so on the -Z axis: canvas Y is the ground's -Z. */
    REQUIRE(fabsf(ground.world_x) < 1e-3f && ground.world_y > 10);
    engine_viewport_free(viewport);
    return true;
}

void test_render_viewport_resize()
{
#ifdef __linux__
    /* As in interior_border.c: no display is needed on Linux agents. */
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    CF_Result made = cf_make_app("viewport-resize", 0, 0, 0, 64, 64, CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT, NULL);
    assert(!cf_is_error(made));
    RUN_TEST_CASE(first_request_replaces_the_placeholder_at_once);
    RUN_TEST_CASE(later_sizes_wait_until_held_for_two_draws);
    RUN_TEST_CASE(a_matching_first_request_keeps_later_sizes_waiting);
    RUN_TEST_CASE(a_press_above_the_horizon_is_captured_without_a_ground_point);
    pu_setup(NULL, cleanup_ui);
    RUN_TEST_CASE(transparent_ui_presents_source_over_and_exports_straight_alpha);
    pu_clear_setup();
    cf_destroy_app();
}
