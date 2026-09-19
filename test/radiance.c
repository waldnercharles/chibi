#include "render/radiance.h"

#include "graph/runtime/instance.h"
#include "platform/cute.h"
#include "render/light_field_2d.h"
#include "render/material_raster.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "runtime/generated.h"

#include <math.h>
#include <stdint.h>
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
    CF_Texture inputs[] = { textures.emission, textures.absorption, textures.normal };
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
            for (int channel = 0; channel < 4; channel++) {
                if (fabsf(half_value(pixels[sample + channel]) - 1) > 0.002f) {
                    return false;
                }
            }
        } else {
            sample = 4 * (64 * SIDE + 43);
            if (half_value(pixels[sample]) < 0.4f ||
                fabsf(half_value(pixels[sample + 1])) > 0.2f ||
                half_value(pixels[sample + 3]) < 0.5f) {
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
    assert(frames > 0);
    render_radiance_reset(radiance);
    for (int frame = 0; frame < frames; frame++) {
        cf_app_update(NULL);
        if (!render_material_raster_draw(
                raster,
                entities,
                count,
                (render_material_view){ .min_x = -size * 0.5f,
                                        .max_y = size * 0.5f,
                                        .width = size,
                                        .height = size,
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
        render_radiance_inputs inputs = { textures.scene,
                                          textures.emission,
                                          textures.absorption,
                                          textures.albedo,
                                          textures.normal };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, size, size));
        bool ok = render_radiance_draw(
            radiance,
            inputs,
            (render_view_2d){ .min_x = -size * 0.5f,
                              .max_y = size * 0.5f,
                              .width = size,
                              .height = size,
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

static CF_Texture blank_hdr_texture(int width, int height)
{
    CF_TextureParams params = cf_texture_defaults(width, height);
    params.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    params.usage = CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(params);
    if (!texture.id) {
        return texture;
    }
    size_t bytes = (size_t)width * (size_t)height * 8;
    void *zeros = calloc(1, bytes);
    if (!zeros) {
        cf_destroy_texture(texture);
        return (CF_Texture){ 0 };
    }
    cf_texture_update(texture, zeros, (int)bytes);
    free(zeros);
    return texture;
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

static render_view_2d light_view(int width, int height)
{
    return (render_view_2d){ .min_x = -width * 0.5f,
                             .max_y = height * 0.5f,
                             .width = width,
                             .height = height,
                             .crop_min_u = 0,
                             .crop_min_v = 0,
                             .crop_max_u = 1,
                             .crop_max_v = 1 };
}

static bool light_field_checks()
{
    enum
    {
        width = 64,
        height = 64,
        pixel_count = width * height
    };
    render_light_field_2d *field = render_light_field_2d_new();
    CF_Texture base = blank_hdr_texture(width, height);
    uint16_t *pixels = calloc((size_t)pixel_count * 4, sizeof *pixels);
    render_point_light_2d sources[128];
    bool ok = field && base.id && pixels;
    if (ok) {
        ok = render_light_field_2d_resize(field, width, height);
    }
    for (int i = 0; i < 128; i++) {
        sources[i] = (render_point_light_2d){ .position = { 0, 0 },
                                              .intensity = 2,
                                              .radius = 20,
                                              .color = { 0.25f, 0.5f, 1, 1 } };
    }
    float one = 0;
    float two = 0;
    render_light_field_stats stats = { 0 };
    bool additive_ok = false;
    bool counts_ok = false;
    bool clipping_ok = false;
    bool padded_ok = false;
    bool reset_ok = false;
    if (ok) {
        for (int count = 1; count <= 2; count++) {
            render_light_field_2d_reset(field);
            cf_app_update(NULL);
            ok &= render_light_field_2d_draw(field, sources, count, light_view(width, height));
            ok &= render_light_field_2d_composite(field, base);
            cf_app_draw_onto_screen(false);
            ok &= read_hdr_texture(render_light_field_2d_texture(field), pixels, pixel_count);
            float value = half_value(pixels[4 * (32 * width + 32) + 2]);
            if (count == 1) {
                one = value;
            } else {
                two = value;
            }
        }
        additive_ok = one > 0.5f && fabsf(two - one * 2) < 0.03f;
        ok &= additive_ok;
    }
    if (ok) {
        const int counts[] = { 64, 96, 128 };
        counts_ok = true;
        for (int i = 0; i < 3; i++) {
            int count = counts[i];
            render_light_field_2d_reset(field);
            cf_app_update(NULL);
            ok &= render_light_field_2d_draw(field, sources, count, light_view(width, height));
            stats = render_light_field_2d_stats(field);
            bool count_ok = stats.source_count == count &&
                            stats.accepted_source_count == count &&
                            stats.clipped_source_count == 0 &&
                            stats.capacity >= count &&
                            stats.max_tile_occupancy == count;
            counts_ok &= count_ok;
            ok &= count_ok;
        }
    }
    if (ok) {
        sources[0].position = (CF_V2){ 48, 0 };
        render_light_field_2d_reset(field);
        cf_app_update(NULL);
        ok &= render_light_field_2d_draw(field, sources, 1, light_view(width, height));
        stats = render_light_field_2d_stats(field);
        clipping_ok = stats.source_count == 1 && stats.accepted_source_count == 1 &&
                      stats.clipped_source_count == 1 &&
                      render_light_field_2d_error(field)[0];
        ok &= clipping_ok;
    }
    if (ok) {
        enum
        {
            padded_width = 96,
            padded_height = 96,
            padded_pixels_count = 96 * 96
        };
        CF_Texture padded_base = blank_hdr_texture(padded_width, padded_height);
        uint16_t *padded_pixels = calloc((size_t)padded_pixels_count * 4, sizeof *padded_pixels);
        render_view_2d padded = { .min_x = -48,
                                  .max_y = 48,
                                  .width = 96,
                                  .height = 96,
                                  .crop_min_u = 1.0f / 6,
                                  .crop_min_v = 1.0f / 6,
                                  .crop_max_u = 5.0f / 6,
                                  .crop_max_v = 5.0f / 6 };
        sources[0] = (render_point_light_2d){ .position = { -40, 0 },
                                              .intensity = 2,
                                              .radius = 8,
                                              .color = { 0.25f, 0.5f, 1, 1 } };
        bool ready = padded_base.id && padded_pixels &&
                     render_light_field_2d_resize(field, padded_width, padded_height);
        if (ready) {
            render_light_field_2d_reset(field);
            cf_app_update(NULL);
            ready = render_light_field_2d_draw(field, sources, 1, padded) &&
                    render_light_field_2d_composite(field, padded_base);
            cf_app_draw_onto_screen(false);
        }
        if (ready) {
            stats = render_light_field_2d_stats(field);
            ready = read_hdr_texture(render_light_field_2d_texture(field), padded_pixels, padded_pixels_count) &&
                    half_value(padded_pixels[4 * (48 * padded_width + 8) + 2]) > 1.0f;
            padded_ok = stats.source_count == 1 && stats.accepted_source_count == 1 &&
                        stats.clipped_source_count == 0 && ready;
        }
        ok &= padded_ok;
        if (padded_base.id) {
            cf_destroy_texture(padded_base);
        }
        free(padded_pixels);
    }
    if (field) {
        render_light_field_2d_reset(field);
        stats = render_light_field_2d_stats(field);
        reset_ok = stats.source_count == 0 && stats.accepted_source_count == 0 &&
                   stats.clipped_source_count == 0 &&
                   !render_light_field_2d_error(field)[0];
        ok &= reset_ok;
        ok &= render_light_field_2d_resize(field, 80, 64);
        stats = render_light_field_2d_stats(field);
        ok &= stats.tile_count == 5 * 4 && stats.capacity == 0;
    }
    if (!ok) {
        fprintf(
            stderr,
            "light field details: field=%p base=%llu one=%g two=%g additive=%d "
            "counts=%d clipping=%d reset=%d source=%d accepted=%d clipped=%d "
            "occupancy=%d capacity=%d error=%s\n",
            (void *)field,
            (unsigned long long)base.id,
            one,
            two,
            additive_ok,
            counts_ok,
            clipping_ok,
            reset_ok,
            stats.source_count,
            stats.accepted_source_count,
            stats.clipped_source_count,
            stats.max_tile_occupancy,
            stats.capacity,
            render_light_field_2d_error(field)
        );
    }
    if (field) {
        render_light_field_2d_free(field);
    }
    if (base.id) {
        cf_destroy_texture(base);
    }
    free(pixels);
    printf("light field %s: additive emission, 64/96/128 sources, padded sources, diagnostics and resize\n", ok ? "PASS" : "FAIL");
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
                           .parameters = "{\"Tint\":[0,1,0]}" };
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
    char edited[] = "{\"Tint\":[1,0,0]}";
    render_shader_parameters(id, world, second, edited, b);
    ok &= b[0][0] == 1 && b[0][1] == 0;
    memcpy(edited, "{\"Tint\":[0,0,1]}", sizeof edited);
    render_shader_parameters(id, world, second, edited, b);
    ok &= b[0][0] == 0 && b[0][2] == 1;
    render_shader_parameters(id, world, first, binding.parameters, a);
    ok &= a[0][1] == 1 && a[0][2] == 0;
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

/* The host initializes Cute and mounts/configures render/shaders before this call. */
int test_radiance()
{
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
        ok = graph_material_pixels(raster);
    }
    if (ok) {
        GameHRC2D lighting = game_hrc2d_defaults();
        lighting.scale = .5f;
        ok = render_radiance_set_lighting(radiance, lighting);
        for (int i = 0; i < 30; i++) {
            render_radiance_frame_time(radiance, .05);
        }
        float reduced = render_radiance_get_settings(radiance).scale;
        ok &= reduced < .5f && reduced >= lighting.min_scale;
        ok &= render_radiance_set_lighting(radiance, lighting) &&
              render_radiance_get_settings(radiance).scale == reduced;
        lighting.adaptive = false;
        ok &= render_radiance_set_lighting(radiance, lighting) &&
              render_radiance_get_settings(radiance).scale == .5f;
    }
    bool light_ok = light_field_checks();
    ok &= light_ok;
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (radiance) {
        render_radiance_free(radiance);
    }
    if (raster) {
        render_material_raster_free(raster);
    }
    printf("radiance %s: HRC transport, SDF normals, absorption, reset and resize\n", ok ? "PASS" : "FAIL");
    return !ok;
}
