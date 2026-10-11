#include "render/radiance.h"

#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/material_raster.h"
#include "render/scene_renderer.h"
#include "render/view_2d.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* HRC's layout and lighting settings on the CPU, then its transport and
 * passes in one hidden app, read back. */
enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

/* ---------- Layout and settings, without a GPU ---------- */

typedef struct
{
    float center_x, center_y, width, height, margin, scale, min_scale;
    int output_width, output_height;
} view_case;

static const view_case base_view = {
    .center_x = .25f,
    .center_y = .35f,
    .width = 960,
    .height = 540,
    .output_width = 960,
    .output_height = 540,
    .margin = 64,
    .scale = .5f,
    .min_scale = .1f,
};

static bool lay_out(view_case input, render_view_2d_frame *frame)
{
    render_view_2d_camera camera = {
        .min_x = input.center_x - input.width * .5f,
        .max_y = input.center_y + input.height * .5f,
        .width = input.width,
        .height = input.height,
        .output_width = input.output_width,
        .output_height = input.output_height,
    };
    render_view_2d_settings settings = {
        .margin = input.margin,
        .work_scale = input.scale,
        .min_work_scale = input.min_scale,
        .max_raster_size = 4096,
    };
    return render_view_2d_world_margin(camera, settings, frame);
}

static bool near_edge(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

/* Whether the frame's crop shows exactly the camera's view. */
static bool crops_to(const render_view_2d_frame *frame, float center_x, float center_y, float width, float height)
{
    const render_view_2d *view = &frame->view;
    return near_edge(view->min_x + view->width * view->crop_min_u, center_x - width * .5f) &&
           near_edge(view->min_x + view->width * view->crop_max_u, center_x + width * .5f) &&
           near_edge(view->max_y - view->height * view->crop_min_v, center_y + height * .5f) &&
           near_edge(view->max_y - view->height * view->crop_max_v, center_y - height * .5f);
}

/* A moving camera keeps one raster size, and its padded view stays on whole
 * work cells while the crop shows exactly the camera's view. */
TEST_CASE(world_margins_follow_the_camera_on_whole_cells)
{
    render_view_2d_frame first;
    REQUIRE(lay_out(base_view, &first));
    for (int i = 0; i < 128; i++) {
        view_case moving = base_view;
        moving.center_x = -5 + i * .13f;
        moving.center_y = 4 - i * .09f;
        render_view_2d_frame frame;
        REQUIRE(lay_out(moving, &frame));
        REQUIRE(frame.raster_width == first.raster_width);
        REQUIRE(frame.raster_height == first.raster_height);
        REQUIRE(crops_to(&frame, moving.center_x, moving.center_y, 960, 540));
        const render_view_2d *view = &frame.view;
        float cell_x = view->width / ceilf(frame.raster_width * .5f);
        float cell_y = view->height / ceilf(frame.raster_height * .5f);
        REQUIRE(near_edge(view->min_x / cell_x, roundf(view->min_x / cell_x)));
        REQUIRE(near_edge(view->max_y / cell_y, roundf(view->max_y / cell_y)));
    }
    /* Adaptive's finer steps fit the raster laid out for the coarsest. */
    view_case adaptive = base_view;
    adaptive.scale = .9f;
    render_view_2d_frame finer;
    REQUIRE(lay_out(adaptive, &finer));
    REQUIRE(finer.raster_width == first.raster_width);
    REQUIRE(finer.raster_height == first.raster_height);
    return true;
}

/* Views wider or taller than 4096 pixels give up margin, never the view. */
TEST_CASE(world_margins_stay_within_the_raster_limit)
{
    view_case tall = { .center_x = -12,
                       .center_y = 20,
                       .width = 1024,
                       .height = 4000,
                       .output_width = 1280,
                       .output_height = 4000,
                       .margin = 30,
                       .scale = .5f,
                       .min_scale = .1f };
    view_case capped = { .width = 960,
                         .height = 540,
                         .output_width = 3840,
                         .output_height = 2160,
                         .margin = 64,
                         .scale = .5f,
                         .min_scale = .1f };
    view_case edge = capped;
    edge.output_width = 4096;
    edge.output_height = 1080;
    view_case distant = base_view;
    distant.center_x = 1000000.25f;
    distant.center_y = -1000000.5f;
    view_case bare = { .width = 960,
                       .height = 540,
                       .output_width = 960,
                       .output_height = 540,
                       .scale = 1,
                       .min_scale = 1 };
    view_case odd = { .width = 1001,
                      .height = 563,
                      .output_width = 1001,
                      .output_height = 563,
                      .margin = 64,
                      .scale = .25f,
                      .min_scale = .1f };
    render_view_2d_frame frames[6];
    REQUIRE(
        lay_out(tall, &frames[0]) && lay_out(capped, &frames[1]) &&
        lay_out(edge, &frames[2]) && lay_out(distant, &frames[3]) &&
        lay_out(bare, &frames[4]) && lay_out(odd, &frames[5])
    );
    for (int i = 0; i < 6; i++) {
        REQUIRE(frames[i].raster_width <= 4096 && frames[i].raster_height <= 4096);
    }
    REQUIRE(crops_to(&frames[0], -12, 20, 1024, 4000));
    REQUIRE(frames[1].margin_x < 64 && frames[1].margin_y == 64);
    REQUIRE(crops_to(&frames[1], 0, 0, 960, 540));
    REQUIRE(frames[2].raster_width == 4096 && frames[2].margin_x == 0);
    REQUIRE(!frames[2].grid_aligned);
    REQUIRE(crops_to(&frames[2], 0, 0, 960, 540));
    REQUIRE(crops_to(&frames[3], 1000000.25f, -1000000.5f, 960, 540));
    REQUIRE(frames[4].margin_x == 0 && frames[4].margin_y == 0 && frames[4].grid_aligned);
    REQUIRE(frames[5].grid_aligned);
    /* Scales of 1/2^n keep cells of whole pixels at any output size. */
    const int gridded[] = { 0, 1, 3, 5 };
    for (int i = 0; i < 4; i++) {
        REQUIRE(frames[gridded[i]].raster_width % RENDER_VIEW_2D_RASTER_STEP == 0);
        REQUIRE(frames[gridded[i]].raster_height % RENDER_VIEW_2D_RASTER_STEP == 0);
    }
    view_case too_wide = { .width = 1,
                           .height = 300,
                           .output_width = 4097,
                           .output_height = 300,
                           .scale = .5f,
                           .min_scale = .1f };
    render_view_2d_frame rejected;
    REQUIRE(!lay_out(too_wide, &rejected));
    return true;
}

/* Snapping by whole work cells keeps the grid world-aligned without resizing
 * textures as the camera moves. */
TEST_CASE(axes_snap_to_whole_work_cells)
{
    int pixels = 0;
    for (int i = 0; i < 16; i++) {
        render_view_2d_axis_frame axis;
        REQUIRE(render_view_2d_axis(
            (render_view_2d_axis_input){ .center = i * .37f,
                                         .span = 960,
                                         .margin = 64,
                                         .scale = .5f,
                                         .min_scale = .1f,
                                         .output_pixels = 1920,
                                         .max_pixels = 4096 },
            &axis
        ));
        double cell = axis.raster_pixels / 2.0 / ceil(axis.raster_pixels * .5);
        double steps = axis.origin / cell;
        REQUIRE(axis.grid_aligned && fabs(steps - round(steps)) < 1e-3);
        REQUIRE(!i || axis.raster_pixels == pixels);
        pixels = axis.raster_pixels;
    }
    return true;
}

/* Lighting outside what HRC supports is fitted, not rejected, and each
 * change is noted. */
TEST_CASE(lighting_outside_hrc_ranges_is_fitted_and_noted)
{
    char notes[256];
    GameHRC2D defaults = game_hrc2d_defaults();
    GameHRC2D fitted = render_radiance_normalize(defaults, notes, sizeof notes);
    REQUIRE(!notes[0] && fitted.resolution == defaults.resolution);
    GameHRC2D wild = defaults;
    wild.resolution = 2;
    wild.min_resolution = 0;
    wild.color_resolution = (GameHRCColor)7;
    wild.gather_resolution = (GameHRCGather)-1;
    wild.trace_levels = 9;
    wild.bloom = 2;
    wild.exposure = NAN;
    fitted = render_radiance_normalize(wild, notes, sizeof notes);
    REQUIRE(fitted.resolution == 1 && fitted.min_resolution == .1f);
    REQUIRE(fitted.color_resolution == GAME_HRC_COLOR_EIGHTH);
    REQUIRE(fitted.gather_resolution == GAME_HRC_GATHER_FULL);
    REQUIRE(fitted.trace_levels == 5 && fitted.bloom == 1 && fitted.exposure == 0);
    REQUIRE(strstr(notes, "resolution 2 -> 1") && strstr(notes, "trace_levels 9 -> 5"));
    render_radiance_normalize(fitted, notes, sizeof notes);
    REQUIRE(!notes[0]);
    return true;
}

void test_render_radiance_layout()
{
    RUN_TEST_CASE(world_margins_follow_the_camera_on_whole_cells);
    RUN_TEST_CASE(world_margins_stay_within_the_raster_limit);
    RUN_TEST_CASE(axes_snap_to_whole_work_cells);
    RUN_TEST_CASE(lighting_outside_hrc_ranges_is_fitted_and_noted);
}

/* ---------- Transport and passes, in a hidden app ---------- */

static render_material_raster *raster;
static render_radiance *radiance;
static CF_Canvas canvas;
static CF_Pixel pixels[PIXELS];

static void setup()
{
    raster = render_material_raster_new();
    radiance = render_radiance_new();
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    assert(raster && radiance && canvas.id);
    assert(render_material_raster_resize(raster, SIDE, SIDE));
    assert(render_radiance_resize(radiance, SIDE, SIDE));
}

static void teardown()
{
    cf_destroy_canvas(canvas);
    render_radiance_free(radiance);
    render_material_raster_free(raster);
}

/* A green disc with a red border, emitting both, at world (-28, 0). */
static render_material_entity emitter()
{
    return (render_material_entity){
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { -28, 0 },
        .extent = { 10, 10 },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = 8,
        .aa = 1,
        .border_thickness = 2,
        .scene = { 0, 1, 0, 1 },
        .emission = { 0, .08f, 0, 1 },
        .border_scene = { 1, 0, 0, 1 },
        .border_emission = { .2f, 0, 0, 1 },
    };
}

/* An opaque black wall through the origin, across the whole view's height. */
static render_material_entity wall()
{
    return (render_material_entity){
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 5, 66 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 4, 64 },
        .scene = { 0, 0, 0, 1 },
        .absorption = { 1, 1, 1, 1 },
    };
}

static render_material_entity wall_with(float absorption, float albedo)
{
    render_material_entity entity = wall();
    for (int i = 0; i < 3; i++) {
        entity.absorption[i] = absorption;
        entity.albedo[i] = albedo;
    }
    entity.absorption[3] = absorption > 0 ? 1 : 0;
    entity.albedo[3] = 1;
    return entity;
}

/* Resizes the raster, HRC and the output to size x size. */
static bool resize_targets(int size)
{
    cf_destroy_canvas(canvas);
    canvas = cf_make_canvas(cf_canvas_defaults(size, size));
    return canvas.id && render_material_raster_resize(raster, size, size) &&
           render_radiance_resize(radiance, size, size);
}

static bool read_canvas(CF_Canvas target, void *data, int size)
{
    CF_Readback readback = cf_canvas_readback(target);
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    bool read = cf_readback_size(readback) == size &&
                cf_readback_data(readback, data, size) == size;
    cf_destroy_readback(readback);
    return read;
}

/* Lights size x size pixels showing as many world units around the origin
 * for `frames` frames from a reset, then reads the result into `pixels`. */
static bool solve(const render_material_entity *entities, int count, int size, int frames)
{
    assert(frames > 0 && size <= SIDE);
    float half = size * .5f;
    render_view_2d view = { .min_x = -half,
                            .max_y = half,
                            .width = (float)size,
                            .height = (float)size,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    render_radiance_reset(radiance);
    for (int frame = 0; frame < frames; frame++) {
        cf_app_update(NULL);
        bool drawn = render_material_raster_draw(raster, entities, count, view);
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, (float)size, (float)size));
        render_material_textures textures = render_material_raster_textures(raster);
        drawn = drawn && render_radiance_draw(
                             radiance,
                             (render_radiance_inputs){ .material = textures },
                             view,
                             canvas
                         );
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        if (!drawn) {
            return false;
        }
    }
    return read_canvas(canvas, pixels, size * size * (int)sizeof *pixels);
}

/* Mean light over a patch right of the wall, across from the emitter. */
static double receiver_light()
{
    double sum = 0;
    for (int y = 52; y < 76; y++) {
        for (int x = 84; x < 100; x++) {
            CF_Pixel pixel = pixels[y * SIDE + x];
            sum += pixel.colors.r + pixel.colors.g + pixel.colors.b;
        }
    }
    return sum / (24 * 16);
}

static bool pixels_match(const CF_Pixel *a, const CF_Pixel *b, int count)
{
    for (int i = 0; i < count; i++) {
        if (abs((int)a[i].colors.r - b[i].colors.r) > 2 ||
            abs((int)a[i].colors.g - b[i].colors.g) > 2 ||
            abs((int)a[i].colors.b - b[i].colors.b) > 2 ||
            abs((int)a[i].colors.a - b[i].colors.a) > 2) {
            return false;
        }
    }
    return true;
}

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent == 31 ? (mantissa ? NAN : INFINITY)
                 : exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

/* Reads one of the raster's RGBA16F textures. */
static bool read_texture(CF_Texture texture, uint16_t *halves)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas target = cf_make_canvas(params);
    assert(target.id);
    bool read = read_canvas(target, halves, PIXELS * 4 * (int)sizeof *halves);
    cf_destroy_canvas(target);
    return read;
}

static float texel(const uint16_t *halves, int x, int channel)
{
    return half_value(halves[4 * (SIDE / 2 * SIDE + x) + channel]);
}

/* The emitter lights open space green from its disc and red from its border,
 * and its material reaches HRC as emission, absorption and a flat surface. */
TEST_CASE(emitters_light_open_space_through_their_materials)
{
    render_material_entity light = emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    CF_Pixel center = pixels[SIDE / 2 * SIDE + 36],
             border = pixels[SIDE / 2 * SIDE + 43];
    REQUIRE(center.colors.g > center.colors.r && center.colors.g > 100);
    REQUIRE(border.colors.r > border.colors.g);
    render_material_entity scene[] = { emitter(), wall() };
    REQUIRE(solve(scene, 2, SIDE, 4));
    static uint16_t halves[PIXELS * 4];
    render_material_textures textures = render_material_raster_textures(raster);
    REQUIRE(read_texture(textures.source, halves));
    REQUIRE(fabsf(texel(halves, 36, 1) - .08f) < .002f);
    REQUIRE(fabsf(texel(halves, 43, 0) - .2f) < .002f);
    /* Optical depth is the mean absorption. */
    REQUIRE(fabsf(texel(halves, 64, 3) - 1) < .002f);
    /* A plain solid carries no bump tilt, edge light or gloss. */
    REQUIRE(read_texture(textures.normal, halves));
    for (int channel = 0; channel < 4; channel++) {
        REQUIRE(fabsf(texel(halves, 43, channel)) < .002f);
    }
    return true;
}

TEST_CASE(walls_block_light_by_their_absorption)
{
    render_material_entity light = emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    double open = receiver_light();
    render_material_entity opaque[] = { emitter(), wall() };
    REQUIRE(solve(opaque, 2, SIDE, 4));
    double blocked = receiver_light();
    render_material_entity finite[] = { emitter(), wall_with(.2f, 0) };
    REQUIRE(solve(finite, 2, SIDE, 4));
    double dimmed = receiver_light();
    REQUIRE(open > 3 && blocked < open * .8);
    REQUIRE(dimmed > blocked + .1 && dimmed < open);
    return true;
}

TEST_CASE(bounced_light_gathers_over_frames)
{
    render_material_entity scene[] = { emitter(), wall_with(.2f, .8f) };
    REQUIRE(solve(scene, 2, SIDE, 1));
    double first = receiver_light();
    REQUIRE(solve(scene, 2, SIDE, 8));
    REQUIRE(receiver_light() > first + .1);
    return true;
}

/* Transparent reflective edges must not turn reflected light into an
 * unattenuated light source on every feedback pass. */
TEST_CASE(transparent_reflective_edges_do_not_feed_back)
{
    render_material_entity glass[] = { emitter(), wall_with(.0005f, .9f) };
    REQUIRE(solve(glass, 2, SIDE, 1));
    double first = receiver_light();
    REQUIRE(solve(glass, 2, SIDE, 120));
    double settled = receiver_light();
    REQUIRE(first > 1 && settled > first * .5 && settled < first * 1.25 + 1);
    return true;
}

TEST_CASE(reset_drops_occluder_history)
{
    static CF_Pixel open[PIXELS];
    render_material_entity light = emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    memcpy(open, pixels, sizeof open);
    render_material_entity scene[] = { emitter(), wall() };
    REQUIRE(solve(scene, 2, SIDE, 4));
    REQUIRE(solve(&light, 1, SIDE, 4));
    REQUIRE(pixels_match(open, pixels, PIXELS));
    return true;
}

/* A smaller target lights nothing from an empty scene, at a fractional
 * resolution as at the default one. */
TEST_CASE(resized_empty_scenes_stay_dark)
{
    REQUIRE(resize_targets(64));
    REQUIRE(solve(NULL, 0, 64, 4));
    for (int i = 0; i < 64 * 64; i++) {
        REQUIRE(
            pixels[i].colors.r <= 2 && pixels[i].colors.g <= 2 &&
            pixels[i].colors.b <= 2
        );
    }
    render_radiance_settings settings = render_radiance_get_settings(radiance);
    settings.scale = .976883f;
    REQUIRE(render_radiance_set_settings(radiance, settings));
    REQUIRE(fabsf(render_radiance_get_settings(radiance).scale - .976883f) < .000001f);
    REQUIRE(solve(NULL, 0, 64, 4));
    return true;
}

/* A grid that shrank into capacity sized for a finer one lights as a fresh
 * grid of its own size does. */
TEST_CASE(reused_capacity_matches_a_fresh_grid)
{
    static CF_Pixel reused[64 * 64];
    REQUIRE(resize_targets(64));
    render_material_entity light = emitter();
    light.pos[0] = 0;
    const float scales[] = { .5f, .25f, .5f, .976883f };
    for (int i = 0; i < 4; i++) {
        render_radiance_settings settings = render_radiance_get_settings(radiance);
        settings.scale = scales[i];
        REQUIRE(render_radiance_set_settings(radiance, settings));
        REQUIRE(solve(&light, 1, 64, 4));
        memcpy(reused, pixels, sizeof reused);
        render_radiance *shared = radiance;
        radiance = render_radiance_new();
        bool fresh = radiance && render_radiance_set_settings(radiance, settings) &&
                     render_radiance_resize(radiance, 64, 64) &&
                     solve(&light, 1, 64, 4);
        render_radiance_free(radiance);
        radiance = shared;
        REQUIRE(fresh);
        REQUIRE(pixels_match(reused, pixels, 64 * 64));
    }
    return true;
}

/* Lighting that normalization fitted always applies, and the coarsest color
 * at the lowest resolution still has a grid. */
TEST_CASE(fitted_lighting_applies_to_a_live_renderer)
{
    char notes[256];
    GameHRC2D wild = game_hrc2d_defaults();
    wild.resolution = 2;
    wild.trace_levels = 9;
    wild.exposure = NAN;
    REQUIRE(!render_radiance_set_lighting(radiance, wild));
    REQUIRE(
        render_radiance_set_lighting(radiance, render_radiance_normalize(wild, notes, sizeof notes))
    );
    GameHRC2D coarse = game_hrc2d_defaults();
    coarse.adaptive = false;
    coarse.resolution = .1f;
    coarse.color_resolution = GAME_HRC_COLOR_EIGHTH;
    REQUIRE(render_radiance_set_lighting(radiance, coarse));
    REQUIRE(render_radiance_resize(radiance, SIDE, SIDE));
    return true;
}

/* Lays out a 1280x720 view for the renderer's grids, as the scene renderer
 * does, reserving room for adaptive's lowest step. */
static bool view_fits()
{
    render_radiance_settings settings = render_radiance_get_settings(radiance);
    render_view_2d_frame frame;
    return render_view_2d_world_margin(
        (render_view_2d_camera){ .min_x = -640,
                                 .max_y = 360,
                                 .width = 1280,
                                 .height = 720,
                                 .output_width = 1280,
                                 .output_height = 720 },
        (render_view_2d_settings){ .margin = 64,
                                   .work_scale = settings.scale,
                                   .min_work_scale = settings.min_scale,
                                   .max_raster_size = 4096 },
        &frame
    );
}

/* Every resolution, color and gather combination, with and without adaptive,
 * applies to a live renderer in any order and lays out a view, at canvas
 * sizes with and without whole-pixel cells. */
TEST_CASE(every_lighting_combination_applies_and_lays_out)
{
    const int sizes[][2] = { { 1288, 728 }, { 200, 136 } };
    const float resolutions[] = { 1, .9f, .75f, .6f, .5f, .37f, .25f, .1f };
    for (int size = 0; size < 2; size++) {
        REQUIRE(render_radiance_resize(radiance, sizes[size][0], sizes[size][1]));
        for (int i = 0; i < 8 * 4 * 2 * 2; i++) {
            GameHRC2D lighting = game_hrc2d_defaults();
            lighting.resolution = resolutions[i / 16];
            lighting.color_resolution = (GameHRCColor)(i / 4 % 4);
            lighting.gather_resolution = i / 2 % 2 ? GAME_HRC_GATHER_HALF
                                                   : GAME_HRC_GATHER_FULL;
            lighting.adaptive = i % 2;
            REQUIRE(render_radiance_set_lighting(radiance, lighting));
            REQUIRE(view_fits());
        }
    }
    return true;
}

static void feed_time(double ms, int frames)
{
    for (int i = 0; i < frames; i++) {
        render_radiance_frame_time(radiance, ms);
    }
}

static bool rung_is(int rung, float resolution, GameHRCColor color, bool gather_half)
{
    render_radiance_status status = render_radiance_get_status(radiance);
    return status.rung == rung && status.resolution == resolution &&
           status.color == color && status.gather_half == gather_half;
}

/* Over budget, adaptive halves gather first when it is Auto, then coarsens
 * color up to Quarter at the authored resolution, then halves resolution down
 * to min_resolution with the authored color. It steps back up once the
 * measured cost of the step above fits, and restarts when the ladder changes. */
TEST_CASE(adaptive_steps_down_then_back_up)
{
    const GameHRCColor half = GAME_HRC_COLOR_HALF, quarter = GAME_HRC_COLOR_QUARTER;
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.resolution = 1;
    lighting.color_resolution = half;
    lighting.gather_resolution = GAME_HRC_GATHER_AUTO;
    lighting.min_resolution = .25f;
    lighting.target_fps = 125; /* 8 ms */
    REQUIRE(render_radiance_set_lighting(radiance, lighting));
    REQUIRE(render_radiance_get_status(radiance).rungs == 5);
    REQUIRE(rung_is(0, 1, half, false));
    REQUIRE(render_radiance_get_settings(radiance).min_scale == .125f);
    feed_time(20, 60);
    REQUIRE(rung_is(1, 1, half, true));
    /* A step's first frames refill history and are not judged, and one
     * window over budget is not sustained load. */
    feed_time(20, 89);
    REQUIRE(rung_is(1, 1, half, true));
    feed_time(12, 1);
    REQUIRE(rung_is(2, 1, quarter, true));
    REQUIRE(render_radiance_get_settings(radiance).scale == .25f);
    feed_time(12, 90);
    REQUIRE(rung_is(3, .5f, half, true));
    REQUIRE(render_radiance_get_settings(radiance).scale == .25f);
    feed_time(12, 90);
    REQUIRE(rung_is(4, .25f, half, true));
    /* The lowest step holds while still over budget. */
    feed_time(9, 90);
    REQUIRE(rung_is(4, .25f, half, true));
    /* 12 ms above against 9 here: 2 ms predicts 2.7 above, within budget. */
    feed_time(2, 90);
    REQUIRE(rung_is(4, .25f, half, true));
    feed_time(2, 30);
    REQUIRE(rung_is(3, .5f, half, true));
    /* Authoring that keeps the ladder keeps the step; a new ladder restarts. */
    lighting.target_fps = 250;
    REQUIRE(render_radiance_set_lighting(radiance, lighting));
    REQUIRE(rung_is(3, .5f, half, true));
    lighting.resolution = .5f;
    REQUIRE(render_radiance_set_lighting(radiance, lighting));
    REQUIRE(rung_is(0, .5f, half, false));
    lighting.adaptive = false;
    REQUIRE(render_radiance_set_lighting(radiance, lighting));
    REQUIRE(render_radiance_get_status(radiance).rungs == 1);
    REQUIRE(render_radiance_get_settings(radiance).scale == .25f);
    feed_time(50, 120);
    REQUIRE(rung_is(0, .5f, half, false));
    /* Frame intervals at the display's rate fit; missed frames step down. */
    lighting.adaptive = true;
    lighting.target_fps = 60;
    REQUIRE(render_radiance_set_lighting(radiance, lighting));
    for (int i = 0; i < 120; i++) {
        render_radiance_frame_interval(radiance, 1000.0 / 60 + (i % 2 ? 1.5 : -.5));
    }
    REQUIRE(rung_is(0, .5f, half, false));
    for (int i = 0; i < 90; i++) {
        render_radiance_frame_interval(radiance, 1000.0 / 30);
    }
    REQUIRE(rung_is(1, .5f, half, true));
    return true;
}

/* A pass is charged only the part of its command buffers' GPU interval after
 * all earlier work; buffers no mark covers count as Unmarked. */
TEST_CASE(gpu_timestamps_charge_passes_only_for_later_work)
{
    render_gpu_timing_enable(RENDER_GPU_TIMING_TIMESTAMPS, false);
    render_gpu_timing_start();
    unsigned a = render_gpu_timing_committed();
    render_gpu_timing_mark("A");
    unsigned b = render_gpu_timing_committed();
    unsigned c = render_gpu_timing_committed();
    render_gpu_timing_mark("B");
    unsigned unmarked = render_gpu_timing_committed();
    render_gpu_timing_start();
    unsigned d = render_gpu_timing_committed();
    render_gpu_timing_mark("A");
    render_gpu_timing_completed(c, 12, 15);
    render_gpu_timing_completed(a, 0, 10);
    render_gpu_timing_completed(unmarked, 20, 21);
    render_gpu_timing_completed(b, 8, 13);
    render_gpu_timing_completed(d, 21, 25);
    int count;
    const render_gpu_pass *passes = render_gpu_timing_passes(&count);
    bool charged = count == 3 && !strcmp(passes[0].name, "A") &&
                   passes[0].total_ms == 14 && passes[0].count == 2 &&
                   !strcmp(passes[1].name, "B") && passes[1].total_ms == 5 &&
                   !strcmp(passes[2].name, "Unmarked") && passes[2].total_ms == 1;
    render_gpu_timing_enable(RENDER_GPU_TIMING_OFF, false);
    REQUIRE(charged);
    return true;
}

/* ---------- One pass at a time ---------- */

static void draw_pass(CF_Mesh mesh, CF_Shader shader, CF_Material material, CF_Canvas target, int width, int height)
{
    cf_apply_canvas(target, true);
    cf_apply_mesh(mesh);
    cf_apply_shader(shader, material);
    cf_apply_viewport(0, 0, width, height);
    cf_apply_scissor(0, 0, width, height);
    cf_draw_elements();
}

static CF_Canvas level_canvas(int width, int height, CF_PixelFormat format)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.target.pixel_format = format;
    params.target.filter = CF_FILTER_NEAREST;
    CF_Canvas level = cf_make_canvas(params);
    assert(level.id);
    return level;
}

enum
{
    WORLD_W = 2,
    WORLD_H = 5,
    PREV_W = 4,
    CURR_W = 3
};

/* Whether each of the extended level's three directions at the middle row
 * holds what its two serial segments transmit: the near one's light plus the
 * far one's through the near one's optical depth. Even directions follow one
 * path; the odd one mixes both evenly. */
static bool joins_paths(CF_Canvas decoded, float segments[4][4])
{
    float values[WORLD_H][CURR_W][4];
    if (!read_canvas(decoded, values, (int)sizeof values)) {
        return false;
    }
    double paths[2][4];
    for (int path = 0; path < 2; path++) {
        const float *near = segments[path * 2], *far = segments[path * 2 + 1];
        for (int channel = 0; channel < 3; channel++) {
            paths[path][channel] = near[channel] + exp(-near[3]) * far[channel];
        }
        paths[path][3] = exp(-(double)near[3] - far[3]);
    }
    for (int direction = 0; direction < CURR_W; direction++) {
        /* Levels store directions evens first; see hrc/hrc_slots.shd. */
        int slot = direction & 1 ? (CURR_W + 1) / 2 + direction / 2 : direction / 2;
        for (int channel = 0; channel < 4; channel++) {
            double want = direction == 1
                            ? (paths[0][channel] + paths[1][channel]) * .5
                            : paths[direction / 2][channel];
            float actual = values[2][slot][channel];
            double tolerance = channel == 3
                                 ? .002 * fmax(fmin(want, 1 - want), .000001)
                                 : .002 * fmax(want, .01);
            if (!isfinite(actual) || fabs(actual - want) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

/* hrc/extend.frag joins each direction's near and far intervals in half-float
 * levels, as production stores them, for clear, uniform, unequal, thin and
 * saturated optical depths. */
TEST_CASE(extend_joins_serial_intervals)
{
    const char *vertex = "layout(location=0) in vec2 in_pos;\n"
                         "void main(){gl_Position=vec4(in_pos,0.0,1.0);}\n";
    /* At the middle row, even directions traverse A or B alone and the odd
     * one combines them; both far directions hold the same segments. */
    const char
        *seed_source = "layout(location=0) out vec4 out_value;\n"
                       "layout(set=3,binding=0) uniform uniform_block { vec4 "
                       "segments[4]; };\n"
                       "void main(){ivec2 p=ivec2(gl_FragCoord.xy);"
                       "int i=p.y*4+p.x;vec4 value=vec4(0);"
                       "if(i==8)value=segments[0];"
                       "if(i==6||i==7)value=segments[1];"
                       "if(i==9)value=segments[2];"
                       "if(i==14||i==15)value=segments[3];"
                       "out_value=value;}\n";
    const char
        *decode_source = "layout(location=0) out vec4 out_value;\n"
                         "layout(set=2,binding=0) uniform sampler2D u_curr;\n"
                         "void main(){vec4 v=texelFetch(u_curr,"
                         "ivec2(gl_FragCoord.xy),0);"
                         "out_value=vec4(v.rgb,exp(-v.a));}\n";
    CF_Shader seed = cf_make_shader_from_source(vertex, seed_source);
    CF_Shader extend = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/extend.frag");
    CF_Shader decode = cf_make_shader_from_source(vertex, decode_source);
    REQUIRE(seed.id && extend.id && decode.id);
    const float vertices[6] = { -1, -1, 3, -1, -1, 3 };
    CF_VertexAttribute attribute = { .name = "in_pos",
                                     .format = CF_VERTEX_FORMAT_FLOAT2 };
    CF_Mesh mesh = cf_make_mesh(sizeof vertices, &attribute, 1, 2 * (int)sizeof(float));
    cf_mesh_update_vertex_data(mesh, (void *)vertices, 3);
    CF_Material material = cf_make_material();
    CF_Canvas prev = level_canvas(PREV_W, WORLD_H, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    CF_Canvas curr = level_canvas(CURR_W, WORLD_H, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    CF_Canvas decoded = level_canvas(CURR_W, WORLD_H, CF_PIXEL_FORMAT_R32G32B32A32_FLOAT);
    const float depths[][4] = {
        { 0, 0, 0, 0 },
        { 1, 1, 1, 1 },
        { 0, 0, 10, 0 },
        { .5f, 2, 3, .25f },
        { .0001f, .0001f, .0001f, .0001f },
        { 0, .0001f, .0002f, .0001f },
        { 100000, 0, 100000, 0 },
        { 0, 0, 100000, 0 },
    };
    int joined = 0;
    for (int i = 0; i < 8; i++) {
        float segments[4][4] = {
            { 1, 0, 0, depths[i][0] },
            { 0, 3, 0, depths[i][1] },
            { 0, 2, 0, depths[i][2] },
            { 0, 0, 4, depths[i][3] },
        };
        cf_app_update(NULL);
        cf_material_set_uniform_fs(material, "segments", segments, CF_UNIFORM_TYPE_FLOAT4, 4);
        draw_pass(mesh, seed, material, prev, PREV_W, WORLD_H);
        int values[] = { 1, 0, WORLD_W, WORLD_H };
        const char *names[] = { "u_cascade", "u_transposed", "u_world_w", "u_world_h" };
        for (int j = 0; j < 4; j++) {
            cf_material_set_uniform_fs(material, names[j], values + j, CF_UNIFORM_TYPE_INT, 1);
        }
        cf_material_set_texture_fs(material, "u_prev", cf_canvas_get_target(prev));
        draw_pass(mesh, extend, material, curr, CURR_W, WORLD_H);
        cf_material_set_texture_fs(material, "u_curr", cf_canvas_get_target(curr));
        draw_pass(mesh, decode, material, decoded, CURR_W, WORLD_H);
        cf_app_draw_onto_screen(false);
        joined += joins_paths(decoded, segments);
    }
    cf_destroy_canvas(decoded);
    cf_destroy_canvas(curr);
    cf_destroy_canvas(prev);
    cf_destroy_material(material);
    cf_destroy_mesh(mesh);
    cf_destroy_shader(decode);
    cf_destroy_shader(extend);
    cf_destroy_shader(seed);
    REQUIRE(joined == 8);
    return true;
}

/* hrc/filter.frag returns this frame's light where it is smooth: a history
 * shifted by the camera's offset agrees with it, history far outside its
 * neighborhood is rejected, and invalid history is ignored. */
TEST_CASE(filter_keeps_light_true_to_the_current_frame)
{
    enum
    {
        W = 16,
        H = 8
    };
    static float current[W * H][4], history[W * H][4], absorption[W * H][4];
    CF_Texture textures[3];
    for (int i = 0; i < 3; i++) {
        CF_TextureParams params = cf_texture_defaults(W, H);
        params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        params.filter = CF_FILTER_LINEAR;
        params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
        textures[i] = cf_make_texture(params);
    }
    CF_CanvasParams params = cf_canvas_defaults(W, H);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas filtered = cf_make_canvas(params);
    CF_Material material = cf_make_material();
    CF_Mesh mesh = render_make_pass_mesh();
    CF_Shader shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/filter.frag");
    REQUIRE(shader.id && filtered.id);
    float worst = 0;
    for (int scenario = 0; scenario < 3; scenario++) {
        for (int i = 0; i < W * H; i++) {
            for (int c = 0; c < 4; c++) {
                current[i][c] = scenario == 0 ? .1f + .02f * (i % W) : .2f;
                history[i][c] = scenario == 0 ? .1f + .02f * (i % W - .5f) : 10;
            }
        }
        cf_app_update(NULL);
        cf_texture_update(textures[0], current, sizeof current);
        cf_texture_update(textures[1], absorption, sizeof absorption);
        cf_texture_update(textures[2], history, sizeof history);
        cf_material_set_texture_fs(material, "u_in_radiance", textures[0]);
        cf_material_set_texture_fs(material, "u_source", textures[1]);
        cf_material_set_texture_fs(material, "u_prev_radiance", textures[2]);
        float texel_size[] = { 1.f / W, 1.f / H };
        float offset[] = { scenario == 0 ? .5f / W : 0, 0 };
        float threshold = .1f, weight = .1f;
        int valid = scenario != 2;
        cf_material_set_uniform_fs(material, "u_texel_size", texel_size, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_fs(material, "u_history_offset", offset, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_material_set_uniform_fs(material, "u_history_valid", &valid, CF_UNIFORM_TYPE_INT, 1);
        cf_material_set_uniform_fs(material, "u_threshold", &threshold, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_material_set_uniform_fs(material, "u_hysteresis", &weight, CF_UNIFORM_TYPE_FLOAT, 1);
        cf_apply_canvas(filtered, true);
        cf_apply_mesh(mesh);
        cf_apply_shader(shader, material);
        cf_draw_elements();
        cf_app_draw_onto_screen(false);
        static uint16_t halves[W * H][4];
        REQUIRE(read_canvas(filtered, halves, (int)sizeof halves));
        for (int y = 1; y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                int i = y * W + x;
                worst = fmaxf(worst, fabsf(half_value(halves[i][0]) - current[i][0]));
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        cf_destroy_texture(textures[i]);
    }
    cf_destroy_canvas(filtered);
    cf_destroy_material(material);
    cf_destroy_shader(shader);
    cf_destroy_mesh(mesh);
    REQUIRE(worst < .0005f);
    return true;
}

void test_render_radiance()
{
    setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_AppOptionFlags options = CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT;
    CF_Result made = cf_make_app("radiance", 0, 0, 0, SIDE, SIDE, options, NULL);
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(emitters_light_open_space_through_their_materials);
    RUN_TEST_CASE(walls_block_light_by_their_absorption);
    RUN_TEST_CASE(bounced_light_gathers_over_frames);
    RUN_TEST_CASE(transparent_reflective_edges_do_not_feed_back);
    RUN_TEST_CASE(reset_drops_occluder_history);
    RUN_TEST_CASE(resized_empty_scenes_stay_dark);
    RUN_TEST_CASE(reused_capacity_matches_a_fresh_grid);
    RUN_TEST_CASE(fitted_lighting_applies_to_a_live_renderer);
    RUN_TEST_CASE(every_lighting_combination_applies_and_lays_out);
    RUN_TEST_CASE(adaptive_steps_down_then_back_up);
    RUN_TEST_CASE(gpu_timestamps_charge_passes_only_for_later_work);
    pu_clear_setup();
    RUN_TEST_CASE(extend_joins_serial_intervals);
    RUN_TEST_CASE(filter_keeps_light_true_to_the_current_frame);
    cf_destroy_app();
}
