#include "render/arena_post.h"

#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    render_arena_post *post;
    CF_Shader emitter;
} fixture;

static const char
    *emitter_source = "layout(set=3,binding=1) uniform shd_uniforms { vec2 "
                      "dimensions; int pattern; vec4 external_color; };\n"
                      "vec4 shader(vec4 color,ShaderParams params) {\n"
                      " if(pattern==1) return params.screen_uv.y<.5 ? "
                      "vec4(4,0,0,1):vec4(0,2,0,1);\n"
                      " if(pattern==2) return vec4(4,1,.25,1);\n"
                      " if(pattern==3) return external_color;\n"
                      " if(pattern==4) return "
                      "vec4(params.screen_uv.x<2.0/"
                      "dimensions.x?8.0:0.0,0,0,1);\n"
                      " vec2 d=(params.screen_uv-vec2(.5))*dimensions;\n"
                      " float radius=dimensions.y*3.0/128.0;\n"
                      " return "
                      "vec4(vec3(4,1,.25)*step(dot(d,d),radius*radius),1);\n"
                      "}\n";

static void *read_canvas(CF_Canvas canvas, int bytes)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    assert(readback.id && cf_readback_size(readback) == bytes);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    void *pixels = xmalloc((size_t)bytes);
    assert(cf_readback_data(readback, pixels, bytes) == bytes);
    cf_destroy_readback(readback);
    return pixels;
}

static void scene(fixture *f, int width, int height, GameEnvironment settings, int pattern)
{
    cf_app_update(NULL);
    if (!render_arena_post_prepare(f->post, width, height, &settings)) {
        fprintf(stderr, "arena post prepare: %s\n", render_arena_post_error(f->post));
        assert(false);
    }
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_push_shader(f->emitter);
    cf_draw_set_uniform_v2("dimensions", (CF_V2){ (float)width, (float)height });
    cf_draw_set_uniform_int("pattern", pattern);
    cf_draw_set_uniform_color("external_color", (CF_Color){ INFINITY, NAN, -INFINITY, 1 });
    cf_draw_quad_fill((CF_Aabb){ { -1, -1 }, { 1, 1 } }, 0);
    cf_draw_pop_shader();
    cf_render_to(render_arena_post_target(f->post), true);
    cf_draw_pop();
}

static CF_Pixel *finish(fixture *f, int width, int height)
{
    assert(render_arena_post_process(f->post));
    cf_app_draw_onto_screen(false);
    return read_canvas(render_arena_post_output(f->post), width * height * (int)sizeof(CF_Pixel));
}

static CF_Pixel *render(fixture *f, int width, int height, GameEnvironment settings, int pattern)
{
    scene(f, width, height, settings, pattern);
    return finish(f, width, height);
}

static float half_value(uint16_t bits)
{
    int exponent = (bits >> 10) & 31, fraction = bits & 1023;
    float value = exponent == 31
                    ? (fraction ? NAN : INFINITY)
                    : ldexpf(
                          (float)(exponent ? 1024 + fraction : fraction),
                          exponent ? exponent - 25 : -24
                      );
    return bits & 32768 ? -value : value;
}

static bool hdr_and_tonemap(fixture *f)
{
    GameEnvironment settings = render_arena_post_defaults();
    settings.bloom_strength = 0;
    scene(f, 32, 32, settings, 2);
    assert(render_arena_post_process(f->post));
    cf_app_draw_onto_screen(false);
    uint16_t *hdr = read_canvas(render_arena_post_target(f->post), 32 * 32 * 8);
    int center = (16 * 32 + 16) * 4;
    bool ok = fabsf(half_value(hdr[center]) - 4) < 0.01f &&
              fabsf(half_value(hdr[center + 1]) - 1) < 0.01f &&
              fabsf(half_value(hdr[center + 2]) - 0.25f) < 0.01f;
    free(hdr);
    CF_Pixel *pixels = read_canvas(render_arena_post_output(f->post), 32 * 32 * (int)sizeof(CF_Pixel));
    // Independent display references: sRGB(Reinhard(4,1,.25)).
    CF_Pixel p = pixels[16 * 32 + 16];
    ok &= abs(p.r - 231) <= 1 && abs(p.g - 188) <= 1 && abs(p.b - 124) <= 1 &&
          p.a == 255;
    free(pixels);
    return ok;
}

static bool bloom_and_exposure(fixture *f)
{
    enum
    {
        WIDTH = 128,
        COUNT = WIDTH * WIDTH
    };
    GameEnvironment settings = render_arena_post_defaults();
    settings.bloom_radius = 0.06f;
    settings.bloom_strength = 0;
    CF_Pixel *disabled = render(f, WIDTH, WIDTH, settings, 0);
    int center = 64 * WIDTH + 64, outside = 64 * WIDTH + 76;
    settings.bloom_strength = 1;
    CF_Pixel *lit = render(f, WIDTH, WIDTH, settings, 0);
    bool ok = disabled[outside].r == 0 && lit[outside].r > 8 &&
              lit[center].r >= disabled[center].r;
    free(lit);
    settings.bloom_radius = 0;
    CF_Pixel *zero_radius = render(f, WIDTH, WIDTH, settings, 0);
    ok &= !memcmp(disabled, zero_radius, COUNT * sizeof *disabled);
    free(zero_radius);
    settings.bloom_radius = 0.06f;
    settings.bloom_threshold = 8;
    CF_Pixel *threshold = render(f, WIDTH, WIDTH, settings, 0);
    ok &= !memcmp(disabled, threshold, COUNT * sizeof *disabled);
    free(threshold);
    settings.bloom_strength = 0;
    settings.exposure = -1;
    CF_Pixel *dark = render(f, WIDTH, WIDTH, settings, 0);
    settings.exposure = 1;
    CF_Pixel *light = render(f, WIDTH, WIDTH, settings, 0);
    ok &= dark[center].r < disabled[center].r &&
          light[center].r > disabled[center].r;
    free(dark);
    free(light);
    free(disabled);
    return ok;
}

static bool source_boundaries(fixture *f)
{
    GameEnvironment settings = render_arena_post_defaults();
    bool ok = true;
    for (int bloom = 0; bloom <= 1; bloom++) {
        settings.bloom_strength = (float)bloom;
        CF_Pixel *pixels = render(f, 8, 8, settings, 3);
        CF_Pixel p = pixels[4 * 8 + 4];
        bool finite = p.r >= 254 && p.g == 0 && p.b == 0 && p.a == 255;
        if (!finite) {
            fprintf(
                stderr,
                "arena post nonfinite bloom=%d: rgba=(%u,%u,%u,%u)\n",
                bloom,
                p.r,
                p.g,
                p.b,
                p.a
            );
            uint16_t *hdr = read_canvas(render_arena_post_target(f->post), 8 * 8 * 8);
            int center = (4 * 8 + 4) * 4;
            fprintf(
                stderr,
                "arena post source: rgba=(%g,%g,%g,%g)\n",
                half_value(hdr[center]),
                half_value(hdr[center + 1]),
                half_value(hdr[center + 2]),
                half_value(hdr[center + 3])
            );
            free(hdr);
        }
        ok &= finite;
        free(pixels);
    }
    settings.bloom_radius = 0.06f;
    CF_Pixel *edge = render(f, 128, 128, settings, 4);
    bool clamped = edge[64 * 128 + 8].r > 8 && edge[64 * 128 + 127].r == 0;
    if (!clamped) {
        fprintf(
            stderr,
            "arena post edge clamp: near=%u far=%u\n",
            edge[64 * 128 + 8].r,
            edge[64 * 128 + 127].r
        );
    }
    ok &= clamped;
    free(edge);
    return ok;
}

static float scene_linear(unsigned char channel)
{
    float c = channel / 255.0f;
    float mapped = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    return mapped / fmaxf(1 - mapped, 0.00001f);
}

static CF_V2 glow_spread(const CF_Pixel *pixels, int width, int height)
{
    double total = 0, xx = 0, yy = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float dx = x + 0.5f - width * 0.5f, dy = y + 0.5f - height * 0.5f;
            if (dx * dx + dy * dy < height * height * (4.0f / 128) * (4.0f / 128)) {
                continue; // Exclude the emitter; measure only the surrounding glow.
            }
            float weight = scene_linear(pixels[y * width + x].r);
            total += weight;
            xx += dx * dx * weight;
            yy += dy * dy * weight;
        }
    }
    return (CF_V2){ (float)sqrt(xx / fmax(total, 0.00001)),
                    (float)sqrt(yy / fmax(total, 0.00001)) };
}

static bool resize_and_radius(fixture *f)
{
    GameEnvironment settings = render_arena_post_defaults();
    settings.bloom_strength = 1;
    settings.bloom_radius = 0.06f;
    CF_Pixel *small = render(f, 256, 128, settings, 0);
    CF_V2 a = glow_spread(small, 256, 128);
    CF_Pixel *large = render(f, 512, 256, settings, 0);
    CF_V2 b = glow_spread(large, 512, 256);
    bool ok = a.x > 5 && fabsf(a.x / a.y - 1) < 0.1f &&
              fabsf(b.x / b.y - 1) < 0.1f && fabsf(b.x / (2 * a.x) - 1) < 0.15f;
    free(small);
    free(large);
    settings.bloom_radius = 0.0624f;
    small = render(f, 256, 128, settings, 0);
    settings.bloom_radius = 0.0626f;
    large = render(f, 256, 128, settings, 0);
    int max_jump = 0;
    for (int i = 0; i < 256 * 128; i++) {
        int delta = abs(small[i].r - large[i].r);
        if (delta > max_jump) {
            max_jump = delta;
        }
    }
    ok &= max_jump <= 3;
    free(small);
    free(large);
    int sizes[][2] = { { 127, 65 }, { 3, 1 }, { 1, 1 }, { 1, 7 } };
    for (int i = 0; i < 4; i++) {
        int width = sizes[i][0], height = sizes[i][1];
        CF_Pixel *pixels = render(f, width, height, settings, 2);
        int actual_w, actual_h;
        cf_canvas_get_size(render_arena_post_output(f->post), &actual_w, &actual_h);
        ok &= actual_w == width && actual_h == height && pixels[0].r > 200 &&
              pixels[0].a == 255;
        free(pixels);
    }
    printf("arena post radius: %.2f/%.2f pixels, resized %.2f/%.2f, level jump %d\n", a.x, a.y, b.x, b.y, max_jump);
    return ok;
}

static bool state_and_retention(fixture *f)
{
    GameEnvironment settings = render_arena_post_defaults();
    settings.bloom_strength = 0;
    scene(f, 33, 17, settings, 1);
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 3, 4 }, (CF_V2){ 2, 3 }, 0.4f);
    cf_draw_projection(cf_ortho_2d(5, 6, 90, 80));
    cf_draw_push_scissor((CF_Rect){ 3, 4, 10, 11 });
    cf_draw_push_viewport((CF_Rect){ 6, 7, 12, 13 });
    CF_V2 before = cf_world_to_screen((CF_V2){ 20, 30 });
    assert(render_arena_post_process(f->post));
    CF_V2 after = cf_world_to_screen((CF_V2){ 20, 30 });
    CF_Rect viewport = cf_draw_peek_viewport(), scissor = cf_draw_peek_scissor();
    bool ok = fabsf(before.x - after.x) < 0.001f &&
              fabsf(before.y - after.y) < 0.001f && viewport.x == 6 &&
              viewport.h == 13 && scissor.x == 3 && scissor.h == 11;
    cf_draw_pop_viewport();
    cf_draw_pop_scissor();
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    CF_Pixel *pixels = read_canvas(render_arena_post_output(f->post), 33 * 17 * (int)sizeof(CF_Pixel));
    ok &= pixels[2 * 33 + 16].r > 200 && pixels[2 * 33 + 16].g == 0 &&
          pixels[14 * 33 + 16].g > 200 && pixels[14 * 33 + 16].r == 0;
    CF_Canvas saved = render_arena_post_output(f->post);
    float invalid[][4] = { { NAN, 0, 1, 0 },
                           { 17, 0, 1, 0 },
                           { 0, -1, 1, 0 },
                           { 0, 0, INFINITY, 0 },
                           { 0, 1, 1, 0.26f } };
    for (int i = 0; i < 5; i++) {
        GameEnvironment bad = { invalid[i][0],
                                invalid[i][1],
                                invalid[i][2],
                                invalid[i][3] };
        ok &= !render_arena_post_prepare(f->post, 129, 75, &bad) &&
              render_arena_post_error(f->post)[0] &&
              !render_arena_post_process(f->post) &&
              render_arena_post_output(f->post).id == saved.id;
    }
    CF_Pixel *retained = read_canvas(saved, 33 * 17 * (int)sizeof(CF_Pixel));
    ok &= !memcmp(pixels, retained, 33 * 17 * sizeof *pixels);
    free(pixels);
    free(retained);
    return ok;
}

int test_arena_post()
{
    fixture f = { .post = render_arena_post_new(),
                  .emitter = cf_make_draw_shader_from_source(emitter_source) };
    assert(f.emitter.id);
    bool hdr = hdr_and_tonemap(&f);
    bool bloom = bloom_and_exposure(&f);
    bool boundaries = source_boundaries(&f);
    bool radius = resize_and_radius(&f);
    bool state = state_and_retention(&f);
    render_arena_post_free(f.post);
    cf_destroy_shader(f.emitter);
    bool ok = hdr && bloom && boundaries && radius && state;
    printf(
        "arena post %s: HDR/tone map %d, bloom/exposure/disabled %d, "
        "nonfinite/edge clamp %d, radius/resize %d, "
        "orientation/state/retention %d\n",
        ok ? "PASS" : "FAIL",
        hdr,
        bloom,
        boundaries,
        radius,
        state
    );
    fflush(stdout);
    return !ok;
}
