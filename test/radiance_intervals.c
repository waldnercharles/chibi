#include "platform/cute.h"

#include <math.h>
#include <stdio.h>

enum
{
    WORLD_W = 2,
    WORLD_H = 5,
    PREV_W = 4,
    CURR_W = 3
};

static CF_StorageBuffer interval_buffer(int width)
{
    CF_StorageBufferParams params = cf_storage_buffer_defaults(width * WORLD_H * 8);
    params.compute_readable = true;
    params.compute_writable = true;
    return cf_make_storage_buffer(params);
}

static bool check_paths(CF_Canvas canvas, const char *name, float segments[4][4])
{
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return false;
    }
    while (!cf_readback_ready(readback)) {
    }
    float pixels[WORLD_H][CURR_W][4];
    bool ok = cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
    cf_destroy_readback(readback);
    if (!ok) {
        return false;
    }

    /* Two serial paths, then their equal-probability mixture. The CPU oracle
     * uses physical transmission independently of the GPU storage format. */
    double paths[2][4];
    for (int path = 0; path < 2; path++) {
        const float *near = segments[path * 2];
        const float *far = segments[path * 2 + 1];
        for (int channel = 0; channel < 3; channel++) {
            paths[path][channel] = near[channel] + exp(-near[3]) * far[channel];
        }
        paths[path][3] = exp(-(double)near[3] - far[3]);
    }
    for (int direction = 0; direction < CURR_W; direction++) {
        for (int channel = 0; channel < 4; channel++) {
            double want = direction == 1
                            ? (paths[0][channel] + paths[1][channel]) * .5
                            : paths[direction / 2][channel];
            float actual = pixels[2][direction][channel];
            double tolerance = channel == 3
                                 ? .002 * fmax(fmin(want, 1 - want), .000001)
                                 : .002 * fmax(want, .01);
            if (!isfinite(actual) || fabs(actual - want) > tolerance) {
                fprintf(stderr, "interval %s direction %d channel %d: got %.9g, expected %.9g\n", name, direction, channel, actual, want);
                ok = false;
            }
        }
    }
    return ok;
}

bool test_radiance_intervals()
{
    /* At the center row, even directions traverse A or B alone. The odd
     * direction combines them. Populate both far directions identically so
     * all three have an independent analytic reference. */
    const char *seed_source = "#include \"hrc_common.shd\"\n"
                              "layout(local_size_x=32) in;\n"
                              "layout(std430,set=1,binding=0) writeonly buffer "
                              "Seed { uvec2 data[]; };\n"
                              "layout(set=2,binding=0) uniform Inputs { vec4 "
                              "segments[4]; };\n"
                              "void main(){int "
                              "i=int(gl_GlobalInvocationID.x);if(i>=20)return;"
                              "vec4 value=vec4(0);"
                              "if(i==8)value=segments[0];"
                              "if(i==6||i==7)value=segments[1];"
                              "if(i==9)value=segments[2];"
                              "if(i==14||i==15)value=segments[3];"
                              "data[i]=f16_store(value);}\n";
    const char
        *decode_source = "#include \"hrc_common.shd\"\n"
                         "layout(local_size_x=3,local_size_y=5) in;\n"
                         "layout(std430,set=0,binding=0) readonly buffer "
                         "Result { uvec2 data[]; };\n"
                         "layout(set=1,binding=0,rgba32f) uniform writeonly "
                         "image2D result_image;\n"
                         "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);"
                         "vec4 value=f16_load(data[p.y*3+p.x]);"
                         "imageStore(result_image,p,vec4(value.rgb,exp(-value."
                         "a)));}\n";
    CF_ComputeShader seed = cf_make_compute_shader_from_source(seed_source);
    CF_ComputeShader extend = cf_make_compute_shader("hrc/extend.shd");
    CF_ComputeShader decode = cf_make_compute_shader_from_source(decode_source);
    CF_Material material = cf_make_material();
    CF_StorageBuffer prev = interval_buffer(PREV_W);
    CF_StorageBuffer curr = interval_buffer(CURR_W);
    CF_CanvasParams params = cf_canvas_defaults(CURR_W, WORLD_H);
    params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(params);
    bool ok = seed.id && extend.id && decode.id && material.id && prev.id &&
              curr.id && canvas.id;
    const struct
    {
        const char *name;
        float tau[4];
    } cases[] = {
        { "transparent", { 0, 0, 0, 0 } },
        { "uniform", { 1, 1, 1, 1 } },
        { "clear and opaque", { 0, 0, 10, 0 } },
        { "unequal serial paths", { .5f, 2, 3, .25f } },
        { "thin fog", { .0001f, .0001f, .0001f, .0001f } },
        { "unequal thin fog", { 0, .0001f, .0002f, .0001f } },
        { "saturated depth", { 100000, 0, 100000, 0 } },
        { "clear and saturated depth", { 0, 0, 100000, 0 } },
    };
    if (ok) {
        for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
            float segments[4][4] = {
                { 1, 0, 0, cases[i].tau[0] },
                { 0, 3, 0, cases[i].tau[1] },
                { 0, 2, 0, cases[i].tau[2] },
                { 0, 0, 4, cases[i].tau[3] },
            };
            cf_app_update(NULL);
            cf_material_set_uniform_cs(material, "segments", segments, CF_UNIFORM_TYPE_FLOAT4, 4);
            CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
            dispatch.rw_buffers = &prev;
            dispatch.rw_buffer_count = 1;
            cf_dispatch_compute(seed, material, dispatch);

            int values[] = { 1, WORLD_W, WORLD_H, PREV_W, CURR_W };
            const char *names[] = { "u_cascade", "u_world_w", "u_world_h", "u_prev_w", "u_curr_w" };
            for (int j = 0; j < 5; j++) {
                cf_material_set_uniform_cs(material, names[j], values + j, CF_UNIFORM_TYPE_INT, 1);
            }
            dispatch.ro_buffers = &prev;
            dispatch.ro_buffer_count = 1;
            dispatch.rw_buffers = &curr;
            cf_dispatch_compute(extend, material, dispatch);

            CF_Texture target = cf_canvas_get_target(canvas);
            dispatch.ro_buffers = &curr;
            dispatch.rw_buffers = NULL;
            dispatch.rw_buffer_count = 0;
            dispatch.rw_textures = &target;
            dispatch.rw_texture_count = 1;
            cf_dispatch_compute(decode, material, dispatch);
            cf_app_draw_onto_screen(false);
            bool passed = check_paths(canvas, cases[i].name, segments);
            printf("%s: HRC intervals %s\n", passed ? "PASS" : "FAIL", cases[i].name);
            ok &= passed;
        }
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (curr.id) {
        cf_destroy_storage_buffer(curr);
    }
    if (prev.id) {
        cf_destroy_storage_buffer(prev);
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (decode.id) {
        cf_destroy_compute_shader(decode);
    }
    if (extend.id) {
        cf_destroy_compute_shader(extend);
    }
    if (seed.id) {
        cf_destroy_compute_shader(seed);
    }
    return ok;
}
