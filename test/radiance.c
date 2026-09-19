#include "render/radiance.h"

#include "graph/runtime/instance.h"
#include "platform/cute.h"
#include "render/fluid_raster.h"
#include "render/light_field_2d.h"
#include "render/material_raster.h"
#include "render/reflections.h"
#include "render/scene_renderer.h"
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

static bool packed_fluence()
{
    /* RGB9_E5 reference words cover zero, HDR saturation, exponent carry,
     * subnormal light and unequal channels. Test the production GLSL codec. */
    float colors[8][4] = { { 0, 0, 0, 0 },        { 1, 1, 1, 0 },
                           { 1, .5f, .25f, 0 },   { 65408, 65408, 65408, 0 },
                           { 0x1p-24f, 0, 0, 0 }, { 1.999f, .5f, .25f, 0 },
                           { -1, 0, 70000, 0 },   { .001f, .0005f, .00025f, 0 } };
    const uint32_t expected[] = { 0, 0x84020100, 0x81010100, 0xffffffff,
                                  1, 0x88808100, 0xfffc0000, 0x31090706 };
    const char *
        source = "#include \"hrc_common.shd\"\n"
                 "layout(local_size_x=8,local_size_y=1,local_size_z=1) in;\n"
                 "layout(set=1,binding=0,rgba32f) uniform writeonly image2D "
                 "result_image;\n"
                 "layout(set=2,binding=0) uniform Inputs { vec4 colors[8]; };\n"
                 "void main(){int x=int(gl_GlobalInvocationID.x);"
                 "uint encoded=fluence_store(colors[x].rgb);"
                 "imageStore(result_image,ivec2(x,0),"
                 "vec4(float(encoded & 65535u),float(encoded >> 16),0,0));"
                 "imageStore(result_image,ivec2(x,1),vec4(fluence_load(encoded)"
                 ",1));}\n";
    CF_ComputeShader shader = cf_make_compute_shader_from_source(source);
    CF_Material material = cf_make_material();
    CF_CanvasParams params = cf_canvas_defaults(8, 2);
    params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(params);
    bool ok = shader.id && material.id && canvas.id;
    if (ok) {
        cf_app_update(NULL);
        cf_material_set_uniform_cs(material, "colors", colors, CF_UNIFORM_TYPE_FLOAT4, 8);
        CF_Texture target = cf_canvas_get_target(canvas);
        CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
        dispatch.rw_textures = &target;
        dispatch.rw_texture_count = 1;
        cf_dispatch_compute(shader, material, dispatch);
        cf_app_draw_onto_screen(false);
        CF_Readback readback = cf_canvas_readback(canvas);
        ok = readback.id != 0;
        if (readback.id) {
            while (!cf_readback_ready(readback)) {
            }
            float values[2][8][4];
            ok = cf_readback_data(readback, values, sizeof values) == sizeof values;
            cf_destroy_readback(readback);
            for (int i = 0; ok && i < 8; i++) {
                uint32_t encoded = (uint32_t)values[0][i][0] |
                                   ((uint32_t)values[0][i][1] << 16);
                ok = encoded == expected[i];
                for (int channel = 0; channel < 3; channel++) {
                    float want = ldexpf(
                        (float)((expected[i] >> (channel * 9)) & 511),
                        (int)(expected[i] >> 27) - 24
                    );
                    ok &= values[1][i][channel] == want;
                }
            }
        }
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (shader.id) {
        cf_destroy_compute_shader(shader);
    }
    printf("radiance packing %s: HDR, dark colors, shared exponent carry and saturation\n", ok ? "PASS" : "FAIL");
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
        ok &= render_fluid_raster_draw(draw, fluid, textures, view, SIDE, SIDE);
        cf_app_draw_onto_screen(false);
        ok &= read_hdr_texture(textures.emission, pixels, PIXELS);
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
        ok &= render_fluid_raster_draw(draw, fluid, textures, view, SIDE, SIDE);
        cf_app_draw_onto_screen(false);
        ok &= read_hdr_texture(
            stage >= 7                 ? textures.absorption
            : stage == 2 || stage >= 5 ? textures.scene
                                       : textures.emission,
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
            ok &= read_hdr_texture(textures.emission, pixels, PIXELS);
            ok &= half_value(pixels[4 * (64 * SIDE + 64)]) == 0;
            ok &= read_hdr_texture(render_fluid_raster_visibility(draw), pixels, PIXELS);
            ok &= half_value(pixels[4 * (64 * SIDE + 64)]) > .01f;
        } else if (stage == 3) {
            /* Fuel burns as small separate cores; the exhaust bridge is unchanged. */
            ok &= center < .01f && half_value(pixels[4 * (64 * SIDE + 70)]) > .9f;
        } else if (stage == 4) {
            ok &= read_hdr_texture(render_fluid_raster_visibility(draw), pixels, PIXELS);
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
            // Wisp support crosses tile boundaries and rotates with game velocity.
            float horizontal = half_value(pixels[4 * (64 * SIDE + 81)]);
            float vertical = half_value(pixels[4 * (81 * SIDE + 64)]);
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
    ok &= read_hdr_texture(render_material_raster_textures(raster).emission, emission, PIXELS);
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
                ok &= render_fluid_raster_draw(draw, fluid, textures, view, SIDE, SIDE);
                render_radiance_inputs inputs = {
                    .scene = textures.scene,
                    .emission = textures.emission,
                    .absorption = textures.absorption,
                    .albedo = textures.albedo,
                    .normal = textures.normal,
                    .visibility = render_fluid_raster_visibility(draw)
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
        ok &= read_hdr_texture(render_material_raster_textures(raster).emission, emission, PIXELS);
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
        ok &= render_fluid_raster_draw(draw, fluid, textures, view, SIDE, SIDE);
        render_radiance_inputs inputs = { .scene = textures.scene,
                                          .emission = textures.emission,
                                          .absorption = textures.absorption,
                                          .albedo = textures.albedo,
                                          .normal = textures.normal,
                                          .visibility = render_fluid_raster_visibility(draw),
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
    render_material_raster *raster = render_material_raster_new();
    render_radiance *radiance = render_radiance_new();
    CF_Canvas canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    bool ok = raster && radiance && canvas.id && packed_fluence();
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
    ok &= fluid_alignment(raster);
    ok &= fluid_surfaces(raster);
    cf_destroy_canvas(canvas);
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    ok &= canvas.id && render_radiance_resize(radiance, SIDE, SIDE);
    for (int levels = 1; levels <= 3; levels += 2) {
        ok &= silhouette_lighting(raster, radiance, canvas, levels);
        ok &= fluid_lighting(raster, radiance, canvas, levels);
    }
    ok &= liquid_refraction(raster, radiance, canvas);
    ok &= reflection_pixels();
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

int radiance_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Material alignment regression",
        0,
        0,
        0,
        SIDE,
        SIDE,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "%s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    int failed = test_radiance();
    cf_destroy_app();
    return failed;
}
