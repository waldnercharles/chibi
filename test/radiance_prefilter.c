#include "platform/cute.h"
#include "render/shader_program.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original two-dimensional quadrature is independent of the optimized
 * interpolation-region reduction. Keep it as a GPU reference, including mip
 * filtering, edge clamping and fractional cell boundaries. */
static const char *reference_main =
    "void main(){\n"
    "ivec2 p=ivec2(gl_GlobalInvocationID.xy);\n"
    "if(any(greaterThanEqual(p,ivec2(u_work_w,u_work_h)))) return;\n"
    "vec2 canvas=vec2(u_canvas_w,u_canvas_h), "
    "cell=canvas/vec2(u_work_w,u_work_h);\n"
    "vec2 origin=vec2(p)*cell, center=origin+cell*.5;\n"
    "ivec2 start=ivec2(floor(origin)), "
    "end=min(ivec2(ceil(origin+cell)),ivec2(canvas));\n"
    "float lod=ceil(log2(max(cell.x,cell.y))), weights=0, tau=0;\n"
    "vec4 emission=vec4(0); vec3 bounce=vec3(0);\n"
    "for(int y=start.y;y<end.y;y++) for(int x=start.x;x<end.x;x++){\n"
    "vec2 point=vec2(x,y)+.5, uv=point/canvas, delta=(point-center)/cell;\n"
    "float w=exp(-2.0*dot(delta,delta)); weights+=w;\n"
    "emission+=textureLod(u_emission,uv,lod)*w;\n"
    "bounce+=textureLod(u_bounce,uv,0).rgb*w;\n"
    "tau+=dot(textureLod(u_transmittance,uv,lod).rgb,vec3(1.0/3.0));}\n"
    "imageStore(u_emission_out,p,emission/weights);\n"
    "imageStore(u_transmittance_out,p,vec4(tau/"
    "float((end.x-start.x)*(end.y-start.y)),0,0,0));\n"
    "imageStore(u_bounce_out,p,vec4(bounce/weights,1));}\n";

/* Remove hardware interpolation-coordinate quantization to check the filter
 * identity separately from native sampler rounding around HDR discontinuities. */
static const char *exact_sampling =
    "vec4 exact_sample(sampler2D s,vec2 uv,float lod){\n"
    "int level=int(lod); ivec2 size=textureSize(s,level);\n"
    "vec2 p=uv*vec2(size)-.5, f=fract(p); ivec2 a=ivec2(floor(p));\n"
    "vec4 v00=texelFetch(s,clamp(a,ivec2(0),size-1),level);\n"
    "vec4 v10=texelFetch(s,clamp(a+ivec2(1,0),ivec2(0),size-1),level);\n"
    "vec4 v01=texelFetch(s,clamp(a+ivec2(0,1),ivec2(0),size-1),level);\n"
    "vec4 v11=texelFetch(s,clamp(a+ivec2(1,1),ivec2(0),size-1),level);\n"
    "return mix(mix(v00,v10,f.x),mix(v01,v11,f.x),f.y); }\n"
    "#define textureLod exact_sample\n";

static CF_ComputeShader shader(const char *source, bool exact)
{
    const char *prefix = exact ? exact_sampling : "";
    char *joined = xmalloc(strlen(prefix) + strlen(source) + 1);
    strcpy(joined, prefix);
    strcat(joined, source);
    CF_ComputeShader s = cf_make_compute_shader_from_source(joined);
    free(joined);
    assert(s.id);
    return s;
}

static CF_Texture input(int width, int height, int seed)
{
    CF_TextureParams p = cf_texture_defaults(width, height);
    p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    p.filter = CF_FILTER_LINEAR;
    p.mip_filter = CF_MIP_FILTER_LINEAR;
    p.wrap_u = p.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    p.allocate_mipmaps = true;
    CF_Texture t = cf_make_texture(p);
    assert(t.id);
    float *pixels = xmalloc(width * height * 4 * sizeof(float));
    for (int i = 0; i < width * height * 4; i++) {
        pixels[i] = ((i * 73 + seed * 31) % 127) / 32.0f;
    }
    pixels[(height / 2 * width + width / 2) * 4] = 500;
    cf_texture_update(t, pixels, width * height * 4 * sizeof(float));
    cf_generate_mipmaps(t);
    free(pixels);
    return t;
}

static float half_value(uint16_t v)
{
    int e = (v >> 10) & 31, m = v & 1023;
    return e ? ldexpf(1 + m / 1024.0f, e - 15) : ldexpf((float)m, -24);
}

bool test_radiance_prefilter()
{
    char *source = render_shader_source("hrc/prefilter.shd");
    assert(source);
    CF_ComputeShader shaders[2][2];
    shaders[0][0] = shader(source, false);
    shaders[1][0] = shader(source, true);
    char *main = strstr(source, "void main()");
    assert(main);
    *main = 0;
    char *reference = xmalloc(strlen(source) + strlen(reference_main) + 1);
    strcpy(reference, source);
    strcat(reference, reference_main);
    shaders[0][1] = shader(reference, false);
    shaders[1][1] = shader(reference, true);
    free(reference);
    free(source);
    CF_Material material = cf_make_material();
    assert(material.id);
    int sizes[][4] = { { 96, 57, 13, 8 },   { 65, 39, 16, 9 }, { 97, 61, 7, 5 },
                       { 100, 62, 23, 17 }, { 30, 129, 5, 7 }, { 33, 21, 11, 7 } };
    bool ok = true;
    for (int i = 0; i < 12; i++) {
        int exact = i / 6;
        int *size = sizes[i % 6];
        int width = size[2], height = size[3], count = width * height;
        cf_app_update(NULL);
        CF_Texture inputs[] = { input(size[0], size[1], 1),
                                input(size[0], size[1], 2),
                                input(width, height, 3) };
        const char *names[] = { "u_emission", "u_transmittance", "u_bounce" };
        for (int j = 0; j < 3; j++) {
            cf_material_set_texture_cs(material, names[j], inputs[j]);
        }
        const char *uniforms[] = { "u_canvas_w", "u_canvas_h", "u_work_w", "u_work_h" };
        for (int j = 0; j < 4; j++) {
            cf_material_set_uniform_cs(material, uniforms[j], size + j, CF_UNIFORM_TYPE_INT, 1);
        }
        CF_Canvas outputs[2][3];
        for (int method = 0; method < 2; method++) {
            CF_Texture targets[3];
            for (int j = 0; j < 3; j++) {
                CF_CanvasParams p = cf_canvas_defaults(width, height);
                p.target.pixel_format = j == 1 ? CF_PIXEL_FORMAT_R16_FLOAT
                                               : CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
                p.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
                outputs[method][j] = cf_make_canvas(p);
                assert(outputs[method][j].id);
                targets[j] = cf_canvas_get_target(outputs[method][j]);
            }
            CF_ComputeDispatch
                d = cf_compute_dispatch_defaults((width + 15) / 16, (height + 15) / 16, 1);
            d.rw_textures = targets;
            d.rw_texture_count = 3;
            cf_dispatch_compute(shaders[exact][method], material, d);
        }
        cf_app_draw_onto_screen(false);
        double worst = 0, worst_integrated = 0;
        for (int field = 0; field < 3; field++) {
            int channels = field == 1 ? 1 : 4, n = count * channels;
            uint16_t *pixels[2] = { xmalloc(n * sizeof(uint16_t)),
                                    xmalloc(n * sizeof(uint16_t)) };
            for (int method = 0; method < 2; method++) {
                CF_Readback r = cf_canvas_readback(outputs[method][field]);
                assert(r.id);
                while (!cf_readback_ready(r)) {
                }
                int bytes = n * (int)sizeof(uint16_t);
                ok &= cf_readback_data(r, pixels[method], bytes) == bytes;
                cf_destroy_readback(r);
                cf_destroy_canvas(outputs[method][field]);
            }
            double difference = 0, energy = 0;
            for (int k = 0; k < n; k++) {
                ok &= (pixels[0][k] & 0x7c00) != 0x7c00 &&
                      (pixels[1][k] & 0x7c00) != 0x7c00;
                double want = half_value(pixels[1][k]);
                double error = fabs(half_value(pixels[0][k]) - want) / fmax(want, .001);
                difference += fabs(half_value(pixels[0][k]) - want);
                energy += want;
                worst = fmax(worst, error);
            }
            worst_integrated = fmax(worst_integrated, difference / energy);
            free(pixels[0]);
            free(pixels[1]);
            cf_destroy_texture(inputs[field]);
        }
        /* Exact bilinear interpolation differs by at most half-float rounding.
         * Native interpolation can shift individual HDR neighbors, so bound
         * both its local difference and the relative L1 error of each field. */
        ok &= worst <= (exact ? .001 : .015) &&
              worst_integrated < (exact ? .001 : .002);
        printf(
            "prefilter exact=%d %dx%d -> %dx%d: max %.6f relative L1 %.6f\n",
            exact,
            size[0],
            size[1],
            width,
            height,
            worst,
            worst_integrated
        );
    }
    cf_destroy_material(material);
    for (int i = 0; i < 2; i++) {
        cf_destroy_compute_shader(shaders[0][i]);
        cf_destroy_compute_shader(shaders[1][i]);
    }
    printf("prefilter %s: capped grids, fractional cells, anisotropy and HDR\n", ok ? "PASS" : "FAIL");
    return ok;
}
