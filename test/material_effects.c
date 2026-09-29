#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/backdrop_2d.h"
#include "render/fluid_raster.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

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

bool test_material_effects()
{
    render_material_raster *r = render_material_raster_new();
    bool ok = r && render_material_raster_resize(r, SIDE, SIDE) && hit_flash(r);
    ok &= backdrops();
    ok &= caustics(r);
    render_material_raster_free(r);
    printf("material effects %s: hit flash, backdrops, caustics\n", ok ? "PASS" : "FAIL");
    return ok;
}
