#include "render/radiance.h"

#include "hidden_app.h"
#include "hrc_cases.h"
#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/material_raster.h"
#include "render/view_2d.h"
#include "third_party/pico_headers/pico_unit.h"
#if CHIBI_TEST_MATERIAL_GRAPHS
#include "material_graphs.h"
#include "render/shader_catalog.h"
#endif

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
    REQUIRE(first.raster_width % RENDER_VIEW_2D_RASTER_STEP == 0);
    REQUIRE(first.raster_height % RENDER_VIEW_2D_RASTER_STEP == 0);
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
    HRC2D defaults = hrc2d_defaults();
    HRC2D fitted = render_radiance_normalize(defaults, notes, sizeof notes);
    REQUIRE(!notes[0] && fitted.resolution == defaults.resolution);
    HRC2D wild = defaults;
    wild.resolution = 2;
    wild.surface_mode = (HRCSurfaceMode)7;
    wild.surface_depth_quality = (HRCDepthQuality)-1;
    wild.bloom = 2;
    wild.exposure = NAN;
    fitted = render_radiance_normalize(wild, notes, sizeof notes);
    REQUIRE(fitted.resolution == 1 && fitted.surface_mode == HRC_SURFACE_DEPTH);
    REQUIRE(fitted.surface_depth_quality == HRC_DEPTH_LOW);
    REQUIRE(fitted.bloom == 1 && fitted.exposure == 0);
    REQUIRE(strstr(notes, "resolution 2 -> 1") && strstr(notes, "surface_mode 7 -> 2"));
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
    assert(
        render_radiance_set_lighting(radiance, test_reference_lighting(), render_radiance_lab_defaults())
    );
}

static void teardown()
{
    cf_destroy_canvas(canvas);
    render_radiance_free(radiance);
    render_material_raster_free(raster);
}

static render_material_entity wall_with(float absorption, float albedo)
{
    render_material_entity entity = test_wall();
    for (int i = 0; i < 3; i++) {
        entity.absorption[i] = absorption;
        entity.albedo[i] = albedo;
    }
    entity.absorption[3] = absorption > 0 ? 1 : 0;
    entity.albedo[3] = 1;
    return entity;
}

/* Output pixels as displayed. */
static const CF_PixelFormat LDR = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;

/* Resizes the raster, HRC and the output to size x size, with output pixels
 * in `format`. */
static bool resize_targets(int size, CF_PixelFormat format)
{
    cf_destroy_canvas(canvas);
    CF_CanvasParams params = cf_canvas_defaults(size, size);
    params.target.pixel_format = format;
    canvas = cf_make_canvas(params);
    return canvas.id && render_material_raster_resize(raster, size, size) &&
           render_radiance_resize(radiance, size, size);
}

/* Lights `view` into size x size pixels of the canvas for `frames` frames,
 * continuing from the light HRC already holds. */
static bool draw(const render_material_entity *entities, int count, int size, render_view_2d view, int frames)
{
    assert(frames > 0 && size <= SIDE);
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
    return true;
}

static bool read_pixels(int size)
{
    return test_read_canvas(canvas, pixels, size * size * (int)sizeof *pixels);
}

/* Lights size x size pixels showing as many world units around the origin
 * for `frames` frames from a reset, then reads the result into `pixels`. */
static bool solve(const render_material_entity *entities, int count, int size, int frames)
{
    render_radiance_reset(radiance);
    return draw(entities, count, size, test_centered((float)size), frames) &&
           read_pixels(size);
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

static float texel(const uint16_t *halves, int x, int channel)
{
    return test_half(halves[4 * (SIDE / 2 * SIDE + x) + channel]);
}

/* The emitter lights open space green from its disc and red from its border,
 * and its material reaches HRC as emission, absorption and a flat surface. */
TEST_CASE(emitters_light_open_space_through_their_materials)
{
    render_material_entity light = test_emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    CF_Pixel center = pixels[SIDE / 2 * SIDE + 36],
             border = pixels[SIDE / 2 * SIDE + 43];
    REQUIRE(center.colors.g > center.colors.r && center.colors.g > 100);
    REQUIRE(border.colors.r > border.colors.g);
    render_material_entity scene[] = { test_emitter(), test_wall() };
    REQUIRE(solve(scene, 2, SIDE, 4));
    static uint16_t halves[PIXELS * 4];
    render_material_textures textures = render_material_raster_textures(raster);
    REQUIRE(test_read_texture(textures.source, halves, PIXELS));
    REQUIRE(fabsf(texel(halves, 36, 1) - .08f) < .002f);
    REQUIRE(fabsf(texel(halves, 43, 0) - .2f) < .002f);
    /* Optical depth is the mean absorption. */
    REQUIRE(fabsf(texel(halves, 64, 3) - 1) < .002f);
    /* A plain solid carries no bump tilt, edge light or gloss. */
    REQUIRE(test_read_texture(textures.normal, halves, PIXELS));
    for (int channel = 0; channel < 4; channel++) {
        REQUIRE(fabsf(texel(halves, 43, channel)) < .002f);
    }
    return true;
}

TEST_CASE(walls_block_light_by_their_absorption)
{
    render_material_entity light = test_emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    double open = receiver_light();
    render_material_entity opaque[] = { test_emitter(), test_wall() };
    REQUIRE(solve(opaque, 2, SIDE, 4));
    double blocked = receiver_light();
    render_material_entity finite[] = { test_emitter(), wall_with(.2f, 0) };
    REQUIRE(solve(finite, 2, SIDE, 4));
    double dimmed = receiver_light();
    REQUIRE(open > 3 && blocked < open * .8);
    REQUIRE(dimmed > blocked + .1 && dimmed < open);
    return true;
}

TEST_CASE(bounced_light_gathers_over_frames)
{
    render_material_entity scene[] = { test_emitter(), wall_with(.2f, .8f) };
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
    render_material_entity glass[] = { test_emitter(), wall_with(.0005f, .9f) };
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
    render_material_entity light = test_emitter();
    REQUIRE(solve(&light, 1, SIDE, 4));
    memcpy(open, pixels, sizeof open);
    render_material_entity scene[] = { test_emitter(), test_wall() };
    REQUIRE(solve(scene, 2, SIDE, 4));
    REQUIRE(solve(&light, 1, SIDE, 4));
    REQUIRE(pixels_match(open, pixels, PIXELS));
    return true;
}

static bool dark(int count)
{
    for (int i = 0; i < count; i++) {
        if (pixels[i].colors.r > 2 || pixels[i].colors.g > 2 || pixels[i].colors.b > 2) {
            return false;
        }
    }
    return true;
}

/* A smaller target lights nothing from an empty scene, at a fractional
 * resolution as at the default one. */
TEST_CASE(resized_empty_scenes_stay_dark)
{
    REQUIRE(resize_targets(64, LDR));
    REQUIRE(solve(NULL, 0, 64, 4));
    REQUIRE(dark(64 * 64));
    render_radiance_settings settings = render_radiance_get_settings(radiance);
    settings.scale = .976883f;
    REQUIRE(render_radiance_set_settings(radiance, settings));
    REQUIRE(fabsf(render_radiance_get_settings(radiance).scale - .976883f) < .000001f);
    REQUIRE(solve(NULL, 0, 64, 4));
    REQUIRE(dark(64 * 64));
    return true;
}

/* A grid that shrank into capacity sized for a finer one lights as a fresh
 * grid of its own size does. */
TEST_CASE(reused_capacity_matches_a_fresh_grid)
{
    static CF_Pixel reused[64 * 64];
    REQUIRE(resize_targets(64, LDR));
    render_material_entity light = test_emitter();
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
        bool fresh = radiance &&
                     render_radiance_set_lighting(
                         radiance,
                         test_reference_lighting(),
                         render_radiance_lab_defaults()
                     ) &&
                     render_radiance_set_settings(radiance, settings) &&
                     render_radiance_resize(radiance, 64, 64) &&
                     solve(&light, 1, 64, 4);
        render_radiance_free(radiance);
        radiance = shared;
        REQUIRE(fresh);
        REQUIRE(pixels_match(reused, pixels, 64 * 64));
    }
    return true;
}

/* A receiver right of the view's center, across the wall from the emitter. */
static render_material_entity receiver(float absorption)
{
    render_material_entity entity = wall_with(absorption, .8f);
    entity.pos[0] = 28;
    entity.points[0] = 8;
    entity.extent[0] = 9;
    return entity;
}

/* Mean difference of 8-bit channels over the whole view. */
static double mean_difference(const CF_Pixel *a, const CF_Pixel *b)
{
    double sum = 0;
    for (int i = 0; i < PIXELS; i++) {
        sum += abs((int)a[i].colors.r - b[i].colors.r) +
               abs((int)a[i].colors.g - b[i].colors.g) +
               abs((int)a[i].colors.b - b[i].colors.b);
    }
    return sum / (3.0 * PIXELS);
}

/* Lighting without fog, so the receiver measures transport alone. */
static HRC2D clear_lighting()
{
    HRC2D lighting = test_reference_lighting();
    lighting.adaptive = false;
    lighting.fog = 0;
    return lighting;
}

/* Color two and four times coarser than brightness keeps light, shadow and
 * bounce close to full color at the same resolution. */
TEST_CASE(split_luminance_stays_close_to_full_color)
{
    static CF_Pixel full[PIXELS];
    render_material_entity open[] = { test_emitter(), receiver(.2f) };
    render_material_entity blocked[] = { test_emitter(), test_wall(), receiver(.2f) };
    HRC2D lighting = clear_lighting();
    render_radiance_lab lab = render_radiance_lab_defaults();
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    REQUIRE(solve(open, 2, SIDE, 8));
    double full_light = receiver_light();
    memcpy(full, pixels, sizeof full);
    for (int shift = 1; shift <= 2; shift++) {
        lab.color_resolution = (render_radiance_color)shift;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        REQUIRE(render_radiance_get_settings(radiance).scale == 1.f / (float)(1 << shift));
        REQUIRE(solve(open, 2, SIDE, 8));
        double split_light = receiver_light();
        REQUIRE(split_light > full_light * .9 && split_light < full_light * 1.1);
        REQUIRE(mean_difference(full, pixels) < .25);
        REQUIRE(solve(blocked, 3, SIDE, 8));
        REQUIRE(receiver_light() < split_light * .05);
    }
    return true;
}

/* Work cells that are not a whole number of canvas pixels still average each
 * pixel's optical depth once: light through a thin hazy wall matches a grid
 * whose cells are exactly two pixels. */
TEST_CASE(fractional_cells_absorb_as_whole_ones)
{
    render_material_entity haze = wall_with(.4f, 0);
    haze.points[0] = 1;
    haze.extent[0] = 2;
    render_material_entity scene[] = { test_emitter(), haze, receiver(0) };
    HRC2D lighting = clear_lighting();
    lighting.feedback = false;
    lighting.resolution = .5f;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, render_radiance_lab_defaults()));
    REQUIRE(solve(scene, 3, SIDE, 2));
    double whole = receiver_light();
    lighting.resolution = .52f;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, render_radiance_lab_defaults()));
    REQUIRE(solve(scene, 3, SIDE, 2));
    REQUIRE(fabs(receiver_light() - whole) < whole * .03);
    return true;
}

/* An emitter smaller than a pixel. */
static render_material_entity spark()
{
    render_material_entity entity = test_emitter();
    entity.radius = .8f;
    entity.extent[0] = entity.extent[1] = 2;
    entity.border_thickness = .5f;
    entity.emission[1] = .4f;
    entity.border_emission[0] = .5f;
    return entity;
}

/* Split color and a half-resolution gather keep a spark smaller than a pixel
 * lit and stop at a wall thinner than one, at full size and resized. */
TEST_CASE(reduced_reconstruction_keeps_sparks_and_thin_walls)
{
    render_material_entity thin = wall_with(1, .8f);
    thin.points[0] = .8f;
    thin.extent[0] = 1.8f;
    render_material_entity open[] = { spark(), receiver(0) };
    render_material_entity blocked[] = { spark(), thin, receiver(0) };
    HRC2D lighting = clear_lighting();
    render_radiance_lab lab = render_radiance_lab_defaults();
    lighting.feedback = false;
    double full_gather = 0;
    for (int split = 0; split <= 1; split++) {
        for (int half = 0; half <= 1; half++) {
            /* Color at a half either way; split solves brightness at full. */
            lighting.resolution = split ? 1 : .5f;
            lab.color_resolution = split ? RENDER_RADIANCE_COLOR_HALF
                                         : RENDER_RADIANCE_COLOR_FULL;
            lab.gather_resolution = half ? RENDER_RADIANCE_GATHER_HALF
                                         : RENDER_RADIANCE_GATHER_FULL;
            REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
            REQUIRE(solve(open, 2, SIDE, 4));
            double lit = receiver_light();
            REQUIRE(solve(blocked, 3, SIDE, 4));
            REQUIRE(lit > .01 && receiver_light() < lit * .98);
            if (!split && !half) {
                full_gather = lit;
            }
            if (half) {
                REQUIRE(lit > full_gather * .2 && lit < full_gather * 4);
            }
        }
    }
    REQUIRE(resize_targets(64, LDR));
    REQUIRE(solve(open, 2, 64, 2));
    REQUIRE(pixels[32 * 64 + 60].colors.g > 0);
    return true;
}

/* Lighting that normalization fitted always applies, and the coarsest color
 * at the lowest resolution still has a grid. */
TEST_CASE(fitted_lighting_applies_to_a_live_renderer)
{
    char notes[256];
    render_radiance_lab lab = render_radiance_lab_defaults();
    HRC2D wild = hrc2d_defaults();
    wild.resolution = 2;
    wild.surface_mode = (HRCSurfaceMode)9;
    wild.exposure = NAN;
    REQUIRE(!render_radiance_set_lighting(radiance, wild, lab));
    REQUIRE(
        render_radiance_set_lighting(radiance, render_radiance_normalize(wild, notes, sizeof notes), lab)
    );
    HRC2D coarse = hrc2d_defaults();
    coarse.adaptive = false;
    coarse.resolution = .1f;
    lab.color_resolution = RENDER_RADIANCE_COLOR_EIGHTH;
    REQUIRE(render_radiance_set_lighting(radiance, coarse, lab));
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
            HRC2D lighting = hrc2d_defaults();
            render_radiance_lab lab = render_radiance_lab_defaults();
            lighting.resolution = resolutions[i / 16];
            lab.color_resolution = (render_radiance_color)(i / 4 % 4);
            lab.gather_resolution = i / 2 % 2 ? RENDER_RADIANCE_GATHER_HALF
                                              : RENDER_RADIANCE_GATHER_FULL;
            lighting.adaptive = i % 2;
            REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
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

static bool rung_is(int rung, float resolution, render_radiance_color color, bool gather_half)
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
    const render_radiance_color half = RENDER_RADIANCE_COLOR_HALF,
                                quarter = RENDER_RADIANCE_COLOR_QUARTER;
    HRC2D lighting = hrc2d_defaults();
    render_radiance_lab lab = render_radiance_lab_defaults();
    lighting.resolution = 1;
    lab.color_resolution = half;
    lab.gather_resolution = RENDER_RADIANCE_GATHER_AUTO;
    lighting.target_fps = 125; /* 8 ms */
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
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
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    REQUIRE(rung_is(3, .5f, half, true));
    lighting.resolution = .5f;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    REQUIRE(rung_is(0, .5f, half, false));
    lighting.adaptive = false;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    REQUIRE(render_radiance_get_status(radiance).rungs == 1);
    REQUIRE(render_radiance_get_settings(radiance).scale == .25f);
    feed_time(50, 120);
    REQUIRE(rung_is(0, .5f, half, false));
    /* Frame intervals at the display's rate fit; missed frames step down. */
    lighting.adaptive = true;
    lighting.target_fps = 60;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
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
    if (!test_read_canvas(decoded, values, (int)sizeof values)) {
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
    /* fmaxf drops NaN, so a NaN error alone would leave `worst` at zero. */
    bool finite = true;
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
        REQUIRE(test_read_canvas(filtered, halves, (int)sizeof halves));
        for (int y = 1; y < H - 1; y++) {
            for (int x = 1; x < W - 1; x++) {
                int i = y * W + x;
                float value = test_half(halves[i][0]);
                finite &= isfinite(value);
                worst = fmaxf(worst, fabsf(value - current[i][0]));
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
    REQUIRE(finite && worst < .0005f);
    return true;
}

/* ---------- Surface light, with material graphs ---------- */
#if CHIBI_TEST_MATERIAL_GRAPHS

/* Output pixels of unclamped light. */
static const CF_PixelFormat HDR = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
static uint16_t hdr[PIXELS * 4];
/* A second HRC to compare the shared one against. */
static render_radiance *other;

static void surface_setup()
{
    setup();
    other = render_radiance_new();
    assert(other && render_radiance_resize(other, SIDE, SIDE));
}

static void surface_teardown()
{
    render_radiance_free(other);
    teardown();
    render_shader_catalog_clear();
}

/* Reads size x size pixels of the HDR canvas into `hdr`. */
static bool read_hdr(int size)
{
    return test_read_canvas(canvas, hdr, size * size * 4 * (int)sizeof *hdr);
}

/* Lights the view for `frames` frames from a reset into the HDR canvas and
 * reads it into `hdr`. */
static bool solve_hdr(const render_material_entity *entities, int count, int frames)
{
    render_radiance_reset(radiance);
    return draw(entities, count, SIDE, test_centered(SIDE), frames) && read_hdr(SIDE);
}

/* Registers a relief material and returns its id for an entity's
 * reserved[0]. */
static float relief_material(const char *name, test_relief relief)
{
    bool registered = test_register_relief(name, relief);
    assert(registered);
    return (float)render_shader_find(name);
}

/* An opaque, reflective wall through the origin that receives surface light
 * from `edge_light` world units in. */
static render_material_entity lit_wall(float half_width, float edge_light)
{
    render_material_entity entity = wall_with(1, .6f);
    entity.extent[0] = half_width + 1;
    entity.points[0] = half_width;
    entity.reserved[2] = edge_light;
    return entity;
}

/* Green across the middle row of `pixels`. */
static int green_at(int x)
{
    return pixels[SIDE / 2 * SIDE + x].colors.g;
}

/* Surface light 12 world units into solids, with relief and without bevels
 * or highlights. */
static HRC2D surface_lighting()
{
    HRC2D lighting = clear_lighting();
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = 0;
    lighting.specular = 0;
    return lighting;
}

static bool no_emission(const uint16_t *halves)
{
    for (int i = 0; i < PIXELS; i++) {
        if (halves[4 * i] || halves[4 * i + 1] || halves[4 * i + 2]) {
            return false;
        }
    }
    return true;
}

/* Two touching blocks with edge light and no emission stay dark. An emitter
 * on the left lights the near edge only, not the seam between them or the
 * far edge, whether HRC traces one level or three. */
TEST_CASE(silhouettes_light_from_their_lit_side)
{
    render_material_entity pieces[3];
    for (int i = 0; i < 2; i++) {
        pieces[i] = wall_with(1, .6f);
        pieces[i].pos[0] = i ? 12 : -12;
        pieces[i].extent[0] = 14;
        pieces[i].extent[1] = 22;
        pieces[i].points[0] = 12;
        pieces[i].points[1] = 20;
        pieces[i].reserved[2] = 2;
    }
    pieces[2] = test_emitter();
    pieces[2].pos[0] = -38;
    for (int levels = 1; levels <= 3; levels += 2) {
        HRC2D lighting = clear_lighting();
        render_radiance_lab lab = render_radiance_lab_defaults();
        lab.trace_levels = levels;
        lighting.surface_radius = 12;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        REQUIRE(solve(pieces, 2, SIDE, 12));
        REQUIRE(green_at(41) < 2);
        REQUIRE(test_read_texture(render_material_raster_textures(raster).source, hdr, PIXELS));
        REQUIRE(no_emission(hdr));
        REQUIRE(solve(pieces, 3, SIDE, 12));
        REQUIRE(green_at(41) > 10 && green_at(64) < 2 && green_at(86) < 2);
    }
    return true;
}

/* A material's bump tilt and gloss reach the surface channels, and relief
 * facing the light shines brighter than flat relief, which shines brighter
 * than relief facing away. */
TEST_CASE(relief_facing_the_light_shines_brighter)
{
    REQUIRE(
        render_radiance_set_lighting(radiance, surface_lighting(), render_radiance_lab_defaults())
    );
    const char *names[] = { "Relief facing the light", "Flat relief", "Relief facing away" };
    const float tilts[] = { -.75f, 0, .75f };
    int face[3];
    for (int i = 0; i < 3; i++) {
        render_material_entity scene[] = { test_emitter(), lit_wall(6, 1.5f) };
        scene[1].reserved[0] = relief_material(
            names[i],
            (test_relief){ .tilt = { tilts[i] }, .roughness = .25f }
        );
        REQUIRE(solve(scene, 2, SIDE, 12));
        face[i] = green_at(60);
        if (i) {
            continue;
        }
        REQUIRE(test_read_texture(render_material_raster_textures(raster).normal, hdr, PIXELS));
        REQUIRE(fabsf(texel(hdr, SIDE / 2, 0) + .6f) < .01f);
        REQUIRE(fabsf(texel(hdr, SIDE / 2, 1)) < .01f);
        REQUIRE(fabsf(texel(hdr, SIDE / 2, 2) - 1.5f) < .01f);
        REQUIRE(fabsf(texel(hdr, SIDE / 2, 3) - .75f) < .01f);
    }
    REQUIRE(face[0] > face[1] + 8 && face[1] > face[2] + 8);
    return true;
}

/* The surface radius is in world units: the same picture at twice the world
 * scale lights half as many pixels deep into a solid. */
TEST_CASE(surface_light_reaches_a_world_space_depth)
{
    REQUIRE(
        render_radiance_set_lighting(radiance, surface_lighting(), render_radiance_lab_defaults())
    );
    const int face_x = SIDE / 2 - 40;
    int depth[2] = { 0 };
    for (int zoom = 0; zoom < 2; zoom++) {
        float k = zoom ? 2.f : 1.f;
        render_material_entity scene[] = { test_emitter(), lit_wall(40 * k, .4f) };
        scene[0].pos[0] = -55 * k;
        scene[0].radius *= k;
        scene[0].border_thickness *= k;
        scene[0].extent[0] *= k;
        scene[0].extent[1] *= k;
        scene[1].points[1] *= k;
        scene[1].extent[1] *= k;
        render_radiance_reset(radiance);
        REQUIRE(draw(scene, 2, SIDE, test_centered(SIDE * k), 12) && read_pixels(SIDE));
        int face = green_at(face_x + 1);
        REQUIRE(face > 20);
        for (int x = face_x + 1; x < SIDE && !depth[zoom]; x++) {
            if (green_at(x) * 4 < face) {
                depth[zoom] = x - face_x;
            }
        }
    }
    REQUIRE(depth[0] >= 6 && depth[0] <= 18);
    REQUIRE(abs(2 * depth[1] - depth[0]) <= 3);
    return true;
}

/* Below a ceiling lit from beneath, relief facing down is brighter than
 * relief facing up. */
TEST_CASE(relief_below_a_ceiling_faces_down_to_the_light)
{
    REQUIRE(
        render_radiance_set_lighting(radiance, surface_lighting(), render_radiance_lab_defaults())
    );
    const char *names[] = { "Relief facing down", "Relief facing up" };
    int below[2];
    for (int i = 0; i < 2; i++) {
        render_material_entity scene[] = { test_emitter(), lit_wall(64, 1.5f) };
        scene[0].pos[0] = 0;
        scene[0].pos[1] = -30;
        scene[1].pos[1] = 20;
        scene[1].points[1] = 6;
        scene[1].extent[1] = 8;
        scene[1].reserved[0] = relief_material(
            names[i],
            (test_relief){ .tilt = { 0, i ? .75f : -.75f }, .roughness = .25f }
        );
        REQUIRE(solve(scene, 2, SIDE, 12));
        below[i] = pixels[48 * SIDE + 64].colors.g;
    }
    REQUIRE(below[0] > below[1] + 8);
    return true;
}

/* Mean red and blue over the 4 x 4 pixels at the center of `hdr`. */
static bool center_red_blue(float result[2])
{
    result[0] = result[1] = 0;
    for (int y = 62; y <= 65; y++) {
        for (int x = 62; x <= 65; x++) {
            int p = (y * SIDE + x) * 4;
            float red = test_half(hdr[p]), blue = test_half(hdr[p + 2]);
            if (!isfinite(red + blue)) {
                return false;
            }
            result[0] += red / 16;
            result[1] += blue / 16;
        }
    }
    return true;
}

/* A red emitter left of a lit wall, a blue one right of it, and a thin wall
 * between the red one and the lit wall. */
static void opposing_lights(render_material_entity scene[4])
{
    scene[0] = test_emitter();
    scene[1] = test_emitter();
    for (int i = 0; i < 2; i++) {
        scene[i].pos[0] = i ? 28 : -28;
        scene[i].radius = 4;
        scene[i].extent[0] = scene[i].extent[1] = 6;
        scene[i].border_thickness = 0;
        memset(scene[i].scene, 0, sizeof scene[i].scene);
        memset(scene[i].emission, 0, sizeof scene[i].emission);
        memset(scene[i].border_emission, 0, sizeof scene[i].border_emission);
        scene[i].emission[i ? 2 : 0] = .2f;
        scene[i].emission[3] = 1;
    }
    scene[2] = lit_wall(6, 1.5f);
    scene[2].points[1] = 18;
    scene[2].extent[1] = 20;
    scene[3] = test_wall();
    scene[3].pos[0] = -16;
    scene[3].points[0] = 2;
    scene[3].extent[0] = 4;
}

/* Surface light keeps RGB attached to the direction it came from, in every
 * shadow mode: opposing colors respond to relief banked toward each, and
 * blocking one color's path, by a wall or by raised relief, leaves the other
 * as it was. */
TEST_CASE(surface_colors_keep_their_directions)
{
    REQUIRE(resize_targets(SIDE, HDR));
    float banks[3] = {
        relief_material("Relief facing red", (test_relief){ .tilt = { -.75f }, .roughness = 1 }),
        relief_material("Relief facing blue", (test_relief){ .tilt = { .75f }, .roughness = 1 }),
        relief_material("Flat rough relief", (test_relief){ .roughness = 1 }),
    };
    float ridge = relief_material("Raised ridge", (test_relief){ .roughness = 1, .height = 12 });
    const HRCShadowMode modes[] = { HRC_SHADOW_REFERENCE,
                                    HRC_SHADOW_HORIZON4,
                                    HRC_SHADOW_HORIZON8 };
    for (int mode = 0; mode < 3; mode++) {
        HRC2D lighting = surface_lighting();
        lighting.feedback = false;
        lighting.ambient = lighting.shadow_floor = 0;
        lighting.tone_mapping = HRC_TONE_AGX;
        lighting.surface_shadow_mode = modes[mode];
        REQUIRE(render_radiance_set_lighting(radiance, lighting, render_radiance_lab_defaults()));
        render_material_entity scene[4];
        opposing_lights(scene);
        float open[3][2];
        for (int bank = 0; bank < 3; bank++) {
            scene[2].reserved[0] = banks[bank];
            REQUIRE(solve_hdr(scene, 3, 12) && center_red_blue(open[bank]));
        }
        float facing_red = open[0][0] / fmaxf(open[0][1], 1e-6f);
        float facing_blue = open[1][0] / fmaxf(open[1][1], 1e-6f);
        REQUIRE(open[0][0] > .001f && open[1][1] > .001f);
        REQUIRE(facing_red > 1.25f && facing_blue < .8f);
        REQUIRE(facing_red > facing_blue * 1.8f);
        float blocked[2];
        REQUIRE(solve_hdr(scene, 4, 12) && center_red_blue(blocked));
        REQUIRE(blocked[0] < open[2][0] * .65f);
        REQUIRE(blocked[1] > open[2][1] * .8f && blocked[1] < open[2][1] * 1.2f);
        /* A ridge over the same opaque receiver changes only its height, so
         * transport is unchanged. Its shadow toward red must not darken blue. */
        scene[3] = scene[2];
        scene[3].pos[0] = -4;
        scene[3].points[0] = 1;
        scene[3].extent[0] = 3;
        scene[3].reserved[0] = ridge;
        REQUIRE(solve_hdr(scene, 4, 12) && center_red_blue(blocked));
        REQUIRE(blocked[0] < open[2][0] * .65f);
        REQUIRE(blocked[1] > open[2][1] * .9f && blocked[1] < open[2][1] * 1.1f);
    }
    return true;
}

/* Summed light difference over a strip of the lit wall's face. */
static float face_difference(const uint16_t *a, const uint16_t *b)
{
    float sum = 0;
    for (int y = 54; y < 74; y++) {
        for (int x = 58; x < 63; x++) {
            for (int channel = 0; channel < 3; channel++) {
                int p = (y * SIDE + x) * 4 + channel;
                float va = test_half(a[p]), vb = test_half(b[p]);
                if (!isfinite(va + vb)) {
                    return INFINITY;
                }
                sum += fabsf(va - vb) / 300;
            }
        }
    }
    return sum;
}

/* Received surface light follows the same hysteresis as open-space light. A
 * small move of its source keeps some history, which later frames settle and
 * a reset drops. */
TEST_CASE(surface_light_history_follows_its_hysteresis)
{
    static uint16_t instant[PIXELS * 4], changed[PIXELS * 4], settled[PIXELS * 4];
    REQUIRE(resize_targets(SIDE, HDR));
    HRC2D lighting = clear_lighting();
    render_radiance_lab lab = render_radiance_lab_defaults();
    lighting.feedback = false;
    lighting.surface_radius = 12;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.tone_mapping = HRC_TONE_AGX;
    lab.debug_view = RENDER_RADIANCE_DEBUG_SURFACE_LIGHT;
    lighting.radiance_hysteresis = 1;
    render_material_entity scene[] = { test_emitter(), lit_wall(6, 1.5f) };
    scene[0].pos[0] = -26;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    REQUIRE(solve_hdr(scene, 2, 12));
    memcpy(instant, hdr, sizeof instant);
    lighting.radiance_hysteresis = .2f;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    scene[0].pos[0] = -28;
    REQUIRE(solve_hdr(scene, 2, 20));
    scene[0].pos[0] = -26;
    REQUIRE(draw(scene, 2, SIDE, test_centered(SIDE), 1) && read_hdr(SIDE));
    memcpy(changed, hdr, sizeof changed);
    REQUIRE(draw(scene, 2, SIDE, test_centered(SIDE), 20) && read_hdr(SIDE));
    memcpy(settled, hdr, sizeof settled);
    REQUIRE(solve_hdr(scene, 2, 1));
    float first = face_difference(changed, instant);
    REQUIRE(isfinite(first) && first > .0001f);
    REQUIRE(face_difference(settled, instant) < first * .3f);
    REQUIRE(face_difference(hdr, instant) < .0001f);
    return true;
}

/* A sky of (.4, .6, .8), which the cases read in blue. */
static const unsigned TEST_SKY = 0x6699ccffu;

/* An opaque rectangle between world (x0, y0) and (x1, y1) that reflects half
 * its light. */
static render_material_entity block(float x0, float y0, float x1, float y1)
{
    render_material_entity entity = wall_with(1, .5f);
    entity.pos[0] = (x0 + x1) * .5f;
    entity.pos[1] = (y0 + y1) * .5f;
    entity.points[0] = (x1 - x0) * .5f;
    entity.points[1] = (y1 - y0) * .5f;
    entity.extent[0] = entity.points[0] + 2;
    entity.extent[1] = entity.points[1] + 2;
    return entity;
}

/* A backdrop filling the view that reflects half its light and blocks none. */
static render_material_entity backdrop()
{
    render_material_entity entity = block(-SIDE / 2, -SIDE / 2, SIDE / 2, SIDE / 2);
    memset(entity.absorption, 0, sizeof entity.absorption);
    return entity;
}

/* Channel `c` of the HDR output at world (x, y). */
static float hdr_at(float x, float y, int c)
{
    int px = (int)floorf(x) + SIDE / 2, py = SIDE / 2 - 1 - (int)floorf(y);
    return test_half(hdr[4 * (py * SIDE + px) + c]);
}

/* The share of the sky a surface of albedo .5 reflects at world (x, y). */
static float sky_seen(float x, float y)
{
    return hdr_at(x, y, 2) / (.5f * .8f);
}

/* Sky light reaches open ground whole, out to the view's edges, whatever the
 * falloff, and keeps its color; it never glows in open air. Solids that absorb
 * block it from above and not from below: the ground under an overhang keeps
 * only the sky beside it, a closed room stays dark, its roof is lit, and the
 * roof's own surface light comes from the sky above it. Each holds on the full
 * grid, a coarser one and with split color. */
TEST_CASE(sky_lights_what_solids_leave_open_above)
{
    REQUIRE(resize_targets(SIDE, HDR));
    struct
    {
        float resolution;
        render_radiance_color color;
    } grids[] = { { 1, RENDER_RADIANCE_COLOR_FULL },
                  { .5f, RENDER_RADIANCE_COLOR_FULL },
                  { 1, RENDER_RADIANCE_COLOR_HALF } };
    for (int g = 0; g < 3; g++) {
        HRC2D lighting = clear_lighting();
        render_radiance_lab lab = render_radiance_lab_defaults();
        lighting.feedback = false;
        lighting.surface_radius = 0;
        lighting.tone_mapping = HRC_TONE_AGX;
        lighting.sky = TEST_SKY;
        lighting.resolution = grids[g].resolution;
        lab.color_resolution = grids[g].color;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        render_material_entity scene[4] = { backdrop() };
        REQUIRE(solve_hdr(scene, 1, 4));
        const float edge = SIDE / 2 - 1.5f;
        float open[] = { sky_seen(0, 0),
                         sky_seen(-edge, 0),
                         sky_seen(edge, 0),
                         sky_seen(0, -edge),
                         sky_seen(0, edge) };
        for (int i = 0; i < 5; i++) {
            REQUIRE(open[i] > .97f && open[i] < 1.03f);
        }
        REQUIRE(fabsf(hdr_at(0, 0, 0) / hdr_at(0, 0, 2) - .5f) < .01f);
        lighting.falloff = 10;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        REQUIRE(solve_hdr(scene, 1, 4));
        REQUIRE(fabsf(sky_seen(0, 0) - open[0]) < .01f);
        lighting.falloff = 1;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));

        scene[1] = block(-SIDE / 2, 17, 0, 23);
        REQUIRE(solve_hdr(scene, 2, 4));
        float under = sky_seen(-32, 0), beside = sky_seen(32, 0);
        REQUIRE(under > .2f && under < .4f);
        REQUIRE(beside > .75f && beside < .95f);
        REQUIRE(sky_seen(-32, 40) > .97f);

        scene[1] = block(-40, 17, 40, 23);
        scene[2] = block(-40, -SIDE / 2, -34, 23);
        scene[3] = block(34, -SIDE / 2, 40, 23);
        REQUIRE(solve_hdr(scene, 4, 4));
        REQUIRE(sky_seen(0, 0) < .03f);
        REQUIRE(sky_seen(0, 30) > .97f);
        /* The roof's top row absorbs part of the sky like other light, and its
         * underside sees none. */
        REQUIRE(sky_seen(0, 22.5f) > .05f && sky_seen(0, 22.5f) < .3f);
        REQUIRE(sky_seen(0, 17.5f) < .02f);
        lighting.fog = 1;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        REQUIRE(solve_hdr(scene + 1, 3, 4));
        REQUIRE(hdr_at(0, 40, 2) < .001f && hdr_at(0, 0, 2) < .001f);

        lighting.fog = 0;
        lighting.surface_radius = 12;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        scene[1].reserved[2] = 4;
        REQUIRE(solve_hdr(scene + 1, 1, 4));
        float top = sky_seen(0, 22.5f), bottom = sky_seen(0, 17.5f);
        REQUIRE(bottom > .5f && top > bottom * 2);
        lighting.sky = 0;
        REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
        REQUIRE(solve_hdr(scene + 1, 1, 4));
        REQUIRE(sky_seen(0, 22.5f) < .001f);
    }
    return true;
}

/* ---------- Relief shadows by horizons ---------- */

/* The mode the horizon selector checks. Each mode runs in its own process:
 * lavapipe maps GPU memory a page at a time, and the cache case's resizes
 * in both modes reach Linux's limit of 65530 mappings. */
static HRCShadowMode horizon_mode;

/* Relief shadows from horizons in `mode`, shown as visibility in blue with
 * horizon_lab(). */
static HRC2D horizon_lighting(HRCShadowMode mode)
{
    HRC2D lighting = surface_lighting();
    lighting.feedback = false;
    lighting.radiance_hysteresis = 1;
    lighting.surface_shadow_mode = mode;
    return lighting;
}

static render_radiance_lab horizon_lab()
{
    render_radiance_lab lab = render_radiance_lab_defaults();
    lab.debug_view = RENDER_RADIANCE_DEBUG_LIGHT_DIRECTION;
    return lab;
}

/* Resets `hrc` and lights one frame of `view` through it into `hdr`. Later
 * draws go through the shared HRC again. */
static bool solve_with(
    render_radiance *hrc,
    const render_material_entity *entities,
    int count,
    int size,
    render_view_2d view
)
{
    render_radiance *shared = radiance;
    radiance = hrc;
    render_radiance_reset(hrc);
    bool solved = draw(entities, count, size, view, 1) && read_hdr(size);
    radiance = shared;
    return solved;
}

/* Whether every channel of `count` pixels differs by less than `tolerance`. */
static bool light_matches(const uint16_t *a, const uint16_t *b, int count, float tolerance)
{
    for (int i = 0; i < 4 * count; i++) {
        float va = test_half(a[i]), vb = test_half(b[i]);
        if (!isfinite(va + vb) || fabsf(va - vb) >= tolerance) {
            return false;
        }
    }
    return true;
}

/* The horizon pass of the latest frame: one build over every pixel with
 * the mode's directions. */
static bool built_horizons_once(int size, HRCShadowMode mode)
{
    render_horizon_work work = render_radiance_horizon_work(radiance);
    int directions = mode == HRC_SHADOW_HORIZON4 ? 4 : 8;
    return work.builds == 1 && work.pixels == size * size &&
           work.directions == directions &&
           work.canvas_bytes == (size_t)size * size * (directions == 4 ? 16 : 24);
}

/* A lit receiver with a raised ridge across it, lit from the left. */
static void ridge_scene(render_material_entity scene[3], float ridge)
{
    scene[0] = test_emitter();
    scene[0].border_thickness = 0;
    scene[1] = lit_wall(6, 1.5f);
    scene[2] = lit_wall(1, 1.5f);
    scene[1].points[1] = scene[2].points[1] = 18;
    scene[1].extent[1] = scene[2].extent[1] = 20;
    scene[2].reserved[0] = ridge;
}

/* Horizons rebuild from the current height and coverage without a reset, so
 * a ridge that moves, leaves or returns leaves no old shadow behind, also
 * after resizing down and back up. Past an uncovered gap the scan stops
 * before a detached ridge. */
TEST_CASE(horizons_follow_moving_relief)
{
    static uint16_t moved[PIXELS * 4];
    float ridge = relief_material("Moving ridge", (test_relief){ .roughness = 1, .height = 12 });
    const int sizes[] = { SIDE, SIDE, SIDE, SIDE, 64, SIDE };
    REQUIRE(render_radiance_set_lighting(radiance, horizon_lighting(horizon_mode), horizon_lab()));
    render_material_entity scene[3];
    ridge_scene(scene, ridge);
    for (int stage = 0; stage < 6; stage++) {
        int size = sizes[stage], count = stage == 2 ? 2 : 3;
        /* At x = -4 the ridge's antialiased edge joins the receiver's
         * coverage. From stage 3 a narrower receiver leaves a gap at
         * both pixel spacings. */
        scene[2].pos[0] = stage >= 3 ? -6 : stage == 1 ? 4 : -4;
        scene[1].points[0] = stage >= 3 ? 1.25f : 6;
        scene[1].extent[0] = scene[1].points[0] + 1;
        if (!stage || size != sizes[stage - 1]) {
            REQUIRE(resize_targets(size, HDR));
        }
        REQUIRE(draw(scene, count, size, test_centered(SIDE), 1) && read_hdr(size));
        REQUIRE(built_horizons_once(size, horizon_mode));
        memcpy(moved, hdr, (size_t)size * size * 4 * sizeof *hdr);
        REQUIRE(solve_with(radiance, scene, count, size, test_centered(SIDE)));
        REQUIRE(built_horizons_once(size, horizon_mode));
        REQUIRE(light_matches(moved, hdr, size * size, .002f));
        if (stage < 3) {
            continue;
        }
        int row = size / 2 * size;
        REQUIRE(test_half(moved[4 * (row + size / 2) + 2]) > .99f);
        int gap = (int)(size * .5f - 3.f * size / SIDE);
        REQUIRE(
            test_read_texture(render_material_raster_textures(raster).cover, hdr, size * size)
        );
        REQUIRE(test_half(hdr[4 * (row + gap) + 1]) < .5f);
    }
    return true;
}

/* Visibility the hierarchy's coarse scan finds against the fine scan's, over
 * the covered pixels of `approx` and `fine`. */
typedef struct
{
    int samples;
    float mean_error, max_error, mean_visibility, center, fine_center;
} hierarchy_error;

static bool compare_hierarchy(const uint16_t *approx, const uint16_t *fine, hierarchy_error *result)
{
    static uint16_t cover[PIXELS * 4];
    if (!test_read_texture(render_material_raster_textures(raster).cover, cover, PIXELS)) {
        return false;
    }
    double error = 0, visibility = 0;
    *result = (hierarchy_error){ 0 };
    for (int i = 0; i < PIXELS; i++) {
        float a = test_half(approx[i * 4 + 2]), b = test_half(fine[i * 4 + 2]);
        if (!isfinite(a + b) || a < -.002f || a > 1.002f) {
            return false;
        }
        if (test_half(cover[i * 4 + 1]) < .5f) {
            continue;
        }
        error += fabsf(a - b);
        result->max_error = fmaxf(result->max_error, fabsf(a - b));
        visibility += a;
        result->samples++;
    }
    int center = 4 * (SIDE / 2 * SIDE + SIDE / 2) + 2;
    result->mean_error = result->samples ? (float)(error / result->samples) : 1;
    result->mean_visibility = result->samples ? (float)(visibility / result->samples)
                                              : 0;
    result->center = test_half(approx[center]);
    result->fine_center = test_half(fine[center]);
    return true;
}

/* The horizon hierarchy's coarse scan sees what the fine scan sees on a flat
 * plane, a shallow ramp, a raised ridge and a ridge past a gap. */
TEST_CASE(horizon_hierarchy_matches_the_fine_scan)
{
    static uint16_t approx[PIXELS * 4];
    REQUIRE(resize_targets(SIDE, HDR));
    float plane = relief_material("Flat plane", (test_relief){ .roughness = 1 });
    float ramp = relief_material(
        "Shallow ramp",
        (test_relief){ .tilt = { -.1f }, .roughness = 1, .slope = .1f }
    );
    float ridge = relief_material("Low ridge", (test_relief){ .roughness = 1, .height = 6 });
    render_view_2d view = test_centered(32);
    HRC2D lighting = horizon_lighting(horizon_mode);
    render_radiance_lab lab = horizon_lab();
    REQUIRE(render_radiance_set_lighting(other, lighting, lab));
    lab.horizon_hierarchy = true;
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    render_material_entity scene[3];
    ridge_scene(scene, ridge);
    scene[0].pos[0] = -12;
    scene[0].radius = 2;
    scene[0].extent[0] = scene[0].extent[1] = 4;
    scene[2].pos[0] = -2;
    for (int shape = 0; shape < 4; shape++) {
        int count = shape < 2 ? 2 : 3;
        scene[1].reserved[0] = shape == 1 ? ramp : plane;
        if (shape == 3) {
            scene[1].points[0] = 1.25f;
            scene[1].extent[0] = 2.25f;
            scene[2].pos[0] = -6;
        }
        REQUIRE(solve_with(radiance, scene, count, SIDE, view));
        REQUIRE(render_radiance_horizon_work(radiance).hierarchy_passes > 0);
        memcpy(approx, hdr, sizeof approx);
        REQUIRE(solve_with(other, scene, count, SIDE, view));
        hierarchy_error error;
        REQUIRE(compare_hierarchy(approx, hdr, &error));
        REQUIRE(error.samples > 100);
        REQUIRE(error.mean_error < .003f && error.max_error < .003f);
        if (shape < 2) {
            REQUIRE(error.mean_visibility > .95f && error.center > .99f);
        }
        if (shape == 2) {
            REQUIRE(error.fine_center < .5f && error.center <= error.fine_center + .15f);
        }
        if (shape == 3) {
            REQUIRE(error.center > .99f && error.fine_center > .99f);
        }
    }
    return true;
}

/* Tiles whose horizons the latest frame rebuilt, of all of them. */
static bool dirty_tiles(int size, int *dirty, int *total)
{
    uint16_t mask[((SIDE + 15) / 16) * ((SIDE + 15) / 16) * 4];
    assert(size <= SIDE);
    int tiles = (size + 15) / 16;
    *total = tiles * tiles;
    *dirty = 0;
    if (!test_read_texture(render_radiance_horizon_dirty_texture(radiance), mask, *total)) {
        return false;
    }
    for (int i = 0; i < *total; i++) {
        float red = test_half(mask[i * 4]);
        if (!isfinite(red) || red < 0 || red > 1) {
            return false;
        }
        *dirty += red > .5f;
    }
    return true;
}

/* Two receivers with raised ridges, each with its emitter. The second set
 * starts right of the view, so a large camera move brings it in. */
static void cached_scene(render_material_entity scene[6], float ridge)
{
    ridge_scene(scene, ridge);
    ridge_scene(scene + 3, ridge);
    for (int i = 0; i < 6; i += 3) {
        memset(scene[i].scene, 0, sizeof scene[i].scene);
        memset(scene[i].border_scene, 0, sizeof scene[i].border_scene);
    }
    scene[2].pos[0] = -4;
    scene[3].pos[0] = 100;
    scene[4].pos[0] = 80;
    scene[5].pos[0] = 84;
}

/* Compares the light the shared HRC just drew with what `other` draws of
 * the same frame from a reset. */
static bool matches_fresh(const render_material_entity *entities, int count, int size, render_view_2d view)
{
    static uint16_t cached[PIXELS * 4];
    REQUIRE(read_hdr(size));
    memcpy(cached, hdr, (size_t)size * size * 4 * sizeof *hdr);
    REQUIRE(solve_with(other, entities, count, size, view));
    REQUIRE(light_matches(cached, hdr, size * size, .003f));
    return true;
}

enum
{
    REUSE_FUSED = 1,
    REUSE_REPROJECT = 2,
    REUSE_HIERARCHY = 4,
};

/* Cached horizons with the `options` above rebuild only the tiles a change
 * touches and match horizons built fresh, through light and geometry
 * changes, camera moves, a reset, resizes and switching the mode and
 * fusion. */
static bool cache_holds(HRCShadowMode mode, unsigned options, float ridge)
{
    HRC2D fresh = horizon_lighting(mode);
    fresh.resolution = .5f;
    /* Raw HDR, without display dithering that changes by frame. */
    fresh.tone_mapping = HRC_TONE_AGX;
    HRC2D cached = fresh;
    render_radiance_lab fresh_lab = horizon_lab();
    fresh_lab.horizon_hierarchy = options & REUSE_HIERARCHY;
    render_radiance_lab cached_lab = fresh_lab;
    cached_lab.horizon_fuse_relief = options & REUSE_FUSED;
    cached_lab.horizon_cache = true;
    cached_lab.horizon_reproject = options & REUSE_REPROJECT;
    render_material_entity scene[6];
    cached_scene(scene, ridge);
    render_view_2d view = test_centered(SIDE);
    int size = SIDE;
    REQUIRE(resize_targets(SIDE, HDR) && render_radiance_resize(other, SIDE, SIDE));
    /* A reset rather than new renderers per set: on lavapipe, new ones
     * raised the selector's peak to 60500 of the 65530 mappings. */
    render_radiance_reset(radiance);
    for (int stage = 0; stage < 15; stage++) {
        switch (stage) {
            case 1: scene[0].emission[1] = .15f; break;
            case 2: scene[0].pos[0] = -26; break;
            case 3: scene[2].pos[0] = 4; break;
            case 5:
                scene[2].pos[0] = -6;
                scene[1].points[0] = 1.25f;
                scene[1].extent[0] = 2.25f;
                break;
            case 6:
                view.min_x += 1;
                view.center_x += 1;
                break;
            case 7:
                view.max_y += 2;
                view.center_y += 2;
                break;
            case 8:
                view.min_x += .25f;
                view.center_x += .25f;
                break;
            case 9:
                view.min_x += 40;
                view.center_x += 40;
                break;
            case 10: render_radiance_reset(radiance); break;
            case 11:
            case 12:
                size = stage == 11 ? 64 : SIDE;
                REQUIRE(resize_targets(size, HDR) && render_radiance_resize(other, size, size));
                break;
            case 13:
                cached.surface_shadow_mode = fresh.surface_shadow_mode = mode == HRC_SHADOW_HORIZON4
                                                                           ? HRC_SHADOW_HORIZON8
                                                                           : HRC_SHADOW_HORIZON4;
                break;
            case 14:
                cached_lab.horizon_fuse_relief = !cached_lab.horizon_fuse_relief;
                break;
        }
        /* Stage 4 removes the first ridge, and stage 5 brings it back. */
        render_material_entity active[6];
        int count = 0;
        for (int i = 0; i < 6; i++) {
            if (stage != 4 || i != 2) {
                active[count++] = scene[i];
            }
        }
        REQUIRE(render_radiance_set_lighting(radiance, cached, cached_lab));
        REQUIRE(render_radiance_set_lighting(other, fresh, fresh_lab));
        REQUIRE(draw(active, count, size, view, 1));
        render_horizon_work work = render_radiance_horizon_work(radiance);
        int dirty, total;
        REQUIRE(dirty_tiles(size, &dirty, &total));
        /* Whole-pixel camera moves reproject; with the hierarchy only the
         * large one does. Every other change but light rebuilds all. */
        bool moved = stage == 6 || stage == 7 || stage == 9;
        bool reprojects = (options & REUSE_REPROJECT) &&
                          (!(options & REUSE_HIERARCHY) || stage == 9);
        bool all = !stage || (stage >= 8 && stage != 9) || (moved && !reprojects);
        REQUIRE(work.tiles_x * work.tiles_y == total && work.dirty_passes > 0);
        REQUIRE(work.reuse_passes == (all ? 0 : 1));
        REQUIRE(!all || dirty == total);
        REQUIRE((stage != 1 && stage != 2) || !dirty);
        REQUIRE(stage < 3 || stage > 5 || dirty);
        REQUIRE(!moved || !reprojects || (work.reproject_passes == 1 && dirty));
        REQUIRE(work.fused_packs == (cached_lab.horizon_fuse_relief ? 1 : 0));
        REQUIRE(work.pack_passes == (cached_lab.horizon_fuse_relief ? 0 : 1));
        REQUIRE(matches_fresh(active, count, size, view));
        /* The shaded light matches as well as the visibility. */
        cached_lab.debug_view = fresh_lab.debug_view = RENDER_RADIANCE_DEBUG_NORMAL;
        REQUIRE(render_radiance_set_lighting(radiance, cached, cached_lab));
        REQUIRE(render_radiance_set_lighting(other, fresh, fresh_lab));
        REQUIRE(draw(active, count, size, view, 1));
        REQUIRE(matches_fresh(active, count, size, view));
        cached_lab.debug_view = fresh_lab.debug_view = RENDER_RADIANCE_DEBUG_LIGHT_DIRECTION;
    }
    return true;
}

TEST_CASE(cached_horizons_match_fresh_ones)
{
    float ridge = relief_material("Cached ridge", (test_relief){ .roughness = 1, .height = 12 });
    for (unsigned options = 0; options < 8; options++) {
        REQUIRE(cache_holds(horizon_mode, options, ridge));
    }
    return true;
}

#endif

void test_render_radiance()
{
    test_open_hidden_app("radiance", SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(emitters_light_open_space_through_their_materials);
    RUN_TEST_CASE(walls_block_light_by_their_absorption);
    RUN_TEST_CASE(bounced_light_gathers_over_frames);
    RUN_TEST_CASE(transparent_reflective_edges_do_not_feed_back);
    RUN_TEST_CASE(reset_drops_occluder_history);
    RUN_TEST_CASE(resized_empty_scenes_stay_dark);
    RUN_TEST_CASE(reused_capacity_matches_a_fresh_grid);
    RUN_TEST_CASE(split_luminance_stays_close_to_full_color);
    RUN_TEST_CASE(fractional_cells_absorb_as_whole_ones);
    RUN_TEST_CASE(reduced_reconstruction_keeps_sparks_and_thin_walls);
    RUN_TEST_CASE(fitted_lighting_applies_to_a_live_renderer);
    RUN_TEST_CASE(every_lighting_combination_applies_and_lays_out);
    RUN_TEST_CASE(adaptive_steps_down_then_back_up);
    RUN_TEST_CASE(gpu_timestamps_charge_passes_only_for_later_work);
    pu_clear_setup();
    RUN_TEST_CASE(extend_joins_serial_intervals);
    RUN_TEST_CASE(filter_keeps_light_true_to_the_current_frame);
    cf_destroy_app();
}

#if CHIBI_TEST_MATERIAL_GRAPHS
void test_render_radiance_surface()
{
    test_open_hidden_app("radiance surface", SIDE, SIDE);
    pu_setup(surface_setup, surface_teardown);
    RUN_TEST_CASE(silhouettes_light_from_their_lit_side);
    RUN_TEST_CASE(relief_facing_the_light_shines_brighter);
    RUN_TEST_CASE(surface_light_reaches_a_world_space_depth);
    RUN_TEST_CASE(relief_below_a_ceiling_faces_down_to_the_light);
    RUN_TEST_CASE(surface_colors_keep_their_directions);
    RUN_TEST_CASE(surface_light_history_follows_its_hysteresis);
    RUN_TEST_CASE(sky_lights_what_solids_leave_open_above);
    pu_clear_setup();
    cf_destroy_app();
}

/* Checks horizons with `directions`, 4 or 8. */
void test_render_radiance_horizons(int directions)
{
    assert(directions == 4 || directions == 8);
    horizon_mode = directions == 4 ? HRC_SHADOW_HORIZON4 : HRC_SHADOW_HORIZON8;
    test_open_hidden_app("radiance horizons", SIDE, SIDE);
    pu_setup(surface_setup, surface_teardown);
    RUN_TEST_CASE(horizons_follow_moving_relief);
    RUN_TEST_CASE(horizon_hierarchy_matches_the_fine_scan);
    RUN_TEST_CASE(cached_horizons_match_fresh_ones);
    pu_clear_setup();
    cf_destroy_app();
}
#endif
