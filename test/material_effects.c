#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/backdrop_2d.h"
#include "render/fluid_raster.h"
#include "render/material_raster.h"
#include "render/reflections.h"
#include "render/shader_catalog.h"
#include "render/trail_2d.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};

static float half_value(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

/* Reads one pixel of a side x side RGBA16F texture as floats. */
static bool texel(CF_Texture texture, int x, int y, float out[4], int side)
{
    static uint16_t pixels[PIXELS * 4];
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = canvas.id ? cf_canvas_readback(canvas)
                                     : (CF_Readback){ 0 };
    bool ok = readback.id != 0;
    if (ok) {
        while (!cf_readback_ready(readback)) {
        }
        int bytes = side * side * 4 * (int)sizeof *pixels;
        ok = cf_readback_size(readback) == bytes &&
             cf_readback_data(readback, pixels, bytes) == bytes;
        cf_destroy_readback(readback);
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    for (int c = 0; ok && c < 4; c++) {
        out[c] = half_value(pixels[4 * (y * side + x) + c]);
    }
    return ok;
}

static render_material_entity block()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 18, 18 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 16, 16 },
        .scene = { .1f, .2f, .6f, 1 },
        .albedo = { .1f, .2f, .6f, 1 },
        .absorption = { 1, 1, 1, 1 },
    };
    return entity;
}

static bool raster(render_material_raster *r, const render_material_entity *entities, int count)
{
    render_material_view view = { .min_x = -32, .max_y = 32, .width = SIDE, .height = SIDE };
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(r, entities, count, view);
    cf_app_draw_onto_screen(false);
    return ok;
}

/* A flashed solid turns toward the flash color and emits it. */
static bool hit_flash(render_material_raster *r)
{
    render_material_entity plain = block(), flashed = block();
    flashed.flash[0] = 1;
    flashed.flash[1] = flashed.flash[2] = .9f;
    flashed.flash[3] = .75f;
    float scene[4], source[4], lit[4], glow[4];
    render_material_textures textures = render_material_raster_textures(r);
    bool ok = raster(r, &plain, 1) && texel(textures.scene, 32, 32, scene, SIDE) &&
              texel(textures.source, 32, 32, source, SIDE);
    ok = ok && raster(r, &flashed, 1) && texel(textures.scene, 32, 32, lit, SIDE) &&
         texel(textures.source, 32, 32, glow, SIDE);
    printf(
        "material hit flash: scene %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f; emission "
        "%.3f -> %.3f\n",
        scene[0],
        scene[1],
        scene[2],
        lit[0],
        lit[1],
        lit[2],
        source[0],
        glow[0]
    );
    return ok && fabsf(lit[0] - .775f) < .01f && fabsf(lit[2] - .825f) < .01f &&
           source[0] == 0 && glow[0] > .1f;
}

/* A backdrop material whose emission is its world position and whose base
 * color is white. */
static behavior_asset *position_material()
{
    behavior_asset *material = behavior_asset_new("Backdrop regression");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(material->graph, false);
    node *world = graph_add_node(material->graph, registry_node_by_name("Shader WorldPosition"));
    float white[3] = { 1, 1, 1 };
    pin_value base = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = white };
    shader_error error;
    if (graph_set_pin_default(material->graph, output, 0, 0, &base) ||
        graph_connect_data(material->graph, world, 0, output, 1) ||
        !render_shader_upsert(material, &error)) {
        behavior_asset_free(material);
        return NULL;
    }
    return material;
}

static ecs_world *backdrop_world(const char *material, bool lit)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    GameBackdrop2D backdrop = { .material = material,
                                .parallax = .5f,
                                .intensity = 1,
                                .lit = lit };
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameBackdrop2D.id, &backdrop, 0);
    ecs_world_apply_commands(world);
    return world;
}

/* Layers draw at half resolution with parallax-scaled world positions, and
 * lit layers multiply base color by received light. */
static bool backdrops()
{
    behavior_asset *material = position_material();
    render_backdrops *b = render_backdrops_new();
    CF_Texture gray = cf_make_texture(cf_texture_defaults(1, 1));
    uint32_t quarter = 0xff404040u;
    cf_texture_update(gray, &quarter, sizeof quarter);
    bool ok = material != NULL;
    for (int lit = 0; ok && lit < 2; lit++) {
        ecs_world *world = backdrop_world(material->name, lit);
        render_backdrop_frame frame = {
            .camera = { .min_x = 100 - 32,
                        .max_y = 32,
                        .width = SIDE,
                        .height = SIDE,
                        .crop_max_u = 1,
                        .crop_max_v = 1 },
            .light = { .texture = gray, .map = { 1, 1 } },
            .width = SIDE,
            .height = SIDE,
        };
        cf_app_update(NULL);
        ok = render_backdrops_collect(b, world);
        CF_Texture layers = ok ? render_backdrops_draw(b, &frame) : (CF_Texture){ 0 };
        cf_app_draw_onto_screen(false);
        float center[4] = { 0 }, left[4] = { 0 };
        ok = layers.id && texel(layers, 16, 16, center, SIDE / 2) &&
             texel(layers, 0, 16, left, SIDE / 2);
        /* World X at the center is the camera's scaled by parallax, plus the
         * base color, lit or not. */
        float base = lit ? .25f : 1;
        printf(
            "backdrop %s: center %.2f left %.2f alpha %.2f\n",
            lit ? "lit" : "unlit",
            center[0],
            left[0],
            center[3]
        );
        /* Half-resolution pixel centers sit 1 and -31 world units off the
         * camera center. */
        ok = ok && fabsf(center[0] - (51 + base)) < .1f &&
             fabsf(left[0] - (19 + base)) < .1f &&
             fabsf(center[3] - 1) < .01f && fabsf(center[1] - base) < .02f;
        ecs_world_free(world);
    }
    render_backdrops_free(b);
    cf_destroy_texture(gray);
    behavior_asset_free(material);
    return ok;
}

/* Glass: transmission absorbs gray like its least transmitted channel and
 * adds the colored rest to albedo; refraction writes its offset along the
 * surface tilt wherever the shape covers. */
static bool glass(render_material_raster *r)
{
    behavior_asset *material = behavior_asset_new("Glass regression");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(material->graph, false);
    float black[3] = { 0, 0, 0 }, tilt[3] = { .5f, 1, 0 }, pass[3] = { .2f, .9f, .9f };
    pin_value base = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = black };
    pin_value normal = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = tilt };
    pin_value transmission = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = pass };
    pin_value opacity = { .type = PIN_F32, .f = .5f },
              refraction = { .type = PIN_F32, .f = 10 };
    shader_error error;
    bool ok = !graph_set_pin_default(material->graph, output, 0, 0, &base) &&
              !graph_set_pin_default(material->graph, output, 0, 4, &normal) &&
              !graph_set_pin_default(material->graph, output, 0, 5, &opacity) &&
              !graph_set_pin_default(material->graph, output, 0, 8, &refraction) &&
              !graph_set_pin_default(material->graph, output, 0, 9, &transmission) &&
              render_shader_upsert(material, &error);
    render_material_entity entity = block();
    entity.reserved[0] = (float)render_shader_find(material->name);
    render_shader_parameters(
        (int)entity.reserved[0],
        NULL,
        (ecs_entity){ 0 },
        NULL,
        entity.material_parameters
    );
    render_material_textures textures = render_material_raster_textures(r);
    float source[4], albedo[4], cover[4];
    ok = ok && render_shader_refracts((int)entity.reserved[0]) &&
         raster(r, &entity, 1) && texel(textures.source, 32, 32, source, SIDE) &&
         texel(textures.albedo, 32, 32, albedo, SIDE) &&
         texel(textures.cover, 32, 32, cover, SIDE);
    float depth = -logf(.2f) / 32;
    printf(
        "glass: depth %.4f (expected %.4f) albedo %.2f,%.2f,%.2f bend "
        "%.2f,%.2f\n",
        source[3],
        depth,
        albedo[0],
        albedo[1],
        albedo[2],
        cover[1],
        cover[2]
    );
    ok = ok && fabsf(source[3] - depth) < .002f && albedo[0] < .01f &&
         fabsf(albedo[1] - .7f) < .01f &&
         fabsf(cover[1] - 10 * .5f / sqrtf(1.25f)) < .05f && fabsf(cover[2]) < .01f;
    behavior_asset_free(material);
    return ok;
}

/* Mean and peak albedo red across the liquid's middle rows. */
static bool liquid_albedo(
    render_material_raster *r,
    render_fluid_raster *draw,
    const material_fluid *fluid,
    float caustics,
    float time,
    float *mean,
    float *peak
)
{
    render_material_view view = { .min_x = -32, .max_y = 32, .width = SIDE, .height = SIDE };
    cf_app_update(NULL);
    render_fluid_raster_set_caustics(draw, time, caustics);
    bool ok = render_material_raster_draw(r, NULL, 0, view) &&
              render_fluid_raster_draw(draw, fluid, render_material_raster_canvas(r), view, SIDE, SIDE);
    cf_app_draw_onto_screen(false);
    *mean = *peak = 0;
    float sample[4];
    int count = 0;
    for (int y = 24; ok && y < 40; y += 3) {
        for (int x = 12; ok && x < 52; x += 3) {
            ok = texel(render_material_raster_textures(r).albedo, x, y, sample, SIDE);
            *mean += sample[0];
            *peak = fmaxf(*peak, sample[0]);
            count++;
        }
    }
    *mean /= count;
    return ok;
}

/* Rippling liquid focuses light into caustic lines that brighten its albedo
 * and move with time; zero strength leaves the liquid plain. */
static bool caustics(render_material_raster *r)
{
    material_fluid *fluid = material_fluid_new(10, 256);
    render_fluid_raster *draw = render_fluid_raster_new();
    for (int y = -16; y <= 16; y += 4) {
        for (int x = -28; x <= 28; x += 4) {
            material_fluid_add(
                fluid,
                (fluid_particle){ .position = { (float)x, (float)y },
                                  .mass = 1,
                                  .radius = 10,
                                  .kind = FLUID_LIQUID,
                                  .color = 0x2080a0ffu,
                                  .refraction = 8 }
            );
        }
    }
    float plain = 0, plain_peak = 0, lit = 0, lit_peak = 0, later = 0, later_peak = 0;
    bool ok = draw && liquid_albedo(r, draw, fluid, 0, 0, &plain, &plain_peak) &&
              liquid_albedo(r, draw, fluid, .5f, 0, &lit, &lit_peak) &&
              liquid_albedo(r, draw, fluid, .5f, 1.3f, &later, &later_peak);
    printf("liquid caustics: plain %.3f (peak %.3f), caustic %.3f (peak %.3f), later %.3f\n", plain, plain_peak, lit, lit_peak, later);
    ok = ok && plain_peak - plain < .01f && lit > plain + .02f &&
         lit_peak > plain_peak + .5f && fabsf(later - lit) > .001f;
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

static bool read_frame(CF_Canvas canvas, CF_Pixel *pixels)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    bool ok = readback.id != 0;
    if (ok) {
        while (!cf_readback_ready(readback)) {
        }
        int bytes = PIXELS * (int)sizeof *pixels;
        ok = cf_readback_size(readback) == bytes &&
             cf_readback_data(readback, pixels, bytes) == bytes;
        cf_destroy_readback(readback);
    }
    return ok;
}

/* Brightest red in rows [from, to) of a frame. */
static int brightest(const CF_Pixel *pixels, int from, int to)
{
    int best = 0;
    for (int i = from * SIDE; i < to * SIDE; i++) {
        best = pixels[i].colors.r > best ? pixels[i].colors.r : best;
    }
    return best;
}

/* A liquid surface mirrors the bright bar just above it below the surface;
 * without liquid reflection the frame passes through unchanged. */
static bool liquid_reflection(render_material_raster *r)
{
    material_fluid *fluid = material_fluid_new(10, 256);
    render_fluid_raster *draw = render_fluid_raster_new();
    for (int y = 12; y <= 30; y += 3) {
        for (int x = -30; x <= 30; x += 3) {
            material_fluid_add(
                fluid,
                (fluid_particle){ .position = { (float)x, (float)y },
                                  .mass = 1,
                                  .radius = 6,
                                  .kind = FLUID_LIQUID,
                                  .color = 0x2080a0ffu }
            );
        }
    }
    render_material_view view = { .min_x = -32,
                                  .max_y = 32,
                                  .width = SIDE,
                                  .height = SIDE,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    CF_Canvas source = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    render_reflections *reflections = render_reflections_new();
    CF_Pixel plain[PIXELS], mirrored[PIXELS];
    bool ok = draw != NULL;
    for (int pass = 0; ok && pass < 2; pass++) {
        cf_app_update(NULL);
        ok = render_material_raster_draw(r, NULL, 0, view) &&
             render_fluid_raster_draw(draw, fluid, render_material_raster_canvas(r), view, SIDE, SIDE);
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
        cf_draw_push_color(cf_make_color_rgba_f(1, .2f, .2f, 1));
        cf_draw_box_fill((CF_Aabb){ { -20, 2 }, { 20, 5 } }, 0);
        cf_draw_pop_color();
        cf_canvas_set_clear_color(source, cf_make_color_rgba_f(0, 0, 0, 1));
        cf_render_to(source, true);
        render_reflection_frame frame = {
            .source = source,
            .destination = render_reflections_canvas(reflections, SIDE, SIDE),
            .view = view,
            .normal = render_material_raster_textures(r).normal,
            .liquid = pass ? 1 : 0
        };
        ok = ok && render_reflections_draw(reflections, &frame);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        ok = ok && read_frame(frame.destination, pass ? mirrored : plain);
    }
    /* Rows 32 and below are under the surface, which lies near world y -6. */
    int under = brightest(plain, 40, 60), reflected = brightest(mirrored, 40, 60);
    int bar = brightest(plain, 26, 31), bar_after = brightest(mirrored, 26, 31);
    printf("liquid reflection: under the surface %d -> %d, bar %d -> %d\n", under, reflected, bar, bar_after);
    ok = ok && under < 20 && reflected > 60 && bar == bar_after;
    render_reflections_free(reflections);
    cf_destroy_canvas(source);
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

/* Two trails become two ribbons of at most eight capsules that widen and
 * brighten toward their heads and emit their color. */
static bool trails(render_material_raster *r)
{
    arena_trail_segment segments[30];
    for (int i = 0; i < 30; i++) {
        int t = i < 20 ? i : i - 20, n = i < 20 ? 20 : 10;
        float x = -28 + 2.5f * t, fade = (t + 1.0f) / n, y = i < 20 ? 10 : -10;
        segments[i] = (arena_trail_segment){ .from = { x, 0, y },
                                             .to = { x + 2.5f, 0, y },
                                             .color = { 1, .5f, .2f, fade * fade },
                                             .width = 6 * (.15f + .85f * fade),
                                             .emission = 2 };
    }
    render_material_entity *entities = NULL;
    render_trail_2d_append(segments, 30, .5f, 0, &entities);
    bool ok = acount(entities) == 16 && entities[0].radius < entities[7].radius &&
              entities[0].scene[3] < entities[7].scene[3] &&
              fabsf(entities[7].emission[0] - 2) < 1e-6f &&
              entities[0].pos[1] == -10 && entities[8].pos[1] == 10;
    float head[4] = { 0 }, empty[4] = { 0 };
    ok = ok && raster(r, entities, acount(entities)) &&
         texel(render_material_raster_textures(r).source, 20, 42, head, SIDE) &&
         texel(render_material_raster_textures(r).source, 20, 32, empty, SIDE);
    printf(
        "trails: %d capsules, head emission %.2f, between %.2f\n",
        acount(entities),
        head[0],
        empty[0]
    );
    ok = ok && head[0] > .5f && empty[0] == 0;
    afree(entities);
    return ok;
}

bool test_material_effects()
{
    render_material_raster *r = render_material_raster_new();
    bool ok = r && render_material_raster_resize(r, SIDE, SIDE) && hit_flash(r);
    ok &= backdrops();
    ok &= caustics(r);
    ok &= glass(r);
    ok &= liquid_reflection(r);
    ok &= trails(r);
    render_material_raster_free(r);
    printf("material effects %s: hit flash, backdrops, caustics, glass, liquid reflection, trails\n", ok ? "PASS" : "FAIL");
    return ok;
}
