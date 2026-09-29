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

static CF_Canvas level_canvas(int width, CF_PixelFormat format)
{
    CF_CanvasParams params = cf_canvas_defaults(width, WORLD_H);
    params.target.pixel_format = format;
    params.target.filter = CF_FILTER_NEAREST;
    return cf_make_canvas(params);
}

static void draw_pass(CF_Mesh mesh, CF_Shader shader, CF_Material material, CF_Canvas target, int width)
{
    cf_apply_canvas(target, true);
    cf_apply_mesh(mesh);
    cf_apply_shader(shader, material);
    cf_apply_viewport(0, 0, width, WORLD_H);
    cf_apply_scissor(0, 0, width, WORLD_H);
    cf_draw_elements();
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
            /* Levels store directions evens first; see hrc/hrc_slots.shd. */
            int slot = direction & 1 ? (CURR_W + 1) / 2 + direction / 2
                                     : direction / 2;
            float actual = pixels[2][slot][channel];
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
     * all three have an independent analytic reference. Levels are half-float
     * render targets, as in production. */
    const char
        *vertex_source = "layout(location=0) in vec2 in_pos;\n"
                         "void main(){gl_Position=vec4(in_pos,0.0,1.0);}\n";
    const char
        *seed_source = "layout(location=0) out vec4 out_value;\n"
                       "layout(set=3,binding=0) uniform uniform_block { vec4 "
                       "segments[4]; };\n"
                       "void main(){ivec2 p=ivec2(gl_FragCoord.xy);"
                       "int i=p.y*4+p.x;vec4 value=vec4(0);"
                       "if(i==8)value=segments[0];"
                       "if(i==6||i==7)value=segments[1];"
                       "if(i==9)value=segments[2];"
                       "if(i==14||i==15)value=segments[3];"
                       "out_value=value;}\n";
    const char *decode_source = "layout(location=0) out vec4 out_value;\n"
                                "layout(set=2,binding=0) uniform sampler2D "
                                "u_curr;\n"
                                "void main(){vec4 v=texelFetch(u_curr,"
                                "ivec2(gl_FragCoord.xy),0);"
                                "out_value=vec4(v.rgb,exp(-v.a));}\n";
    CF_Shader seed = cf_make_shader_from_source(vertex_source, seed_source);
    CF_Shader extend = cf_make_shader("/chibi-render/hrc/pass.vert", "/chibi-render/hrc/extend.frag");
    CF_Shader decode = cf_make_shader_from_source(vertex_source, decode_source);
    const float vertices[6] = { -1, -1, 3, -1, -1, 3 };
    CF_VertexAttribute attribute = { .name = "in_pos",
                                     .format = CF_VERTEX_FORMAT_FLOAT2 };
    CF_Mesh mesh = cf_make_mesh(sizeof vertices, &attribute, 1, 2 * (int)sizeof(float));
    cf_mesh_update_vertex_data(mesh, (void *)vertices, 3);
    CF_Material material = cf_make_material();
    CF_Canvas prev = level_canvas(PREV_W, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    CF_Canvas curr = level_canvas(CURR_W, CF_PIXEL_FORMAT_R16G16B16A16_FLOAT);
    CF_Canvas canvas = level_canvas(CURR_W, CF_PIXEL_FORMAT_R32G32B32A32_FLOAT);
    bool ok = seed.id && extend.id && decode.id && mesh.id && material.id &&
              prev.id && curr.id && canvas.id;
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
            cf_material_set_uniform_fs(material, "segments", segments, CF_UNIFORM_TYPE_FLOAT4, 4);
            draw_pass(mesh, seed, material, prev, PREV_W);
            int values[] = { 1, 0, WORLD_W, WORLD_H };
            const char *names[] = { "u_cascade", "u_transposed", "u_world_w", "u_world_h" };
            for (int j = 0; j < 4; j++) {
                cf_material_set_uniform_fs(material, names[j], values + j, CF_UNIFORM_TYPE_INT, 1);
            }
            cf_material_set_texture_fs(material, "u_prev", cf_canvas_get_target(prev));
            draw_pass(mesh, extend, material, curr, CURR_W);
            cf_material_set_texture_fs(material, "u_curr", cf_canvas_get_target(curr));
            draw_pass(mesh, decode, material, canvas, CURR_W);
            cf_app_draw_onto_screen(false);
            bool passed = check_paths(canvas, cases[i].name, segments);
            printf("%s: HRC intervals %s\n", passed ? "PASS" : "FAIL", cases[i].name);
            ok &= passed;
        }
    }
    CF_Canvas canvases[] = { canvas, curr, prev };
    for (int i = 0; i < 3; i++) {
        if (canvases[i].id) {
            cf_destroy_canvas(canvases[i]);
        }
    }
    if (material.id) {
        cf_destroy_material(material);
    }
    if (mesh.id) {
        cf_destroy_mesh(mesh);
    }
    CF_Shader shaders[] = { decode, extend, seed };
    for (int i = 0; i < 3; i++) {
        if (shaders[i].id) {
            cf_destroy_shader(shaders[i]);
        }
    }
    return ok;
}
