#include "render/radiance.h"

#include "graph/runtime/instance.h"
#include "platform/cute.h"
#include "render/fluid_raster.h"
#include "render/fullscreen.h"
#include "render/gpu_timing.h"
#include "render/material_raster.h"
#include "render/reflections.h"
#include "render/scene_renderer.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "runtime/generated.h"
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

static bool material_pixels(render_material_textures textures)
{
    CF_Texture inputs[] = { textures.source, textures.source, textures.normal };
    uint16_t pixels[PIXELS * 4];
    for (int i = 0; i < 3; i++) {
        CF_CanvasParams params = cf_canvas_defaults(0, 0);
        params.attach_target = inputs[i];
        CF_Canvas canvas = cf_make_canvas(params);
        if (!canvas.id) {
            return false;
        }
        CF_Readback readback = cf_canvas_readback(canvas);
        bool ok = readback.id != 0;
        if (ok) {
            while (!cf_readback_ready(readback)) {
            }
            int size = (int)sizeof pixels;
            ok = cf_readback_size(readback) == size &&
                 cf_readback_data(readback, pixels, size) == size;
            cf_destroy_readback(readback);
        }
        cf_destroy_canvas(canvas);
        if (!ok) {
            return false;
        }
        int sample = 4 * (64 * SIDE + (i ? 64 : 36));
        if (i == 0) {
            if (fabsf(half_value(pixels[sample + 1]) - 0.08f) > 0.002f) {
                return false;
            }
            sample = 4 * (64 * SIDE + 43);
            if (fabsf(half_value(pixels[sample]) - 0.2f) > 0.002f) {
                return false;
            }
        } else if (i == 1) {
            /* Optical depth is the mean absorption. */
            if (fabsf(half_value(pixels[sample + 3]) - 1) > 0.002f) {
                return false;
            }
        } else {
            /* A plain solid carries no bump tilt, edge light or gloss. */
            sample = 4 * (64 * SIDE + 43);
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(half_value(pixels[sample + channel])) > 0.002f) {
                    return false;
                }
            }
        }
    }
    return true;
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

/* Renders size x size pixels covering world x world units around the origin. */
static bool draw_frames_scaled(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    float world,
    int frames,
    bool reset
)
{
    assert(frames > 0);
    if (reset) {
        render_radiance_reset(radiance);
    }
    for (int frame = 0; frame < frames; frame++) {
        cf_app_update(NULL);
        if (!render_material_raster_draw(
                raster,
                entities,
                count,
                (render_material_view){ .min_x = -world * 0.5f,
                                        .max_y = world * 0.5f,
                                        .width = world,
                                        .height = world,
                                        .crop_min_u = 0,
                                        .crop_min_v = 0,
                                        .crop_max_u = 1,
                                        .crop_max_v = 1 }
            )) {
            fprintf(stderr, "radiance material raster frame %d: %s\n", frame, render_material_raster_error(raster));
            cf_app_draw_onto_screen(false);
            return false;
        }
        render_material_textures textures = render_material_raster_textures(raster);
        render_radiance_inputs inputs = { .material = textures };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, size, size));
        bool ok = render_radiance_draw(
            radiance,
            inputs,
            (render_view_2d){ .min_x = -world * 0.5f,
                              .max_y = world * 0.5f,
                              .width = world,
                              .height = world,
                              .crop_min_u = 0,
                              .crop_min_v = 0,
                              .crop_max_u = 1,
                              .crop_max_v = 1 },
            canvas
        );
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        if (!ok) {
            fprintf(stderr, "radiance transport frame %d failed\n", frame);
            return false;
        }
    }
    return true;
}

static bool draw_scaled(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    float world,
    int frames
)
{
    return draw_frames_scaled(raster, radiance, canvas, entities, count, size, world, frames, true);
}

static bool draw_view_once(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    render_view_2d view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(raster, entities, count, view);
    render_radiance_inputs inputs = { .material = render_material_raster_textures(raster) };
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    if (ok) {
        ok = render_radiance_draw(radiance, inputs, view, canvas);
    }
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    return ok;
}

static bool solve_scaled(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    float world,
    int frames,
    CF_Pixel *pixels
)
{
    return draw_scaled(raster, radiance, canvas, entities, count, size, world, frames) &&
           read_pixels(canvas, pixels, size * size);
}

static bool solve(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    int frames,
    CF_Pixel *pixels
)
{
    return solve_scaled(raster, radiance, canvas, entities, count, size, (float)size, frames, pixels);
}

static double receiver_light(const CF_Pixel *pixels)
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

/* Coarse diffuse reconstruction must retain a subpixel emitter, respect a
 * thin opaque boundary, and resize with and without split luminance. */
static bool reconstruction_sampling(render_material_raster *raster)
{
    render_material_entity spark = emitter();
    spark.radius = .8f;
    spark.extent[0] = spark.extent[1] = 2;
    spark.border_thickness = .5f;
    spark.emission[1] = .4f;
    spark.border_emission[0] = .5f;
    render_material_entity thin_wall = wall_with(1, .8f);
    thin_wall.points[0] = .8f;
    thin_wall.extent[0] = 1.8f;
    render_material_entity receiver = wall_with(0, .8f);
    receiver.pos[0] = 28;
    receiver.points[0] = 8;
    receiver.extent[0] = 9;

    render_radiance *radiance = render_radiance_new();
    CF_Canvas full_canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    CF_Canvas small_canvas = cf_make_canvas(cf_canvas_defaults(64, 64));
    bool ok = radiance && full_canvas.id && small_canvas.id &&
              render_material_raster_resize(raster, SIDE, SIDE);
    float reference_open = 0;
    for (int split = 0; split <= 1 && ok; split++) {
        for (int reconstruction = 0; reconstruction <= 1 && ok; reconstruction++) {
            GameHRC2D lighting = game_hrc2d_defaults();
            lighting.adaptive = false;
            lighting.feedback = false;
            lighting.fog = 0;
            /* Color at 0.5 either way; split solves brightness at 1. */
            lighting.resolution = split ? 1 : .5f;
            lighting.color_resolution = split ? GAME_HRC_COLOR_HALF : GAME_HRC_COLOR_FULL;
            lighting.gather_resolution = reconstruction ? GAME_HRC_GATHER_HALF
                                                        : GAME_HRC_GATHER_FULL;
            ok = render_radiance_set_lighting(radiance, lighting) &&
                 render_radiance_resize(radiance, SIDE, SIDE);
            CF_Pixel open[PIXELS], blocked[PIXELS];
            if (ok) {
                render_material_entity entities[] = { spark, receiver };
                ok = solve(raster, radiance, full_canvas, entities, 2, SIDE, 4, open);
            }
            if (ok) {
                render_material_entity entities[] = { spark, thin_wall, receiver };
                ok = solve(raster, radiance, full_canvas, entities, 3, SIDE, 4, blocked);
            }
            if (!ok) {
                break;
            }

            double open_light = receiver_light(open);
            double blocked_light = receiver_light(blocked);
            if (!split && !reconstruction) {
                reference_open = (float)open_light;
            }
            printf(
                "radiance reconstruction split=%d gather half=%d: open %.3f "
                "blocked "
                "%.3f\n",
                split,
                reconstruction,
                open_light,
                blocked_light
            );
            ok = open_light > 0.01 && blocked_light < open_light * .98;
            if (reconstruction) {
                ok &= open_light > reference_open * .2 &&
                      open_light < reference_open * 4;
            }
        }
    }
    if (ok) {
        GameHRC2D lighting = game_hrc2d_defaults();
        lighting.adaptive = false;
        lighting.feedback = false;
        lighting.fog = 0;
        lighting.resolution = 1;
        lighting.color_resolution = GAME_HRC_COLOR_HALF;
        lighting.gather_resolution = GAME_HRC_GATHER_HALF;
        CF_Pixel small_pixels[64 * 64];
        render_material_entity small_entities[] = { spark, receiver };
        ok = render_radiance_set_lighting(radiance, lighting) &&
             render_material_raster_resize(raster, 64, 64) &&
             render_radiance_resize(radiance, 64, 64) &&
             solve(raster, radiance, small_canvas, small_entities, 2, 64, 2, small_pixels) &&
             small_pixels[32 * 64 + 60].colors.g > 0.01f;
    }
    if (small_canvas.id) {
        cf_destroy_canvas(small_canvas);
    }
    if (full_canvas.id) {
        cf_destroy_canvas(full_canvas);
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    render_material_raster_resize(raster, SIDE, SIDE);
    if (!ok) {
        fprintf(stderr, "HRC reduced reconstruction lost source light or crossed an occluder\n");
    }
    return ok;
}

static bool save_capture(const CF_Pixel *pixels)
{
    const char *path = getenv("CHIBI_RADIANCE_SCREENSHOT");
    if (!path) {
        return true;
    }
    CF_Image image = { SIDE, SIDE, (CF_Pixel *)pixels };
    void *data = NULL;
    int size = 0;
    CF_Result result = cf_image_save_png_to_memory(&image, &data, &size);
    if (cf_is_error(result)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    bool ok = file && fwrite(data, 1, (size_t)size, file) == (size_t)size;
    if (file) {
        fclose(file);
    }
    cf_free(data);
    return ok;
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
                           .parameters = "(array)Tint 0 1 0; " };
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
    char edited[] = "(array)Tint 1 0 0; ";
    render_shader_parameters(id, world, second, edited, b);
    ok &= b[0][0] == 1 && b[0][1] == 0;
    memcpy(edited, "(array)Tint 0 0 1; ", sizeof edited);
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

static bool silhouette_lighting(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    int trace_levels
)
{
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = false;
    lighting.fog = 0;
    lighting.trace_levels = trace_levels;
    lighting.surface_radius = 12;
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
    pieces[2] = emitter();
    pieces[2].pos[0] = -38;
    CF_Pixel pixels[PIXELS];
    bool ok = render_radiance_set_lighting(radiance, lighting) &&
              solve(raster, radiance, canvas, pieces, 2, SIDE, 12, pixels);
    if (!ok) {
        return false;
    }
    int dark = pixels[64 * SIDE + 41].colors.g;
    ok &= dark < 2;
    uint16_t emission[PIXELS * 4];
    ok &= read_hdr_texture(render_material_raster_textures(raster).source, emission, PIXELS);
    for (int i = 0; i < PIXELS; i++) {
        ok &= emission[4 * i] == 0 && emission[4 * i + 1] == 0 &&
              emission[4 * i + 2] == 0;
    }
    ok &= solve(raster, radiance, canvas, pieces, 3, SIDE, 12, pixels);
    if (!ok) {
        return false;
    }
    int edge = pixels[64 * SIDE + 41].colors.g;
    int seam = pixels[64 * SIDE + 64].colors.g;
    int far_edge = pixels[64 * SIDE + 86].colors.g;
    printf("silhouette lighting: unlit %d lit %d seam %d far edge %d\n", dark, edge, seam, far_edge);
    return ok && edge > 10 && seam < 2 && far_edge < 2;
}

/* A flat gray material whose normal tilts sideways (x) and up (z). */
static behavior_asset *tilted_material_xz(const char *name, float tilt_x, float tilt_z, float roughness)
{
    behavior_asset *material = behavior_asset_new(name);
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(material->graph, false);
    float base[3] = { .6f, .6f, .6f }, normal[3] = { tilt_x, 1, tilt_z };
    pin_value color = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = base };
    pin_value bump = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = normal };
    pin_value rough = { .type = PIN_F32, .f = roughness };
    shader_error error;
    if (graph_set_pin_default(material->graph, output, 0, 0, &color) ||
        graph_set_pin_default(material->graph, output, 0, 2, &rough) ||
        graph_set_pin_default(material->graph, output, 0, 4, &bump) ||
        !render_shader_upsert(material, &error)) {
        behavior_asset_free(material);
        return NULL;
    }
    return material;
}

static behavior_asset *tilted_material(const char *name, float tilt, float roughness)
{
    return tilted_material_xz(name, tilt, 0, roughness);
}

static render_material_entity lit_wall(float half_width, float edge_light)
{
    render_material_entity entity = wall_with(1, .6f);
    entity.extent[0] = half_width + 1;
    entity.points[0] = half_width;
    entity.reserved[2] = edge_light;
    return entity;
}

static int green_at(const CF_Pixel *pixels, int x)
{
    return pixels[64 * SIDE + x].colors.g;
}

/* Material bump tilt and gloss reach the surface channels; light near a solid
 * reaches a world-space depth into it and lights relief facing its source. */
static bool surface_lighting(render_material_raster *raster, render_radiance *radiance, CF_Canvas canvas)
{
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = false;
    lighting.fog = 0;
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = 0;
    lighting.specular = 0;
    bool ok = render_radiance_set_lighting(radiance, lighting);
    const float tilts[] = { -.75f, 0, .75f };
    int face[3] = { 0 };
    CF_Pixel pixels[PIXELS];
    for (int i = 0; i < 3 && ok; i++) {
        behavior_asset *material = tilted_material(
            i ? "Surface tilt right" : "Surface tilt left",
            tilts[i],
            .25f
        );
        ok = material != NULL;
        if (!ok) {
            break;
        }
        render_material_entity scene[2] = { emitter(), lit_wall(6, 1.5f) };
        scene[1].reserved[0] = (float)render_shader_find(material->name);
        ok = solve(raster, radiance, canvas, scene, 2, SIDE, 12, pixels);
        if (ok && i == 0) {
            uint16_t normal[PIXELS * 4];
            ok = read_hdr_texture(render_material_raster_textures(raster).normal, normal, PIXELS);
            int center = 4 * (64 * SIDE + 64);
            ok &= fabsf(half_value(normal[center]) + .6f) < .01f &&
                  fabsf(half_value(normal[center + 1])) < .01f &&
                  fabsf(half_value(normal[center + 2]) - 1.5f) < .01f &&
                  fabsf(half_value(normal[center + 3]) - .75f) < .01f;
        }
        face[i] = green_at(pixels, 60);
        behavior_asset_free(material);
    }
    printf("surface relief: facing light %d flat %d facing away %d\n", face[0], face[1], face[2]);
    ok &= face[0] > face[1] + 8 && face[1] > face[2] + 8;

    /* The same picture drawn at twice the world scale lights half the depth
     * in pixels: the radius is in world units. */
    int depth[2] = { 0 };
    for (int zoom = 0; zoom < 2 && ok; zoom++) {
        float k = zoom ? 2.f : 1.f;
        render_material_entity scene[2] = { emitter(), lit_wall(40 * k, .4f) };
        scene[0].pos[0] = -55 * k;
        scene[0].radius *= k;
        scene[0].border_thickness *= k;
        scene[0].extent[0] *= k;
        scene[0].extent[1] *= k;
        scene[1].points[1] *= k;
        scene[1].extent[1] *= k;
        ok = solve_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE * k, 12, pixels);
        int face_x = SIDE / 2 - 40;
        int face = green_at(pixels, face_x + 1);
        for (int x = face_x + 1; x < SIDE && ok; x++) {
            if (green_at(pixels, x) * 4 < face) {
                depth[zoom] = x - face_x;
                break;
            }
        }
        ok &= face > 20;
    }
    printf("surface depth: %d px at one unit per pixel, %d px at two\n", depth[0], depth[1]);
    ok &= depth[0] >= 6 && depth[0] <= 18 && abs(2 * depth[1] - depth[0]) <= 3;

    /* Light below a ceiling: relief facing down is brighter than facing up. */
    int vertical[2] = { 0 };
    for (int i = 0; i < 2 && ok; i++) {
        behavior_asset *material = tilted_material_xz(
            i ? "Surface tilt up" : "Surface tilt down",
            0,
            i ? .75f : -.75f,
            .25f
        );
        ok = material != NULL;
        if (!ok) {
            break;
        }
        render_material_entity scene[2] = { emitter(), lit_wall(64, 1.5f) };
        scene[0].pos[0] = 0;
        scene[0].pos[1] = -30;
        scene[1].pos[1] = 20;
        scene[1].points[1] = 6;
        scene[1].extent[1] = 8;
        scene[1].reserved[0] = (float)render_shader_find(material->name);
        ok = solve(raster, radiance, canvas, scene, 2, SIDE, 12, pixels);
        vertical[i] = pixels[48 * SIDE + 64].colors.g;
        behavior_asset_free(material);
    }
    printf("surface relief below a ceiling: facing down %d facing up %d\n", vertical[0], vertical[1]);
    ok &= vertical[0] > vertical[1] + 8;
    return ok;
}

static bool surface_rgb(const uint16_t *pixels, float result[2])
{
    result[0] = result[1] = 0;
    for (int y = 62; y <= 65; y++) {
        for (int x = 62; x <= 65; x++) {
            int p = (y * SIDE + x) * 4;
            float red = half_value(pixels[p]), blue = half_value(pixels[p + 2]);
            if (!isfinite(red + blue)) {
                return false;
            }
            result[0] += red / 16;
            result[1] += blue / 16;
        }
    }
    return true;
}

/* Keep RGB attached to its incident direction: opposing colors must respond
 * independently to a banked normal, including when one path is occluded. */
static bool directional_surface_colors(render_material_raster *raster, GameHRCShadowMode shadow_mode)
{
    render_radiance *radiance = render_radiance_new();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.ambient = lighting.fog = lighting.shadow_floor = 0;
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.tone_mapping = 1;
    bool ok = canvas.id && radiance && render_radiance_resize(radiance, SIDE, SIDE) &&
              render_radiance_set_lighting(radiance, lighting);
    lighting.surface_shadow_mode = shadow_mode;
    ok &= render_radiance_set_lighting(radiance, lighting);
    float responses[3][2] = { 0 };
    const float tilts[] = { -.75f, .75f, 0 };
    behavior_asset *materials[3] = { 0 };
    render_material_entity scene[4] = { emitter(), emitter(), lit_wall(6, 1.5f), wall() };
    for (int i = 0; i < 2; i++) {
        scene[i].pos[0] = i ? 28 : -28;
        scene[i].radius = 4;
        scene[i].extent[0] = scene[i].extent[1] = 6;
        scene[i].border_thickness = 0;
        memset(scene[i].scene, 0, sizeof scene[i].scene);
        memset(scene[i].border_emission, 0, sizeof scene[i].border_emission);
        memset(scene[i].emission, 0, sizeof scene[i].emission);
        scene[i].emission[i ? 2 : 0] = .2f;
        scene[i].emission[3] = 1;
    }
    scene[2].points[1] = 18;
    scene[2].extent[1] = 20;
    scene[3].pos[0] = -16;
    scene[3].points[0] = 2;
    scene[3].extent[0] = 4;
    uint16_t pixels[PIXELS * 4];
    const char *names[] = { "Directional red-facing",
                            "Directional blue-facing",
                            "Directional flat" };
    for (int bank = 0; bank < 3 && ok; bank++) {
        materials[bank] = tilted_material(names[bank], tilts[bank], 1);
        ok = materials[bank] != NULL;
        if (!ok) {
            break;
        }
        scene[2].reserved[0] = (float)render_shader_find(names[bank]);
        ok = draw_scaled(raster, radiance, canvas, scene, 3, SIDE, SIDE, 12) &&
             read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS);
        if (ok) {
            ok = surface_rgb(pixels, responses[bank]);
        }
    }
    float red_ratio = responses[0][0] / fmaxf(responses[0][1], 1e-6f);
    float blue_ratio = responses[1][0] / fmaxf(responses[1][1], 1e-6f);
    ok &= isfinite(red_ratio + blue_ratio) && responses[0][0] > .001f &&
          responses[1][1] > .001f && red_ratio > 1.25f && blue_ratio < .8f &&
          red_ratio > blue_ratio * 1.8f;
    float blocked[2] = { 0 };
    if (ok) {
        ok = draw_scaled(raster, radiance, canvas, scene, 4, SIDE, SIDE, 12) &&
             read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS);
        if (ok) {
            ok = surface_rgb(pixels, blocked);
        }
        ok &= blocked[0] < responses[2][0] * .65f &&
              blocked[1] > responses[2][1] * .8f &&
              blocked[1] < responses[2][1] * 1.2f;
    }
    /* This ridge overlays an already opaque receiver with identical base,
       opacity and normal. Only its height changes; transport stays identical.
       Its leftward relief shadow must not darken rightward blue light. */
    behavior_asset *ridge = NULL;
    float relief_blocked[2] = { 0 };
    if (ok) {
        ridge = tilted_material("Directional raised ridge", 0, 1);
        shader_error error;
        pin_value height = { .type = PIN_F32, .f = 12 };
        ok = ridge &&
             graph_set_pin_default(ridge->graph, ridge->graph->nodes[0], 0, 7, &height) ==
                 0 &&
             render_shader_upsert(ridge, &error);
        scene[3] = scene[2];
        scene[3].pos[0] = -4;
        scene[3].points[0] = 1;
        scene[3].extent[0] = 3;
        scene[3].reserved[0] = (float)render_shader_find("Directional raised ridge");
        if (ok) {
            ok = draw_scaled(raster, radiance, canvas, scene, 4, SIDE, SIDE, 12) &&
                 read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS) &&
                 surface_rgb(pixels, relief_blocked);
        }
        ok &= relief_blocked[0] < responses[2][0] * .65f &&
              relief_blocked[1] > responses[2][1] * .9f &&
              relief_blocked[1] < responses[2][1] * 1.1f;
    }
    behavior_asset_free(ridge);
    printf(
        "directional relief visibility: raised/open red %.4f/%.4f blue "
        "%.4f/%.4f\n",
        relief_blocked[0],
        responses[2][0],
        relief_blocked[1],
        responses[2][1]
    );
    printf(
        "directional surface colors mode=%d: banks R/B %.3f %.3f; blocked/open "
        "red "
        "%.4f/%.4f blue %.4f/%.4f %s\n",
        shadow_mode,
        red_ratio,
        blue_ratio,
        blocked[0],
        responses[2][0],
        blocked[1],
        responses[2][1],
        ok ? "PASS" : "FAIL"
    );
    for (int i = 0; i < 3; i++) {
        behavior_asset_free(materials[i]);
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(radiance);
    return ok;
}

static bool horizon_work_matches(const render_radiance *radiance, int size, GameHRCShadowMode mode)
{
    render_horizon_work work = render_radiance_horizon_work(radiance);
    int directions = mode == GAME_HRC_SHADOW_HORIZON4 ? 4 : 8;
    bool ok = work.builds == 1 && work.pixels == size * size &&
              work.directions == directions &&
              work.canvas_bytes == (size_t)size * size * (directions == 4 ? 16 : 24);
    if (!ok) {
        fprintf(
            stderr,
            "horizon work mode=%d size=%d: builds %d pixels %d directions %d "
            "bytes %zu\n",
            mode,
            size,
            work.builds,
            work.pixels,
            work.directions,
            work.canvas_bytes
        );
    }
    return ok;
}

/* Rebuild visibility from current height and coverage, even without resetting
 * transport. Moving a height-only ridge must not leave its old horizon behind;
 * an uncovered gap stops the bounded scan before a detached ridge. */
static bool horizon_updates(render_material_raster *raster, GameHRCShadowMode shadow_mode)
{
    render_radiance *radiance = render_radiance_new();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.radiance_hysteresis = 1;
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.surface_shadow_mode = shadow_mode;
    lighting.debug_view = 8;
    behavior_asset *ridge = tilted_material("Horizon moving ridge", 0, 1);
    shader_error error;
    pin_value height = { .type = PIN_F32, .f = 12 };
    bool ok = canvas.id && radiance && ridge &&
              graph_set_pin_default(ridge->graph, ridge->graph->nodes[0], 0, 7, &height) ==
                  0 &&
              render_shader_upsert(ridge, &error) &&
              render_radiance_set_lighting(radiance, lighting);
    render_material_entity scene[] = { emitter(), lit_wall(6, 1.5f), lit_wall(1, 1.5f) };
    scene[0].border_thickness = 0;
    scene[1].points[1] = scene[2].points[1] = 18;
    scene[1].extent[1] = scene[2].extent[1] = 20;
    scene[2].reserved[0] = (float)render_shader_find("Horizon moving ridge");
    uint16_t changed[PIXELS * 4], fresh[PIXELS * 4];
    float largest_error = 0, gap_visibility = 0;
    /* Resize down and back up, retaining the radiance object and GPU capacity. */
    const int sizes[] = { SIDE, SIDE, SIDE, SIDE, 64, SIDE };
    for (int stage = 0; stage < 6 && ok; stage++) {
        int size = sizes[stage], count = stage == 2 ? 2 : 3;
        /* The original x=-4 ridge's antialiased edge joined the receiver's
         * coverage at >= .5. Leave a rasterized gap at both pixel spacings. */
        scene[2].pos[0] = stage >= 3 ? -6 : stage == 1 ? 4 : -4;
        scene[1].points[0] = stage >= 3 ? 1.25f : 6;
        scene[1].extent[0] = scene[1].points[0] + 1;
        if (stage == 0 || size != sizes[stage - 1]) {
            cf_destroy_canvas(canvas);
            params = cf_canvas_defaults(size, size);
            params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
            canvas = cf_make_canvas(params);
            ok = canvas.id && render_material_raster_resize(raster, size, size) &&
                 render_radiance_resize(radiance, size, size);
        }
        if (!ok) {
            break;
        }
        ok = draw_frames_scaled(raster, radiance, canvas, scene, count, size, SIDE, 1, false) &&
             horizon_work_matches(radiance, size, shadow_mode) &&
             read_hdr_texture(cf_canvas_get_target(canvas), changed, size * size) &&
             draw_scaled(raster, radiance, canvas, scene, count, size, SIDE, 1) &&
             horizon_work_matches(radiance, size, shadow_mode) &&
             read_hdr_texture(cf_canvas_get_target(canvas), fresh, size * size);
        for (int pixel = 0; pixel < size * size && ok; pixel++) {
            for (int channel = 0; channel < 3; channel++) {
                float a = half_value(changed[4 * pixel + channel]);
                float b = half_value(fresh[4 * pixel + channel]);
                float difference = fabsf(a - b);
                largest_error = fmaxf(largest_error, difference);
                if (!isfinite(a + b) || difference >= .002f) {
                    printf(
                        "horizon mismatch stage %d mode %d pixel %d channel %d "
                        "values %.6f %.6f\n",
                        stage,
                        shadow_mode,
                        pixel,
                        channel,
                        a,
                        b
                    );
                    ok = false;
                }
            }
        }
        if (stage >= 3 && ok) {
            int center = 4 * ((size / 2) * size + size / 2);
            gap_visibility = half_value(changed[center + 2]);
            uint16_t coverage[PIXELS * 4];
            int gap_x = (int)(size * 0.5f - 3.0f * size / SIDE);
            int gap_pixel = 4 * ((size / 2) * size + gap_x);
            ok &= read_hdr_texture(render_material_raster_textures(raster).cover, coverage, size * size) &&
                  half_value(coverage[gap_pixel + 1]) < .5f;
            ok &= gap_visibility > .99f;
        }
    }
    printf(
        "horizon updates mode=%d: reset error %.6f gap visibility %.4f %s\n",
        shadow_mode,
        largest_error,
        gap_visibility,
        ok ? "PASS" : "FAIL"
    );
    behavior_asset_free(ridge);
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(radiance);
    ok &= render_material_raster_resize(raster, SIDE, SIDE);
    return ok;
}

static float surface_history_difference(const uint16_t *a, const uint16_t *b)
{
    float error = 0;
    for (int y = 54; y < 74; y++) {
        for (int x = 58; x < 63; x++) {
            int p = (y * SIDE + x) * 4;
            for (int channel = 0; channel < 3; channel++) {
                float va = half_value(a[p + channel]),
                      vb = half_value(b[p + channel]);
                if (!isfinite(va + vb)) {
                    return INFINITY;
                }
                error += fabsf(va - vb) / 300;
            }
        }
    }
    return error;
}

/* Received surface light obeys the same temporal setting as open-space GI.
 * A small source translation retains useful clamped history in its gradient;
 * a reset must discard it, and continued rendering must converge. */
/* Cached visibility must depend on geometry, not lights or stale camera pixels. */
static bool horizon_draw_view(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *scene,
    int count,
    int size,
    render_view_2d view
)
{
    cf_app_update(NULL);
    bool ok = render_material_raster_draw(raster, scene, count, view);
    render_radiance_inputs inputs = { .material = render_material_raster_textures(raster) };
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, size, size));
    if (ok) {
        ok = render_radiance_draw(radiance, inputs, view, canvas);
    }
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    return ok;
}
static bool horizon_compare(
    CF_Canvas canvas,
    render_material_raster *raster,
    render_radiance *fresh,
    const render_material_entity *scene,
    int count,
    int size,
    render_view_2d view,
    float tolerance,
    float *maximum
)
{
    uint16_t a[PIXELS * 4], b[PIXELS * 4];
    bool ok = read_hdr_texture(cf_canvas_get_target(canvas), a, size * size);
    render_radiance_reset(fresh);
    ok = ok && horizon_draw_view(raster, fresh, canvas, scene, count, size, view) &&
         read_hdr_texture(cf_canvas_get_target(canvas), b, size * size);
    for (int i = 0; ok && i < size * size * 4; i++) {
        float av = half_value(a[i]), bv = half_value(b[i]);
        *maximum = fmaxf(*maximum, fabsf(av - bv));
        if (!isfinite(av + bv) || fabsf(av - bv) > tolerance) {
            fprintf(stderr, "horizon reuse parity channel%d values%g/%g tolerance%g\n", i, av, bv, tolerance);
            ok = false;
        }
    }
    return ok;
}
static bool horizon_dirty_counts(render_radiance *radiance, int size, int *dirty, int *total)
{
    int tiles = (size + 15) / 16;
    *total = tiles * tiles;
    *dirty = 0;
    uint16_t pixels[PIXELS * 4];
    CF_Texture mask = render_radiance_horizon_dirty_texture(radiance);
    bool ok = mask.id && read_hdr_texture(mask, pixels, *total);
    for (int i = 0; ok && i < *total; i++) {
        float red = half_value(pixels[i * 4]);
        if (!isfinite(red) || red < 0 || red > 1) {
            ok = false;
        }
        *dirty += red > .5f;
    }
    return ok;
}
static bool horizon_reuse_sequence(
    render_material_raster *raster,
    GameHRCShadowMode mode,
    bool fused,
    bool reproject,
    bool hierarchy
)
{
    render_radiance *cached = render_radiance_new(), *fresh = render_radiance_new();
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.resolution = .5f;
    lighting.radiance_hysteresis = 1;
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.surface_shadow_mode = mode;
    // Raw HDR bypasses frame-dependent display dithering during parity checks.
    lighting.tone_mapping = GAME_HRC_TONE_AGX;
    lighting.debug_view = 8;
    lighting.horizon_hierarchy = hierarchy;
    GameHRC2D reference = lighting;
    lighting.horizon_fuse_relief = fused;
    lighting.horizon_cache = true;
    lighting.horizon_reproject = reproject;
    behavior_asset *ridge = tilted_material("Horizon cache ridge", 0, 1);
    pin_value height = { .type = PIN_F32, .f = 12 };
    shader_error error;
    bool ok = cached && fresh && ridge &&
              !graph_set_pin_default(ridge->graph, ridge->graph->nodes[0], 0, 7, &height) &&
              render_shader_upsert(ridge, &error);
    render_material_entity scene[] = { emitter(),         lit_wall(6, 1.5f),
                                       lit_wall(1, 1.5f), lit_wall(6, 1.5f),
                                       lit_wall(1, 1.5f), emitter() };
    scene[0].border_thickness = 0;
    memset(scene[0].scene, 0, sizeof scene[0].scene);
    memset(scene[0].border_scene, 0, sizeof scene[0].border_scene);
    scene[1].points[1] = scene[2].points[1] = 18;
    scene[1].extent[1] = scene[2].extent[1] = 20;
    scene[2].pos[0] = -4;
    scene[2].reserved[0] = (float)render_shader_find(ridge->name);
    // A second lit receiver and raised blocker are outside the initial view,
    // then enter the newly exposed strip during the large camera translation.
    scene[3].pos[0] = 80;
    scene[4].pos[0] = 84;
    scene[5].pos[0] = 100;
    scene[3].points[1] = scene[4].points[1] = 18;
    scene[3].extent[1] = scene[4].extent[1] = 20;
    scene[4].reserved[0] = (float)render_shader_find(ridge->name);
    scene[5].border_thickness = 0;
    memset(scene[5].scene, 0, sizeof scene[5].scene);
    memset(scene[5].border_scene, 0, sizeof scene[5].border_scene);
    CF_Canvas canvas = { 0 };
    float max_error = 0;
    int size = SIDE;
    render_view_2d view = { .min_x = -64,
                            .max_y = 64,
                            .width = 128,
                            .height = 128,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    for (int stage = 0; ok && stage < 15; stage++) {
        int count = 0;
        render_material_entity active[6];
        if (stage == 1) {
            scene[0].emission[1] = .15f;
        }
        if (stage == 2) {
            scene[0].pos[0] = -26;
        }
        if (stage == 3) {
            scene[2].pos[0] = 4;
        }
        if (stage == 5) {
            scene[2].pos[0] = -6;
            scene[1].points[0] = 1.25f;
            scene[1].extent[0] = 2.25f;
        }
        if (stage == 6) {
            view.min_x += 1;
            view.center_x += 1;
        }
        if (stage == 7) {
            view.max_y += 2;
            view.center_y += 2;
        }
        if (stage == 8) {
            view.min_x += .25f;
            view.center_x += .25f;
        }
        if (stage == 9) {
            view.min_x += 40;
            view.center_x += 40;
        }
        if (stage == 10) {
            render_radiance_reset(cached);
        }
        if (stage == 11) {
            size = 64;
        }
        if (stage == 12) {
            size = SIDE;
        }
        if (stage == 13) {
            lighting.surface_shadow_mode = reference.surface_shadow_mode = mode == GAME_HRC_SHADOW_HORIZON4
                                                                             ? GAME_HRC_SHADOW_HORIZON8
                                                                             : GAME_HRC_SHADOW_HORIZON4;
        }
        if (stage == 14) {
            lighting.horizon_fuse_relief = !lighting.horizon_fuse_relief;
        }
        if (stage == 0 || stage == 11 || stage == 12) {
            if (canvas.id) {
                cf_destroy_canvas(canvas);
            }
            CF_CanvasParams params = cf_canvas_defaults(size, size);
            params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
            canvas = cf_make_canvas(params);
            ok = canvas.id && render_material_raster_resize(raster, size, size) &&
                 render_radiance_resize(cached, size, size) &&
                 render_radiance_resize(fresh, size, size);
        }
        for (int i = 0; i < 6; i++) {
            if (stage != 4 || i != 2) {
                active[count++] = scene[i];
            }
        }
        ok = ok && render_radiance_set_lighting(cached, lighting) &&
             render_radiance_set_lighting(fresh, reference) &&
             horizon_draw_view(raster, cached, canvas, active, count, size, view);
        int dirty = 0, total = 0;
        render_horizon_work work = render_radiance_horizon_work(cached);
        ok = ok && horizon_dirty_counts(cached, size, &dirty, &total);
        bool effective_reproject = reproject && (!hierarchy || stage == 9);
        bool all = stage == 0 || stage == 8 || stage == 10 || stage == 11 ||
                   stage == 12 || stage == 13 || stage == 14 ||
                   (!effective_reproject && (stage == 6 || stage == 7 || stage == 9));
        bool unchanged = stage == 1 || stage == 2;
        if (ok) {
            ok = (!all || dirty == total) && (!unchanged || dirty == 0) &&
                 work.reuse_passes == (all ? 0 : 1) && work.dirty_passes > 0 &&
                 work.tiles_x * work.tiles_y == total;
        }
        if (ok && (stage == 3 || stage == 4 || stage == 5)) {
            ok = dirty > 0;
        }
        if (ok && effective_reproject && (stage == 6 || stage == 7 || stage == 9)) {
            ok = work.reproject_passes == 1 && dirty > 0;
        }
        if (ok) {
            ok = work.fused_packs == (lighting.horizon_fuse_relief ? 1 : 0) &&
                 work.pack_passes == (lighting.horizon_fuse_relief ? 0 : 1);
        }
        if (ok) {
            ok = horizon_compare(canvas, raster, fresh, active, count, size, view, .003f, &max_error);
        }
        if (ok) {
            // Check actual shaded output as well as diagnostic visibility.
            lighting.debug_view = reference.debug_view = 0;
            ok = render_radiance_set_lighting(cached, lighting) &&
                 render_radiance_set_lighting(fresh, reference) &&
                 horizon_draw_view(raster, cached, canvas, active, count, size, view) &&
                 horizon_compare(canvas, raster, fresh, active, count, size, view, .003f, &max_error);
            lighting.debug_view = reference.debug_view = 8;
        }
        if (!ok) {
            fprintf(
                stderr,
                "horizon cache mode%d fused%d reproject%d stage%d dirty%d/%d "
                "passes(reuse%d,reproject%d,pack%d,fused%d)\n",
                mode,
                fused,
                reproject,
                stage,
                dirty,
                total,
                work.reuse_passes,
                work.reproject_passes,
                work.pack_passes,
                work.fused_packs
            );
        }
    }
    printf(
        "horizon reuse mode%d fused%d reproject%d hierarchy%d: max_error%g "
        "%s\n",
        mode,
        fused,
        reproject,
        hierarchy,
        max_error,
        ok ? "PASS" : "FAIL"
    );
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(cached);
    render_radiance_free(fresh);
    behavior_asset_free(ridge);
    ok = render_material_raster_resize(raster, SIDE, SIDE) && ok;
    return ok;
}

static behavior_asset *horizon_height_material(const char *name, float slope, float height)
{
    behavior_asset *material = tilted_material(name, -slope, 1);
    if (!material) {
        return NULL;
    }
    graph *g = material->graph;
    node *output = g->nodes[0];
    shader_error error;
    if (slope == 0) {
        pin_value value = { .type = PIN_F32, .f = height };
        if (graph_set_pin_default(g, output, 0, 7, &value)) {
            behavior_asset_free(material);
            return NULL;
        }
    } else {
        node *position = graph_add_node(g, registry_node_by_name("Shader WorldPosition")),
             *x = graph_add_node(g, registry_node_by_name("Shader X")),
             *multiply = graph_add_node(g, registry_node_by_name("Shader Multiply")),
             *offset = graph_add_node(g, registry_node_by_name("Shader Add"));
        pin_value scale = { .type = PIN_F32, .f = slope },
                  bias = { .type = PIN_F32, .f = height };
        if (graph_connect_data(g, position, 0, x, 0) ||
            graph_connect_data(g, x, 0, multiply, 0) ||
            graph_set_pin_default(g, multiply, 0, 1, &scale) ||
            graph_connect_data(g, multiply, 0, offset, 0) ||
            graph_set_pin_default(g, offset, 0, 1, &bias) ||
            graph_connect_data(g, offset, 0, output, 7)) {
            behavior_asset_free(material);
            return NULL;
        }
    }
    if (!render_shader_upsert(material, &error)) {
        behavior_asset_free(material);
        return NULL;
    }
    return material;
}
static bool horizon_hierarchy_quality(render_material_raster *raster, GameHRCShadowMode mode)
{
    render_radiance *approx = render_radiance_new(), *fine = render_radiance_new();
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.radiance_hysteresis = 1;
    lighting.surface_radius = 12;
    lighting.surface_relief = 1.5f;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.surface_shadow_mode = mode;
    lighting.debug_view = 8;
    GameHRC2D reference = lighting;
    lighting.horizon_hierarchy = true;
    behavior_asset *plane = horizon_height_material("Hierarchy flat plane", 0, 0),
                   *ramp = horizon_height_material("Hierarchy shallow ramp", .1f, 0),
                   *ridge = horizon_height_material("Hierarchy raised ridge", 0, 6);
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    bool ok = approx && fine && plane && ramp && ridge && canvas.id &&
              render_material_raster_resize(raster, SIDE, SIDE) &&
              render_radiance_resize(approx, SIDE, SIDE) &&
              render_radiance_resize(fine, SIDE, SIDE) &&
              render_radiance_set_lighting(approx, lighting) &&
              render_radiance_set_lighting(fine, reference);
    render_view_2d view = { .min_x = -16,
                            .max_y = 16,
                            .width = 32,
                            .height = 32,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    render_material_entity scene[] = { emitter(), lit_wall(6, 1.5f), lit_wall(1, 1.5f) };
    scene[0].pos[0] = -12;
    scene[0].radius = 2;
    scene[0].extent[0] = scene[0].extent[1] = 4;
    scene[0].border_thickness = 0;
    scene[1].points[1] = scene[2].points[1] = 18;
    scene[1].extent[1] = scene[2].extent[1] = 20;
    scene[2].pos[0] = -2;
    scene[2].reserved[0] = (float)render_shader_find(ridge->name);
    const char *names[] = { "plane", "ramp", "ridge", "gap" };
    for (int shape = 0; ok && shape < 4; shape++) {
        int count = shape < 2 ? 2 : 3;
        scene[1].reserved[0] = (float)render_shader_find(
            shape == 1 ? ramp->name : plane->name
        );
        if (shape == 3) {
            scene[1].points[0] = 1.25f;
            scene[1].extent[0] = 2.25f;
            scene[2].pos[0] = -6;
        }
        uint16_t a[PIXELS * 4], b[PIXELS * 4], cover[PIXELS * 4];
        render_radiance_reset(approx);
        render_radiance_reset(fine);
        ok = horizon_draw_view(raster, approx, canvas, scene, count, SIDE, view) &&
             read_hdr_texture(cf_canvas_get_target(canvas), a, PIXELS);
        render_horizon_work work = render_radiance_horizon_work(approx);
        ok = ok && work.hierarchy_passes > 0 &&
             horizon_draw_view(raster, fine, canvas, scene, count, SIDE, view) &&
             read_hdr_texture(cf_canvas_get_target(canvas), b, PIXELS) &&
             read_hdr_texture(render_material_raster_textures(raster).cover, cover, PIXELS);
        double error = 0, visibility = 0;
        int samples = 0;
        float max_error = 0;
        for (int i = 0; ok && i < PIXELS; i++) {
            float av = half_value(a[i * 4 + 2]), bv = half_value(b[i * 4 + 2]);
            if (!isfinite(av + bv) || av < -.002f || av > 1.002f) {
                ok = false;
            }
            if (half_value(cover[i * 4 + 1]) < .5f) {
                continue;
            }
            float d = fabsf(av - bv);
            error += d;
            max_error = fmaxf(max_error, d);
            visibility += av;
            samples++;
        }
        float mean_error = samples ? (float)(error / samples) : 1;
        float mean_visibility = samples ? (float)(visibility / samples) : 0;
        int center = 4 * ((SIDE / 2) * SIDE + SIDE / 2);
        float center_visibility = half_value(a[center + 2]),
              fine_center = half_value(b[center + 2]);
        ok = ok && samples > 100 && mean_error < .003f && max_error < .003f;
        if (shape < 2) {
            ok = ok && mean_visibility > .95f && center_visibility > .99f;
        }
        if (shape == 2) {
            ok = ok && fine_center < .5f && center_visibility <= fine_center + .15f;
        }
        if (shape == 3) {
            ok = ok && center_visibility > .99f && fine_center > .99f;
        }
        printf(
            "horizon hierarchy mode%d %s: mean_error%g max_error%g "
            "mean_visibility%g center%g fine_center%g %s\n",
            mode,
            names[shape],
            mean_error,
            max_error,
            mean_visibility,
            center_visibility,
            fine_center,
            ok ? "PASS" : "FAIL"
        );
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(approx);
    render_radiance_free(fine);
    behavior_asset_free(plane);
    behavior_asset_free(ramp);
    behavior_asset_free(ridge);
    return ok;
}

static bool surface_history(render_material_raster *raster)
{
    render_radiance *radiance = render_radiance_new();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.fog = 0;
    lighting.surface_radius = 12;
    lighting.surface_bevel = lighting.specular = 0;
    lighting.tone_mapping = 1;
    lighting.debug_view = 7;
    lighting.radiance_hysteresis = 1;
    render_material_entity scene[] = { emitter(), lit_wall(6, 1.5f) };
    uint16_t instant[PIXELS * 4], changed[PIXELS * 4], settled[PIXELS * 4],
        reset[PIXELS * 4];
    scene[0].pos[0] = -26;
    bool ok = canvas.id && radiance && render_radiance_resize(radiance, SIDE, SIDE) &&
              render_radiance_set_lighting(radiance, lighting) &&
              draw_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE, 12) &&
              read_hdr_texture(cf_canvas_get_target(canvas), instant, PIXELS);
    lighting.radiance_hysteresis = .2f;
    scene[0].pos[0] = -28;
    if (ok) {
        ok = render_radiance_set_lighting(radiance, lighting) &&
             draw_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE, 20);
    }
    scene[0].pos[0] = -26;
    if (ok) {
        ok = draw_frames_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE, 1, false) &&
             read_hdr_texture(cf_canvas_get_target(canvas), changed, PIXELS) &&
             draw_frames_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE, 20, false) &&
             read_hdr_texture(cf_canvas_get_target(canvas), settled, PIXELS) &&
             draw_scaled(raster, radiance, canvas, scene, 2, SIDE, SIDE, 1) &&
             read_hdr_texture(cf_canvas_get_target(canvas), reset, PIXELS);
    }
    float first_error = INFINITY, settled_error = INFINITY, reset_error = INFINITY;
    if (ok) {
        first_error = surface_history_difference(changed, instant);
        settled_error = surface_history_difference(settled, instant);
        reset_error = surface_history_difference(reset, instant);
        ok &= first_error > .0001f && settled_error < first_error * .3f &&
              reset_error < .0001f;
    }
    printf(
        "surface history: first %.6f settled %.6f reset %.6f %s\n",
        first_error,
        settled_error,
        reset_error,
        ok ? "PASS" : "FAIL"
    );
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(radiance);
    return ok;
}

static bool surface_history_after_probe_transition(render_material_raster *raster)
{
    render_radiance *transition = render_radiance_new();
    render_radiance *reference = render_radiance_new();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    GameHRC2D normal = game_hrc2d_defaults();
    normal.adaptive = normal.feedback = false;
    normal.ambient = normal.fog = 0;
    normal.surface_mode = GAME_HRC_SURFACE_GATHER;
    normal.surface_radius = 32;
    normal.surface_relief = 1.5f;
    normal.surface_bevel = normal.specular = 0;
    normal.radiance_hysteresis = .3f;
    normal.debug_view = GAME_HRC_DEBUG_NORMAL;
    render_material_entity scene[] = { emitter(), lit_wall(6, 1.5f) };
    scene[0].emission[1] = scene[0].border_emission[1] = 2.0f;
    render_view_2d view = { .min_x = -64,
                            .max_y = 64,
                            .width = SIDE,
                            .height = SIDE,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    uint16_t transitioned[PIXELS * 4], reset[PIXELS * 4];
    bool ok = transition && reference && canvas.id &&
              render_material_raster_resize(raster, SIDE, SIDE) &&
              render_radiance_resize(transition, SIDE, SIDE) &&
              render_radiance_resize(reference, SIDE, SIDE) &&
              render_radiance_set_lighting(transition, normal) &&
              render_radiance_set_lighting(reference, normal);
    for (int frame = 0; frame < 12 && ok; frame++) {
        ok = draw_view_once(raster, transition, canvas, scene, 2, view) &&
             draw_view_once(raster, reference, canvas, scene, 2, view);
    }

    if (ok) {
        GameHRC2D probes = normal;
        probes.debug_view = GAME_HRC_DEBUG_GATHER_PROBES;
        view.min_x = -61;
        view.max_y = 62;
        scene[0].pos[0] = -8;
        ok = render_radiance_set_lighting(transition, probes) &&
             render_radiance_set_lighting(reference, normal) &&
             draw_view_once(raster, transition, canvas, scene, 2, view) &&
             draw_view_once(raster, reference, canvas, scene, 2, view);
    }

    if (ok) {
        view.min_x = -58;
        view.max_y = 60;
        scene[0].pos[0] = 20;
        ok = draw_view_once(raster, transition, canvas, scene, 2, view) &&
             draw_view_once(raster, reference, canvas, scene, 2, view);
    }

    if (ok) {
        ok = render_radiance_set_lighting(transition, normal);
        GameHRC2D no_surface_history = normal;
        no_surface_history.surface_radius = 0;
        ok = ok && render_radiance_set_lighting(reference, no_surface_history) &&
             render_radiance_set_lighting(reference, normal) &&
             draw_view_once(raster, transition, canvas, scene, 2, view) &&
             read_hdr_texture(cf_canvas_get_target(canvas), transitioned, PIXELS) &&
             draw_view_once(raster, reference, canvas, scene, 2, view) &&
             read_hdr_texture(cf_canvas_get_target(canvas), reset, PIXELS);
    }
    float error = ok ? surface_history_difference(transitioned, reset) : INFINITY;
    ok &= error < .0001f;
    printf("surface history after normal/probes/normal with camera and light motion: %.6f %s\n", error, ok ? "PASS" : "FAIL");
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(transition);
    render_radiance_free(reference);
    return ok;
}

static bool magenta_marker(const uint16_t *pixels)
{
    int count = 0;
    for (int i = 0; i < PIXELS; i++) {
        int p = 4 * i;
        count += half_value(pixels[p]) > .95f && half_value(pixels[p + 1]) < .05f &&
                 half_value(pixels[p + 2]) > .95f;
    }
    return count > PIXELS * .95f;
}

static float diagnostic_local_y(int y, int vertical_tiles, bool flip_y)
{
    float uv = ((float)y + .5f) / SIDE;
    if (flip_y) {
        uv = 1 - uv;
    }
    float tiled = uv * vertical_tiles;
    return tiled - floorf(tiled);
}

static bool diagnostic_fraction(float value, float denominator)
{
    if (!isfinite(value) || value < -0.002f || value > 1.002f) {
        return false;
    }
    return fabsf(value * denominator - roundf(value * denominator)) < 0.006f;
}

static bool surface_quadrant_channels(const uint16_t *pixels)
{
    for (int flip_y = 0; flip_y < 2; flip_y++) {
        bool valid = true, ordered_rgb = false;
        bool open_openness = false, partial_openness = false;
        for (int y = 0; y < SIDE && valid; y++) {
            float local_y = diagnostic_local_y(y, 1, flip_y != 0);
            for (int x = 0; x < SIDE; x++) {
                int p = 4 * (y * SIDE + x);
                float r = half_value(pixels[p]);
                float g = half_value(pixels[p + 1]);
                float b = half_value(pixels[p + 2]);
                float a = half_value(pixels[p + 3]);
                if (!isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(a)) {
                    valid = false;
                    break;
                }
                if (local_y < .82f) {
                    if (r < -0.002f || r > 1.002f || g < -0.002f || g > 1.002f ||
                        b < -0.002f || b > 1.002f || fabsf(a - 1) > .002f) {
                        valid = false;
                        break;
                    }
                    ordered_rgb |= r > g + .01f && g > b + .01f && r > .02f;
                    continue;
                }
                if (fabsf(r - g) >= .002f || fabsf(g - b) >= .002f ||
                    fabsf(a - 1) >= .002f || r < -0.002f || r > 1.002f) {
                    valid = false;
                    break;
                }
                open_openness |= r > .99f;
                partial_openness |= r > .02f && r < .98f;
            }
        }
        if (valid && ordered_rgb && open_openness && partial_openness) {
            return true;
        }
    }
    return false;
}

static bool surface_closed_region_openness(const uint16_t *pixels)
{
    for (int flip_y = 0; flip_y < 2; flip_y++) {
        bool valid = true;
        int closed = 0, total = 0;
        for (int y = 0; y < SIDE && valid; y++) {
            float local_y = diagnostic_local_y(y, 1, flip_y != 0);
            if (local_y < .82f) {
                continue;
            }
            for (int x = 0; x < SIDE; x++) {
                int p = 4 * (y * SIDE + x);
                float r = half_value(pixels[p]);
                float g = half_value(pixels[p + 1]);
                float b = half_value(pixels[p + 2]);
                float a = half_value(pixels[p + 3]);
                if (!isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(a) ||
                    fabsf(r - g) >= .002f || fabsf(g - b) >= .002f ||
                    fabsf(a - 1) >= .002f || r < -0.002f || r > 0.01f) {
                    valid = false;
                    break;
                }
                closed++;
                total++;
            }
        }
        if (valid && total >= (int)(SIDE * SIDE * .17f) && closed == total) {
            return true;
        }
    }
    return false;
}

static bool gather_probe_channels(const uint16_t *pixels)
{
    for (int flip_y = 0; flip_y < 2; flip_y++) {
        bool valid = true;
        bool candidate_without_receiver = false;
        bool fully_visible_receiver = false;
        bool partly_or_fully_blocked_receiver = false;
        bool open_openness = false, partial_openness = false, closed_openness = false;
        for (int y = 0; y < SIDE && valid; y++) {
            float local_y = diagnostic_local_y(y, 2, flip_y != 0);
            for (int x = 0; x < SIDE; x++) {
                int p = 4 * (y * SIDE + x);
                float r = half_value(pixels[p]);
                float g = half_value(pixels[p + 1]);
                float b = half_value(pixels[p + 2]);
                float a = half_value(pixels[p + 3]);
                if (!isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(a)) {
                    valid = false;
                    break;
                }
                if (local_y < .68f) {
                    int visibility_tests = (int)roundf(g * 3.0f);
                    if (fabsf(a - 1) > .002f || !diagnostic_fraction(r, 3) ||
                        !diagnostic_fraction(g, 3) || b < -0.002f || b > 1.002f ||
                        g > r + .002f || (g > .002f && fabsf(g - r) >= .002f) ||
                        (visibility_tests == 0 && b > .002f) ||
                        (visibility_tests > 0 &&
                         !diagnostic_fraction(b, visibility_tests)) ||
                        (g < .002f && b > .002f)) {
                        valid = false;
                        break;
                    }
                    candidate_without_receiver |= r >= (1.0f / 3.0f) - .002f &&
                                                  g < .002f && b < .002f;
                    fully_visible_receiver |= fabsf(r - 1) < .002f &&
                                              fabsf(g - 1) < .002f &&
                                              fabsf(b - 1) < .002f;
                    partly_or_fully_blocked_receiver |= r > .002f &&
                                                        g > .002f && b < .998f;
                    continue;
                }
                if (fabsf(r - g) >= .002f || fabsf(g - b) >= .002f ||
                    fabsf(a - 1) >= .002f || r < -0.002f || r > 1.002f) {
                    valid = false;
                    break;
                }
                open_openness |= r > .99f;
                partial_openness |= r > .02f && r < .98f;
                closed_openness |= r < .01f;
            }
        }
        if (valid && candidate_without_receiver && fully_visible_receiver &&
            partly_or_fully_blocked_receiver && open_openness &&
            partial_openness && closed_openness) {
            return true;
        }
    }
    return false;
}

static behavior_asset *horizon_height_material(const char *name, float slope, float height);

static bool surface_mode_debug_views(render_material_raster *raster)
{
    render_radiance *radiance = render_radiance_new();
    CF_CanvasParams params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas canvas = cf_make_canvas(params);
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    lighting.surface_radius = 24;
    lighting.surface_depth_radius = 128;
    lighting.surface_depth_gain = 1;
    lighting.surface_thickness = 12;
    lighting.radiance_hysteresis = .25f;
    lighting.debug_view = GAME_HRC_DEBUG_SURFACE_QUADRANTS;
    render_material_entity scene[] = { emitter(), lit_wall(6, 1.5f) };
    scene[0].border_thickness = 0;
    memset(scene[0].border_emission, 0, sizeof scene[0].border_emission);
    scene[0].emission[0] = .24f;
    scene[0].emission[1] = .12f;
    scene[0].emission[2] = .06f;
    behavior_asset *probe_blocker = horizon_height_material("Gather probe blocker", 0, 12);
    render_material_entity probe_scene[] = { emitter(), lit_wall(6, 1.5f), wall() };
    probe_scene[2].pos[0] = -16;
    probe_scene[2].reserved[0] = (float)render_shader_find("Gather probe blocker");
    /* Alpha is absorption coverage; RGB carries optical depth. */
    for (int channel = 0; channel < 3; channel++) {
        probe_scene[1].absorption[channel] = 12;
        probe_scene[2].absorption[channel] = 12;
    }
    render_view_2d view = { .min_x = -64,
                            .max_y = 64,
                            .width = SIDE,
                            .height = SIDE,
                            .crop_max_u = 1,
                            .crop_max_v = 1 };
    uint16_t pixels[PIXELS * 4];
    bool ok = radiance && canvas.id && probe_blocker &&
              render_material_raster_resize(raster, SIDE, SIDE) &&
              render_radiance_resize(radiance, SIDE, SIDE);
    for (int mode = GAME_HRC_SURFACE_BLUR; mode <= GAME_HRC_SURFACE_DEPTH && ok;
         mode++) {
        lighting.surface_mode = (GameHRCSurfaceMode)mode;
        ok = render_radiance_set_lighting(radiance, lighting) &&
             draw_view_once(raster, radiance, canvas, scene, 2, view) &&
             read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS);
        if (ok) {
            bool is_magenta = magenta_marker(pixels);
            ok = mode == GAME_HRC_SURFACE_DEPTH ? is_magenta : !is_magenta;
            printf("surface quadrant debug mode %d: %s\n", mode, ok ? "PASS" : "FAIL");
            if (ok && mode != GAME_HRC_SURFACE_DEPTH) {
                ok = surface_quadrant_channels(pixels);
                printf("surface quadrant RGB/openness channels mode %d: %s\n", mode, ok ? "PASS" : "FAIL");
                if (ok) {
                    render_material_entity closed_region = wall_with(12, 0);
                    closed_region.extent[0] = closed_region.extent[1] = SIDE * .5f + 2;
                    closed_region.points[0] = closed_region.points[1] = SIDE * .5f + 1;
                    render_radiance_reset(radiance);
                    ok = draw_view_once(raster, radiance, canvas, &closed_region, 1, view) &&
                         read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS) &&
                         surface_closed_region_openness(pixels);
                    printf("surface closed-region openness mode %d: %s\n", mode, ok ? "PASS" : "FAIL");
                }
            }
        }

        lighting.debug_view = GAME_HRC_DEBUG_GATHER_PROBES;
        if (ok) {
            const render_material_entity *probe_entities = mode == GAME_HRC_SURFACE_GATHER
                                                             ? probe_scene
                                                             : scene;
            int probe_count = mode == GAME_HRC_SURFACE_GATHER ? 3 : 2;
            ok = render_radiance_set_lighting(radiance, lighting) &&
                 draw_view_once(raster, radiance, canvas, probe_entities, probe_count, view) &&
                 read_hdr_texture(cf_canvas_get_target(canvas), pixels, PIXELS);
        }
        if (ok) {
            bool is_magenta = magenta_marker(pixels);
            ok = mode == GAME_HRC_SURFACE_GATHER ? !is_magenta : is_magenta;
            printf("Gather probe debug mode %d: %s\n", mode, ok ? "PASS" : "FAIL");
            if (ok && mode == GAME_HRC_SURFACE_GATHER) {
                ok = gather_probe_channels(pixels);
                printf("Gather probe R/G/B/openness channels: %s\n", ok ? "PASS" : "FAIL");
            }
        }
        lighting.debug_view = GAME_HRC_DEBUG_SURFACE_QUADRANTS;
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    render_radiance_free(radiance);
    behavior_asset_free(probe_blocker);
    return ok;
}

/* Known corner values distinguish all four directions and both ends of each
 * band's complete source domain, independently of the transport solver. */
static bool diagnostic_quadrant_mapping()
{
    CF_Shader shader = cf_make_draw_shader("hrc/composite.shd");
    CF_Texture quadrants[4] = { 0 };
    CF_TextureParams texture = cf_texture_defaults(2, 2);
    texture.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    texture.filter = CF_FILTER_NEAREST;
    texture.wrap_u = texture.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    for (int q = 0; q < 4; q++) {
        float corners[4][4];
        for (int p = 0; p < 4; p++) {
            corners[p][0] = (q + 1) * .125f;
            corners[p][1] = p % 2;
            corners[p][2] = p / 2;
            corners[p][3] = p / 3.0f;
        }
        quadrants[q] = cf_make_texture(texture);
        cf_texture_update(quadrants[q], corners, sizeof corners);
    }
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = quadrants[0];
    CF_Canvas input = cf_make_canvas(params);
    params = cf_canvas_defaults(SIDE, SIDE);
    params.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    CF_Canvas output = cf_make_canvas(params);
    bool ok = shader.id && input.id && output.id;
    uint16_t pixels[PIXELS * 4];
    for (int mode = GAME_HRC_DEBUG_TRANSPORT_QUADRANTS;
         ok && mode <= GAME_HRC_DEBUG_GATHER_PROBES;
         mode++) {
        cf_app_update(NULL);
        cf_draw_push_shader(shader);
        const char *names[] = { "scene_tex",        "fluence_tex",
                                "source_tex",       "albedo_tex",
                                "normal_tex",       "background_tex",
                                "visibility_tex",   "cover_tex",
                                "horizon_cardinal", "horizon_diagonal" };
        for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
            cf_draw_set_texture(names[i], quadrants[0]);
        }
        for (int q = 0; q < 4; q++) {
            char name[16];
            snprintf(name, sizeof name, "surface_q%d", q);
            cf_draw_set_texture(name, quadrants[q]);
        }
        float crop[] = { .2f, .1f, .8f, .9f }, map[] = { 1, 1, .5f, .5f };
        int surface = GAME_HRC_SURFACE_GATHER;
        cf_draw_set_uniform_float("debug_view", (float)mode);
        cf_draw_set_uniform("surface_mode", &surface, CF_UNIFORM_TYPE_INT, 1);
        cf_draw_set_uniform("surface_map", map, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
        render_draw_fullscreen(input);
        cf_render_to(output, true);
        cf_draw_pop_shader();
        cf_app_draw_onto_screen(false);
        ok = read_hdr_texture(cf_canvas_get_target(output), pixels, PIXELS);
        bool matched = false;
        /* Readback orientation varies by backend; one orientation must match
         * every pixel, quadrant and band, including their outermost rows. */
        for (int flip = 0; ok && flip < 2 && !matched; flip++) {
            bool exact = true;
            int columns = mode == GAME_HRC_DEBUG_GATHER_PROBES ? 2 : 4;
            int rows = mode == GAME_HRC_DEBUG_GATHER_PROBES ? 2 : 1;
            float band = rows == 2 ? .68f : .82f;
            for (int y = 0; exact && y < SIDE; y++) {
                float v = ((flip ? SIDE - 1 - y : y) + .5f) / SIDE * rows;
                float local_y = v - floorf(v);
                bool opacity = local_y >= band;
                float source_y = opacity ? (local_y - band) / (1 - band)
                                         : local_y / band;
                for (int x = 0; exact && x < SIDE; x++) {
                    float u = (x + .5f) / SIDE * columns;
                    int q = (int)u + columns * (int)v;
                    int corner = (u - floorf(u) >= .5f) + 2 * (source_y >= .5f);
                    float expected[] = { (q + 1) * .125f, corner % 2, corner / 2, 1 };
                    for (int c = 0; c < 3; c++) {
                        if (opacity) {
                            expected[c] = corner / 3.0f;
                        } else if (rows == 1) {
                            expected[c] = log1pf(expected[c] * 65535) / logf(65536);
                        }
                    }
                    for (int c = 0; c < 4; c++) {
                        exact &= fabsf(
                                     half_value(pixels[4 * (y * SIDE + x) + c]) -
                                     expected[c]
                                 ) < .001f;
                    }
                }
            }
            matched |= exact;
        }
        ok &= matched;
        printf("diagnostic quadrant identity and full-domain bands view %d: %s\n", mode, ok ? "PASS" : "FAIL");
    }
    cf_destroy_canvas(input);
    cf_destroy_canvas(output);
    for (int q = 0; q < 4; q++) {
        cf_destroy_texture(quadrants[q]);
    }
    cf_destroy_shader(shader);
    return ok;
}

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    ecs_entity settings;
    game_particles *particles;
    engine_viewport *viewport;
    CF_Canvas canvas;
} diagnostic_scene;

static bool diagnostic_scene_frame(
    diagnostic_scene *scene,
    app_scene_renderer *renderer,
    GameHRC2D lighting,
    CF_Pixel *pixels
)
{
    *GAME_GameHRC2D_get(scene->world, scene->settings) = lighting;
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    bool ok = app_scene_renderer_draw(
        renderer,
        scene->world,
        scene->particles,
        scene->viewport,
        scene->runtime,
        (render_skip){ 0 }
    );
    cf_render_to(scene->canvas, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    return ok && read_pixels(scene->canvas, pixels, PIXELS);
}

static bool diagnostic_scene_effects(diagnostic_scene *scene, app_scene_renderer *renderer, GameHRC2D lighting)
{
    ecs_world *world = scene->world;
    ecs_entity mirror = ecs_world_create_entity(world);
    GameDestructible destructible = { .cell_size = 2, .min_fragment_cells = 1 };
    GameReflection style = { .roughness = .4f, .angle = 1.5707963f, .offset = -24 };
    assert(GAME_GameTransform_add(world, mirror, 0) == ECS_OK);
    assert(GAME_GameAppearance_add(world, mirror, 0) == ECS_OK);
    assert(GAME_GameCollider_add(world, mirror, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, mirror, GAME_COMPONENT_GameDestructible.id, &destructible, 0) == ECS_OK
    );
    assert(GAME_GameMaterial_add(world, mirror, 0) == ECS_OK);
    assert(
        ecs_world_record_add(world, mirror, GAME_COMPONENT_GameReflection.id, &style, 0) == ECS_OK
    );
    assert(GAME_GameFog_add(world, scene->settings, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, mirror) = (GameTransform){ 24, 0 };
    *GAME_GameAppearance_get(world, mirror) = (GameAppearance){ .shape = GAME_SHAPE_BOX,
                                                                .width = 64,
                                                                .height = 80,
                                                                .antialias = 1 };
    *GAME_GameCollider_get(world, mirror) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                            .width = 64,
                                                            .height = 80,
                                                            .category = 8,
                                                            .mask = 15 };
    *GAME_GameMaterial_get(world, mirror) = (GameMaterial){ .scene = 0x202020ff,
                                                            .albedo = 0x40a0ffff,
                                                            .emission_multiplier = 1 };
    GameReflection *reflection = ecs_component_data(
        world,
        mirror,
        GAME_COMPONENT_GameReflection.id
    );
    GameFog *fog = GAME_GameFog_get(world, scene->settings);
    *fog = (GameFog){ .origin = { -64, -64 },
                      .columns = 16,
                      .rows = 16,
                      .cell_size = 8,
                      .color = 0x123456ff,
                      .disabled = true };
    bool ok = !game_materials_prepare(scene->live->materials);
    const material_mask *mask = game_materials_mask(scene->live->materials, mirror);
    ok &= mask && material_mask_collision(mask).count > 0;
    lighting.tone_mapping = GAME_HRC_TONE_AGX;
    CF_Pixel expected[PIXELS], actual[PIXELS];
    const GameHRCDebugView views[] = {
        GAME_HRC_DEBUG_TRANSPORT_QUADRANTS, GAME_HRC_DEBUG_SURFACE_QUADRANTS,
        GAME_HRC_DEBUG_GATHER_PROBES,       GAME_HRC_DEBUG_SURFACE_QUADRANTS,
        GAME_HRC_DEBUG_GATHER_PROBES,       GAME_HRC_DEBUG_NORMAL
    };
    for (int i = 0; i < 6 && ok; i++) {
        lighting.debug_view = views[i];
        lighting.surface_mode = i == 3 ? GAME_HRC_SURFACE_DEPTH
                              : i == 4 ? GAME_HRC_SURFACE_BLUR
                                       : GAME_HRC_SURFACE_GATHER;
        reflection->strength = 0;
        fog->disabled = true;
        ok = !game_visibility_update(scene->live->visibility, 0) &&
             diagnostic_scene_frame(scene, renderer, lighting, expected);
        /* Exercise each effect and their composition. In normal mode they
         * must change the image, proving these are active fixtures. */
        for (int effects = 1; effects <= 3 && ok; effects++) {
            reflection->strength = effects & 1 ? .8f : 0;
            fog->disabled = !(effects & 2);
            GameFog settings;
            ok = !game_visibility_update(scene->live->visibility, 0) &&
                 (game_visibility_cells(scene->live->visibility, &settings) !=
                  NULL) == !fog->disabled &&
                 diagnostic_scene_frame(scene, renderer, lighting, actual);
            if (ok) {
                bool same = !memcmp(actual, expected, sizeof actual);
                ok = views[i] == GAME_HRC_DEBUG_NORMAL ? !same : same;
            }
            printf(
                "scene view %d surface %d reflection/fog %d: %s\n",
                views[i],
                lighting.surface_mode,
                effects,
                ok ? "PASS" : "FAIL"
            );
        }
    }
    reflection->strength = 0;
    fog->disabled = true;
    ok = ok && !game_visibility_update(scene->live->visibility, 0) &&
         diagnostic_scene_frame(scene, renderer, lighting, actual) &&
         !memcmp(actual, expected, sizeof actual);
    printf("scene normal output restored after reflection/fog: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool diagnostic_scene_presentation()
{
    diagnostic_scene scene = { .world = ecs_world_new(),
                               .viewport = engine_viewport_new(SIDE, SIDE),
                               .canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE)) };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(
            ecs_world_register_component(scene.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
        );
    }
    scene.live = game_live_world_new(scene.world);
    scene.runtime = engine_new(scene.world, scene.live);
    scene.particles = scene.live->particles;
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = lighting.feedback = false;
    /* One means current samples only; the separate transition regression
     * exercises temporal history. Keep this comparison about presentation. */
    lighting.radiance_hysteresis = 1;
    lighting.surface_mode = GAME_HRC_SURFACE_GATHER;
    lighting.tone_mapping = GAME_HRC_TONE_AGX;
    lighting.exposure = 2;
    lighting.bloom = .6f;
    scene.settings = ecs_world_create_entity(scene.world);
    assert(
        ecs_world_record_add(
            scene.world,
            scene.settings,
            GAME_COMPONENT_GameHRC2D.id,
            &lighting,
            0
        ) == ECS_OK
    );
    for (int i = 0; i < 2; i++) {
        ecs_entity entity = ecs_world_create_entity(scene.world);
        GameTransform position = { .x = i ? 12 : -28, .y = i ? -8 : 16 };
        GameAppearance appearance = { .shape = GAME_SHAPE_BOX,
                                      .width = i ? 12 : 18,
                                      .height = i ? 80 : 18,
                                      .antialias = 1 };
        GameMaterial material = { .scene = 0x804020ff,
                                  .albedo = 0xffffffff,
                                  .emission_color = 0xff8040ff,
                                  .emission_intensity = i ? 0 : .2f,
                                  .emission_multiplier = 1,
                                  .absorption = i ? 12 : 0 };
        assert(
            ecs_world_record_add(
                scene.world,
                entity,
                GAME_COMPONENT_GameTransform.id,
                &position,
                0
            ) == ECS_OK
        );
        assert(
            ecs_world_record_add(
                scene.world,
                entity,
                GAME_COMPONENT_GameAppearance.id,
                &appearance,
                0
            ) == ECS_OK
        );
        assert(
            ecs_world_record_add(
                scene.world,
                entity,
                GAME_COMPONENT_GameMaterial.id,
                &material,
                0
            ) == ECS_OK
        );
    }
    assert(ecs_world_apply_commands(scene.world) == ECS_OK);
    engine_viewport_view view = { .width = SIDE, .height = SIDE };
    engine_viewport_set_view(scene.viewport, &view);
    app_scene_renderer *renderer = app_scene_renderer_new();
    CF_Pixel normal[PIXELS], actual[PIXELS], expected[PIXELS];
    bool ok = scene.canvas.id &&
              diagnostic_scene_frame(&scene, renderer, lighting, normal);
    const GameHRCDebugView views[] = { GAME_HRC_DEBUG_TRANSPORT_QUADRANTS,
                                       GAME_HRC_DEBUG_SURFACE_QUADRANTS,
                                       GAME_HRC_DEBUG_GATHER_PROBES,
                                       GAME_HRC_DEBUG_SURFACE_QUADRANTS,
                                       GAME_HRC_DEBUG_GATHER_PROBES };
    for (int i = 0; i < 5 && ok; i++) {
        lighting.debug_view = views[i];
        lighting.surface_mode = i == 3 ? GAME_HRC_SURFACE_DEPTH
                              : i == 4 ? GAME_HRC_SURFACE_BLUR
                                       : GAME_HRC_SURFACE_GATHER;
        GameHRC2D unprocessed = lighting;
        unprocessed.tone_mapping = GAME_HRC_TONE_CLAMP;
        unprocessed.exposure = unprocessed.bloom = 0;
        app_scene_renderer_set_time(renderer, (i + 1) * .137f);
        ok = diagnostic_scene_frame(&scene, renderer, lighting, actual);
        app_scene_renderer_set_time(renderer, 0);
        ok = ok && diagnostic_scene_frame(&scene, renderer, unprocessed, expected);
        if (ok && memcmp(actual, expected, sizeof actual)) {
            for (int p = 0; p < PIXELS; p++) {
                if (memcmp(&actual[p], &expected[p], sizeof *actual)) {
                    fprintf(
                        stderr,
                        "diagnostic presentation view %d pixel %d: %u,%u,%u,%u "
                        "expected %u,%u,%u,%u\n",
                        views[i],
                        p,
                        actual[p].colors.r,
                        actual[p].colors.g,
                        actual[p].colors.b,
                        actual[p].colors.a,
                        expected[p].colors.r,
                        expected[p].colors.g,
                        expected[p].colors.b,
                        expected[p].colors.a
                    );
                    break;
                }
            }
            ok = false;
        }
        int varied = 0;
        for (int p = 0; p < PIXELS && ok; p++) {
            ok &= actual[p].colors.a == 255;
            if (i >= 3) {
                ok &= actual[p].colors.r == 255 && actual[p].colors.g == 0 &&
                      actual[p].colors.b == 255;
            } else {
                varied += memcmp(&actual[p], &actual[0], sizeof *actual) != 0;
            }
        }
        if (ok && i < 3 && varied <= PIXELS / 20) {
            fprintf(stderr, "diagnostic presentation view %d only %d varied pixels\n", views[i], varied);
            ok = false;
        }
        printf(
            "scene diagnostic view %d surface %d bypasses "
            "AgX/exposure/bloom/dither: %s\n",
            views[i],
            lighting.surface_mode,
            ok ? "PASS" : "FAIL"
        );
    }
    lighting.debug_view = GAME_HRC_DEBUG_NORMAL;
    lighting.surface_mode = GAME_HRC_SURFACE_GATHER;
    app_scene_renderer_set_time(renderer, 0);
    ok = ok && diagnostic_scene_frame(&scene, renderer, lighting, actual) &&
         !memcmp(actual, normal, sizeof normal);
    lighting.tone_mapping = GAME_HRC_TONE_CLAMP;
    ok = ok && diagnostic_scene_frame(&scene, renderer, lighting, expected) &&
         memcmp(actual, expected, sizeof actual) != 0;
    printf("scene normal presentation restored after diagnostics: %s\n", ok ? "PASS" : "FAIL");
    ok &= diagnostic_scene_effects(&scene, renderer, lighting);
    app_scene_renderer_free(renderer);
    cf_destroy_canvas(scene.canvas);
    engine_viewport_free(scene.viewport);
    engine_free(scene.runtime);
    game_live_world_free(scene.live);
    ecs_world_free(scene.world);
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

/* Timestamp pass timing charges each pass the GPU time after all earlier
 * work, whatever order command buffers report in: overlap is not counted
 * twice, idle gaps are not counted, and buffers no mark covers accumulate as
 * Unmarked. */
static bool gpu_timestamp_passes()
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
    bool ok = count == 3 && !strcmp(passes[0].name, "A") &&
              passes[0].total_ms == 14 && passes[0].count == 2 &&
              !strcmp(passes[1].name, "B") && passes[1].total_ms == 5 &&
              !strcmp(passes[2].name, "Unmarked") && passes[2].total_ms == 1;
    render_gpu_timing_enable(RENDER_GPU_TIMING_OFF, false);
    if (!ok) {
        fprintf(stderr, "GPU timestamp passes must charge only work after earlier passes\n");
    }
    return ok;
}

/* Color two and four times coarser than brightness keeps light, shadow and
 * bounce close to full color at the same resolution. */
static bool split_luminance_transport(render_material_raster *raster, CF_Canvas canvas)
{
    render_material_entity receiver = wall_with(.2f, .8f);
    receiver.pos[0] = 28;
    receiver.points[0] = 8;
    receiver.extent[0] = 9;
    render_material_entity open_scene[] = { emitter(), receiver };
    render_material_entity blocked_scene[] = { emitter(), wall(), receiver };
    static CF_Pixel full[PIXELS], split[PIXELS], blocked[PIXELS];
    render_radiance *radiance = render_radiance_new();
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = false;
    lighting.fog = 0;
    bool ok = radiance && render_radiance_set_lighting(radiance, lighting) &&
              render_radiance_resize(radiance, SIDE, SIDE) &&
              solve(raster, radiance, canvas, open_scene, 2, SIDE, 8, full);
    for (int shift = 1; shift <= 2 && ok; shift++) {
        lighting.color_resolution = (GameHRCColor)shift;
        ok = render_radiance_set_lighting(radiance, lighting) &&
             render_radiance_get_settings(radiance).scale ==
                 1.0f / (float)(1 << shift) &&
             solve(raster, radiance, canvas, open_scene, 2, SIDE, 8, split) &&
             solve(raster, radiance, canvas, blocked_scene, 3, SIDE, 8, blocked);
        if (!ok) {
            break;
        }
        double full_light = receiver_light(full), split_light = receiver_light(split);
        double shadow = receiver_light(blocked),
               difference = mean_difference(full, split);
        printf(
            "radiance split luminance color=1/%d: receiver %.3f of %.3f, "
            "shadow %.3f, "
            "mean difference %.3f\n",
            1 << shift,
            split_light,
            full_light,
            shadow,
            difference
        );
        ok = split_light > full_light * .9 && split_light < full_light * 1.1 &&
             shadow < split_light * .05 && difference < .25;
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (!ok) {
        fprintf(stderr, "HRC split luminance must stay close to RGB at its scale\n");
    }
    return ok;
}

/* Work cells that are not a whole number of canvas pixels still average each
 * pixel's optical depth once: light through a thin semi-transparent wall
 * matches a grid whose cells are exactly two pixels. */
static bool fractional_cells(render_material_raster *raster, CF_Canvas canvas)
{
    render_material_entity receiver = wall_with(0, .8f);
    receiver.pos[0] = 28;
    receiver.points[0] = 8;
    receiver.extent[0] = 9;
    render_material_entity haze = wall_with(.4f, 0);
    haze.points[0] = 1;
    haze.extent[0] = 2;
    render_material_entity scene[] = { emitter(), haze, receiver };
    static CF_Pixel whole[PIXELS], fractional[PIXELS];
    render_radiance *radiance = render_radiance_new();
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.adaptive = false;
    lighting.feedback = false;
    lighting.fog = 0;
    lighting.resolution = .5f;
    bool ok = radiance && render_radiance_set_lighting(radiance, lighting) &&
              render_radiance_resize(radiance, SIDE, SIDE) &&
              solve(raster, radiance, canvas, scene, 3, SIDE, 2, whole);
    lighting.resolution = .52f;
    ok = ok && render_radiance_set_lighting(radiance, lighting) &&
         solve(raster, radiance, canvas, scene, 3, SIDE, 2, fractional);
    if (ok) {
        double whole_light = receiver_light(whole),
               fractional_light = receiver_light(fractional);
        printf("radiance fractional cells: receiver %.3f of %.3f\n", fractional_light, whole_light);
        ok = fabs(fractional_light - whole_light) < whole_light * .03;
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (!ok) {
        fprintf(stderr, "HRC cells of fractional pixels must not thicken absorbers\n");
    }
    return ok;
}

static bool view_crop_matches(
    const render_view_2d_frame *frame,
    float center_x,
    float center_y,
    float width,
    float height
)
{
    const render_view_2d *view = &frame->view;
    return fabsf(view->min_x + view->width * view->crop_min_u - (center_x - width * .5f)) <
               1e-4f &&
           fabsf(view->min_x + view->width * view->crop_max_u - (center_x + width * .5f)) <
               1e-4f &&
           fabsf(view->max_y - view->height * view->crop_min_v - (center_y + height * .5f)) <
               1e-4f &&
           fabsf(view->max_y - view->height * view->crop_max_v - (center_y - height * .5f)) <
               1e-4f;
}

typedef struct
{
    float center_x, center_y, width, height, margin, scale, min_scale;
    int output_width, output_height, max_raster_size;
} view_grid_case;

static bool view_grid_make(view_grid_case input, render_view_2d_frame *frame)
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
        .max_raster_size = input.max_raster_size,
    };
    return render_view_2d_world_margin(camera, settings, frame);
}

static bool view_grid()
{
    render_view_2d_frame a = { 0 }, wide = { 0 }, capped = { 0 }, edge = { 0 },
                         adaptive = { 0 }, distant = { 0 }, zero_margin = { 0 },
                         odd = { 0 };
    bool ok = view_grid_make(
                  (view_grid_case){
                      .center_x = .25f,
                      .center_y = .35f,
                      .width = 960,
                      .height = 540,
                      .output_width = 960,
                      .output_height = 540,
                      .margin = 64,
                      .scale = .5f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &a
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .center_x = -12,
                      .center_y = 20,
                      .width = 1024,
                      .height = 4000,
                      .output_width = 1280,
                      .output_height = 4000,
                      .margin = 30,
                      .scale = .5f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &wide
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .width = 960,
                      .height = 540,
                      .output_width = 3840,
                      .output_height = 2160,
                      .margin = 64,
                      .scale = .5f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &capped
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .width = 960,
                      .height = 540,
                      .output_width = 4096,
                      .output_height = 1080,
                      .margin = 64,
                      .scale = .5f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &edge
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .center_x = .25f,
                      .center_y = .35f,
                      .width = 960,
                      .height = 540,
                      .output_width = 960,
                      .output_height = 540,
                      .margin = 64,
                      .scale = .9f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &adaptive
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .center_x = 1000000.25f,
                      .center_y = -1000000.5f,
                      .width = 960,
                      .height = 540,
                      .output_width = 960,
                      .output_height = 540,
                      .margin = 64,
                      .scale = .5f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &distant
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .width = 960,
                      .height = 540,
                      .output_width = 960,
                      .output_height = 540,
                      .scale = 1,
                      .min_scale = 1,
                      .max_raster_size = 4096,
                  },
                  &zero_margin
              ) &&
              view_grid_make(
                  (view_grid_case){
                      .width = 1001,
                      .height = 563,
                      .output_width = 1001,
                      .output_height = 563,
                      .margin = 64,
                      .scale = .25f,
                      .min_scale = .1f,
                      .max_raster_size = 4096,
                  },
                  &odd
              );
    ok &= adaptive.raster_width == a.raster_width &&
          adaptive.raster_height == a.raster_height;
    for (int i = 0; i < 128 && ok; i++) {
        float center_x = -5 + i * .13f;
        float center_y = 4 - i * .09f;
        render_view_2d_frame moving;
        ok = view_grid_make(
                 (view_grid_case){
                     .center_x = center_x,
                     .center_y = center_y,
                     .width = 960,
                     .height = 540,
                     .output_width = 960,
                     .output_height = 540,
                     .margin = 64,
                     .scale = .5f,
                     .min_scale = .1f,
                     .max_raster_size = 4096,
                 },
                 &moving
             ) &&
             moving.raster_width == a.raster_width &&
             moving.raster_height == a.raster_height;
        if (!ok) {
            break;
        }
        const render_view_2d *view = &moving.view;
        float left = view->min_x + view->width * view->crop_min_u;
        float right = view->min_x + view->width * view->crop_max_u;
        float top = view->max_y - view->height * view->crop_min_v;
        float bottom = view->max_y - view->height * view->crop_max_v;
        float cell_x = view->width / ceilf(moving.raster_width * .5f);
        float cell_y = view->height / ceilf(moving.raster_height * .5f);
        ok &= fabsf(left - (center_x - 480)) < 1e-4f &&
              fabsf(right - (center_x + 480)) < 1e-4f &&
              fabsf(top - (center_y + 270)) < 1e-4f &&
              fabsf(bottom - (center_y - 270)) < 1e-4f &&
              fabsf(view->min_x / cell_x - roundf(view->min_x / cell_x)) < 1e-4f &&
              fabsf(view->max_y / cell_y - roundf(view->max_y / cell_y)) < 1e-4f;
    }
    ok &= wide.raster_width <= 4096 && wide.raster_height <= 4096 &&
          capped.raster_width <= 4096 && capped.raster_height <= 4096 &&
          capped.margin_x < 64 && capped.margin_y == 64 &&
          edge.raster_width == 4096 && edge.margin_x == 0 &&
          !edge.grid_aligned && view_crop_matches(&wide, -12, 20, 1024, 4000) &&
          view_crop_matches(&capped, 0, 0, 960, 540) &&
          view_crop_matches(&edge, 0, 0, 960, 540) &&
          view_crop_matches(&distant, 1000000.25f, -1000000.5f, 960, 540) &&
          zero_margin.margin_x == 0 && zero_margin.margin_y == 0 &&
          zero_margin.grid_aligned && odd.grid_aligned;
    /* Scales of 1/2^n keep cells of whole pixels at any output size. */
    const render_view_2d_frame *gridded[] = { &a,        &wide,    &capped,
                                              &adaptive, &distant, &odd };
    for (int i = 0; i < (int)(sizeof gridded / sizeof *gridded); i++) {
        ok &= gridded[i]->raster_width % RENDER_VIEW_2D_RASTER_STEP == 0 &&
              gridded[i]->raster_height % RENDER_VIEW_2D_RASTER_STEP == 0;
    }
    render_view_2d_frame too_wide;
    ok &= !view_grid_make(
        (view_grid_case){
            .width = 1,
            .height = 300,
            .output_width = 4097,
            .output_height = 300,
            .scale = .5f,
            .min_scale = .1f,
            .max_raster_size = 4096,
        },
        &too_wide
    );
    /* Snapping by whole work cells keeps the grid world-aligned without
     * resizing textures as the camera moves. */
    int snapped_pixels = 0;
    for (int i = 0; i < 16; i++) {
        render_view_2d_axis_frame axis;
        ok &= render_view_2d_axis(
            (render_view_2d_axis_input){ .center = i * .37f,
                                         .span = 960,
                                         .margin = 64,
                                         .scale = .5f,
                                         .min_scale = .1f,
                                         .output_pixels = 1920,
                                         .max_pixels = 4096 },
            &axis
        );
        double cell = axis.raster_pixels / 2.0 / ceil(axis.raster_pixels * .5);
        double steps = axis.origin / cell;
        ok &= axis.grid_aligned && fabs(steps - round(steps)) < 1e-3 &&
              (!i || axis.raster_pixels == snapped_pixels);
        snapped_pixels = axis.raster_pixels;
    }
    if (!ok) {
        fprintf(stderr, "HRC world margin must preserve its grid, crop, aspect and 4K bound\n");
    }
    return ok;
}

/* Lighting outside what HRC supports is fitted, not rejected, and each change
 * is noted; the fitted lighting is always accepted. */
static bool normalized_lighting(render_radiance *radiance)
{
    char notes[256];
    GameHRC2D defaults = game_hrc2d_defaults();
    GameHRC2D fitted = render_radiance_normalize(defaults, notes, sizeof notes);
    bool ok = !notes[0] && fitted.resolution == defaults.resolution;

    GameHRC2D wild = defaults;
    wild.resolution = 2;
    wild.min_resolution = 0;
    wild.color_resolution = (GameHRCColor)7;
    wild.gather_resolution = (GameHRCGather)-1;
    wild.trace_levels = 9;
    wild.bloom = 2;
    wild.exposure = NAN;
    fitted = render_radiance_normalize(wild, notes, sizeof notes);
    ok &= fitted.resolution == 1 && fitted.min_resolution == .1f &&
          fitted.color_resolution == GAME_HRC_COLOR_EIGHTH &&
          fitted.gather_resolution == GAME_HRC_GATHER_FULL &&
          fitted.trace_levels == 5 && fitted.bloom == 1 &&
          fitted.exposure == 0 && strstr(notes, "resolution 2 -> 1") &&
          strstr(notes, "trace_levels 9 -> 5") &&
          render_radiance_set_lighting(radiance, fitted);

    /* The coarsest color at the lowest resolution still has a grid. */
    GameHRC2D coarse = defaults;
    coarse.adaptive = false;
    coarse.resolution = .1f;
    coarse.color_resolution = GAME_HRC_COLOR_EIGHTH;
    ok &= render_radiance_set_lighting(radiance, coarse) &&
          render_radiance_resize(radiance, SIDE, SIDE) &&
          render_radiance_set_lighting(radiance, defaults);
    if (!ok) {
        fprintf(stderr, "HRC lighting normalization: %s\n", notes);
    }
    return ok;
}

/* Lays out a 1280x720 view for the renderer's grids, as the scene renderer
 * does, reserving room for adaptive's lowest step. */
static bool lighting_view_fits(const render_radiance *radiance)
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
static bool lighting_transitions()
{
    const int sizes[][2] = { { 1288, 728 }, { 200, 136 } };
    const float resolutions[] = { 1, .9f, .75f, .6f, .5f, .37f, .25f, .1f };
    render_radiance *radiance = render_radiance_new();
    bool ok = radiance != NULL;
    for (int size = 0; size < 2 && ok; size++) {
        ok = render_radiance_resize(radiance, sizes[size][0], sizes[size][1]);
        for (int i = 0; i < 8 * 4 * 2 * 2 && ok; i++) {
            int r = i / 16, color = i / 4 % 4, half = i / 2 % 2, adaptive = i % 2;
            GameHRC2D lighting = game_hrc2d_defaults();
            lighting.adaptive = adaptive;
            lighting.resolution = resolutions[r];
            lighting.color_resolution = (GameHRCColor)color;
            lighting.gather_resolution = half ? GAME_HRC_GATHER_HALF
                                              : GAME_HRC_GATHER_FULL;
            ok = render_radiance_set_lighting(radiance, lighting) &&
                 lighting_view_fits(radiance);
            if (!ok) {
                fprintf(
                    stderr,
                    "HRC lighting transition failed: %dx%d resolution %g color "
                    "%d gather half %d adaptive %d\n",
                    sizes[size][0],
                    sizes[size][1],
                    resolutions[r],
                    color,
                    half,
                    adaptive
                );
            }
        }
    }
    render_radiance_free(radiance);
    return ok;
}

static void feed_lighting_time(render_radiance *radiance, double ms, int frames)
{
    for (int i = 0; i < frames; i++) {
        render_radiance_frame_time(radiance, ms);
    }
}

static bool rung_is(render_radiance *radiance, int rung, float resolution, GameHRCColor color, bool gather_half)
{
    render_radiance_status status = render_radiance_get_status(radiance);
    return status.rung == rung && status.resolution == resolution &&
           status.color == color && status.gather_half == gather_half;
}

/* Over budget, adaptive halves gather first when it is Auto, then coarsens
 * color up to Quarter at the authored resolution, then halves resolution down
 * to min_resolution with the authored color. It steps back up once the
 * measured cost of the step above fits, and restarts when the ladder changes. */
static bool adaptive_ladder(render_radiance *radiance)
{
    const GameHRCColor half = GAME_HRC_COLOR_HALF, quarter = GAME_HRC_COLOR_QUARTER;
    GameHRC2D lighting = game_hrc2d_defaults();
    lighting.resolution = 1;
    lighting.color_resolution = half;
    lighting.gather_resolution = GAME_HRC_GATHER_AUTO;
    lighting.min_resolution = .25f;
    lighting.target_fps = 125; /* 8 ms */
    bool ok = render_radiance_set_lighting(radiance, lighting) &&
              render_radiance_get_status(radiance).rungs == 5 &&
              rung_is(radiance, 0, 1, half, false) &&
              render_radiance_get_settings(radiance).min_scale == .125f;
    feed_lighting_time(radiance, 20, 60);
    ok &= rung_is(radiance, 1, 1, half, true);
    /* A step's first frames refill history and are not judged, and one
     * window over budget is not sustained load. */
    feed_lighting_time(radiance, 20, 89);
    ok &= rung_is(radiance, 1, 1, half, true);
    feed_lighting_time(radiance, 12, 1);
    ok &= rung_is(radiance, 2, 1, quarter, true) &&
          render_radiance_get_settings(radiance).scale == .25f;
    feed_lighting_time(radiance, 12, 90);
    ok &= rung_is(radiance, 3, .5f, half, true) &&
          render_radiance_get_settings(radiance).scale == .25f;
    feed_lighting_time(radiance, 12, 90);
    ok &= rung_is(radiance, 4, .25f, half, true);
    /* The lowest step holds while still over budget. */
    feed_lighting_time(radiance, 9, 90);
    ok &= rung_is(radiance, 4, .25f, half, true);
    /* 12 ms above against 9 here: 2 ms predicts 2.7 above, within budget. */
    feed_lighting_time(radiance, 2, 90);
    ok &= rung_is(radiance, 4, .25f, half, true);
    feed_lighting_time(radiance, 2, 30);
    ok &= rung_is(radiance, 3, .5f, half, true);
    /* Authoring that keeps the ladder keeps the step; a new ladder restarts. */
    lighting.target_fps = 250;
    ok &= render_radiance_set_lighting(radiance, lighting) &&
          rung_is(radiance, 3, .5f, half, true);
    lighting.resolution = .5f;
    ok &= render_radiance_set_lighting(radiance, lighting) &&
          rung_is(radiance, 0, .5f, half, false);
    lighting.adaptive = false;
    ok &= render_radiance_set_lighting(radiance, lighting) &&
          render_radiance_get_status(radiance).rungs == 1 &&
          render_radiance_get_settings(radiance).scale == .25f;
    feed_lighting_time(radiance, 50, 120);
    ok &= rung_is(radiance, 0, .5f, half, false);
    /* Frame intervals at the display's rate fit; missed frames step down. */
    lighting.adaptive = true;
    lighting.target_fps = 60;
    ok &= render_radiance_set_lighting(radiance, lighting);
    for (int i = 0; i < 120; i++) {
        render_radiance_frame_interval(radiance, 1000.0 / 60 + (i % 2 ? 1.5 : -.5));
    }
    ok &= rung_is(radiance, 0, .5f, half, false);
    for (int i = 0; i < 90; i++) {
        render_radiance_frame_interval(radiance, 1000.0 / 30);
    }
    ok &= rung_is(radiance, 1, .5f, half, true);
    if (!ok) {
        render_radiance_status status = render_radiance_get_status(radiance);
        fprintf(
            stderr,
            "HRC adaptive ladder: rung %d of %d, resolution %.3f, color %d, "
            "gather half %d\n",
            status.rung,
            status.rungs,
            status.resolution,
            (int)status.color,
            status.gather_half
        );
    }
    return ok;
}

int test_radiance()
{
    extern bool test_radiance_intervals();
    extern bool test_radiance_history();
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
    extern bool test_surface_gather();
    extern bool test_surface_depth();
    extern bool test_sdf_spatial();
    extern int test_shader_program_gpu();
    extern int test_shader_graph_gpu();
    render_material_raster *raster = render_material_raster_new();
    render_radiance *radiance = render_radiance_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    bool ok = raster && radiance && canvas.id && view_grid() &&
              normalized_lighting(radiance) && lighting_transitions() &&
              gpu_timestamp_passes();
    if (!ok) {
        fprintf(
            stderr,
            "radiance initialization: raster %d, transport %d, canvas %d\n",
            raster != NULL,
            radiance != NULL,
            canvas.id != 0
        );
    }
    CF_Pixel open[PIXELS], blocked[PIXELS], reset[PIXELS];
    render_material_entity entities[] = { emitter(), wall() };
    if (ok) {
        ok = render_material_raster_resize(raster, SIDE, SIDE) &&
             render_radiance_resize(radiance, SIDE, SIDE);
        if (!ok) {
            fprintf(stderr, "radiance target resize failed\n");
        }
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, 4, open);
    }
    if (ok) {
        CF_Pixel center = open[64 * SIDE + 36];
        CF_Pixel border = open[64 * SIDE + 43];
        ok = center.colors.g > center.colors.r && center.colors.g > 100 &&
             border.colors.r > border.colors.g;
        if (!ok) {
            fprintf(
                stderr,
                "radiance emitter/border: center %u,%u border %u,%u\n",
                center.colors.r,
                center.colors.g,
                border.colors.r,
                border.colors.g
            );
        }
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 2, SIDE, 4, blocked);
    }
    if (ok) {
        ok = material_pixels(render_material_raster_textures(raster));
        if (!ok) {
            fprintf(stderr, "radiance source material emission/border/absorption mismatch\n");
        }
    }
    if (ok) {
        double before = receiver_light(open), after = receiver_light(blocked);
        printf("radiance receiver: open %.3f, occluded %.3f\n", before, after);
        ok = before > 3 && after < before * 0.8;
    }
    CF_Pixel semi[PIXELS];
    if (ok) {
        render_material_entity finite_entities[] = { emitter(), wall_with(.2f, 0) };
        ok = solve(raster, radiance, canvas, finite_entities, 2, SIDE, 4, semi);
    }
    if (ok) {
        double before = receiver_light(open);
        double semi_light = receiver_light(semi);
        double after = receiver_light(blocked);
        printf("radiance absorption: open %.3f, finite %.3f, opaque %.3f\n", before, semi_light, after);
        ok = semi_light > after + 0.1 && semi_light < before;
    }
    CF_Pixel bounce_one[PIXELS], bounce_settled[PIXELS];
    if (ok) {
        render_material_entity bounce_entities[] = { emitter(), wall_with(.2f, .8f) };
        ok = solve(raster, radiance, canvas, bounce_entities, 2, SIDE, 1, bounce_one) &&
             solve(raster, radiance, canvas, bounce_entities, 2, SIDE, 8, bounce_settled);
    }
    if (ok) {
        double first = receiver_light(bounce_one);
        double settled = receiver_light(bounce_settled);
        printf("radiance bounce: first %.3f, settled %.3f\n", first, settled);
        ok = settled > first + 0.1;
    }
    if (ok) {
        /* Transparent reflective edges must not turn reflected light into an
         * unattenuated light source on every feedback pass. */
        render_material_entity glass[] = { emitter(), wall_with(.0005f, .9f) };
        ok = solve(raster, radiance, canvas, glass, 2, SIDE, 1, bounce_one) &&
             solve(raster, radiance, canvas, glass, 2, SIDE, 120, bounce_settled);
        double first = receiver_light(bounce_one);
        double settled = receiver_light(bounce_settled);
        printf("radiance transparent feedback: early %.3f, settled %.3f\n", first, settled);
        ok &= first > 1 && settled > first * .5 && settled < first * 1.25 + 1;
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, 4, reset);
    }
    if (ok) {
        for (int i = 0; i < PIXELS; i++) {
            const unsigned char *a = (const unsigned char *)&open[i];
            const unsigned char *b = (const unsigned char *)&reset[i];
            for (int c = 0; c < 4; c++) {
                if (abs(a[c] - b[c]) > 2) {
                    ok = false;
                }
            }
        }
        if (!ok) {
            fprintf(stderr, "radiance reset retained occluder history\n");
        }
    }
    if (ok) {
        ok = save_capture(blocked);
    }
    if (ok) {
        cf_destroy_canvas(canvas);
        canvas = cf_make_canvas(cf_canvas_defaults(64, 64));
        ok = canvas.id && render_material_raster_resize(raster, 64, 64) &&
             render_radiance_resize(radiance, 64, 64) &&
             solve(raster, radiance, canvas, NULL, 0, 64, 4, reset);
        if (ok) {
            for (int i = 0; i < 64 * 64; i++) {
                if (reset[i].colors.r > 2 || reset[i].colors.g > 2 ||
                    reset[i].colors.b > 2) {
                    fprintf(
                        stderr,
                        "HRC empty resized pixel %d: %d %d %d\n",
                        i,
                        reset[i].colors.r,
                        reset[i].colors.g,
                        reset[i].colors.b
                    );
                    ok = false;
                    break;
                }
            }
        }
    }
    if (ok) {
        render_radiance_settings settings = render_radiance_get_settings(radiance);
        settings.scale = .976883f;
        ok = render_radiance_set_settings(radiance, settings) &&
             fabsf(render_radiance_get_settings(radiance).scale - .976883f) < .000001f &&
             solve(raster, radiance, canvas, NULL, 0, 64, 4, reset);
        if (!ok) {
            fprintf(stderr, "HRC must preserve fractional authored resolution\n");
        }
    }
    if (ok) {
        render_material_entity light = emitter();
        light.pos[0] = 0;
        const float scales[] = { .5f, .25f, .5f, .976883f };
        for (int scale = 0; scale < 4 && ok; scale++) {
            render_radiance_settings settings = render_radiance_get_settings(radiance);
            settings.scale = scales[scale];
            render_radiance *fresh = render_radiance_new();
            ok = fresh && render_radiance_set_settings(fresh, settings) &&
                 render_radiance_resize(fresh, 64, 64) &&
                 render_radiance_set_settings(radiance, settings) &&
                 solve(raster, radiance, canvas, &light, 1, 64, 4, reset) &&
                 solve(raster, fresh, canvas, &light, 1, 64, 4, open);
            for (int i = 0; i < 64 * 64 && ok; i++) {
                ok = abs((int)reset[i].colors.r - open[i].colors.r) <= 2 &&
                     abs((int)reset[i].colors.g - open[i].colors.g) <= 2 &&
                     abs((int)reset[i].colors.b - open[i].colors.b) <= 2;
            }
            render_radiance_free(fresh);
        }
        if (!ok) {
            fprintf(stderr, "HRC reused capacity must match a fresh work grid\n");
        }
    }
    if (ok) {
        ok = graph_material_pixels(raster) && material_frame_pixels(raster);
    }
    ok &= adaptive_ladder(radiance);
    ok &= fluid_alignment(raster);
    ok &= fluid_surfaces(raster);
    cf_destroy_canvas(canvas);
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    ok &= canvas.id && render_radiance_resize(radiance, SIDE, SIDE);
    for (int levels = 1; levels <= 3; levels += 2) {
        ok &= silhouette_lighting(raster, radiance, canvas, levels);
        ok &= fluid_lighting(raster, radiance, canvas, levels);
    }
    ok &= surface_lighting(raster, radiance, canvas);
    const GameHRCShadowMode shadow_modes[] = { GAME_HRC_SHADOW_REFERENCE,
                                               GAME_HRC_SHADOW_HORIZON4,
                                               GAME_HRC_SHADOW_HORIZON8 };
    for (int mode = 0; mode < 3; mode++) {
        ok &= directional_surface_colors(raster, shadow_modes[mode]);
        if (mode) {
            ok &= horizon_updates(raster, shadow_modes[mode]);
            ok &= horizon_hierarchy_quality(raster, shadow_modes[mode]);
            for (int fused = 0; fused < 2; fused++) {
                for (int reproject = 0; reproject < 2; reproject++) {
                    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
                        ok &= horizon_reuse_sequence(raster, shadow_modes[mode], fused, reproject, hierarchy);
                    }
                }
            }
        }
    }
    ok &= surface_history(raster);
    ok &= surface_history_after_probe_transition(raster);
    ok &= surface_mode_debug_views(raster);
    ok &= diagnostic_quadrant_mapping();
    ok &= diagnostic_scene_presentation();
    ok &= split_luminance_transport(raster, canvas);
    ok &= fractional_cells(raster, canvas);
    ok &= liquid_refraction(raster, radiance, canvas);
    ok &= reflection_pixels();
    ok &= reconstruction_sampling(raster);
    ok &= test_radiance_intervals();
    ok &= test_radiance_history();
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
    ok &= test_surface_gather();
    ok &= test_surface_depth();
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
