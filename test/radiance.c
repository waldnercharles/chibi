#include "render/radiance.h"

#include "graph/runtime/instance.h"
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "runtime/generated.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
    float result = exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}

static bool material_pixels(render_material_textures textures)
{
    CF_Texture inputs[] = { textures.emission, textures.transmittance };
    uint16_t pixels[PIXELS * 4];
    for (int i = 0; i < 2; i++) {
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
        if (i) {
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(half_value(pixels[sample + channel]) - 1) > 0.002f) {
                    return false;
                }
            }
        } else {
            if (fabsf(half_value(pixels[sample + 1]) - 0.08f) > 0.002f) {
                return false;
            }
            sample = 4 * (64 * SIDE + 43);
            if (fabsf(half_value(pixels[sample]) - 0.2f) > 0.002f) {
                return false;
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
        .transmittance = { 1, 1, 1, 1 },
    };
    return entity;
}

static bool solve(
    render_material_raster *raster,
    render_radiance *radiance,
    CF_Canvas canvas,
    const render_material_entity *entities,
    int count,
    int size,
    CF_Pixel *pixels
)
{
    render_radiance_reset(radiance);
    for (int frame = 0; frame < 4; frame++) {
        cf_app_update(NULL);
        if (!render_material_raster_draw(
                raster,
                entities,
                count,
                (render_material_view){ -size * 0.5f, size * 0.5f, size, size }
            )) {
            cf_app_draw_onto_screen(false);
            return false;
        }
        render_material_textures textures = render_material_raster_textures(raster);
        render_radiance_inputs inputs = { textures.scene,
                                          textures.emission,
                                          textures.transmittance,
                                          textures.albedo };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, size, size));
        bool ok = render_radiance_draw(radiance, inputs, canvas);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        if (!ok) {
            return false;
        }
    }
    return read_pixels(canvas, pixels, size * size);
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

static bool save_capture(const CF_Pixel *pixels)
{
    const char *path = getenv("CBLUEPRINT_RADIANCE_SCREENSHOT");
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

static bool material_parameter_writes(const behavior_asset *material, int id)
{
    ecs_world *world = ecs_world_new();
    bool ok = ecs_world_register_component(world, &GAME_COMPONENT_GameShader, NULL) ==
              ECS_OK;
    ecs_entity first = ecs_world_create_entity(world),
               second = ecs_world_create_entity(world);
    GameShader binding = { .material = material->name,
                           .parameters = "{\"Tint\":[0,1,0,1]}" };
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
    float blue[4] = { 0, 0, 1, 1 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec4", .ptr = blue };
    ok &= graph_set_pin_default(g, write, 0, 1, &target) == 0;
    ok &= graph_set_pin_default(g, write, 0, 2, &name) == 0;
    ok &= graph_set_pin_type(g, write, 0, 3, PIN_STRUCT, "ShaderVec4") == 0;
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
    behavior_instance_destroy(instance);
    behavior_asset_free(behavior);
    ecs_world_free(world);
    return ok;
}

static bool graph_material_pixels(render_material_raster *raster)
{
    behavior_asset *material = behavior_asset_new("GPU material regression");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    node *output = shader_graph_add_output(material->graph, false);
    float color[4] = { 1, 0, 0, 1 };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec4", .ptr = color };
    node *parameter = graph_add_node(material->graph, registry_node_by_name("Shader Parameter"));
    bool ok = graph_set_node_reference(material->graph, parameter, 0, "Tint") == 0;
    ok &= graph_set_pin_type(material->graph, parameter, 1, 0, PIN_STRUCT, "ShaderVec4") ==
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
    CF_Shader fragment = render_shader_fragment_new();
    ok &= fragment.id != 0;
    if (fragment.id) {
        CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(16, 16));
        cf_app_update(NULL);
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, 16, 16));
        render_shader_fragment_draw(fragment, &entity);
        cf_render_to(canvas, true);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        CF_Pixel pixels[256];
        ok &= read_pixels(canvas, pixels, 256);
        ok &= pixels[8 * 16 + 8].colors.b > 200 && pixels[0].colors.a < 5;
        cf_destroy_canvas(canvas);
        cf_destroy_shader(fragment);
    }
    behavior_asset_free(shape);
    behavior_asset_free(material);
    printf("shader-material %s: graph GPU channels, hot replacement, coverage and fragment compilation\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* The host initializes Cute and mounts/configures render/shaders before this call. */
int test_radiance()
{
    render_material_raster *raster = render_material_raster_new();
    render_radiance *radiance = render_radiance_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    bool ok = raster && radiance && canvas.id;
    CF_Pixel open[PIXELS], blocked[PIXELS], reset[PIXELS];
    render_material_entity entities[] = { emitter(), wall() };
    if (ok) {
        ok = render_material_raster_resize(raster, SIDE, SIDE) &&
             render_radiance_resize(radiance, SIDE, SIDE);
    }
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, open);
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
        ok = solve(raster, radiance, canvas, entities, 2, SIDE, blocked);
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
    if (ok) {
        ok = solve(raster, radiance, canvas, entities, 1, SIDE, reset);
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
             solve(raster, radiance, canvas, NULL, 0, 64, reset);
        if (ok) {
            for (int i = 0; i < 64 * 64; i++) {
                if (reset[i].colors.r > 2 || reset[i].colors.g > 2 ||
                    reset[i].colors.b > 2) {
                    ok = false;
                }
            }
        }
    }
    if (ok) {
        ok = graph_material_pixels(raster);
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    printf("radiance %s: emissive fill/border, light transport, occlusion, reset and resize\n", ok ? "PASS" : "FAIL");
    return !ok;
}
