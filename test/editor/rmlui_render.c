#include "editor/ui/kit.h"
#include "platform/cute.h"
#include "test/suite_internal.h"
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

int rmlui_render_run(const char *program)
{
    CF_Result result = cf_make_app(
        "RmlUi render smoke",
        0,
        0,
        0,
        1024,
        768,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
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
    kit_destroy(k);
    rml_backend_destroy(backend);
    cf_destroy_app();
    puts("RmlUi render PASS: retained UI renders beside queued layer-0 draws");
    return 0;
}
