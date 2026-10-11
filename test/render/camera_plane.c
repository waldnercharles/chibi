#include "render/material_raster.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WIDTH = 96,
    HEIGHT = 80,
    VALUES = WIDTH * HEIGHT * 4
};

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31, mantissa = value & 1023;
    if (exponent == 31) {
        return mantissa ? NAN : INFINITY;
    }
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static bool read_target(CF_Texture texture, uint16_t *pixels)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    uint64_t start = cf_get_ticks();
    while (readback.id && !cf_readback_ready(readback) &&
           cf_get_ticks() - start < cf_get_tick_frequency() * 10) {
        cf_sleep(1);
    }
    int bytes = VALUES * (int)sizeof(uint16_t);
    bool ok = readback.id && cf_readback_ready(readback) &&
              cf_readback_size(readback) == bytes &&
              cf_readback_data(readback, pixels, bytes) == bytes;
    for (int i = 0; i < VALUES && ok; i++) {
        ok = isfinite(half_value(pixels[i]));
    }
    if (readback.id) {
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

static bool draw(render_material_raster *r, render_material_entity marker, render_material_view view)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(r, &marker, 1, view);
    cf_app_draw_onto_screen(false);
    if (!ok) {
        fprintf(stderr, "Camera plane draw: %s\n", render_material_raster_error(r));
    }
    return ok;
}

/* Scalar oracle deliberately does not call the renderer's camera helpers. */
static void projected_center(render_material_view view, float x, float y, double *px, double *py)
{
    double sy = sin(view.yaw), cy = cos(view.yaw);
    double sp = sin(view.pitch), cp = cos(view.pitch);
    double dx = x - view.center_x, dy = -view.target_elevation, dz = -y + view.center_y;
    double right = cy * dx - sy * dz;
    double toward = sy * sp * dx + cp * dy + cy * sp * dz;
    double down = sy * cp * dx - sp * dy + cy * cp * dz;
    double scale = view.focal_distance > 0
                     ? view.focal_distance / (view.focal_distance - toward)
                     : 1;
    *px = (view.center_x + right * scale - view.min_x) * WIDTH / view.width;
    *py = (view.max_y - view.center_y + down * scale) * HEIGHT / view.height;
}

static bool center_matches(const uint16_t *pixels, render_material_view view, render_material_entity marker)
{
    double mass = 0, x = 0, y = 0, expected_x, expected_y;
    for (int j = 0; j < HEIGHT; j++) {
        for (int i = 0; i < WIDTH; i++) {
            double coverage = half_value(pixels[4 * (j * WIDTH + i) + 3]);
            mass += coverage;
            x += coverage * (i + .5);
            y += coverage * (j + .5);
        }
    }
    projected_center(view, marker.pos[0], marker.pos[1], &expected_x, &expected_y);
    bool ok = mass > 5 && fabs(x / mass - expected_x) < .75 &&
              fabs(y / mass - expected_y) < .75;
    if (!ok) {
        fprintf(
            stderr,
            "Camera plane center: mass %.3f, actual %.3f/%.3f expected "
            "%.3f/%.3f\n",
            mass,
            mass ? x / mass : -1,
            mass ? y / mass : -1,
            expected_x,
            expected_y
        );
    }
    return ok;
}

/* A plane of 2D material layers draws as before under a perspective mesh
 * camera without yaw or pitch; turned and pitched, orthographic and in
 * perspective, its marker lands where a scalar projection puts it, facing
 * the camera; seen edge-on, every target stays finite and empty. */
TEST_CASE(material_planes_follow_the_camera)
{
    render_material_raster *r = render_material_raster_new();
    REQUIRE(render_material_raster_resize(r, WIDTH, HEIGHT));
    uint16_t *pixels = xmalloc(VALUES * sizeof(uint16_t));
    uint16_t *reference = xmalloc(VALUES * sizeof(uint16_t));
    render_material_entity marker = { .inv_rot = { 1, 0, 0, 1 },
                                      .pos = { 17, 9 },
                                      .extent = { 5, 5 },
                                      .shape_type = RENDER_SHAPE_CIRCLE,
                                      .radius = 3,
                                      .aa = .8f,
                                      .scene = { 1, .25f, .1f, 1 },
                                      .albedo = { 1, .25f, .1f, 1 },
                                      .absorption = { 1, 1, 1, 1 } };
    render_material_view view = { .min_x = -48,
                                  .max_y = 40,
                                  .width = WIDTH,
                                  .height = HEIGHT,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    REQUIRE(draw(r, marker, view));
    REQUIRE(read_target(render_material_raster_textures(r).scene, reference));
    view.focal_distance = 80;
    view.center_x = 3;
    view.center_y = -2;
    REQUIRE(draw(r, marker, view));
    REQUIRE(read_target(render_material_raster_textures(r).scene, pixels));
    REQUIRE(!memcmp(pixels, reference, VALUES * sizeof(uint16_t)));
    const float yaws[] = { -.65f, .9f };
    for (int perspective = 0; perspective < 2; perspective++) {
        view.focal_distance = perspective ? 80 : 0;
        view.pitch = .55f;
        view.target_elevation = 4;
        for (int i = 0; i < 2; i++) {
            view.yaw = yaws[i];
            REQUIRE(draw(r, marker, view));
            REQUIRE(read_target(render_material_raster_textures(r).scene, pixels));
            REQUIRE(center_matches(pixels, view, marker));
            REQUIRE(read_target(render_material_raster_textures(r).normal, pixels));
            double px, py;
            projected_center(view, marker.pos[0], marker.pos[1], &px, &py);
            int at = 4 * ((int)floor(py) * WIDTH + (int)floor(px));
            REQUIRE(fabsf(half_value(pixels[at])) < .02f);
            REQUIRE(fabsf(half_value(pixels[at + 1]) - sinf(view.pitch)) < .025f);
        }
    }
    view.focal_distance = 0;
    view.pitch = 1.57079632679f;
    REQUIRE(draw(r, marker, view));
    render_material_textures t = render_material_raster_textures(r);
    CF_Texture targets[] = { t.scene,  t.source,     t.albedo,
                             t.normal, t.visibility, t.cover };
    for (int i = 0; i < 6; i++) {
        REQUIRE(read_target(targets[i], pixels));
    }
    REQUIRE(read_target(t.scene, pixels));
    for (int i = 3; i < VALUES; i += 4) {
        REQUIRE(half_value(pixels[i]) == 0);
    }
    free(reference);
    free(pixels);
    render_material_raster_free(r);
    return true;
}

/* Runs in an open hidden app. */
void test_camera_plane()
{
    RUN_TEST_CASE(material_planes_follow_the_camera);
}
