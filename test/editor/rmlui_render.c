#include "editor/ui/kit.h"
#include "platform/cute.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

static void queue_quad(CF_Color color)
{
    cf_draw_push_layer(0);
    cf_draw_push_color(color);
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

static void queue_witness_geometry(void)
{
    queue_quad(cf_make_color_rgba(255, 0, 255, 255));
}

/* A retained UI scissors to its whole target, as a game's HUD does inside the
 * Scene view, so it must draw whatever scissor an earlier pass left behind. */
static void check_scissor_left_behind()
{
    enum
    {
        WIDTH = 96,
        HEIGHT = 64
    };
    rml_backend *ui = rml_backend_create(WIDTH, HEIGHT, 1);
    assert(ui);
    rml_element *doc = rml_context_load_document_from_memory(
        rml_backend_context(ui),
        "<rml><body style=\"width: 100%; height: 100%; "
        "background-color: #ff00ff;\"></body></rml>",
        "scissor"
    );
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    CF_Canvas clipped = cf_make_canvas(cf_canvas_defaults(256, 256));
    CF_Canvas target = cf_make_canvas(cf_canvas_defaults(WIDTH, HEIGHT));
    assert(clipped.id && target.id);
    cf_app_update(NULL);
    /* Clipped to a pixel outside the target, as the editor clips a glyph. */
    cf_draw_push_scissor((CF_Rect){ 200, 200, 1, 1 });
    queue_quad(cf_make_color_rgba(0, 255, 0, 255));
    cf_render_to(clipped, true);
    cf_draw_pop_scissor();
    /* Unclipped into a canvas of the UI's size, as the world renders. */
    queue_quad(cf_make_color_rgba(0, 255, 0, 255));
    cf_render_to(target, true);
    rml_backend_update(ui, 1.0 / 60.0);
    rml_backend_render(ui);
    cf_draw_push();
    rml_backend_present(ui);
    cf_render_to(target, false);
    cf_draw_pop();
    cf_app_draw_onto_screen(true);
    assert(has_magenta(target));
    cf_destroy_canvas(clipped);
    cf_destroy_canvas(target);
    rml_document_close(doc);
    rml_backend_destroy(ui);
}

/* Verify the public CSS effect on real rendered pixels, including its hole
   around an opaque control and the soft outer tail. Two frames also exercise
   cached callback textures and release of temporary shadow geometry. */
static void check_box_shadows()
{
    enum
    {
        WIDTH = 128,
        HEIGHT = 96
    };
    rml_backend *ui = rml_backend_create(WIDTH, HEIGHT, 1);
    assert(ui);
    rml_element *doc = rml_context_load_document_from_memory(
        rml_backend_context(ui),
        "<rml><body style=\"width:100%;height:100%;\">"
        "<div "
        "style=\"position:absolute;left:24px;top:24px;width:24px;height:24px;"
        "background-color:#ffffff;box-shadow:0px 0px 0px 3px "
        "#ff000080;\"></div>"
        "<div "
        "style=\"position:absolute;left:80px;top:24px;width:24px;height:24px;"
        "background-color:#ffffff;box-shadow:0px 4px 8px 0px "
        "#0000ff80,0px 2px 4px 0px #0000ff40;\"></div>"
        "</body></rml>",
        "shadows"
    );
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    CF_Canvas target = cf_make_canvas(cf_canvas_defaults(WIDTH, HEIGHT));
    for (int frame = 0; frame < 2; frame++) {
        cf_app_update(NULL);
        rml_backend_update(ui, 1.0 / 60.0);
        rml_backend_render(ui);
        cf_draw_push();
        rml_backend_present(ui);
        cf_render_to(target, true);
        cf_draw_pop();
        cf_app_draw_onto_screen(true);
        CF_Readback readback = cf_canvas_readback(target);
        CF_Pixel pixels[WIDTH * HEIGHT];
        uint64_t start = cf_get_ticks();
        while (!cf_readback_ready(readback)) {
            assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
            cf_sleep(1);
        }
        assert(cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels);
        cf_destroy_readback(readback);
        CF_Pixel ring = pixels[36 * WIDTH + 22];
        CF_Pixel center = pixels[36 * WIDTH + 36];
        CF_Pixel outside = pixels[36 * WIDTH + 19];
        CF_Pixel near = pixels[54 * WIDTH + 92];
        CF_Pixel far = pixels[60 * WIDTH + 92];
        assert(
            ring.colors.r > 220 && ring.colors.g < 20 && ring.colors.a > 100 &&
            ring.colors.a < 150
        );
        assert(
            center.colors.r > 240 && center.colors.g > 240 &&
            center.colors.b > 240 && center.colors.a > 240
        );
        assert(outside.colors.a < 5);
        assert(near.colors.b > 200 && near.colors.a > 15 && near.colors.a < 100);
        assert(far.colors.a < near.colors.a && far.colors.a < 15);
    }
    cf_destroy_canvas(target);
    rml_document_close(doc);
    rml_backend_destroy(ui);
}

int rmlui_render_run(const char *program, bool gles)
{
    CF_Result result = cf_make_app(
        "RmlUi render smoke",
        0,
        0,
        0,
        1024,
        768,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT |
            (gles ? CF_APP_OPTIONS_GFX_OPENGL_BIT : 0),
        program
    );
    if (cf_is_error(result) && gles) {
        /* Desktop GLES needs a driver such as Mesa's. On macOS, put ANGLE's
           libEGL.dylib and libGLESv2.dylib on DYLD_FALLBACK_LIBRARY_PATH. */
        printf("RmlUi render SKIP: no GLES window: %s\n", result.details);
        return 0;
    }
    if (cf_is_error(result)) {
        fprintf(stderr, "RmlUi render app: %s\n", result.details);
        return 1;
    }
    int width = cf_app_get_width(), height = cf_app_get_height();
    rml_backend *backend = rml_backend_create(width, height, cf_app_get_pixel_scale());
    assert(backend);
    kit *k = test_kit_create(rml_backend_context(backend), "studio.rml");
    assert(k);
    kit_frame_input in = { .dp_ratio = cf_app_get_pixel_scale(),
                           .width = (float)width,
                           .height = (float)height };
    CF_Canvas witness = cf_make_canvas(cf_canvas_defaults(128, 128));
    assert(witness.id);
    for (int frame = 0; frame < 3; frame++) {
        cf_app_update(NULL);
        kit_begin(k, &in);
        kit_root(k, "app");
        kit_label(k, "label", NULL, "Studio");
        kit_end(k);
        rml_backend_update(backend, 1.0 / 60.0);
        queue_witness_geometry();
        rml_backend_render(backend);
        /* Rendering the retained UI must leave ordinary Cute layer-0 work queued. */
        cf_render_layers_to(witness, 0, 0, true);
        rml_backend_present(backend);
        /* Submit the frame first: on Vulkan a readback only sees submitted work. */
        cf_app_draw_onto_screen(true);
        assert(has_magenta(witness));
    }
    cf_destroy_canvas(witness);
    check_scissor_left_behind();
    check_box_shadows();
    kit_destroy(k);
    rml_backend_destroy(backend);
    cf_destroy_app();
    printf(
        "RmlUi render PASS%s: retained UI renders beside queued layer-0 draws "
        "under a scissor another pass left, and renders focus rings and "
        "blurred shadows\n",
        gles ? " on GLES" : ""
    );
    return 0;
}
