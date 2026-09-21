#include "platform/cute.h"

#include <math.h>
#include <stdio.h>

bool test_radiance_medium()
{
    /* Read the production DDA interval directly. Constant emission/extinction
     * have an analytic integral, independent of cascade reconstruction. */
    const char
        *decode_source = "#include \"hrc_common.shd\"\n"
                         "layout(local_size_x=4,local_size_y=5) in;\n"
                         "layout(std430,set=0,binding=0) readonly buffer "
                         "Result { uvec2 data[]; };\n"
                         "layout(set=1,binding=0,rgba32f) uniform writeonly "
                         "image2D result_image;\n"
                         "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);"
                         "imageStore(result_image,p,f16_load(data[p.y*4+p.x]));"
                         "}\n";
    CF_ComputeShader trace = cf_make_compute_shader("hrc/trace.shd");
    CF_ComputeShader decode = cf_make_compute_shader_from_source(decode_source);
    CF_Material material = cf_make_material();
    CF_StorageBufferParams storage = cf_storage_buffer_defaults(4 * 5 * 8);
    storage.compute_readable = storage.compute_writable = true;
    CF_StorageBuffer intervals = cf_make_storage_buffer(storage);
    CF_CanvasParams canvas_params = cf_canvas_defaults(4, 5);
    canvas_params.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    canvas_params.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas canvas = cf_make_canvas(canvas_params);
    CF_TextureParams texture_params = cf_texture_defaults(2, 5);
    texture_params.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    texture_params.stream = true;
    CF_Texture emission = cf_make_texture(texture_params);
    CF_Texture absorption = cf_make_texture(texture_params);
    CF_Texture bounce = cf_make_texture(texture_params);
    bool ok = trace.id && decode.id && intervals.id && canvas.id &&
              emission.id && absorption.id && bounce.id && material.id;
    float empty[40] = { 0 };
    cf_texture_update(bounce, empty, sizeof empty);
    float densities[] = { 0, .0001f, .001f, .003f, .2f, 10 };
    for (int i = 0; i < 6 && ok; i++) {
        for (int scale = 1; scale <= 4 && ok; scale *= 2) {
            float tau = densities[i] / scale, light[40], depth[40];
            for (int p = 0; p < 10; p++) {
                for (int c = 0; c < 3; c++) {
                    light[p * 4 + c] = .2f;
                    depth[p * 4 + c] = tau;
                }
                light[p * 4 + 3] = tau;
                depth[p * 4 + 3] = 1;
            }
            cf_app_update(NULL);
            cf_texture_update(emission, light, sizeof light);
            cf_texture_update(absorption, depth, sizeof depth);
            cf_material_set_texture_cs(material, "u_emission", emission);
            cf_material_set_texture_cs(material, "u_transmittance", absorption);
            cf_material_set_texture_cs(material, "u_bounce", bounce);
            const char *names[] = { "u_cascade", "u_rotate", "u_world_w",
                                    "u_world_h", "u_work_w", "u_work_h",
                                    "u_medium" };
            int values[] = { 0, 0, 2, 5, 2, 5, 1 };
            for (int v = 0; v < 7; v++) {
                cf_material_set_uniform_cs(material, names[v], values + v, CF_UNIFORM_TYPE_INT, 1);
            }
            float step = (float)scale, zero = 0, pixel_size = 1.f / scale;
            cf_material_set_uniform_cs(material, "u_pixel_size", &pixel_size, CF_UNIFORM_TYPE_FLOAT, 1);
            cf_material_set_uniform_cs(material, "u_step_scale", &step, CF_UNIFORM_TYPE_FLOAT, 1);
            cf_material_set_uniform_cs(material, "u_bounce_intensity", &zero, CF_UNIFORM_TYPE_FLOAT, 1);
            CF_ComputeDispatch dispatch = cf_compute_dispatch_defaults(1, 1, 1);
            dispatch.rw_buffers = &intervals;
            dispatch.rw_buffer_count = 1;
            cf_dispatch_compute(trace, material, dispatch);
            CF_Texture target = cf_canvas_get_target(canvas);
            dispatch.rw_buffers = NULL;
            dispatch.rw_buffer_count = 0;
            dispatch.ro_buffers = &intervals;
            dispatch.ro_buffer_count = 1;
            dispatch.rw_textures = &target;
            dispatch.rw_texture_count = 1;
            cf_dispatch_compute(decode, material, dispatch);
            cf_app_draw_onto_screen(false);
            CF_Readback readback = cf_canvas_readback(canvas);
            if (!readback.id) {
                ok = false;
                break;
            }
            while (!cf_readback_ready(readback)) {
            }
            float pixels[80];
            ok &= cf_readback_data(readback, pixels, sizeof pixels) == sizeof pixels;
            cf_destroy_readback(readback);
            double length = sqrt(2);
            double expected = densities[i] > 0
                                ? .2 * -expm1(-densities[i] * length) / densities[i]
                                : .2 * length;
            double actual = pixels[2 * 4 * 4];
            bool passed = isfinite(actual) && fabs(actual - expected) < expected * .001;
            printf(
                "%s: medium source tau=%.4f step=%d L=%.6f expected=%.6f\n",
                passed ? "PASS" : "FAIL",
                tau,
                scale,
                actual,
                expected
            );
            ok &= passed;
        }
    }
    if (emission.id) {
        cf_destroy_texture(emission);
    }
    if (absorption.id) {
        cf_destroy_texture(absorption);
    }
    if (bounce.id) {
        cf_destroy_texture(bounce);
    }
    if (canvas.id) {
        cf_destroy_canvas(canvas);
    }
    if (intervals.id) {
        cf_destroy_storage_buffer(intervals);
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (trace.id) {
        cf_destroy_compute_shader(trace);
    }
    if (decode.id) {
        cf_destroy_compute_shader(decode);
    }
    return ok;
}
