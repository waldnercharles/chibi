/* Not built: the HRC cases still to port to test/render/radiance.c
 * (roadmap L4): material pixels, fluids and reflections, and the externs below
 * from the other unbuilt render sources. */
#include "render/radiance.h"

#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"
#include "platform/cute.h"
#include "render/fluid_raster.h"
#include "render/material_raster.h"
#include "render/reflections.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "runtime/component_catalog.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static bool read_pixels(CF_Canvas canvas, CF_Pixel *pixels, int count)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return false;
    }
    while (!cf_readback_ready(readback)) {
    }
    int size = count * (int)sizeof *pixels;
    bool ok = cf_readback_size(readback) == size &&
              cf_readback_data(readback, pixels, size) == size;
    cf_destroy_readback(readback);
    return ok;
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

static render_material_entity emitter()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { -28, 0 },
        .extent = { 10, 10 },
        .shape_type = RENDER_SHAPE_CIRCLE,
        .radius = 8,
        .aa = 1,
        .border_thickness = 2,
        .scene = { 0, 1, 0, 1 },
        .emission = { 0, 0.08f, 0, 1 },
        .border_scene = { 1, 0, 0, 1 },
        .border_emission = { 0.2f, 0, 0, 1 },
    };
    return entity;
}

static render_material_entity wall()
{
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .extent = { 5, 66 },
        .shape_type = RENDER_SHAPE_BOX,
        .aa = 1,
        .points = { 4, 64 },
        .scene = { 0, 0, 0, 1 },
        .absorption = { 1, 1, 1, 1 },
    };
    return entity;
}

static bool read_hdr_texture(CF_Texture texture, uint16_t *pixels, int count)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas canvas = cf_make_canvas(params);
    if (!canvas.id) {
        return false;
    }
    CF_Readback readback = cf_canvas_readback(canvas);
    bool ok = readback.id != 0;
    if (ok) {
        while (!cf_readback_ready(readback)) {
        }
        int bytes = count * 4 * (int)sizeof *pixels;
        ok = cf_readback_size(readback) == bytes &&
             cf_readback_data(readback, pixels, bytes) == bytes;
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

static bool material_parameter_writes(const behavior_asset *material, int id)
{
    ecs_world *world = ecs_world_new();
    bool ok = ecs_world_register_component(world, &GAME_COMPONENT_GameShader, NULL) ==
              ECS_OK;
    ecs_entity first = ecs_world_create_entity(world),
               second = ecs_world_create_entity(world);
    GameShader binding = { .material = material->name,
                           .parameters = "(array)Tint 0 1 0\n" };
    ok &= ecs_world_record_add(world, first, GAME_COMPONENT_GameShader.id, &binding, 0) ==
          ECS_OK;
    ok &= ecs_world_record_add(world, second, GAME_COMPONENT_GameShader.id, &binding, 0) ==
          ECS_OK;
    ok &= ecs_world_apply_commands(world) == ECS_OK;
    behavior_asset *behavior = behavior_asset_new("Material parameter writer");
    graph *g = behavior->graph;
    node *event = graph_add_node(g, &NODE_Event);
    ok &= graph_set_event_name(g, event, "Go") == 0;
    node *write = graph_add_node(g, registry_node_by_name("Set Material Parameter"));
    pin_value target = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(first) };
    pin_value name = { .type = PIN_STR, .str = "Tint" };
    float blue[3] = { 0, 0, 1 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = blue };
    ok &= graph_set_pin_default(g, write, 0, 1, &target) == 0;
    ok &= graph_set_pin_default(g, write, 0, 2, &name) == 0;
    ok &= graph_set_pin_type(g, write, 0, 3, PIN_STRUCT, "ShaderVec3") == 0;
    ok &= graph_set_pin_default(g, write, 0, 3, &value) == 0;
    ok &= graph_connect_exec(g, event, 0, write, 0, 0) == 0;
    behavior_instance *instance = behavior_instance_create(behavior, NULL, NULL, NULL);
    ok &= instance &&
          behavior_instance_bind_service(instance, "ecs.world", world) == 0;
    flow_error error;
    flow_run *run = instance
                      ? behavior_instance_dispatch(instance, "Go", (flow_inputs){ 0 }, &error)
                      : NULL;
    ok &= run && flow_run_status(run) == FLOW_COMPLETED;
    float a[32][4], b[32][4];
    render_shader_parameters(id, world, first, binding.parameters, a);
    render_shader_parameters(id, world, second, binding.parameters, b);
    ok &= a[0][2] == 1 && a[0][1] == 0 && b[0][1] == 1 && b[0][2] == 0;
    render_shader_clear_world(world);
    render_shader_parameters(id, world, first, binding.parameters, a);
    ok &= a[0][1] == 1;
    char edited[] = "(array)Tint 1 0 0\n";
    render_shader_parameters(id, world, second, edited, b);
    ok &= b[0][0] == 1 && b[0][1] == 0;
    memcpy(edited, "(array)Tint 0 0 1\n", sizeof edited);
    render_shader_parameters(id, world, second, edited, b);
    ok &= b[0][0] == 0 && b[0][2] == 1;
    render_shader_parameters(id, world, first, binding.parameters, a);
    ok &= a[0][1] == 1 && a[0][2] == 0;
    behavior_instance_destroy(instance);
    behavior_asset_free(behavior);
    ecs_world_free(world);
    return ok;
}

static bool scene_pixels(render_material_raster *raster, uint16_t *pixels, int count)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = render_material_raster_textures(raster).scene;
    CF_Canvas canvas = cf_make_canvas(params);
    CF_Readback readback = cf_canvas_readback(canvas);
    bool ok = readback.id != 0;
    if (ok) {
        while (!cf_readback_ready(readback)) {
        }
        ok = cf_readback_data(readback, pixels, count * 8) == count * 8;
        cf_destroy_readback(readback);
    }
    cf_destroy_canvas(canvas);
    return ok;
}

/* A world-space material follows an entity's laid-out frame: red where the
 * sampled world X is positive. */
static bool material_frame_pixels(render_material_raster *raster)
{
    behavior_asset *material = behavior_asset_new("GPU material frame");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *world = graph_add_node(g, registry_node_by_name("Shader WorldPosition"));
    node *x = graph_add_node(g, registry_node_by_name("Shader X"));
    node *step = graph_add_node(g, registry_node_by_name("Shader Step"));
    node *color = graph_add_node(g, registry_node_by_name("Shader Vector3"));
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    bool ok = graph_set_pin_default(g, output, 0, 6, &unlit) == 0;
    ok &= graph_connect_data(g, world, 0, x, 0) == 0;
    ok &= graph_connect_data(g, x, 0, step, 0) == 0;
    ok &= graph_connect_data(g, step, 0, color, 0) == 0;
    ok &= graph_connect_data(g, color, 0, output, 0) == 0;
    shader_error error;
    ok &= render_shader_upsert(material, &error);
    if (!ok) {
        fprintf(stderr, "material frame shader: %s\n", error.message);
    }
    render_material_entity entity = emitter();
    entity.pos[0] = entity.pos[1] = 0;
    entity.extent[0] = entity.extent[1] = 8;
    entity.radius = 6;
    entity.border_thickness = 0;
    entity.reserved[0] = (float)render_shader_find(material->name);
    const float frames[3][4] = { { 0 }, { 0, 0, -1, 0 }, { 0, 0, 0, 1 } };
    /* Expected red at left, right, top and bottom samples. */
    const bool red[3][4] = { { 0, 1, 1, 1 }, { 1, 0, 0, 0 }, { 1, 1, 0, 1 } };
    const int samples[4] = { 8 * 16 + 4, 8 * 16 + 12, 4 * 16 + 8, 12 * 16 + 8 };
    ok &= render_material_raster_resize(raster, 16, 16);
    for (int f = 0; ok && f < 3; f++) {
        memcpy(entity.material_frame, frames[f], sizeof entity.material_frame);
        cf_app_update(NULL);
        ok &= render_material_raster_draw(
            raster,
            &entity,
            1,
            (render_material_view){ -8, 8, 16, 16, 0, 0, 1, 1 }
        );
        cf_app_draw_onto_screen(false);
        uint16_t pixels[16 * 16 * 4];
        ok &= scene_pixels(raster, pixels, 16 * 16);
        for (int i = 0; ok && i < 4; i++) {
            /* Unframed entities sample the pixel itself; the first case
             * checks left and right only. */
            if (f == 0 && i >= 2) {
                continue;
            }
            bool is_red = half_value(pixels[samples[i] * 4]) > .5f;
            if (is_red != red[f][i]) {
                fprintf(stderr, "material frame %d sample %d: red %d\n", f, i, is_red);
                ok = false;
            }
        }
    }
    behavior_asset_free(material);
    return ok;
}

static bool graph_material_pixels(render_material_raster *raster)
{
    behavior_asset *material = behavior_asset_new("GPU material regression");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(material->graph, false);
    float color[3] = { 1, 0, 0 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = color };
    node *parameter = graph_add_node(material->graph, registry_node_by_name("Shader Parameter"));
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    bool ok = graph_set_pin_default(material->graph, output, 0, 6, &unlit) == 0;
    ok &= graph_set_node_reference(material->graph, parameter, 0, "Tint") == 0;
    ok &= graph_set_pin_type(material->graph, parameter, 1, 0, PIN_STRUCT, "ShaderVec3") ==
          0;
    ok &= graph_set_pin_default(material->graph, parameter, 1, 0, &value) == 0;
    ok &= graph_connect_data(material->graph, parameter, 0, output, 0) == 0;
    shader_error error;
    ok &= render_shader_upsert(material, &error);
    int id = render_shader_find(material->name);
    render_material_entity entity = emitter();
    entity.pos[0] = 0;
    entity.pos[1] = 0;
    entity.extent[0] = 8;
    entity.extent[1] = 8;
    entity.shape_type = RENDER_SHAPE_CIRCLE;
    entity.radius = 4;
    entity.reserved[0] = (float)id;
    render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, entity.material_parameters);
    ok &= material_parameter_writes(material, id);
    behavior_asset *shape = behavior_asset_new("GPU shape regression");
    shape->domain = BEHAVIOR_DOMAIN_SHAPE;
    shape->bounds[0] = shape->bounds[1] = 8;
    node *shape_output = shader_graph_add_output(shape->graph, true);
    node *position = graph_add_node(shape->graph, registry_node_by_name("Shader Position"));
    node *circle = graph_add_node(shape->graph, registry_node_by_name("Shader Circle"));
    pin_value radius = { .type = PIN_F32, .f = 4 };
    ok &= graph_set_pin_default(shape->graph, circle, 0, 1, &radius) == 0;
    ok &= graph_connect_data(shape->graph, position, 0, circle, 0) == 0;
    ok &= graph_connect_data(shape->graph, circle, 0, shape_output, 0) == 0;
    ok &= render_shader_upsert(shape, &error);
    entity.reserved[1] = (float)render_shader_find(shape->name);
    entity.shape_type = RENDER_SHAPE_BOX;
    entity.points[0] = entity.points[1] = 8;
    GameShader shape_binding = { .shape = shape->name };
    ok &= render_shader_contains(
        &shape_binding,
        NULL,
        (ecs_entity){ 0 },
        (float[2]){ 0, 0 },
        (float[2]){ 0, 0 },
        0
    );
    ok &= !render_shader_contains(
        &shape_binding,
        NULL,
        (ecs_entity){ 0 },
        (float[2]){ 6, 0 },
        (float[2]){ 6, 0 },
        0
    );
    render_material_view view = { -8, 8, 16, 16 };
    ok &= render_material_raster_resize(raster, 16, 16);
    for (int pass = 0; ok && pass < 2; pass++) {
        if (pass) {
            color[0] = 0;
            color[2] = 1;
            ok &= graph_set_pin_default(material->graph, parameter, 1, 0, &value) == 0;
            ok &= render_shader_upsert(material, &error);
        }
        render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, entity.material_parameters);
        cf_app_update(NULL);
        ok &= render_material_raster_draw(raster, &entity, 1, view);
        cf_app_draw_onto_screen(false);
        CF_CanvasParams params = cf_canvas_defaults(0, 0);
        params.attach_target = render_material_raster_textures(raster).scene;
        CF_Canvas canvas = cf_make_canvas(params);
        CF_Readback readback = cf_canvas_readback(canvas);
        uint16_t pixels[16 * 16 * 4];
        ok &= readback.id != 0;
        if (readback.id) {
            while (!cf_readback_ready(readback)) {
            }
            ok &= cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
            int center = (8 * 16 + 8) * 4;
            ok &= half_value(pixels[center + (pass ? 2 : 0)]) > 0.9f;
            ok &= half_value(pixels[3]) < 0.01f;
            cf_destroy_readback(readback);
        }
        cf_destroy_canvas(canvas);
    }
    render_programs *fragment = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    int program = render_shader_fragment_prepare(fragment, &entity, &error);
    ok &= program != 0;
    if (program) {
        CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(16, 16));
        cf_app_update(NULL);
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, 16, 16));
        render_shader_fragment_draw(fragment, program, &entity, (CF_V2){ 1, 1 });
        cf_render_to(canvas, true);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        CF_Pixel pixels[256];
        ok &= read_pixels(canvas, pixels, 256);
        ok &= pixels[8 * 16 + 8].colors.b > 200 && pixels[0].colors.a < 5;
        cf_destroy_canvas(canvas);
    }
    render_programs_free(fragment);
    behavior_asset_free(shape);
    behavior_asset_free(material);
    printf("shader-material %s: graph GPU channels, hot replacement, coverage and fragment compilation\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* Game physics uses Y-down; the raster camera is Y-up. Exercise the public
 * fluid draw path at nonzero positions and after vertical camera movement. */
static bool fluid_alignment(render_material_raster *raster)
{
    material_fluid *fluid = material_fluid_new(10, 1);
    render_fluid_raster *draw = render_fluid_raster_new();
    material_fluid_add(
        fluid,
        (fluid_particle){ .position = { 17, 23 },
                          .mass = 1,
                          .radius = 5,
                          .color = 0xff0000ffu,
                          .emission = 1 }
    );
    bool ok = draw && render_material_raster_resize(raster, SIDE, SIDE);
    uint16_t pixels[PIXELS * 4];
    for (int offset = 0; offset <= 16 && ok; offset += 16) {
        cf_app_update(NULL);
        render_material_view view = { .min_x = -64,
                                      .max_y = 64 - offset,
                                      .width = SIDE,
                                      .height = SIDE,
                                      .crop_max_u = 1,
                                      .crop_max_v = 1 };
        ok = render_material_raster_draw(raster, NULL, 0, view);
        render_material_textures textures = render_material_raster_textures(raster);
        ok &= render_fluid_raster_draw(draw, fluid, render_material_raster_canvas(raster), view, SIDE, SIDE);
        cf_app_draw_onto_screen(false);
        ok &= read_hdr_texture(render_material_raster_textures(raster).source, pixels, PIXELS);
        float actual = half_value(pixels[4 * ((87 - offset) * SIDE + 81)]);
        float mirrored = half_value(pixels[4 * ((41 - offset) * SIDE + 81)]);
        printf("fluid Y alignment camera=%d: expected %.3f mirrored %.3f\n", offset, actual, mirrored);
        ok &= actual > .5f && mirrored < .01f;
    }
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

static bool fluid_surfaces(render_material_raster *raster)
{
    material_fluid *fluid = material_fluid_new(20, 2);
    render_fluid_raster *draw = render_fluid_raster_new();
    render_material_view view = { .min_x = -64,
                                  .max_y = 64,
                                  .width = SIDE,
                                  .height = SIDE,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    uint16_t pixels[PIXELS * 4];
    bool ok = draw != NULL;
    for (int stage = 0; stage < 9 && ok; stage++) {
        material_fluid_clear(fluid);
        fluid_particle p = { .position = { -6.5f, .5f },
                             .mass = 1,
                             .radius = 6,
                             .color = 0xffffffffu,
                             .emission = 1,
                             .style = { .blend = 4 } };
        if (stage == 1) {
            p.lifetime = 1;
            p.style.duration = 2;
            p.style.size_exponent = 1;
            p.style.end_color = p.color;
        }
        if (stage == 2) {
            p.position = (fluid_vec){ .5f, .5f };
            p.radius = 12;
            p.kind = FLUID_LIQUID;
            p.emission = 0;
            p.visibility = .06f;
            p.style = (fluid_style){ 0 };
        }
        if (stage == 3) {
            p.burning = true;
            p.glow = 1;
            p.style = (fluid_style){ 0 };
        }
        if (stage == 5) {
            p.position.x = -3.5f;
            p.radius = 12;
            p.emission = 0;
            p.style = (fluid_style){ 0 };
        }
        if (stage >= 6) {
            p.position = (fluid_vec){ .5f, .5f };
            p.radius = 12;
            p.emission = 0;
            p.absorption = 1;
            p.style = (fluid_style){ 0 };
            p.velocity = (fluid_vec){ stage == 7 ? 40 : 0, stage == 8 ? 40 : 0 };
        }
        if (stage != 4) {
            material_fluid_add(fluid, p);
        }
        if (stage != 2 && stage != 4 && stage < 6) {
            p.position.x = stage == 5 ? 4.5f : 6.5f;
            material_fluid_add(fluid, p);
        }
        cf_app_update(NULL);
        ok &= render_material_raster_draw(raster, NULL, 0, view);
        render_material_textures textures = render_material_raster_textures(raster);
        ok &= render_fluid_raster_draw(draw, fluid, render_material_raster_canvas(raster), view, SIDE, SIDE);
        cf_app_draw_onto_screen(false);
        render_material_textures with_fluid = render_material_raster_textures(raster);
        ok &= read_hdr_texture(
            stage == 2 || stage == 5 || stage == 6 ? with_fluid.scene
                                                   : with_fluid.source,
            pixels,
            PIXELS
        );
        float center = half_value(pixels[4 * (64 * SIDE + 64)]);
        float inside = half_value(pixels[4 * (64 * SIDE + 61)]);
        float outside = half_value(pixels[4 * (64 * SIDE + 71)]);
        printf("fluid surface stage=%d center=%.3f inside=%.3f outside=%.3f\n", stage, center, inside, outside);
        if (stage == 0) {
            ok &= center > .9f &&
                  inside > .9f; /* Solid bridge across separate circles. */
        } else if (stage == 1) {
            ok &= center < .01f; /* Authored shrinking removes that bridge. */
        } else if (stage == 2) {
            ok &= inside > .9f && outside < .01f; /* Crisp non-emissive liquid edge. */
            ok &= read_hdr_texture(with_fluid.source, pixels, PIXELS);
            ok &= half_value(pixels[4 * (64 * SIDE + 64)]) == 0;
            ok &= read_hdr_texture(render_material_raster_textures(raster).visibility, pixels, PIXELS);
            ok &= half_value(pixels[4 * (64 * SIDE + 64)]) > .01f;
        } else if (stage == 3) {
            /* Fuel burns as small separate cores; the exhaust bridge is unchanged. */
            ok &= center < .01f && half_value(pixels[4 * (64 * SIDE + 70)]) > .9f;
        } else if (stage == 4) {
            ok &= read_hdr_texture(render_material_raster_textures(raster).visibility, pixels, PIXELS);
            ok &= half_value(pixels[4 * (64 * SIDE + 64)]) == 0;
        } else if (stage == 5) {
            // A joined plume has no particle-sized holes or isolated bright dots.
            float low = 1, high = 0;
            for (int x = 61; x <= 67; x++) {
                float value = half_value(pixels[4 * (64 * SIDE + x)]);
                low = fminf(low, value);
                high = fmaxf(high, value);
            }
            printf("joined gas density: min %.3f max %.3f\n", low, high);
            ok &= low > .08f && high < low * 1.6f;
            ok &= half_value(pixels[4 * (64 * SIDE + 84)]) == 0;
        } else if (stage == 6) {
            // A solitary cold parcel fades instead of surviving as a bright bead.
            ok &= center < .04f;
        } else {
            // Wisp support crosses tile boundaries and rotates with game
            // velocity; optical depth is source's alpha.
            float horizontal = half_value(pixels[4 * (64 * SIDE + 81) + 3]);
            float vertical = half_value(pixels[4 * (81 * SIDE + 64) + 3]);
            ok &= stage == 7 ? horizontal > .02f && vertical == 0
                             : vertical > .02f && horizontal == 0;
        }
    }
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

static bool fluid_lighting(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    int trace_levels
)
{
    material_fluid *fluid = material_fluid_new(20, 16);
    render_fluid_raster *draw = render_fluid_raster_new();
    render_material_view view = { .min_x = -64,
                                  .max_y = 64,
                                  .width = SIDE,
                                  .height = SIDE,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.trace_levels = trace_levels;
    lighting.fog = 0;
    bool ok = draw && render_radiance_set_lighting(radiance, lighting);
    for (int kind = FLUID_GAS; kind <= FLUID_LIQUID && ok; kind++) {
        material_fluid_clear(fluid);
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                material_fluid_add(
                    fluid,
                    (fluid_particle){ .position = { 11 + 6 * x, -9 + 6 * y },
                                      .mass = 1,
                                      .radius = 10,
                                      .kind = kind,
                                      .color = 0xffffffffu,
                                      .absorption = .02f,
                                      .visibility = kind == FLUID_GAS ? .02f : 0 }
                );
            }
        }
        float color[4][3] = { 0 };
        for (int stage = 0; stage < 4 && ok; stage++) {
            render_material_entity solids[] = { emitter(), wall() };
            // Only the off-volume source emits. The fourth stage inserts a wall.
            for (int c = 0; c < 3; c++) {
                solids[0].emission[c] = solids[0].border_emission[c] = stage && c == (stage == 2 ? 2 : 1)
                                                                         ? .08f
                                                                         : 0;
            }
            render_radiance_reset(radiance);
            for (int frame = 0; frame < 8 && ok; frame++) {
                cf_app_update(NULL);
                ok &= render_material_raster_draw(raster, solids, stage == 3 ? 2 : 1, view);
                render_material_textures textures = render_material_raster_textures(raster);
                ok &= render_fluid_raster_draw(
                    draw,
                    fluid,
                    render_material_raster_canvas(raster),
                    view,
                    SIDE,
                    SIDE
                );
                render_radiance_inputs inputs = {
                    .material = render_material_raster_textures(raster),
                };
                cf_draw_push();
                cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
                ok &= render_radiance_draw(radiance, inputs, view, canvas);
                cf_draw_pop();
                cf_app_draw_onto_screen(false);
            }
            CF_Pixel pixels[PIXELS];
            ok &= read_pixels(canvas, pixels, PIXELS);
            if (!ok) {
                break;
            }
            for (int y = 62; y <= 66; y++) {
                for (int x = 82; x <= 86; x++) {
                    CF_Pixel p = pixels[y * SIDE + x];
                    color[stage][0] += p.colors.r / 25.f;
                    color[stage][1] += p.colors.g / 25.f;
                    color[stage][2] += p.colors.b / 25.f;
                }
            }
        }
        printf(
            "fluid lighting kind=%d: dark %.2f green %.2f blue %.2f shadow "
            "%.2f\n",
            kind,
            color[0][1],
            color[1][1],
            color[2][2],
            color[3][1]
        );
        ok &= color[1][1] > color[0][1] + 3 && color[2][2] > color[0][2] + 3;
        ok &= color[1][1] > color[1][2] + 3 && color[2][2] > color[2][1] + 3;
        ok &= color[3][1] < color[0][1] + (color[1][1] - color[0][1]) * .35f;
        uint16_t emission[PIXELS * 4];
        ok &= read_hdr_texture(render_material_raster_textures(raster).source, emission, PIXELS);
        int p = 4 * (64 * SIDE + 84);
        ok &= emission[p] == 0 && emission[p + 1] == 0 && emission[p + 2] == 0;
    }
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

static bool liquid_refraction(render_material_raster *raster, render_radiance *radiance, CF_Canvas canvas)
{
    material_fluid *fluid = material_fluid_new(32, 1);
    render_fluid_raster *draw = render_fluid_raster_new();
    CF_Texture background = cf_make_texture(cf_texture_defaults(SIDE, SIDE));
    CF_Pixel pattern[PIXELS], images[2][PIXELS];
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            bool stripe = (x / 3) % 2;
            pattern[y * SIDE + x] = (CF_Pixel){
                .colors = { stripe ? 180 : 15, 0, stripe ? 15 : 180, 255 }
            };
        }
    }
    cf_texture_update(background, pattern, sizeof pattern);
    render_material_view view = { .min_x = -64,
                                  .max_y = 64,
                                  .width = SIDE,
                                  .height = SIDE,
                                  .crop_max_u = 1,
                                  .crop_max_v = 1 };
    bool ok = draw && background.id;
    for (int pass = 0; pass < 2 && ok; pass++) {
        material_fluid_clear(fluid);
        material_fluid_add(
            fluid,
            (fluid_particle){ .position = { .5f, .5f },
                              .mass = 1,
                              .radius = 30,
                              .kind = FLUID_LIQUID,
                              .color = 0x8080ff99,
                              .refraction = pass ? 12 : 0 }
        );
        render_radiance_reset(radiance);
        cf_app_update(NULL);
        ok &= render_material_raster_draw(raster, NULL, 0, view);
        render_material_textures textures = render_material_raster_textures(raster);
        ok &= render_fluid_raster_draw(draw, fluid, render_material_raster_canvas(raster), view, SIDE, SIDE);
        render_radiance_inputs inputs = { .material = render_material_raster_textures(raster),
                                          .background = background };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
        ok &= render_radiance_draw(radiance, inputs, view, canvas);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        ok &= read_pixels(canvas, images[pass], PIXELS);
    }
    if (ok) {
        int changed = 0, outside = 0;
        for (int y = 0; y < SIDE; y++) {
            for (int x = 0; x < SIDE; x++) {
                int d = abs(
                    (int)images[0][y * SIDE + x].colors.r -
                    images[1][y * SIDE + x].colors.r
                );
                if (x > 50 && x < 78 && y > 50 && y < 78) {
                    changed += d > 8;
                }
                if (x < 45 || x > 83 || y < 45 || y > 83) {
                    outside += d > 2;
                }
            }
        }
        printf("liquid refraction: changed interior pixels %d outside %d\n", changed, outside);
        ok &= changed > 20 && outside == 0;
    }
    cf_destroy_texture(background);
    render_fluid_raster_free(draw);
    material_fluid_free(fluid);
    return ok;
}

static bool reflection_pixels()
{
    CF_Canvas source = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    render_reflections *r = render_reflections_new();
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, 1));
    cf_draw_box_fill((CF_Aabb){ { -30, 8 }, { -18, 16 } }, 0);
    cf_draw_pop_color();
    cf_draw_push_color(cf_make_color_rgba_f(0, 1, 0, 1));
    cf_draw_box_fill((CF_Aabb){ { -30, -16 }, { -18, -8 } }, 0);
    cf_draw_pop_color();
    cf_canvas_set_clear_color(source, cf_make_color_rgba_f(0, 0, .1f, 1));
    cf_render_to(source, true);
    render_reflection surface = {
        .corners = { { 4, -24 }, { 40, -24 }, { 40, 24 }, { 4, 24 } },
        .plane = { 0, 0, 0, 1 },
        .tint = { 1, 1, 1, 1 },
        .strength = 1
    };
    render_view_2d view = { .min_x = -64, .max_y = 64, .width = SIDE, .height = SIDE };
    CF_Canvas output = render_reflections_draw(r, source, SIDE, SIDE, view, &surface, 1);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    CF_Pixel pixels[PIXELS];
    bool ok = output.id && read_pixels(output, pixels, PIXELS);
    if (ok) {
        CF_Pixel red = pixels[52 * SIDE + 88], green = pixels[76 * SIDE + 88];
        printf(
            "crystal reflection detail: upper %u,%u lower %u,%u\n",
            red.colors.r,
            red.colors.g,
            green.colors.r,
            green.colors.g
        );
        ok = red.colors.r > 180 && red.colors.g < 30 && green.colors.g > 180 &&
             green.colors.r < 30;
    }
    render_reflections_free(r);
    cf_destroy_canvas(source);
    return ok;
}

/* The host initializes Cute and mounts/configures render/shaders before this call. */
int test_radiance()
{
    extern bool test_material_sampling();
    extern bool test_material_raster_uploads();
    extern bool test_mesh_material();
    extern bool test_sdf_material();
    extern bool test_sdf_quality();
    extern bool test_sdf_visibility();
    extern bool test_camera_orbit();
    extern bool test_camera_centered_orbit();
    extern bool test_camera_emitter_orbit();
    extern bool test_camera_plane();
    extern bool test_sdf_spatial();
    extern int test_shader_program_gpu();
    extern int test_shader_graph_gpu();
    render_material_raster *raster = render_material_raster_new();
    render_radiance *radiance = render_radiance_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    bool ok = raster && radiance && canvas.id;
    if (!ok) {
        fprintf(
            stderr,
            "radiance initialization: raster %d, transport %d, canvas %d\n",
            raster != NULL,
            radiance != NULL,
            canvas.id != 0
        );
    }
    if (ok) {
        ok = render_material_raster_resize(raster, SIDE, SIDE) &&
             render_radiance_resize(radiance, SIDE, SIDE);
        if (!ok) {
            fprintf(stderr, "radiance target resize failed\n");
        }
    }
    if (ok) {
        ok = graph_material_pixels(raster) && material_frame_pixels(raster);
    }
    ok &= fluid_alignment(raster);
    ok &= fluid_surfaces(raster);
    cf_destroy_canvas(canvas);
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    ok &= canvas.id && render_radiance_resize(radiance, SIDE, SIDE);
    for (int levels = 1; levels <= 3; levels += 2) {
        ok &= fluid_lighting(raster, radiance, canvas, levels);
    }
    ok &= liquid_refraction(raster, radiance, canvas);
    ok &= reflection_pixels();
    ok &= test_material_sampling();
    ok &= test_material_raster_uploads();
    ok &= test_mesh_material();
    ok &= test_sdf_material();
    ok &= test_sdf_quality();
    ok &= test_sdf_visibility();
    ok &= test_camera_orbit();
    ok &= test_camera_centered_orbit();
    ok &= test_camera_emitter_orbit();
    ok &= test_camera_plane();
    ok &= test_sdf_spatial();
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    // This test clears the shared shader catalog; run after other renderer
    // resources are released, while the caller still owns the GPU context.
    ok &= test_shader_program_gpu() == 0;
    if (cf_query_backend() == CF_BACKEND_TYPE_GLES3) {
        puts("shader math/gradient compute tests SKIP: GLES 3.0 has no compute shaders");
    } else {
        ok &= test_shader_graph_gpu() == 0;
    }
    printf("radiance %s: HRC transport, surface channels, absorption, reset and resize\n", ok ? "PASS" : "FAIL");
    return !ok;
}

int radiance_run(const char *program, bool gles)
{
    CF_Result result = cf_make_app(
        "Material alignment regression",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT |
            (gles ? CF_APP_OPTIONS_GFX_OPENGL_BIT : 0),
        program
    );
    if (cf_is_error(result) && gles) {
        /* Desktop GLES needs a driver such as Mesa's. On macOS, put ANGLE's
           libEGL.dylib and libGLESv2.dylib on DYLD_FALLBACK_LIBRARY_PATH. */
        printf("radiance SKIP: no GLES window: %s\n", result.details);
        return 0;
    }
    if (cf_is_error(result)) {
        fprintf(stderr, "%s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    int failed = test_radiance();
    cf_destroy_app();
    return failed;
}
