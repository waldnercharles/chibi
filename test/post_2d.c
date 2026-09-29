#include "render/post_2d.h"

#include "platform/cute.h"
#include "runtime/visual_effects.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static bool read_pixels(CF_Canvas canvas, CF_Pixel *pixels)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return false;
    }
    while (!cf_readback_ready(readback)) {
    }
    int size = PIXELS * (int)sizeof *pixels;
    bool ok = cf_readback_size(readback) == size &&
              cf_readback_data(readback, pixels, size) == size;
    cf_destroy_readback(readback);
    return ok;
}

/* A dim gray frame with a bright vertical bar left of center. */
static void draw_frame(render_post_2d *post)
{
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    cf_draw_push_color(cf_make_color_rgba_f(.18f, .18f, .18f, 1));
    cf_draw_box_fill((CF_Aabb){ { -SIDE, -SIDE }, { SIDE, SIDE } }, 0);
    cf_draw_pop_color();
    cf_draw_push_color(cf_make_color_rgba_f(4, 1, .25f, 1));
    cf_draw_box_fill((CF_Aabb){ { -12, -SIDE }, { -6, SIDE } }, 0);
    cf_draw_pop_color();
    cf_draw_pop();
    cf_render_to(render_post_2d_target(post), true);
}

static bool present(render_post_2d *post, CF_Canvas output, const render_post_2d_settings *s, CF_Pixel *pixels)
{
    render_post_2d_present(post, output, s);
    cf_app_draw_onto_screen(false);
    return read_pixels(output, pixels);
}

static int brightness(CF_Pixel p)
{
    return p.colors.r + p.colors.g + p.colors.b;
}

static bool grading(render_post_2d *post, CF_Canvas output)
{
    CF_Pixel plain[PIXELS], graded[PIXELS];
    render_post_2d_settings neutral = { .exposure = 0 };
    draw_frame(post);
    bool ok = present(post, output, &neutral, plain);
    render_post_2d_settings gray = { .look = { .saturation = -1, .vignette = 1 } };
    draw_frame(post);
    ok = ok && present(post, output, &gray, graded);
    if (!ok) {
        fprintf(stderr, "post 2D: present readback failed\n");
        return false;
    }
    CF_Pixel bar = graded[32 * SIDE + 23], corner = graded[1 * SIDE + 1];
    CF_Pixel center = graded[32 * SIDE + 40];
    bool gray_ok = abs(bar.colors.r - bar.colors.b) <= 2 &&
                   abs(bar.colors.r - bar.colors.g) <= 2;
    bool colored = plain[32 * SIDE + 23].colors.r > plain[32 * SIDE + 23].colors.b + 40;
    bool vignette = brightness(corner) + 30 < brightness(center) &&
                    abs(brightness(plain[SIDE + 1]) -
                        brightness(plain[32 * SIDE + 40])) <= 6;
    printf(
        "post 2D grading: bar %u,%u,%u corner %d center %d\n",
        bar.colors.r,
        bar.colors.g,
        bar.colors.b,
        brightness(corner),
        brightness(center)
    );
    render_post_2d_settings flash = { .flash = { 1, 0, 0, 1 } };
    draw_frame(post);
    ok = present(post, output, &flash, graded);
    bool flashed = ok && graded[40 * SIDE + 50].colors.r > 250 &&
                   graded[40 * SIDE + 50].colors.g < 3;
    if (!gray_ok || !colored || !vignette || !flashed) {
        fprintf(
            stderr,
            "post 2D grading: gray %d colored %d vignette %d flash %d\n",
            gray_ok,
            colored,
            vignette,
            flashed
        );
        return false;
    }
    return true;
}

/* Column of the bar's brightest pixel on row y. */
static int bar_column(const CF_Pixel *pixels, int y)
{
    int best = 0;
    for (int x = 1; x < SIDE; x++) {
        if (brightness(pixels[y * SIDE + x]) > brightness(pixels[y * SIDE + best])) {
            best = x;
        }
    }
    return best;
}

static bool shockwave(render_post_2d *post, CF_Canvas output, CF_Texture black)
{
    render_post_2d_distortion wave = {
        .view = { .min_x = -32,
                  .max_y = 32,
                  .width = SIDE,
                  .height = SIDE,
                  .crop_max_u = 1,
                  .crop_max_v = 1 },
        .normal = black,
        .cover = black,
        .waves = { { 0, 0, 13, 6, 6 } },
        .wave_count = 1,
    };
    render_post_2d_settings neutral = { 0 };
    CF_Pixel still[PIXELS], bent[PIXELS];
    draw_frame(post);
    bool ok = present(post, output, &neutral, still);
    draw_frame(post);
    ok = ok && render_post_2d_distort(post, &wave) &&
         present(post, output, &neutral, bent);
    if (!ok) {
        fprintf(stderr, "post 2D: distortion readback failed\n");
        return false;
    }
    /* The ring crosses the bar near the center row; far rows stay still. */
    int moved = abs(bar_column(bent, 32) - bar_column(still, 32));
    int far = abs(bar_column(bent, 1) - bar_column(still, 1));
    printf("post 2D shockwave: bar moved %d px at the ring, %d px away\n", moved, far);
    return moved >= 2 && far == 0;
}

static bool effect_clock()
{
    game_visual_effects *effects = game_visual_effects_new();
    bool ok = game_visual_effects_flash(effects, 0xff0000ffu, .8f, .5f) &&
              !game_visual_effects_flash(effects, 0, -1, .5f) &&
              game_visual_effects_pulse_aberration(effects, 6, .25f);
    game_visual_effects_state state = game_visual_effects_current(effects);
    ok &= fabsf(state.flash[3] - .8f) < 1e-6f && state.flash[0] == 1 &&
          state.aberration == 6;
    game_visual_effects_tick(effects, .25f);
    state = game_visual_effects_current(effects);
    ok &= fabsf(state.flash[3] - .2f) < 1e-5f && state.aberration == 0;
    for (int i = 0; i <= GAME_SHOCKWAVE_MAX; i++) {
        game_shockwave wave = { .position = { (float)i, 0 },
                                .radius = 10,
                                .thickness = 2,
                                .strength = 1,
                                .duration = 1 };
        ok &= game_visual_effects_add_shockwave(effects, wave);
        game_visual_effects_tick(effects, .01f);
    }
    state = game_visual_effects_current(effects);
    ok &= state.shockwave_count == GAME_SHOCKWAVE_MAX &&
          state.shockwaves[0].position.x == 16;
    game_visual_effects_tick(effects, 1);
    ok &= game_visual_effects_current(effects).shockwave_count == 0;
    game_visual_effects_free(effects);
    if (!ok) {
        fprintf(stderr, "visual effects: flash, aberration or shockwave clock mismatch\n");
    }
    return ok;
}

bool test_post_2d()
{
    render_post_2d *post = render_post_2d_new();
    CF_Canvas output = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    CF_TextureParams params = cf_texture_defaults(1, 1);
    CF_Texture black = cf_make_texture(params);
    uint32_t zero = 0;
    cf_texture_update(black, &zero, sizeof zero);
    bool ok = post && output.id && render_post_2d_resize(post, SIDE, SIDE);
    ok = ok && grading(post, output) && shockwave(post, output, black);
    ok &= effect_clock();
    render_post_2d_free(post);
    cf_destroy_canvas(output);
    cf_destroy_texture(black);
    printf("post 2D %s: grading, flash, vignette, shockwave and effect clocks\n", ok ? "PASS" : "FAIL");
    return ok;
}
