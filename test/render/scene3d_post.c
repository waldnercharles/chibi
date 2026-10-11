#include "render/scene3d_post.h"

#include "hidden_app.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 3D post-processing of an emitter pattern drawn straight into its HDR target:
 * the tone map, bloom and exposure, non-finite sources and edges, bloom radius
 * across sizes, and draw state and output kept across invalid settings. */

enum
{
    PATTERN_CENTER, /* A small (4, 1, .25) disc at the center. */
    PATTERN_SPLIT,  /* Red above, green below. */
    PATTERN_FLAT,   /* (4, 1, .25) everywhere. */
    PATTERN_COLOR,  /* The `color` uniform everywhere. */
    PATTERN_EDGE,   /* A bright column at the left edge. */
};

static const char *emitter_source =
    "layout(set=3,binding=1) uniform shd_uniforms { vec2 dimensions; int "
    "pattern; "
    "vec4 external_color; };\n"
    "vec4 shader(vec4 color,ShaderParams params) {\n"
    " if(pattern==1) return params.screen_uv.y<.5 ? "
    "vec4(4,0,0,1):vec4(0,2,0,1);\n"
    " if(pattern==2) return vec4(4,1,.25,1);\n"
    " if(pattern==3) return external_color;\n"
    " if(pattern==4) return "
    "vec4(params.screen_uv.x<2.0/dimensions.x?8.0:0.0,0,0,1);\n"
    " vec2 d=(params.screen_uv-vec2(.5))*dimensions;\n"
    " float radius=dimensions.y*3.0/128.0;\n"
    " return vec4(vec3(4,1,.25)*step(dot(d,d),radius*radius),1);\n"
    "}\n";

static render_scene3d_post *post;
static CF_Shader emitter;
static CF_Color color; /* What PATTERN_COLOR emits. */

static void setup()
{
    post = render_scene3d_post_new();
    emitter = cf_make_draw_shader_from_source(emitter_source);
    assert(emitter.id);
}

static void teardown()
{
    render_scene3d_post_free(post);
    cf_destroy_shader(emitter);
}

static Environment environment()
{
    Environment settings = environment_defaults();
    settings.bloom_strength = 0;
    return settings;
}

/* Prepares for `settings` and queues `pattern` into the HDR target. */
static void scene(int width, int height, Environment settings, int pattern)
{
    cf_app_update(NULL);
    if (!render_scene3d_post_prepare(post, width, height, &settings)) {
        fprintf(stderr, "scene3d post prepare: %s\n", render_scene3d_post_error(post));
        assert(false);
    }
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_push_shader(emitter);
    cf_draw_set_uniform_v2("dimensions", (CF_V2){ (float)width, (float)height });
    cf_draw_set_uniform_int("pattern", pattern);
    cf_draw_set_uniform_color("external_color", color);
    cf_draw_quad_fill((CF_Aabb){ { -1, -1 }, { 1, 1 } }, 0);
    cf_draw_pop_shader();
    cf_render_to(render_scene3d_post_target(post), true);
    cf_draw_pop();
}

static CF_Pixel *read_output(int width, int height)
{
    int size = width * height * (int)sizeof(CF_Pixel);
    CF_Pixel *pixels = xmalloc((size_t)size);
    assert(test_read_canvas(render_scene3d_post_output(post), pixels, size));
    return pixels;
}

static CF_Pixel *finish(int width, int height)
{
    assert(render_scene3d_post_process(post, 0));
    cf_app_draw_onto_screen(false);
    return read_output(width, height);
}

static CF_Pixel *render(int width, int height, Environment settings, int pattern)
{
    scene(width, height, settings, pattern);
    return finish(width, height);
}

/* Khronos PBR Neutral, then the sRGB encoding, of one scene-linear color. */
static void neutral_display(const float in[3], int out[3])
{
    const float start = .76f, desaturation = .15f, d = 1 - start;
    float c[3] = { in[0], in[1], in[2] };
    float low = fminf(c[0], fminf(c[1], c[2]));
    float offset = low < .08f ? low - 6.25f * low * low : .04f;
    for (int i = 0; i < 3; i++) {
        c[i] -= offset;
    }
    float peak = fmaxf(c[0], fmaxf(c[1], c[2]));
    float compressed = 1 - d * d / (peak + d - start);
    float g = 1 - 1 / (desaturation * (peak - compressed) + 1);
    for (int i = 0; i < 3; i++) {
        float mapped = c[i] * compressed / peak;
        mapped += (compressed - mapped) * g;
        out[i] = (int)lroundf((1.055f * powf(mapped, 1 / 2.4f) - .055f) * 255);
    }
}

TEST_CASE(hdr_targets_keep_scene_light_for_the_tone_map)
{
    Environment settings = environment();
    settings.tone_mapping = TONE_MAPPING_NEUTRAL;
    scene(32, 32, settings, PATTERN_FLAT);
    REQUIRE(render_scene3d_post_process(post, 0));
    cf_app_draw_onto_screen(false);
    uint16_t hdr[32 * 32 * 4];
    REQUIRE(test_read_canvas(render_scene3d_post_target(post), hdr, sizeof hdr));
    int center = (16 * 32 + 16) * 4;
    const float light[3] = { 4, 1, .25f };
    for (int i = 0; i < 3; i++) {
        REQUIRE(fabsf(test_half(hdr[center + i]) - light[i]) < .01f);
    }
    CF_Pixel *pixels = read_output(32, 32);
    CF_Pixel p = pixels[16 * 32 + 16];
    free(pixels);
    int expected[3];
    neutral_display(light, expected);
    REQUIRE(abs(p.colors.r - expected[0]) <= 1);
    REQUIRE(abs(p.colors.g - expected[1]) <= 1);
    REQUIRE(abs(p.colors.b - expected[2]) <= 1);
    REQUIRE(p.colors.a == 255);
    return true;
}

TEST_CASE(bloom_spreads_light_past_its_threshold_and_exposure_scales_it)
{
    enum
    {
        SIDE = 128,
        COUNT = SIDE * SIDE
    };
    Environment settings = environment();
    settings.bloom_radius = .06f;
    CF_Pixel *disabled = render(SIDE, SIDE, settings, PATTERN_CENTER);
    int center = 64 * SIDE + 64, outside = 64 * SIDE + 76;
    settings.bloom_strength = 1;
    CF_Pixel *lit = render(SIDE, SIDE, settings, PATTERN_CENTER);
    bool spread = disabled[outside].colors.r == 0 && lit[outside].colors.r > 8 &&
                  lit[center].colors.r >= disabled[center].colors.r;
    free(lit);
    settings.bloom_radius = 0;
    CF_Pixel *zero_radius = render(SIDE, SIDE, settings, PATTERN_CENTER);
    bool unbloomed = !memcmp(disabled, zero_radius, COUNT * sizeof *disabled);
    free(zero_radius);
    settings.bloom_radius = .06f;
    settings.bloom_threshold = 8;
    CF_Pixel *threshold = render(SIDE, SIDE, settings, PATTERN_CENTER);
    bool thresholded = !memcmp(disabled, threshold, COUNT * sizeof *disabled);
    free(threshold);
    settings.bloom_strength = 0;
    settings.exposure = -1;
    CF_Pixel *dark = render(SIDE, SIDE, settings, PATTERN_CENTER);
    settings.exposure = 1;
    CF_Pixel *light = render(SIDE, SIDE, settings, PATTERN_CENTER);
    bool exposed = dark[center].colors.r < disabled[center].colors.r &&
                   light[center].colors.r > disabled[center].colors.r;
    free(dark);
    free(light);
    free(disabled);
    REQUIRE(spread);
    REQUIRE(unbloomed);
    REQUIRE(thresholded);
    REQUIRE(exposed);
    return true;
}

/* An infinite channel clamps to the half float maximum, which tone maps to
 * white; a pixel with a NaN channel shows black rather than spreading through
 * bloom. */
TEST_CASE(non_finite_sources_stay_bounded_with_and_without_bloom)
{
    Environment settings = environment();
    for (int bloom = 0; bloom <= 1; bloom++) {
        settings.bloom_strength = (float)bloom;
        color = (CF_Color){ INFINITY, 0, -INFINITY, 1 };
        CF_Pixel *pixels = render(8, 8, settings, PATTERN_COLOR);
        CF_Pixel infinite = pixels[4 * 8 + 4];
        free(pixels);
        color = (CF_Color){ NAN, 1, 1, 1 };
        pixels = render(8, 8, settings, PATTERN_COLOR);
        CF_Pixel nan = pixels[4 * 8 + 4];
        free(pixels);
        REQUIRE(
            infinite.colors.r >= 254 && infinite.colors.g >= 254 &&
            infinite.colors.b >= 254
        );
        REQUIRE(infinite.colors.a == 255);
        REQUIRE(!nan.colors.r && !nan.colors.g && !nan.colors.b && nan.colors.a == 255);
    }
    return true;
}

TEST_CASE(bloom_clamps_at_the_image_edge)
{
    Environment settings = environment();
    settings.bloom_strength = 1;
    settings.bloom_radius = .06f;
    CF_Pixel *edge = render(128, 128, settings, PATTERN_EDGE);
    int near = edge[64 * 128 + 8].colors.r, far = edge[64 * 128 + 127].colors.r;
    free(edge);
    REQUIRE(near > 8);
    REQUIRE(far == 0);
    return true;
}

static float scene_linear(unsigned char channel)
{
    float c = channel / 255.0f;
    float mapped = c <= .04045f ? c / 12.92f : powf((c + .055f) / 1.055f, 2.4f);
    return mapped / fmaxf(1 - mapped, .00001f);
}

/* The weighted spread of glow around the emitter, in pixels along each axis. */
static CF_V2 glow_spread(const CF_Pixel *pixels, int width, int height)
{
    double total = 0, xx = 0, yy = 0;
    float emitter_radius = height * 4.0f / 128;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float dx = x + .5f - width * .5f, dy = y + .5f - height * .5f;
            if (dx * dx + dy * dy < emitter_radius * emitter_radius) {
                continue;
            }
            float weight = scene_linear(pixels[y * width + x].colors.r);
            total += weight;
            xx += dx * dx * weight;
            yy += dy * dy * weight;
        }
    }
    return (CF_V2){ (float)sqrt(xx / fmax(total, .00001)),
                    (float)sqrt(yy / fmax(total, .00001)) };
}

TEST_CASE(bloom_radius_follows_the_viewport_height)
{
    Environment settings = environment();
    settings.bloom_strength = 1;
    settings.bloom_radius = .06f;
    CF_Pixel *small = render(256, 128, settings, PATTERN_CENTER);
    CF_V2 a = glow_spread(small, 256, 128);
    free(small);
    CF_Pixel *large = render(512, 256, settings, PATTERN_CENTER);
    CF_V2 b = glow_spread(large, 512, 256);
    free(large);
    REQUIRE(a.x > 5 && fabsf(a.x / a.y - 1) < .1f);
    REQUIRE(fabsf(b.x / b.y - 1) < .1f);
    REQUIRE(fabsf(b.x / (2 * a.x) - 1) < .15f);
    return true;
}

/* Radii either side of a bloom level boundary blend instead of jumping. */
TEST_CASE(bloom_radius_crosses_levels_smoothly)
{
    Environment settings = environment();
    settings.bloom_strength = 1;
    settings.bloom_radius = .0624f;
    CF_Pixel *below = render(256, 128, settings, PATTERN_CENTER);
    settings.bloom_radius = .0626f;
    CF_Pixel *above = render(256, 128, settings, PATTERN_CENTER);
    int jump = 0;
    for (int i = 0; i < 256 * 128; i++) {
        int delta = abs(below[i].colors.r - above[i].colors.r);
        jump = delta > jump ? delta : jump;
    }
    free(below);
    free(above);
    REQUIRE(jump <= 3);
    return true;
}

TEST_CASE(outputs_take_any_size_down_to_one_pixel)
{
    Environment settings = environment();
    settings.bloom_strength = 1;
    settings.bloom_radius = .0626f;
    const int sizes[][2] = { { 127, 65 }, { 3, 1 }, { 1, 1 }, { 1, 7 } };
    for (int i = 0; i < 4; i++) {
        int width = sizes[i][0], height = sizes[i][1];
        CF_Pixel *pixels = render(width, height, settings, PATTERN_FLAT);
        CF_Pixel first = pixels[0];
        free(pixels);
        int actual_w, actual_h;
        cf_canvas_get_size(render_scene3d_post_output(post), &actual_w, &actual_h);
        REQUIRE(actual_w == width && actual_h == height);
        REQUIRE(first.colors.r > 200 && first.colors.a == 255);
    }
    return true;
}

TEST_CASE(processing_keeps_the_callers_draw_state)
{
    scene(33, 17, environment(), PATTERN_SPLIT);
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 3, 4 }, (CF_V2){ 2, 3 }, .4f);
    cf_draw_projection(cf_ortho_2d(5, 6, 90, 80));
    cf_draw_push_scissor((CF_Rect){ 3, 4, 10, 11 });
    cf_draw_push_viewport((CF_Rect){ 6, 7, 12, 13 });
    CF_V2 before = cf_world_to_screen((CF_V2){ 20, 30 });
    bool processed = render_scene3d_post_process(post, 0);
    CF_V2 after = cf_world_to_screen((CF_V2){ 20, 30 });
    CF_Rect viewport = cf_draw_peek_viewport(), scissor = cf_draw_peek_scissor();
    cf_draw_pop_viewport();
    cf_draw_pop_scissor();
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    REQUIRE(processed);
    REQUIRE(fabsf(before.x - after.x) < .001f && fabsf(before.y - after.y) < .001f);
    REQUIRE(viewport.x == 6 && viewport.h == 13 && scissor.x == 3 && scissor.h == 11);
    /* The top of the image stays on top; the tone map desaturates both. */
    CF_Pixel *pixels = read_output(33, 17);
    CF_Pixel top = pixels[2 * 33 + 16], bottom = pixels[14 * 33 + 16];
    free(pixels);
    REQUIRE(top.colors.r > 200 && top.colors.r > 2 * top.colors.g);
    REQUIRE(bottom.colors.g > 200 && bottom.colors.g > 2 * bottom.colors.r);
    return true;
}

TEST_CASE(invalid_settings_keep_the_last_output)
{
    CF_Pixel *pixels = render(33, 17, environment(), PATTERN_SPLIT);
    CF_Canvas saved = render_scene3d_post_output(post);
    /* Exposure, strength, threshold and radius each out of range. */
    const float invalid[][4] = { { NAN, 0, 1, 0 },
                                 { 17, 0, 1, 0 },
                                 { 0, -1, 1, 0 },
                                 { 0, 0, INFINITY, 0 },
                                 { 0, 1, 1, .26f } };
    for (int i = 0; i < 5; i++) {
        Environment bad = environment();
        bad.exposure = invalid[i][0];
        bad.bloom_strength = invalid[i][1];
        bad.bloom_threshold = invalid[i][2];
        bad.bloom_radius = invalid[i][3];
        REQUIRE(!render_scene3d_post_prepare(post, 129, 75, &bad));
        REQUIRE(render_scene3d_post_error(post)[0]);
        REQUIRE(!render_scene3d_post_process(post, 0));
        REQUIRE(render_scene3d_post_output(post).id == saved.id);
    }
    CF_Pixel *retained = read_output(33, 17);
    bool same = !memcmp(pixels, retained, 33 * 17 * sizeof *pixels);
    free(pixels);
    free(retained);
    REQUIRE(same);
    return true;
}

void test_scene3d_post()
{
    test_open_hidden_app("scene3d post", 32, 32);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(hdr_targets_keep_scene_light_for_the_tone_map);
    RUN_TEST_CASE(bloom_spreads_light_past_its_threshold_and_exposure_scales_it);
    RUN_TEST_CASE(non_finite_sources_stay_bounded_with_and_without_bloom);
    RUN_TEST_CASE(bloom_clamps_at_the_image_edge);
    RUN_TEST_CASE(bloom_radius_follows_the_viewport_height);
    RUN_TEST_CASE(bloom_radius_crosses_levels_smoothly);
    RUN_TEST_CASE(outputs_take_any_size_down_to_one_pixel);
    RUN_TEST_CASE(processing_keeps_the_callers_draw_state);
    RUN_TEST_CASE(invalid_settings_keep_the_last_output);
    pu_clear_setup();
    cf_destroy_app();
}
