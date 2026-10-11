#include "hidden_app.h"
#include "hrc_cases.h"
#include "render/fullscreen.h"
#include "render/radiance.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* HRC's Depth thickness surface light and its surface gather, lit from
 * material inputs written by hand in one hidden app and read back. */
enum
{
    SIDE = 32,
    PIXELS = SIDE * SIDE,
    /* The side of the cases whose light travels farther. */
    WIDE = 128
};

/* The material inputs HRC reads, as the material raster writes them. */
enum
{
    SCENE,
    SOURCE,
    ALBEDO,
    NORMAL,
    COVER,
    VISIBILITY,
    INPUTS
};

/* Inputs for side x side pixels, the textures they upload to, the HDR output
 * HRC lights and the output's pixels read back. Every case shares one HRC,
 * reset and resized for it: a new one compiles all of HRC's shaders. */
static float data[INPUTS][WIDE * WIDE][4];
static CF_Texture textures[INPUTS];
static CF_Canvas output;
static render_radiance *radiance;
/* The lab settings this case's solves use. Each case starts with color at
 * half resolution and Depth thickness's surface light shown alone. */
static render_radiance_lab lab;
static int side;
static uint16_t halves[WIDE * WIDE * 4];

static CF_Texture make_input(int size)
{
    CF_TextureParams params = cf_texture_defaults(size, size);
    params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    params.filter = CF_FILTER_NEAREST;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(params);
    assert(texture.id);
    return texture;
}

static void open_targets(int size)
{
    side = size;
    lab = render_radiance_lab_defaults();
    lab.color_resolution = RENDER_RADIANCE_COLOR_HALF;
    lab.debug_view = RENDER_RADIANCE_DEBUG_SURFACE_LIGHT;
    for (int i = 0; i < INPUTS; i++) {
        textures[i] = make_input(size);
    }
    CF_CanvasParams params = cf_canvas_defaults(size, size);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    output = cf_make_canvas(params);
    assert(output.id && render_radiance_resize(radiance, size, size));
    render_radiance_reset(radiance);
    memset(data, 0, sizeof data);
}

static void close_targets()
{
    for (int i = 0; i < INPUTS; i++) {
        cf_destroy_texture(textures[i]);
    }
    cf_destroy_canvas(output);
}

static void setup()
{
    open_targets(SIDE);
}

static void teardown()
{
    close_targets();
}

/* Targets for the cases whose light travels farther. */
static void wide_setup()
{
    open_targets(WIDE);
}

/* The lighting these cases were calibrated with: the reference lighting with
 * no fog, feedback or specular, lit by Depth thickness. */
static HRC2D calibrated()
{
    HRC2D lighting = test_reference_lighting();
    lighting.adaptive = lighting.feedback = false;
    lighting.fog = lighting.shadow_floor = 0;
    lighting.specular = 0;
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    return lighting;
}

/* Depth thickness gathering 16 units around 12-unit-thick receivers. */
static HRC2D depth_lighting()
{
    HRC2D lighting = calibrated();
    lighting.radiance_hysteresis = 1;
    lighting.surface_radius = lighting.surface_depth_radius = 16;
    lighting.surface_relief = 1;
    lighting.surface_thickness = 12;
    return lighting;
}

/* Lights `view` from the inputs, continuing from the light HRC holds, and
 * reads the output back. Every pixel must be finite; `color` is the RGB at
 * the center. */
static bool lit(HRC2D lighting, render_view_2d view, float color[3])
{
    REQUIRE(render_radiance_set_lighting(radiance, lighting, lab));
    render_radiance_inputs inputs = { .material = { .scene = textures[SCENE],
                                                    .source = textures[SOURCE],
                                                    .albedo = textures[ALBEDO],
                                                    .normal = textures[NORMAL],
                                                    .cover = textures[COVER],
                                                    .visibility = textures[VISIBILITY] } };
    cf_app_update(NULL);
    for (int i = 0; i < INPUTS; i++) {
        cf_texture_update(textures[i], data[i], (size_t)side * side * sizeof data[i][0]);
    }
    bool drawn = render_radiance_draw(radiance, inputs, view, output);
    cf_app_draw_onto_screen(false);
    REQUIRE(drawn);
    REQUIRE(test_read_canvas(output, halves, side * side * 4 * (int)sizeof *halves));
    bool finite = true;
    for (int i = 0; i < side * side * 4; i++) {
        finite &= isfinite(test_half(halves[i]));
    }
    REQUIRE(finite);
    for (int c = 0; c < 3; c++) {
        color[c] = test_half(halves[4 * (side / 2 * side + side / 2) + c]);
    }
    return true;
}

/* lit() from a reset. */
static bool lit_fresh(HRC2D lighting, render_view_2d view, float color[3])
{
    render_radiance_reset(radiance);
    return lit(lighting, view, color);
}

static bool dark(const float color[3])
{
    return fabsf(color[0]) < .00001f && fabsf(color[1]) < .00001f &&
           fabsf(color[2]) < .00001f;
}

/* ---------- Depth thickness ---------- */

/* Scenes of a flat, fully opaque receiver lit by a red strip at x >= 24 that
 * stands 12 units tall, with a slab at x 20 to 23 between them. The receiver
 * has no open air to blur light from: the strip's light must travel through
 * depth to reach it. */
enum
{
    /* No slab. */
    OPEN,
    /* A slab 12 units tall, which crosses the light's path. */
    CROSSING,
    /* A slab from -16 to -4, below the light's path. */
    BELOW,
    /* A slab from 12 to 24, above the light's path. */
    ABOVE,
    /* The strip without emission. */
    UNLIT,
    /* The receiver tilted toward the strip. */
    TILTED,
    /* A crossing slab that absorbs no light. */
    CLEAR,
    /* A crossing slab that absorbs no light and glows faintly. */
    GLOWING,
};

static void slab_scene(int scene)
{
    memset(data, 0, sizeof data);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            bool strip = x >= 24 && y >= 8 && y <= 24;
            data[SCENE][p][3] = 1;
            data[SOURCE][p][3] = 20;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[NORMAL][p][2] = 1;
            data[NORMAL][p][3] = .5f;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
            if (strip) {
                /* Like the engine's sphere emitters, the strip absorbs
                 * nothing: coverage and absorption are separate inputs. */
                data[ALBEDO][p][3] = 12;
                data[SOURCE][p][3] = 0;
                data[SOURCE][p][0] = scene == UNLIT ? 0 : 4;
            }
            if (x >= 20 && x <= 23) {
                bool clear = scene == CLEAR || scene == GLOWING;
                data[ALBEDO][p][3] = scene == CROSSING || clear ? 12
                                   : scene == BELOW             ? -4
                                   : scene == ABOVE             ? 24
                                                                : 0;
                data[SOURCE][p][3] = clear ? 0 : data[SOURCE][p][3];
                data[SOURCE][p][0] = scene == GLOWING ? .01f : 0;
            }
            /* The whole receiver tilts, so filtering can't mix a tilted and
             * a flat edge. */
            if (scene == TILTED && !strip) {
                data[NORMAL][p][0] = .6f;
            }
        }
    }
}

/* The red light at the center of `scene` from a reset. */
static bool slab_light(int scene, HRC2D lighting, float *red)
{
    float color[3];
    slab_scene(scene);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), color));
    *red = color[0];
    return true;
}

/* A small source of `width` centered at (x, y) in pixels down from the top
 * left, whose total emission stays the same at any size and position, so
 * neither ties it to the gather's sample centers. */
static void small_source(float x, float y, float width)
{
    slab_scene(UNLIT);
    for (int p = 0; p < PIXELS; p++) {
        data[ALBEDO][p][3] = 0;
        data[SOURCE][p][3] = 20;
    }
    for (int py = 0; py < SIDE; py++) {
        float overlap_y = fmaxf(0, fminf(py + 1, y + width * .5f) - fmaxf(py, y - width * .5f));
        for (int px = 0; px < SIDE; px++) {
            float overlap_x = fmaxf(0, fminf(px + 1, x + width * .5f) - fmaxf(px, x - width * .5f));
            float area = overlap_x * overlap_y;
            if (area <= 0) {
                continue;
            }
            int p = py * SIDE + px;
            data[SOURCE][p][0] = 16 * area / (width * width);
            data[SOURCE][p][3] = 0;
            data[ALBEDO][p][3] = 12;
        }
    }
}

/* The red light at the center from a small source at (x, y), from a reset. */
static bool source_light(HRC2D lighting, float x, float y, float width, float *red)
{
    float color[3];
    small_source(x, y, width);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), color));
    *red = color[0];
    return true;
}

/* The mean, least and most red of a set of samples. */
typedef struct
{
    float mean, least, most;
} red_range;

/* The red light at the center from a small source of `width` at `distance`
 * in each of 24 directions around it. */
static bool ring(HRC2D lighting, float distance, float width, red_range *light)
{
    enum
    {
        ANGLES = 24
    };
    *light = (red_range){ .least = INFINITY };
    for (int angle = 0; angle < ANGLES; angle++) {
        float theta = angle * (6.28318530718f / ANGLES), red;
        float x = SIDE * .5f + .5f + distance * cosf(theta),
              y = SIDE * .5f + .5f + distance * sinf(theta);
        REQUIRE(source_light(lighting, x, y, width, &red));
        light->mean += red / ANGLES;
        light->least = fminf(light->least, red);
        light->most = fmaxf(light->most, red);
    }
    return true;
}

/* Blur doesn't light the closed receiver; Depth thickness does, within its
 * budget of 49 candidate tiles and 96 visibility taps for each of the 64
 * cells of 4 x 4 pixels. Bounded segments walk each candidate as well. */
TEST_CASE(depth_light_reaches_closed_receivers_within_its_budget)
{
    HRC2D lighting = depth_lighting();
    lighting.surface_mode = HRC_SURFACE_BLUR;
    float blur[3], depth[3];
    slab_scene(OPEN);
    REQUIRE(lit(lighting, test_centered(SIDE), blur));
    REQUIRE(fabsf(blur[0]) < .001f);
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    REQUIRE(lit(lighting, test_centered(SIDE), depth));
    REQUIRE(depth[0] > .001f);
    render_surface_work work = render_radiance_surface_work(radiance);
    REQUIRE(work.mode == HRC_SURFACE_DEPTH && work.block == 4 && work.cells == 64);
    REQUIRE(work.candidates == (size_t)work.cells * 49);
    REQUIRE(work.visibility_taps == (size_t)work.cells * 96);
    REQUIRE(!work.blur_quadrant_fetches);
    lighting.surface_depth_visibility = HRC_DEPTH_VISIBILITY_SEGMENTS;
    REQUIRE(lit(lighting, test_centered(SIDE), depth));
    work = render_radiance_surface_work(radiance);
    REQUIRE(work.visibility_taps == (size_t)work.cells * 96 + work.candidates * 48);
    return true;
}

/* Switching Depth thickness off brings blur's light back. */
TEST_CASE(depth_light_switches_back_to_blur)
{
    HRC2D lighting = depth_lighting();
    lighting.surface_mode = HRC_SURFACE_BLUR;
    float blur[3], depth[3], restored[3];
    slab_scene(OPEN);
    REQUIRE(lit(lighting, test_centered(SIDE), blur));
    lighting.surface_mode = HRC_SURFACE_DEPTH;
    REQUIRE(lit(lighting, test_centered(SIDE), depth));
    lighting.surface_mode = HRC_SURFACE_BLUR;
    REQUIRE(lit(lighting, test_centered(SIDE), restored));
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(restored[c] - blur[c]) < .001f);
    }
    return true;
}

/* A slab shadows the receiver only where it crosses the light's path, and a
 * strip without emission lights nothing. Beside a raised slab, crossing the
 * path or not, the receiver's filter takes no light from past it, nearer the
 * strip, so the receiver keeps a little less. */
TEST_CASE(slabs_shadow_only_the_light_they_cross)
{
    HRC2D lighting = depth_lighting();
    float open, crossing, below, above, unlit;
    REQUIRE(slab_light(OPEN, lighting, &open) && slab_light(CROSSING, lighting, &crossing));
    REQUIRE(slab_light(BELOW, lighting, &below) && slab_light(ABOVE, lighting, &above));
    REQUIRE(slab_light(UNLIT, lighting, &unlit));
    REQUIRE(open > .001f && crossing < open * .35f);
    REQUIRE(below > open * .8f && above > open * .7f);
    REQUIRE(fabsf(unlit) < .00001f);
    return true;
}

TEST_CASE(receivers_tilted_toward_the_light_take_more)
{
    HRC2D lighting = depth_lighting();
    float flat, tilted;
    REQUIRE(slab_light(OPEN, lighting, &flat) && slab_light(TILTED, lighting, &tilted));
    REQUIRE(tilted > flat * 1.05f);
    return true;
}

/* Height blocks light whether or not it absorbs, so a prop that lets HRC's
 * light pass still shadows by its height; a light never shadows another,
 * though beside it, as beside any raised slab, the receiver keeps a little
 * less. */
TEST_CASE(slabs_shadow_by_height_unless_they_glow)
{
    HRC2D lighting = depth_lighting();
    float open, clear, glowing;
    REQUIRE(slab_light(OPEN, lighting, &open) && slab_light(CLEAR, lighting, &clear));
    REQUIRE(slab_light(GLOWING, lighting, &glowing));
    REQUIRE(open > .001f && clear < open * .35f);
    REQUIRE(glowing > open * .7f);
    return true;
}

/* Small sources light the center from every direction without dark wedges,
 * and sources of equal power light it alike at any size. */
TEST_CASE(small_sources_light_every_direction_by_their_power)
{
    HRC2D lighting = depth_lighting();
    red_range light[3];
    for (int size = 0; size < 3; size++) {
        REQUIRE(ring(lighting, 8, (float)(size + 1), &light[size]));
        REQUIRE(light[size].least > .00001f);
        REQUIRE(light[size].least >= light[size].most * .2f);
    }
    REQUIRE(light[0].mean > .00001f);
    for (int size = 1; size < 3; size++) {
        REQUIRE(light[size].mean > light[0].mean * .5f);
        REQUIRE(light[size].mean < light[0].mean * 2);
    }
    return true;
}

TEST_CASE(depth_light_falls_off_with_distance)
{
    HRC2D lighting = depth_lighting();
    red_range near, far;
    REQUIRE(ring(lighting, 8, 1, &near) && ring(lighting, 12, 1, &far));
    REQUIRE(far.mean > .00001f && far.mean < near.mean * .95f);
    return true;
}

/* A source keeps its light at every half-pixel position. */
TEST_CASE(sources_between_pixels_keep_their_light)
{
    HRC2D lighting = depth_lighting();
    float least = INFINITY, most = 0;
    for (int shift = 0; shift < 4; shift++) {
        float red;
        float x = 24.5f + .5f * (shift & 1), y = 16.5f + .5f * (shift >> 1);
        REQUIRE(source_light(lighting, x, y, 1, &red));
        least = fminf(least, red);
        most = fmaxf(most, red);
    }
    REQUIRE(least > .00001f && least >= most * .4f);
    return true;
}

/* A source that moves smoothly across the x = 24 tile edge changes its light
 * smoothly once the falloff its distance explains is taken out; a jump would
 * be the gather's cells popping. */
TEST_CASE(moving_sources_light_smoothly_across_tiles)
{
    HRC2D lighting = depth_lighting();
    float previous = 0, least = INFINITY, most = 0, jump = 0;
    for (int frame = 0; frame <= 16; frame++) {
        float x = 23 + frame * .125f, red;
        REQUIRE(source_light(lighting, x, 24.5f, 1, &red));
        float dx = x - 16.5f;
        float distance2 = dx * dx + 8 * 8 + 12 * 12;
        float normalized = red * (distance2 + 3 * 3) * sqrtf(distance2) / 12;
        REQUIRE(normalized > .00001f);
        least = fminf(least, normalized);
        most = fmaxf(most, normalized);
        if (frame) {
            jump = fmaxf(jump, fabsf(normalized - previous) / fmaxf((normalized + previous) * .5f, .00001f));
        }
        previous = normalized;
    }
    REQUIRE(jump <= .15f && least >= most * .7f);
    return true;
}

/* A source that leaves the depth radius fades out without a step. The
 * receiver stands alone, so filtering can't borrow light from another
 * surface inside the radius. */
TEST_CASE(light_fades_out_at_the_depth_radius)
{
    HRC2D lighting = depth_lighting();
    lighting.surface_depth_radius = 8;
    float first = 0, last = 0, jump = 0;
    for (int frame = 0; frame <= 32; frame++) {
        small_source(20.5f + frame * .25f, 16.5f, 1);
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                int p = y * SIDE + x;
                bool receiver = x >= 12 && x < 20 && y >= 12 && y < 20;
                if (!receiver && data[SOURCE][p][0] == 0) {
                    data[COVER][p][1] = data[COVER][p][2] = 0;
                }
            }
        }
        float color[3];
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), color));
        if (frame) {
            jump = fmaxf(jump, fabsf(color[0] - last));
        } else {
            first = color[0];
        }
        last = color[0];
    }
    REQUIRE(first > .00001f && last < .00001f && jump < first * .15f);
    return true;
}

/* A source leaving the view keeps only the light of what remains in it;
 * averaging partial tiles must not brighten it. */
TEST_CASE(sources_at_the_view_edge_keep_the_light_left_in_view)
{
    HRC2D lighting = depth_lighting();
    lighting.surface_depth_radius = 32;
    float reference = 0, last = 0;
    for (int frame = 0; frame <= 8; frame++) {
        float x = 30.5f + frame * .25f, red;
        REQUIRE(source_light(lighting, x, 16.5f, 1, &red));
        float dx = x - 16.5f;
        float distance2 = dx * dx + 12 * 12;
        float normalized = red * (distance2 + 3 * 3) * sqrtf(distance2) / 12;
        float kept = fminf(1, fmaxf(0, SIDE - (x - .5f)));
        if (!frame) {
            reference = normalized;
        }
        REQUIRE(fabsf(normalized - reference * kept) <= reference * .2f);
        last = red;
    }
    REQUIRE(reference > .00001f && last < .00001f);
    return true;
}

TEST_CASE(perspective_views_take_depth_light)
{
    render_view_2d view = test_centered(SIDE);
    view.focal_distance = 64;
    float color[3];
    slab_scene(OPEN);
    REQUIRE(lit_fresh(depth_lighting(), view, color));
    REQUIRE(color[0] > .001f);
    return true;
}

/* Depth thickness over HRC's own light, tone mapped by AgX at half
 * intensity. The case's solves show the lit frame. */
static HRC2D fill_lighting()
{
    HRC2D lighting = depth_lighting();
    lab.debug_view = RENDER_RADIANCE_DEBUG_NORMAL;
    lighting.tone_mapping = HRC_TONE_AGX;
    lighting.intensity = .5f;
    return lighting;
}

/* `scene` with a matte receiver that absorbs nothing, tilted `tilt` toward
 * the strip and with `edge` light, so HRC's own light reaches it. */
static void open_scene(int scene, float tilt, float edge)
{
    slab_scene(scene);
    for (int p = 0; p < PIXELS; p++) {
        if (data[SOURCE][p][0] > 0) {
            continue;
        }
        data[SOURCE][p][3] = 0;
        data[NORMAL][p][0] = tilt;
        data[NORMAL][p][2] = edge;
        data[NORMAL][p][3] = 0;
    }
}

/* The red light at the center of open_scene() from a reset. */
static bool open_light(int scene, float tilt, float edge, HRC2D lighting, float *red)
{
    float color[3];
    open_scene(scene, tilt, edge);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), color));
    *red = color[0];
    return true;
}

/* At gain zero Depth thickness runs no passes. Each step of gain adds the
 * same light to a receiver with half edge light that absorbs light, as much
 * at a Material's full absorption of 1 as when opaque, while a flat one that
 * absorbs nothing keeps HRC's light as it is at any gain. */
TEST_CASE(depth_gain_adds_only_to_closed_receivers)
{
    static const float absorption[] = { 0, 1, 20 };
    HRC2D lighting = fill_lighting();
    float steps[3][3];
    for (int a = 0; a < 3; a++) {
        open_scene(OPEN, 0, .5f);
        for (int p = 0; p < PIXELS; p++) {
            data[SOURCE][p][3] = data[SOURCE][p][0] > 0 ? 0 : absorption[a];
        }
        float base[3], gains[3][3];
        lighting.surface_radius = 0;
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), base));
        lighting.surface_radius = 16;
        for (int gain = 0; gain < 3; gain++) {
            lighting.surface_depth_gain = (float)gain;
            REQUIRE(lit_fresh(lighting, test_centered(SIDE), gains[gain]));
            render_surface_work work = render_radiance_surface_work(radiance);
            REQUIRE(gain || (!work.cells && !work.candidates && !work.visibility_taps));
        }
        bool closed = absorption[a] > 0;
        REQUIRE(closed ? gains[1][0] > gains[0][0] + .00001f : base[0] > .00001f);
        for (int c = 0; c < 3; c++) {
            REQUIRE(fabsf(gains[0][c] - base[c]) < .001f);
            steps[a][c] = closed ? gains[1][c] - gains[0][c] : 0;
            float tolerance = fmaxf(.001f, closed ? fabsf(steps[a][c]) * .03f : base[c] * .01f);
            REQUIRE(fabsf(gains[1][c] - gains[0][c] - steps[a][c]) < tolerance);
            REQUIRE(fabsf(gains[2][c] - gains[0][c] - 2 * steps[a][c]) < tolerance);
        }
    }
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(steps[1][c] - steps[2][c]) < fmaxf(.001f, steps[2][c] * .03f));
    }
    return true;
}

/* Depth thickness shades HRC's light on a receiver that absorbs nothing:
 * facing the strip takes more of it, facing away less, and a prop that
 * absorbs nothing, so HRC's light passes it, shadows it by its height.
 * Without edge light it keeps HRC's light flat. The wobble sums the change
 * between neighboring pixels across the shadow. */
TEST_CASE(open_receivers_shade_hrc_light_by_facing_and_height)
{
    HRC2D lighting = fill_lighting();
    float flat, toward, away, shadowed, plain, plain_shadowed;
    REQUIRE(open_light(OPEN, 0, 1, lighting, &flat));
    REQUIRE(open_light(OPEN, .6f, 1, lighting, &toward));
    REQUIRE(open_light(OPEN, -.6f, 1, lighting, &away));
    REQUIRE(open_light(CLEAR, 0, 1, lighting, &shadowed));
    /* Filtered like Depth light, the shadow doesn't show its sampling: its
     * light bends smoothly across the span the slab shadows. */
    float bend = 0, mean = 0;
    for (int y = 10; y < 22; y++) {
        const uint16_t *row = halves + 4 * y * SIDE;
        for (int x = 12; x < 18; x++) {
            float red = test_half(row[4 * x]);
            mean += red;
            float left = test_half(row[4 * (x - 1)]);
            float right = test_half(row[4 * (x + 1)]);
            bend += fabsf(left - 2 * red + right);
        }
    }
    REQUIRE(bend < mean * .035f);
    REQUIRE(open_light(OPEN, 0, 0, lighting, &plain));
    REQUIRE(open_light(CLEAR, 0, 0, lighting, &plain_shadowed));
    REQUIRE(flat > .001f && fabsf(flat - plain) < flat * .01f);
    REQUIRE(toward > flat * 1.05f && away < flat * .95f);
    REQUIRE(shadowed < flat * .5f && plain_shadowed > flat * .9f);
    return true;
}

TEST_CASE(depth_gain_keeps_shadows_and_unlit_scenes_dark)
{
    HRC2D lighting = fill_lighting();
    lighting.surface_depth_gain = 4;
    float open[3], crossing[3], unlit[3];
    slab_scene(OPEN);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), open));
    slab_scene(CROSSING);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), crossing));
    slab_scene(UNLIT);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), unlit));
    REQUIRE(open[0] > .00001f && crossing[0] < open[0] * .35f);
    REQUIRE(dark(unlit));
    return true;
}

/* A half-gray floor that absorbs nothing beside a wall 12 units tall at
 * x >= 20. The wall absorbs light, or with `glow` absorbs nothing and gives a
 * little light. Nothing else emits. */
static void wall_scene(bool glow)
{
    memset(data, 0, sizeof data);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            for (int c = 0; c < 3; c++) {
                data[SCENE][p][c] = data[ALBEDO][p][c] = .5f;
            }
            data[SCENE][p][3] = 1;
            data[NORMAL][p][2] = 1;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
            if (x >= 20) {
                data[ALBEDO][p][3] = 12;
                data[SOURCE][p][3] = glow ? 0 : 20;
                data[SOURCE][p][0] = glow ? .01f : 0;
            }
        }
    }
}

/* Channel `c` of the middle row's pixels at the columns `xs`. */
static void middle_row(const int *xs, int count, int c, float *values)
{
    for (int i = 0; i < count; i++) {
        values[i] = test_half(halves[4 * (SIDE / 2 * SIDE + xs[i]) + c]);
    }
}

/* The ambient light Depth thickness lets through, shown in blue by the light
 * direction view, at the columns `xs` of wall_scene(`glow`). */
static bool occlusion_at(HRC2D lighting, bool glow, const int *xs, int count, float *values)
{
    float center[3];
    render_radiance_debug_view view = lab.debug_view;
    lab.debug_view = RENDER_RADIANCE_DEBUG_LIGHT_DIRECTION;
    wall_scene(glow);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), center));
    middle_row(xs, count, 2, values);
    lab.debug_view = view;
    return true;
}

/* In both visibility modes, Depth thickness lets ambient light reach a floor
 * past the heights around it: a wall hides much of it at its base, less
 * farther away and none beyond the depth radius, evenly along the wall.
 * The frame's ambient and shadow floor follow it, Blur keeps them flat, and a
 * glow that absorbs nothing hides none. */
TEST_CASE(walls_occlude_ambient_light_by_their_height)
{
    static const int xs[] = { 19, 14, 8, 2 };
    for (int mode = HRC_DEPTH_VISIBILITY_MASK; mode <= HRC_DEPTH_VISIBILITY_SEGMENTS;
         mode++) {
        HRC2D lighting = fill_lighting();
        lighting.surface_depth_visibility = (HRCDepthVisibility)mode;
        lighting.ambient = 1;
        float occlusion[4], glow[4], ambient[4], shadow_floor[4], flat[4], center[3];
        REQUIRE(occlusion_at(lighting, true, xs, 4, glow));
        REQUIRE(occlusion_at(lighting, false, xs, 4, occlusion));
        float lowest = 1, highest = 0;
        for (int y = 8; y < 24; y++) {
            float visible = test_half(halves[4 * (y * SIDE + 19) + 2]);
            lowest = fminf(lowest, visible);
            highest = fmaxf(highest, visible);
        }
        REQUIRE(highest - lowest < .03f);
        REQUIRE(occlusion[0] > .45f && occlusion[0] < .72f && occlusion[3] > .95f);
        for (int i = 0; i < 4; i++) {
            REQUIRE(glow[i] > .97f);
            REQUIRE(i == 0 || occlusion[i] > occlusion[i - 1]);
        }
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), center));
        middle_row(xs, 4, 0, ambient);
        lighting.ambient = 0;
        lighting.shadow_floor = 1;
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), center));
        middle_row(xs, 4, 0, shadow_floor);
        lighting.ambient = 1;
        lighting.shadow_floor = 0;
        lighting.surface_mode = HRC_SURFACE_BLUR;
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), center));
        middle_row(xs, 4, 0, flat);
        for (int i = 0; i < 4; i++) {
            REQUIRE(fabsf(ambient[i] - .5f * occlusion[i]) < .005f);
            REQUIRE(fabsf(shadow_floor[i] - .5f * occlusion[i]) < .005f);
            REQUIRE(fabsf(flat[i] - .5f) < .005f);
        }
    }
    return true;
}

/* A flat matte floor `floor` units tall with edge light, closed (it absorbs
 * light) or open, lit by the red strip standing `height` units tall with
 * nothing between them. */
static void floor_scene(bool closed, float floor, float height)
{
    open_scene(OPEN, 0, 1);
    for (int p = 0; p < PIXELS; p++) {
        bool strip = data[SOURCE][p][0] > 0;
        data[ALBEDO][p][3] = strip ? height : floor;
        data[SOURCE][p][3] = strip || !closed ? 0 : 20;
    }
}

/* The red at the center of floor_scene() from a reset. */
static bool floor_light(HRC2D lighting, bool closed, float floor, float height, float *red)
{
    float color[3];
    floor_scene(closed, floor, height);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), color));
    *red = color[0];
    return true;
}

/* At falloff 0, 1 and 2, a closed floor approximately matches the open
 * floor's tone-mapped brightness, and intensity leaves the closed floor
 * unchanged. A light near its plane lights it less, and one below it not
 * at all. */
TEST_CASE(closed_receivers_take_the_light_hrc_brings_open_ones)
{
    HRC2D lighting = fill_lighting();
    lighting.surface_depth_radius = 32;
    for (int falloff = 0; falloff < 3; falloff++) {
        lighting.falloff = (float)falloff;
        float open, closed, low, below, bright;
        REQUIRE(floor_light(lighting, false, 0, 24, &open));
        REQUIRE(floor_light(lighting, true, 0, 24, &closed));
        REQUIRE(floor_light(lighting, true, 0, 4, &low));
        REQUIRE(floor_light(lighting, true, 30, 24, &below));
        lighting.intensity = 2;
        REQUIRE(floor_light(lighting, true, 0, 24, &bright));
        lighting.intensity = .5f;
        REQUIRE(open > .1f && fabsf(closed - open) < open * .15f);
        REQUIRE(low > closed * .3f && low < closed * .65f);
        REQUIRE(fabsf(below) < .00001f);
        REQUIRE(fabsf(bright - closed) < closed * .01f);
    }
    return true;
}

/* Softness fades a shadow's top and bottom edges only within the elevation
 * bins that block, so it never shadows what hard visibility lights there. An
 * open floor's own slab ends at the horizon: a strip on the floor's plane
 * lights it as fully as without softness. Slabs below, above or across the
 * strip's path shadow no more than without softness, and a crossing slab
 * still shadows. */
TEST_CASE(soft_visibility_keeps_lights_on_the_floor)
{
    static const int scenes[] = { OPEN, CROSSING, BELOW, ABOVE };
    HRC2D lighting = fill_lighting();
    float plane[2], light[2][4];
    for (int soft = 0; soft < 2; soft++) {
        lighting.surface_depth_shadow_softness = (float)soft;
        REQUIRE(floor_light(lighting, false, 0, 0, &plane[soft]));
        for (int i = 0; i < 4; i++) {
            REQUIRE(open_light(scenes[i], 0, 1, lighting, &light[soft][i]));
        }
    }
    REQUIRE(plane[0] > .1f && fabsf(plane[1] - plane[0]) < plane[0] * .01f);
    for (int i = 0; i < 4; i++) {
        REQUIRE(light[1][i] >= light[0][i] * .999f);
    }
    REQUIRE(light[1][1] < light[1][0] * .6f);
    return true;
}

/* open_scene(OPEN) with a `green` light at x 0 to 3, as tall as the strip,
 * behind a wall at x 6 to 9 as tall, so its slab hides the green light from
 * the receiver. The wall's optical depth per pixel is `absorption`. */
static void walled_scene(float green, float absorption)
{
    open_scene(OPEN, 0, 1);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < 10; x++) {
            int p = y * SIDE + x;
            bool wall = x >= 6;
            bool light = x <= 3 && y >= 8 && y <= 24;
            data[ALBEDO][p][3] = wall || light ? 12 : 0;
            data[SOURCE][p][1] = light ? green : 0;
            data[SOURCE][p][3] = wall ? absorption : 0;
        }
    }
}

/* An open receiver patch at x and y 14 to 19, lit by the strip from x 28 out
 * of reach of a depth radius of 9, and a green light at x 9 to 10 and y 14 to
 * 19 behind a wall at x 11 to 13, as tall, of optical depth `absorption` per
 * pixel. The rest of the floor is no receiver, so filtering borrows no light
 * from receivers that reach the strip. */
static void near_walled_scene(float green, float absorption)
{
    open_scene(OPEN, 0, 1);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            bool strip = x >= 28 && y >= 8 && y <= 24;
            bool wall = x >= 11 && x <= 13;
            bool light = x >= 9 && x <= 10 && y >= 14 && y < 20;
            bool receiver = x >= 14 && x < 20 && y >= 14 && y < 20;
            data[ALBEDO][p][3] = strip || wall || light ? 12 : 0;
            data[SOURCE][p][0] = strip ? 4 : 0;
            data[SOURCE][p][1] = light ? green : 0;
            data[SOURCE][p][3] = wall ? absorption : 0;
            if (!strip && !wall && !light && !receiver) {
                data[COVER][p][1] = data[COVER][p][2] = 0;
            }
        }
    }
}

/* Depth shades the light HRC brings by each light as much as HRC brings it.
 * A bright light behind an opaque wall, which HRC stops and Depth sees as
 * hidden, leaves the strip's light on the receiver as it was, in both
 * visibility modes, also when the strip is out of Depth's reach and the
 * walled light the only one in it. Behind a faint absorber, which HRC brings
 * nearly all of, the hidden light shades the strip's light, and as much at
 * slightly more absorption: a light HRC dims fades from the shading as
 * smoothly. */
TEST_CASE(lights_behind_absorbing_walls_leave_hrc_light_unshaded)
{
    for (int mode = HRC_DEPTH_VISIBILITY_MASK; mode <= HRC_DEPTH_VISIBILITY_SEGMENTS;
         mode++) {
        HRC2D lighting = fill_lighting();
        lighting.surface_depth_visibility = (HRCDepthVisibility)mode;
        float unlit[3], walled[3], faint[3], fainter[3];
        walled_scene(0, 20);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), unlit));
        walled_scene(16, 20);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), walled));
        REQUIRE(unlit[0] > .1f && fabsf(walled[0] - unlit[0]) < unlit[0] * .01f);
        REQUIRE(fabsf(walled[1] - unlit[1]) < .001f);
        walled_scene(16, .005f);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), fainter));
        walled_scene(16, .02f);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), faint));
        REQUIRE(fainter[0] < unlit[0] * .8f && fabsf(faint[0] - fainter[0]) < fainter[0] * .02f);
        lighting.surface_depth_radius = 9;
        near_walled_scene(0, 3);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), unlit));
        near_walled_scene(16, 3);
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), walled));
        REQUIRE(unlit[0] > .01f && fabsf(walled[0] - unlit[0]) < unlit[0] * .01f);
    }
    return true;
}

/* Scaling a finite HDR source and inversely scaling the receiver albedo
 * leaves its displayed light unchanged. A grazing source and strong relief
 * require large signed shading sums, even though the final light is modest.
 * History must retain those finite weighted sums too. */
TEST_CASE(bright_sources_keep_weighted_depth_shading_finite)
{
    for (int mode = HRC_DEPTH_VISIBILITY_MASK; mode <= HRC_DEPTH_VISIBILITY_SEGMENTS;
         mode++) {
        HRC2D lighting = fill_lighting();
        lighting.surface_depth_visibility = (HRCDepthVisibility)mode;
        lighting.surface_relief = 100;
        lighting.falloff = 0;
        float reference[3], bright[3], held[3];
        for (int exposure = 0; exposure < 2; exposure++) {
            float power = exposure ? 5000 : 50;
            open_scene(OPEN, .99f, 1);
            for (int p = 0; p < PIXELS; p++) {
                data[ALBEDO][p][3] = 0;
                for (int c = 0; c < 3; c++) {
                    data[ALBEDO][p][c] = .005f / power;
                }
                if (data[SOURCE][p][0] > 0) {
                    data[SOURCE][p][0] = power;
                }
            }
            REQUIRE(lit_fresh(lighting, test_centered(SIDE), exposure ? bright : reference));
        }
        REQUIRE(reference[0] > .01f && reference[0] < .95f);
        REQUIRE(fabsf(bright[0] - reference[0]) < reference[0] * .05f);
        lighting.surface_depth_temporal = true;
        REQUIRE(lit_fresh(lighting, test_centered(SIDE), bright));
        REQUIRE(lit(lighting, test_centered(SIDE), held));
        REQUIRE(fabsf(held[0] - reference[0]) < reference[0] * .05f);
    }
    return true;
}

/* A dark receiver, nearly black and tilted toward the half-vector, so gloss
 * shows apart from its diffuse light. The strip stays off the receiver. */
static void dark_receiver(float gloss)
{
    slab_scene(OPEN);
    for (int p = 0; p < PIXELS; p++) {
        data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = .01f;
        data[NORMAL][p][0] = .3f;
        data[NORMAL][p][3] = gloss;
    }
}

TEST_CASE(glossy_receivers_take_highlights_by_the_depth_gain)
{
    HRC2D lighting = fill_lighting();
    lighting.specular = 1;
    float matte[3], glossy[3], none[3], doubled[3];
    dark_receiver(0);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), matte));
    dark_receiver(.6f);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), glossy));
    REQUIRE(glossy[0] > matte[0] * 1.1f + .00001f);
    lighting.surface_depth_gain = 0;
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), none));
    lighting.surface_depth_gain = 2;
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), doubled));
    float fill = glossy[0] - none[0];
    REQUIRE(fabsf(doubled[0] - none[0] - 2 * fill) < fmaxf(.001f, fabsf(fill) * .03f));
    return true;
}

/* A matte patch shares the plane, normal and albedo of the glossy receiver
 * around it, but reconstruction must not lend it the receiver's gloss. */
TEST_CASE(matte_patches_take_no_gloss_from_around_them)
{
    HRC2D lighting = fill_lighting();
    dark_receiver(.6f);
    for (int y = 12; y < 20; y++) {
        for (int x = 12; x < 20; x++) {
            data[NORMAL][y * SIDE + x][3] = 0;
        }
    }
    float off[3], on[3];
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), off));
    lighting.specular = 1;
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), on));
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(on[c] - off[c]) < .00001f);
    }
    return true;
}

/* Every quality, with and without near detail and soft visibility, lights
 * the receiver and lets a crossing slab shadow it. */
TEST_CASE(every_depth_quality_lights_and_shadows)
{
    HRC2D lighting = depth_lighting();
    for (int quality = HRC_DEPTH_LOW; quality <= HRC_DEPTH_HIGH; quality++) {
        lighting.surface_depth_quality = (HRCDepthQuality)quality;
        for (int near = 0; near < 2; near++) {
            float open, crossing;
            lab.surface_depth_near_detail = near;
            lighting.surface_depth_shadow_softness = near ? .5f : 0;
            REQUIRE(
                slab_light(OPEN, lighting, &open) &&
                slab_light(CROSSING, lighting, &crossing)
            );
            REQUIRE(open > .00001f && crossing < open * .6f);
        }
    }
    return true;
}

/* Depth history keeps still light as it is. It drops it for a receiver that
 * turns away, which would otherwise keep its old light as a ghost, for a
 * removed source, for a receiver that tilts toward the light, for a source
 * that dims sharply, a changed distance falloff, and on a reset. */
TEST_CASE(depth_history_keeps_still_light_and_drops_changes)
{
    HRC2D lighting = depth_lighting();
    lighting.surface_depth_history = .9f;
    float initial[3], held[3], tilted[3], dimmed[3], fresh[3], turned[3],
        unlit[3], restored[3];
    slab_scene(OPEN);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), initial));
    lighting.surface_depth_temporal = true;
    for (int frame = 0; frame < 4; frame++) {
        REQUIRE(lit(lighting, test_centered(SIDE), held));
    }
    REQUIRE(initial[0] > .00001f);
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(initial[c] - held[c]) < .001f);
    }
    /* A moderate falloff edit keeps the geometry and source unchanged. Its
     * first frame must match a fresh solve, rather than hold the old light. */
    float falloff[3];
    lighting.falloff += .25f;
    REQUIRE(lit(lighting, test_centered(SIDE), falloff));
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), fresh));
    float change = held[0] - fresh[0];
    REQUIRE(fresh[0] > .00001f && change > held[0] * .01f);
    REQUIRE(fabsf(falloff[0] - fresh[0]) < change * .05f);
    lighting.falloff -= .25f;
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), held));
    /* Normals hold x and y; z flags a solid surface, and the rebuilt normal
     * faces the viewer. */
    for (int p = 0; p < PIXELS; p++) {
        if (data[SOURCE][p][0] <= 0) {
            data[NORMAL][p][0] = -.99f;
            data[NORMAL][p][1] = 0;
        }
    }
    REQUIRE(lit(lighting, test_centered(SIDE), turned));
    REQUIRE(dark(turned));
    slab_scene(OPEN);
    REQUIRE(lit(lighting, test_centered(SIDE), held));
    slab_scene(UNLIT);
    REQUIRE(lit(lighting, test_centered(SIDE), unlit));
    REQUIRE(dark(unlit));
    slab_scene(OPEN);
    lighting.surface_depth_temporal = false;
    REQUIRE(lit(lighting, test_centered(SIDE), restored));
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(initial[c] - restored[c]) < .001f);
    }
    lighting.surface_depth_temporal = true;
    for (int frame = 0; frame < 4; frame++) {
        REQUIRE(lit(lighting, test_centered(SIDE), held));
    }
    /* A receiver tilted toward the strip takes its new light at once. Its
     * light changes too little for that alone to drop the history; the
     * changed normal must. */
    slab_scene(TILTED);
    REQUIRE(lit(lighting, test_centered(SIDE), tilted));
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), fresh));
    REQUIRE(fresh[0] > held[0] && fabsf(tilted[0] - fresh[0]) < fresh[0] * .05f);
    slab_scene(OPEN);
    for (int frame = 0; frame < 4; frame++) {
        REQUIRE(lit(lighting, test_centered(SIDE), held));
    }
    /* A strip four times dimmer replaces the held light at once. Light falls
     * off steeply across the center's neighbors, so clamping the old light to
     * their range alone would still leave the center too bright. */
    for (int p = 0; p < PIXELS; p++) {
        data[SOURCE][p][0] *= .25f;
    }
    REQUIRE(lit(lighting, test_centered(SIDE), dimmed));
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), fresh));
    REQUIRE(fresh[0] > .00001f && fabsf(dimmed[0] - fresh[0]) < fresh[0] * .05f);
    slab_scene(UNLIT);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), unlit));
    REQUIRE(dark(unlit));
    return true;
}

/* Depth history strength left at zero takes the default. */
static HRC2D default_history()
{
    HRC2D lighting = depth_lighting();
    lighting.surface_depth_history = 0;
    return lighting;
}

/* Depth history at its default strength takes part of a brighter source's
 * light in one frame and settles near the new light. */
TEST_CASE(depth_history_settles_on_brighter_light)
{
    HRC2D lighting = default_history();
    float instant[3], initial[3], first[3], settled[3];
    slab_scene(OPEN);
    for (int p = 0; p < PIXELS; p++) {
        data[SOURCE][p][0] *= 1.05f;
    }
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), instant));
    lighting.surface_depth_temporal = true;
    slab_scene(OPEN);
    REQUIRE(lit_fresh(lighting, test_centered(SIDE), initial));
    for (int p = 0; p < PIXELS; p++) {
        data[SOURCE][p][0] *= 1.05f;
    }
    REQUIRE(lit(lighting, test_centered(SIDE), first));
    for (int frame = 0; frame < 24; frame++) {
        REQUIRE(lit(lighting, test_centered(SIDE), settled));
    }
    float step = instant[0] - initial[0];
    REQUIRE(step > initial[0] * .02f);
    REQUIRE(first[0] > initial[0] && first[0] < instant[0] - step * .2f);
    REQUIRE(fabsf(settled[0] - instant[0]) < step * .25f);
    return true;
}

/* Depth history also holds how Depth shades HRC's light on a receiver that
 * absorbs nothing: tilted a little toward the strip, the receiver takes part
 * of its new light in one frame and settles near it. */
TEST_CASE(depth_history_settles_on_open_shading)
{
    HRC2D lighting = fill_lighting();
    lighting.surface_depth_history = 0;
    float instant, initial, color[3];
    REQUIRE(open_light(OPEN, .2f, 1, lighting, &instant));
    lighting.surface_depth_temporal = true;
    REQUIRE(open_light(OPEN, 0, 1, lighting, &initial));
    open_scene(OPEN, .2f, 1);
    REQUIRE(lit(lighting, test_centered(SIDE), color));
    float first = color[0];
    for (int frame = 0; frame < 24; frame++) {
        REQUIRE(lit(lighting, test_centered(SIDE), color));
    }
    float step = instant - initial;
    REQUIRE(step > initial * .02f);
    REQUIRE(first > initial && first < instant - step * .2f);
    REQUIRE(fabsf(color[0] - instant) < step * .25f);
    return true;
}

/* The camera moves one cell right, and the same strip moves four pixels
 * left. Any history sample on the plane looks plausible, but its light
 * changes across it, so history must come from the moved cell. */
TEST_CASE(depth_history_follows_the_camera)
{
    HRC2D lighting = default_history();
    lighting.surface_depth_temporal = true;
    slab_scene(OPEN);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 28; x < SIDE; x++) {
            int p = y * SIDE + x;
            data[SOURCE][p][0] = 0;
            data[SOURCE][p][3] = 20;
            data[ALBEDO][p][3] = 0;
        }
    }
    render_view_2d view = test_centered(SIDE);
    float before[3], moved[3], fresh[3];
    REQUIRE(lit_fresh(lighting, view, before));
    for (int i = 0; i < INPUTS; i++) {
        for (int y = 0; y < SIDE; y++) {
            float (*row)[4] = data[i] + y * SIDE;
            memmove(row, row + 4, (SIDE - 4) * sizeof *row);
            /* What comes into view is the same plane without emission. */
            for (int x = SIDE - 4; x < SIDE; x++) {
                memcpy(row[x], row[0], sizeof *row);
            }
        }
    }
    view.min_x += 4;
    view.center_x += 4;
    REQUIRE(lit(lighting, view, moved));
    lighting.surface_depth_temporal = false;
    REQUIRE(lit_fresh(lighting, view, fresh));
    REQUIRE(fresh[0] > .00001f && fabsf(moved[0] - fresh[0]) < fresh[0] * .1f);
    return true;
}

/* Two colors share one coarse tile, and their brightness crosses by only two
 * percent while their distances and normals light the receiver differently.
 * Giving the whole tile's color to the brighter one would make the receiver
 * jump at the crossing though every input changes smoothly. */
TEST_CASE(mixed_color_sources_light_smoothly)
{
    HRC2D lighting = calibrated();
    lighting.surface_radius = lighting.surface_depth_radius = 128;
    lighting.surface_thickness = 0;
    float previous[3] = { 0 }, step = 0, least = INFINITY;
    for (int frame = 0; frame < 9; frame++) {
        memset(data, 0, sizeof data);
        for (int p = 0; p < WIDE * WIDE; p++) {
            data[SCENE][p][3] = 1;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[NORMAL][p][0] = .9f;
            data[NORMAL][p][2] = .4358899f;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
        }
        for (int y = 64; y < 68; y++) {
            for (int x = 80; x < 96; x++) {
                int p = y * WIDE + x;
                if (x >= 84 && x < 92) {
                    continue;
                }
                data[ALBEDO][p][3] = 12;
                if (x < 84) {
                    data[SOURCE][p][0] = 4 * (1 + (frame - 4) * .005f);
                } else {
                    data[SOURCE][p][2] = 4 * (.2126f / .0722f);
                }
            }
        }
        float color[3];
        REQUIRE(lit_fresh(lighting, test_centered(WIDE), color));
        least = fminf(least, fminf(color[0], color[2]));
        for (int c = 0; frame && c < 3; c += 2) {
            step = fmaxf(
                step,
                fabsf(color[c] - previous[c]) /
                    fmaxf((color[c] + previous[c]) * .5f, .00001f)
            );
        }
        memcpy(previous, color, sizeof previous);
    }
    REQUIRE(least > .00001f && step < .06f);
    return true;
}

/* The red of the output pixels from x0, y0 to x1, y1, read back by lit().
 * Every pixel counts, also those between the coarse receiver centers, where
 * center-only checks miss stripes. */
static red_range patch(int x0, int y0, int x1, int y1)
{
    red_range light = { .least = INFINITY };
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float red = test_half(halves[4 * (y * side + x)]);
            light.mean += red / ((x1 - x0) * (y1 - y0));
            light.least = fminf(light.least, red);
            light.most = fmaxf(light.most, red);
        }
    }
    return light;
}

/* A view of WIDE pixels left of and above the origin, whose scenes place
 * things by world units at twice the pixel spacing. */
static const render_view_2d segment_view = { .min_x = -128,
                                             .max_y = 128,
                                             .width = WIDE,
                                             .height = WIDE,
                                             .crop_max_u = 1,
                                             .crop_max_v = 1 };

/* A source 80 units tall at x = -70 and a blocker 40 tall at `blocker_x`,
 * or none when `blocker_x` is NAN. */
static void segment_scene(float blocker_x)
{
    memset(data, 0, sizeof data);
    for (int y = 0; y < WIDE; y++) {
        for (int x = 0; x < WIDE; x++) {
            int p = y * WIDE + x;
            float wx = -128 + (x + .5f) * 2, wy = 128 - (y + .5f) * 2;
            data[SCENE][p][3] = 1;
            data[SOURCE][p][3] = 20;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[NORMAL][p][2] = 1;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
            if (fabsf(wx + 70) < 4 && fabsf(wy) < 4) {
                data[SOURCE][p][0] = 4;
                data[SOURCE][p][3] = 0;
                data[ALBEDO][p][3] = 80;
            }
            if (fabsf(wx - blocker_x) < 4 && fabsf(wy) < 35) {
                data[ALBEDO][p][3] = 40;
            }
        }
    }
}

/* Bounded segments shadow a receiver behind a blocker, but not one whose
 * blocker stands beyond the light. */
TEST_CASE(segments_shadow_only_receivers_behind_blockers)
{
    HRC2D lighting = calibrated();
    lighting.surface_radius = lighting.surface_depth_radius = 384;
    lighting.surface_thickness = 12;
    lighting.surface_depth_visibility = HRC_DEPTH_VISIBILITY_SEGMENTS;
    const float blockers[] = { 0, NAN, -100 };
    float means[3], color[3];
    for (int i = 0; i < 3; i++) {
        segment_scene(blockers[i]);
        REQUIRE(lit_fresh(lighting, segment_view, color));
        /* World x 56 to 64 and y -4 to 4, well inside the umbra. */
        means[i] = patch(92, 62, 96, 66).mean;
    }
    REQUIRE(means[1] > .00001f && means[0] < means[1] * .25f);
    REQUIRE(fabsf(means[2] - means[1]) < means[1] * .1f);
    return true;
}

/* A plane rising 24 units a pixel, beyond the old raw-height tolerance even
 * between neighbors, with a source at its left. With `detached`, a strip two
 * pixels wide stands 1000 units above it, between the receiver centers at x
 * = 2 mod 4. */
static void steep_plane(bool detached)
{
    float slope = 12, nx = -slope / sqrtf(1 + slope * slope);
    memset(data, 0, sizeof data);
    for (int y = 0; y < WIDE; y++) {
        for (int x = 0; x < WIDE; x++) {
            int p = y * WIDE + x;
            data[SCENE][p][3] = 1;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[ALBEDO][p][3] = slope * (-128 + (x + .5f) * 2);
            data[NORMAL][p][0] = nx;
            data[NORMAL][p][2] = 1;
            data[COVER][p][0] = data[COVER][p][1] = data[COVER][p][2] = 1;
            if (x >= 24 && x < 28 && y >= 62 && y < 66) {
                data[SOURCE][p][0] = 4;
                data[ALBEDO][p][3] = 400;
            }
            if (detached && x >= 64 && x < 66) {
                data[ALBEDO][p][3] += 1000;
            }
        }
    }
}

/* Every pixel of one continuous steep plane is lit, though the light changes
 * with distance across it; incompatible neighbors would leave black stripes
 * between the receiver centers. A detached strip without its own receiver
 * centers borrows none of the plane's light. */
TEST_CASE(steep_planes_light_every_pixel_but_detached_strips)
{
    HRC2D lighting = calibrated();
    lighting.surface_radius = lighting.surface_depth_radius = 1024;
    lighting.surface_thickness = 0;
    float color[3];
    steep_plane(false);
    REQUIRE(lit_fresh(lighting, segment_view, color));
    red_range plane = patch(56, 60, 72, 68);
    REQUIRE(plane.least > 1e-8f);
    steep_plane(true);
    REQUIRE(lit_fresh(lighting, segment_view, color));
    REQUIRE(patch(64, 60, 66, 68).most < plane.mean * .01f);
    return true;
}

/* ---------- Surface gather ---------- */

/* HRC's light by quadrant, the gather's four outputs, and its pass. */
static float quadrant_data[4][PIXELS][4];
static CF_Texture quadrants[4];
static CF_Canvas gathered;
static CF_Material gather_material;
static CF_Mesh pass_mesh;
static CF_Shader gather_shader;

static void gather_setup()
{
    setup();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target_count = 4;
    for (int q = 0; q < 4; q++) {
        quadrants[q] = make_input(SIDE);
        params.targets[q].pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    }
    gathered = cf_make_canvas(params);
    gather_material = cf_make_material();
    pass_mesh = render_make_pass_mesh();
    gather_shader = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/surface_gather.frag");
    assert(gathered.id && gather_material.id && pass_mesh.id && gather_shader.id);
    const char *names[] = { "u_q0", "u_q1", "u_q2", "u_q3" };
    for (int q = 0; q < 4; q++) {
        cf_material_set_texture_fs(gather_material, names[q], quadrants[q]);
    }
    cf_material_set_texture_fs(gather_material, "u_source", textures[SOURCE]);
    cf_material_set_texture_fs(gather_material, "u_albedo", textures[ALBEDO]);
    cf_material_set_texture_fs(gather_material, "u_cover", textures[COVER]);
    cf_material_set_texture_fs(gather_material, "u_normal", textures[NORMAL]);
    float spacing[] = { 1, 1 }, radius = 8, relief = 1;
    int block = 1;
    cf_material_set_uniform_fs(gather_material, "u_world_per_px", spacing, CF_UNIFORM_TYPE_FLOAT2, 1);
    cf_material_set_uniform_fs(gather_material, "u_radius", &radius, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(gather_material, "u_relief", &relief, CF_UNIFORM_TYPE_FLOAT, 1);
    cf_material_set_uniform_fs(gather_material, "u_block", &block, CF_UNIFORM_TYPE_INT, 1);
}

static void gather_teardown()
{
    cf_destroy_shader(gather_shader);
    cf_destroy_mesh(pass_mesh);
    cf_destroy_material(gather_material);
    cf_destroy_canvas(gathered);
    for (int q = 0; q < 4; q++) {
        cf_destroy_texture(quadrants[q]);
    }
    teardown();
}

/* A solid floor, opaque to the camera but not to light, under HRC light
 * that comes into each quadrant from its side: red from the right, green
 * from below, blue from the left and red again from above. */
enum
{
    FLAT,
    /* A gap at x 18 and 19, then a crest 10 units tall at x 21 to 24: the
     * sparse visibility probes must carry on across the gap to find it. */
    CREST,
    /* The gap without the crest. */
    LOW_CREST,
    /* No light at all. */
    NO_LIGHT,
    /* Red light only past x = 30, beyond the gather's reach. */
    OUT_OF_REACH,
};

static void gather_scene(int scene)
{
    memset(data, 0, sizeof data);
    memset(quadrant_data, 0, sizeof quadrant_data);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int p = y * SIDE + x;
            data[SCENE][p][3] = 1;
            data[ALBEDO][p][0] = data[ALBEDO][p][1] = data[ALBEDO][p][2] = 1;
            data[COVER][p][1] = data[COVER][p][2] = 1;
            data[NORMAL][p][2] = 1;
            const bool lit_sides[] = { x >= 20, y >= 20, x <= 12, y <= 12 };
            for (int q = 0; q < 4; q++) {
                bool light = scene == NO_LIGHT     ? false
                           : scene == OUT_OF_REACH ? q == 0 && x >= 30
                                                   : lit_sides[q];
                quadrant_data[q][p][q % 3] = light ? 1 : 0;
            }
            if ((scene == CREST || scene == LOW_CREST) && x >= 18 && x <= 19) {
                data[COVER][p][1] = data[COVER][p][2] = 0;
            }
            if (scene == CREST && x >= 21 && x <= 24) {
                data[ALBEDO][p][3] = 10;
            }
        }
    }
}

/* Runs the gather pass over `scene` and reads the center RGB of each of its
 * four outputs into `colors`. */
static bool gather(int scene, float colors[4][3])
{
    gather_scene(scene);
    cf_app_update(NULL);
    for (int i = 0; i < INPUTS; i++) {
        cf_texture_update(textures[i], data[i], PIXELS * sizeof data[i][0]);
    }
    for (int q = 0; q < 4; q++) {
        cf_texture_update(quadrants[q], quadrant_data[q], sizeof quadrant_data[q]);
    }
    cf_apply_canvas(gathered, true);
    cf_apply_mesh(pass_mesh);
    cf_apply_shader(gather_shader, gather_material);
    cf_apply_viewport(0, 0, SIDE, SIDE);
    cf_apply_scissor(0, 0, SIDE, SIDE);
    cf_draw_elements();
    cf_app_draw_onto_screen(false);
    for (int q = 0; q < 4; q++) {
        REQUIRE(test_read_texture(cf_canvas_get_target2(gathered, q), halves, PIXELS));
        for (int c = 0; c < 3; c++) {
            colors[q][c] = test_half(halves[4 * (SIDE / 2 * SIDE + SIDE / 2) + c]);
        }
    }
    return true;
}

/* On a flat floor each output takes its quadrant's light whole. */
TEST_CASE(surface_gather_takes_each_quadrants_light)
{
    float colors[4][3];
    REQUIRE(gather(FLAT, colors));
    for (int q = 0; q < 4; q++) {
        REQUIRE(fabsf(colors[q][q % 3] - 1) < .001f);
    }
    return true;
}

/* A crest past a gap blocks most of the light from its side; without the
 * crest the gap alone blocks none. */
TEST_CASE(surface_gather_finds_crests_past_gaps)
{
    float flat[4][3], crest[4][3], low[4][3];
    REQUIRE(gather(FLAT, flat) && gather(CREST, crest) && gather(LOW_CREST, low));
    REQUIRE(flat[0][0] > .99f && crest[0][0] < flat[0][0] * .5f);
    REQUIRE(low[0][0] > .99f);
    return true;
}

TEST_CASE(surface_gather_stays_dark_without_light_in_reach)
{
    const int scenes[] = { NO_LIGHT, OUT_OF_REACH };
    for (int i = 0; i < 2; i++) {
        float colors[4][3];
        REQUIRE(gather(scenes[i], colors));
        for (int q = 0; q < 4; q++) {
            REQUIRE(fabsf(colors[q][0]) + fabsf(colors[q][1]) + fabsf(colors[q][2]) < .001f);
        }
    }
    return true;
}

/* Turning the view by yaw or roll starts surface light over: once the light
 * is gone, the receiver keeps none of it though history would hold it. */
TEST_CASE(surface_gather_light_restarts_when_the_view_turns)
{
    HRC2D lighting = calibrated();
    lighting.specular = 1;
    lighting.surface_mode = HRC_SURFACE_GATHER;
    lighting.surface_radius = 8;
    lighting.surface_relief = 1;
    lighting.radiance_hysteresis = .05f;
    for (int axis = 0; axis < 2; axis++) {
        render_view_2d view = test_centered(SIDE);
        float bright[3], turned[3];
        gather_scene(FLAT);
        for (int y = 0; y < SIDE; y++) {
            for (int x = 20; x < SIDE; x++) {
                data[SOURCE][y * SIDE + x][0] = 2;
            }
        }
        render_radiance_reset(radiance);
        for (int frame = 0; frame < 4; frame++) {
            REQUIRE(lit(lighting, view, bright));
        }
        REQUIRE(bright[0] > .01f);
        memset(data[SOURCE], 0, sizeof data[SOURCE]);
        if (axis) {
            view.roll = .7f;
        } else {
            view.yaw = .7f;
        }
        REQUIRE(lit(lighting, view, turned));
        for (int c = 0; c < 3; c++) {
            REQUIRE(fabsf(turned[c]) < .001f);
        }
    }
    return true;
}

void test_render_radiance_depth()
{
    test_open_hidden_app("radiance depth", SIDE, SIDE);
    radiance = render_radiance_new();
    assert(radiance);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(depth_light_reaches_closed_receivers_within_its_budget);
    RUN_TEST_CASE(depth_light_switches_back_to_blur);
    RUN_TEST_CASE(slabs_shadow_only_the_light_they_cross);
    RUN_TEST_CASE(receivers_tilted_toward_the_light_take_more);
    RUN_TEST_CASE(slabs_shadow_by_height_unless_they_glow);
    RUN_TEST_CASE(small_sources_light_every_direction_by_their_power);
    RUN_TEST_CASE(depth_light_falls_off_with_distance);
    RUN_TEST_CASE(sources_between_pixels_keep_their_light);
    RUN_TEST_CASE(moving_sources_light_smoothly_across_tiles);
    RUN_TEST_CASE(light_fades_out_at_the_depth_radius);
    RUN_TEST_CASE(sources_at_the_view_edge_keep_the_light_left_in_view);
    RUN_TEST_CASE(perspective_views_take_depth_light);
    RUN_TEST_CASE(depth_gain_adds_only_to_closed_receivers);
    RUN_TEST_CASE(open_receivers_shade_hrc_light_by_facing_and_height);
    RUN_TEST_CASE(depth_gain_keeps_shadows_and_unlit_scenes_dark);
    RUN_TEST_CASE(walls_occlude_ambient_light_by_their_height);
    RUN_TEST_CASE(closed_receivers_take_the_light_hrc_brings_open_ones);
    RUN_TEST_CASE(soft_visibility_keeps_lights_on_the_floor);
    RUN_TEST_CASE(lights_behind_absorbing_walls_leave_hrc_light_unshaded);
    RUN_TEST_CASE(bright_sources_keep_weighted_depth_shading_finite);
    RUN_TEST_CASE(glossy_receivers_take_highlights_by_the_depth_gain);
    RUN_TEST_CASE(matte_patches_take_no_gloss_from_around_them);
    RUN_TEST_CASE(every_depth_quality_lights_and_shadows);
    RUN_TEST_CASE(depth_history_keeps_still_light_and_drops_changes);
    RUN_TEST_CASE(depth_history_settles_on_brighter_light);
    RUN_TEST_CASE(depth_history_settles_on_open_shading);
    RUN_TEST_CASE(depth_history_follows_the_camera);
    RUN_TEST_CASE(surface_gather_light_restarts_when_the_view_turns);
    pu_setup(wide_setup, teardown);
    RUN_TEST_CASE(mixed_color_sources_light_smoothly);
    RUN_TEST_CASE(segments_shadow_only_receivers_behind_blockers);
    RUN_TEST_CASE(steep_planes_light_every_pixel_but_detached_strips);
    pu_setup(gather_setup, gather_teardown);
    RUN_TEST_CASE(surface_gather_takes_each_quadrants_light);
    RUN_TEST_CASE(surface_gather_finds_crests_past_gaps);
    RUN_TEST_CASE(surface_gather_stays_dark_without_light_in_reach);
    pu_clear_setup();
    render_radiance_free(radiance);
    cf_destroy_app();
}
