/* The RmlUi backend's GPU programs and post-processing passes; see rml_gpu.h. */
#include "ui/rml_gpu.h"

#include "support/math.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

/* RmlUi geometry: position, premultiplied color and texture coordinate. */
static const char ui_vertex_shader
    [] = "layout (location = 0) in vec2 in_position;\n"
         "layout (location = 1) in vec4 in_color;\n"
         "layout (location = 2) in vec2 in_uv;\n"
         "layout (location = 8) in vec4 in_model0;\n"
         "layout (location = 9) in vec4 in_model1;\n"
         "layout (location = 10) in vec4 in_model2;\n"
         "layout (location = 0) out vec4 v_color;\n"
         "layout (location = 1) out vec2 v_uv;\n"
         "layout (set = 1, binding = 0) uniform uniform_block { mat4 "
         "u_view_projection; };\n"
         "void main() {\n"
         "    vec4 p = vec4(in_position, 0.0, 1.0);\n"
         "    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), "
         "dot(in_model2, p));\n"
         "    v_color = in_color;\n"
         "    v_uv = in_uv;\n"
         "    gl_Position = u_view_projection * vec4(world, 1.0);\n"
         "}\n";

static const char ui_fragment_shader
    [] = "layout (location = 0) in vec4 v_color;\n"
         "layout (location = 1) in vec2 v_uv;\n"
         "layout (location = 0) out vec4 result;\n"
         "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"
         "void main() { result = texture(u_texture, v_uv) * v_color; }\n";

/* GL3's gradient program. Colors between stops mix linearly, as CSS does,
 * where GL3 eases them with smoothstep. */
static const char gradient_fragment_shader[] =
    "layout (location = 0) in vec4 v_color;\n"
    "layout (location = 1) in vec2 v_uv;\n"
    "layout (location = 0) out vec4 result;\n"
    "layout (set = 3, binding = 0) uniform uniform_block {\n"
    "    vec4 rml_gradient;\n"         /* function, stop count */
    "    vec4 rml_gradient_vectors;\n" /* p.xy, v.xy */
    "    vec4 rml_stop_positions[4];\n"
    "    vec4 rml_stop_colors[16];\n"
    "};\n"
    /* GLES 3 drivers can crash when a fragment shader indexes a uniform-block
     * array with a loop variable. Keep every UBO access statically indexed. */
    "float stop_position(int i) {\n"
    "    switch (i) {\n"
    "    case 0: return rml_stop_positions[0].x;\n"
    "    case 1: return rml_stop_positions[0].y;\n"
    "    case 2: return rml_stop_positions[0].z;\n"
    "    case 3: return rml_stop_positions[0].w;\n"
    "    case 4: return rml_stop_positions[1].x;\n"
    "    case 5: return rml_stop_positions[1].y;\n"
    "    case 6: return rml_stop_positions[1].z;\n"
    "    case 7: return rml_stop_positions[1].w;\n"
    "    case 8: return rml_stop_positions[2].x;\n"
    "    case 9: return rml_stop_positions[2].y;\n"
    "    case 10: return rml_stop_positions[2].z;\n"
    "    case 11: return rml_stop_positions[2].w;\n"
    "    case 12: return rml_stop_positions[3].x;\n"
    "    case 13: return rml_stop_positions[3].y;\n"
    "    case 14: return rml_stop_positions[3].z;\n"
    "    default: return rml_stop_positions[3].w;\n"
    "    }\n"
    "}\n"
    "vec4 stop_color(int i) {\n"
    "    switch (i) {\n"
    "    case 0: return rml_stop_colors[0];\n"
    "    case 1: return rml_stop_colors[1];\n"
    "    case 2: return rml_stop_colors[2];\n"
    "    case 3: return rml_stop_colors[3];\n"
    "    case 4: return rml_stop_colors[4];\n"
    "    case 5: return rml_stop_colors[5];\n"
    "    case 6: return rml_stop_colors[6];\n"
    "    case 7: return rml_stop_colors[7];\n"
    "    case 8: return rml_stop_colors[8];\n"
    "    case 9: return rml_stop_colors[9];\n"
    "    case 10: return rml_stop_colors[10];\n"
    "    case 11: return rml_stop_colors[11];\n"
    "    case 12: return rml_stop_colors[12];\n"
    "    case 13: return rml_stop_colors[13];\n"
    "    case 14: return rml_stop_colors[14];\n"
    "    default: return rml_stop_colors[15];\n"
    "    }\n"
    "}\n"
    "void main() {\n"
    "    int kind = int(rml_gradient.x);\n"
    "    int count = int(rml_gradient.y);\n"
    "    vec2 v = rml_gradient_vectors.zw;\n"
    "    vec2 d = v_uv - rml_gradient_vectors.xy;\n"
    "    float t;\n"
    "    if (kind == 0 || kind == 3) {\n"
    "        t = dot(v, d) / dot(v, v);\n"
    "    } else if (kind == 1 || kind == 4) {\n"
    "        t = length(v * d);\n"
    "    } else {\n"
    "        vec2 r = mat2(v.x, -v.y, v.y, v.x) * d;\n"
    "        t = 0.5 + atan(-r.x, r.y) / 6.28318531;\n"
    "    }\n"
    "    if (kind >= 3) {\n"
    "        float first = stop_position(0);\n"
    "        float span = max(stop_position(count - 1) - first, 0.000001);\n"
    "        t = first + mod(t - first, span);\n"
    "    }\n"
    "    vec4 color = stop_color(0);\n"
    "    for (int i = 1; i < count; i++) {\n"
    "        float a = stop_position(i - 1);\n"
    "        float b = stop_position(i);\n"
    "        float f = clamp((t - a) / max(b - a, 0.000001), 0.0, 1.0);\n"
    "        color = mix(color, stop_color(i), f);\n"
    "    }\n"
    "    result = v_color * color;\n"
    "}\n";

/* Passes draw the unit square scaled to their rectangle; the mesh attributes
 * carry the rectangle's texture coordinates. */
static const char pass_vertex_shader
    [] = "layout (location = 0) in vec2 in_position;\n"
         "layout (location = 1) in vec2 in_uv;\n"
         "layout (location = 8) in vec4 in_model0;\n"
         "layout (location = 9) in vec4 in_model1;\n"
         "layout (location = 10) in vec4 in_model2;\n"
         "layout (location = 15) in vec4 in_mesh_attributes;\n"
         "layout (location = 0) out vec2 v_uv;\n"
         "layout (set = 1, binding = 0) uniform uniform_block { mat4 "
         "u_view_projection; };\n"
         "void main() {\n"
         "    vec4 p = vec4(in_position, 0.0, 1.0);\n"
         "    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), "
         "dot(in_model2, p));\n"
         "    v_uv = mix(in_mesh_attributes.xy, in_mesh_attributes.zw, "
         "in_uv);\n"
         "    gl_Position = u_view_projection * vec4(world, 1.0);\n"
         "}\n";

#define PASS_INPUTS                                                            \
    "layout (location = 0) in vec2 v_uv;\n"                                    \
    "layout (location = 0) out vec4 result;\n"                                 \
    "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"

static const char copy_fragment_shader[] = PASS_INPUTS
    "layout (set = 3, binding = 0) uniform uniform_block { vec4 rml_tint; };\n"
    "void main() { result = texture(u_texture, v_uv) * rml_tint; }\n";

/* Premultiplied to straight alpha, for the host to sample or present. */
static const char resolve_fragment_shader[] = PASS_INPUTS
    "void main() {\n"
    "    vec4 c = texture(u_texture, v_uv);\n"
    "    result = vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a);\n"
    "}\n";

/* Premultiplied color stays valid: channels clamp to the alpha. */
static const char matrix_fragment_shader[] = PASS_INPUTS
    "layout (set = 3, binding = 0) uniform uniform_block { vec4 rml_rows[3]; "
    "};\n"
    "void main() {\n"
    "    vec4 c = texture(u_texture, v_uv);\n"
    "    vec3 rgb = vec3(dot(rml_rows[0], c), dot(rml_rows[1], c), "
    "dot(rml_rows[2], c));\n"
    "    result = vec4(clamp(rgb, vec3(0.0), vec3(c.a)), c.a);\n"
    "}\n";

/* Seven taps along rml_step. Taps outside the limits read as transparent. */
static const char blur_fragment_shader[] = PASS_INPUTS
    "layout (set = 3, binding = 0) uniform uniform_block {\n"
    "    vec4 rml_weights;\n"
    "    vec4 rml_step;\n"
    "    vec4 rml_limits;\n"
    "};\n"
    "void main() {\n"
    "    vec4 color = vec4(0.0);\n"
    "    for (int i = -3; i <= 3; i++) {\n"
    "        vec2 uv = v_uv + float(i) * rml_step.xy;\n"
    "        vec2 inside = step(rml_limits.xy, uv) * step(uv, rml_limits.zw);\n"
    "        color += texture(u_texture, uv) * (inside.x * inside.y * "
    "rml_weights[abs(i)]);\n"
    "    }\n"
    "    result = color;\n"
    "}\n";

static const char shadow_fragment_shader[] = PASS_INPUTS
    "layout (set = 3, binding = 0) uniform uniform_block {\n"
    "    vec4 rml_shadow;\n"
    "    vec4 rml_limits;\n"
    "};\n"
    "void main() {\n"
    "    vec2 inside = step(rml_limits.xy, v_uv) * step(v_uv, rml_limits.zw);\n"
    "    result = texture(u_texture, v_uv).a * inside.x * inside.y * "
    "rml_shadow;\n"
    "}\n";

static const char mask_fragment_shader[] = PASS_INPUTS
    "layout (set = 2, binding = 1) uniform sampler2D u_mask;\n"
    "void main() { result = texture(u_texture, v_uv) * texture(u_mask, "
    "v_uv).a; }\n";

/* ---------- Compiled filters and gradients ---------- */

static CF_Color premultiplied(rml_color c)
{
    float a = c.a / 255.0f;
    return (CF_Color){ c.r / 255.0f * a, c.g / 255.0f * a, c.b / 255.0f * a, a };
}

static void set_rows(rml_filter *filter, CF_V4 r, CF_V4 g, CF_V4 b)
{
    filter->kind = RML_FILTER_COLOR_MATRIX;
    filter->rows[0] = r;
    filter->rows[1] = g;
    filter->rows[2] = b;
}

/* Rows that mix toward weights by amount, as grayscale and sepia do. */
static void set_mix_rows(rml_filter *filter, float amount, const float weights[3][3])
{
    float keep = 1.0f - amount;
    CF_V4 rows[3];
    for (int i = 0; i < 3; i++) {
        float row[3];
        for (int j = 0; j < 3; j++) {
            row[j] = amount * weights[i][j] + (i == j ? keep : 0.0f);
        }
        rows[i] = cf_v4(row[0], row[1], row[2], 0.0f);
    }
    set_rows(filter, rows[0], rows[1], rows[2]);
}

/* The color matrices of the CSS filter functions, as GL3 builds them. */
static bool compile_color_matrix(const char *name, float v, rml_filter *filter)
{
    if (strcmp(name, "brightness") == 0) {
        set_rows(filter, cf_v4(v, 0, 0, 0), cf_v4(0, v, 0, 0), cf_v4(0, 0, v, 0));
        return true;
    }
    if (strcmp(name, "contrast") == 0) {
        float gray = 0.5f - 0.5f * v;
        set_rows(filter, cf_v4(v, 0, 0, gray), cf_v4(0, v, 0, gray), cf_v4(0, 0, v, gray));
        return true;
    }
    if (strcmp(name, "invert") == 0) {
        float amount = math_clamp(v, 0.0f, 1.0f), scale = 1.0f - 2.0f * amount;
        set_rows(
            filter,
            cf_v4(scale, 0, 0, amount),
            cf_v4(0, scale, 0, amount),
            cf_v4(0, 0, scale, amount)
        );
        return true;
    }
    if (strcmp(name, "grayscale") == 0) {
        static const float gray[3][3] = {
            { 0.2126f, 0.7152f, 0.0722f },
            { 0.2126f, 0.7152f, 0.0722f },
            { 0.2126f, 0.7152f, 0.0722f },
        };
        set_mix_rows(filter, v, gray);
        return true;
    }
    if (strcmp(name, "sepia") == 0) {
        static const float sepia[3][3] = {
            { 0.393f, 0.769f, 0.189f },
            { 0.349f, 0.686f, 0.168f },
            { 0.272f, 0.534f, 0.131f },
        };
        set_mix_rows(filter, v, sepia);
        return true;
    }
    if (strcmp(name, "hue-rotate") == 0) {
        float s = sinf(v), c = cosf(v);
        set_rows(
            filter,
            cf_v4(
                0.213f + 0.787f * c - 0.213f * s,
                0.715f - 0.715f * c - 0.715f * s,
                0.072f - 0.072f * c + 0.928f * s,
                0
            ),
            cf_v4(
                0.213f - 0.213f * c + 0.143f * s,
                0.715f + 0.285f * c + 0.140f * s,
                0.072f - 0.072f * c - 0.283f * s,
                0
            ),
            cf_v4(
                0.213f - 0.213f * c - 0.787f * s,
                0.715f - 0.715f * c + 0.715f * s,
                0.072f + 0.928f * c + 0.072f * s,
                0
            )
        );
        return true;
    }
    if (strcmp(name, "saturate") == 0) {
        set_rows(
            filter,
            cf_v4(0.213f + 0.787f * v, 0.715f - 0.715f * v, 0.072f - 0.072f * v, 0),
            cf_v4(0.213f - 0.213f * v, 0.715f + 0.285f * v, 0.072f - 0.072f * v, 0),
            cf_v4(0.213f - 0.213f * v, 0.715f - 0.715f * v, 0.072f + 0.928f * v, 0)
        );
        return true;
    }
    return false;
}

bool rml_filter_compile(const char *name, const rml_effect_parameters *parameters, rml_filter *filter)
{
    memset(filter, 0, sizeof *filter);
    if (strcmp(name, "opacity") == 0) {
        filter->kind = RML_FILTER_OPACITY;
        filter->value = parameters->value;
        return true;
    }
    if (strcmp(name, "blur") == 0) {
        filter->kind = RML_FILTER_BLUR;
        filter->sigma = parameters->sigma;
        return true;
    }
    if (strcmp(name, "drop-shadow") == 0) {
        filter->kind = RML_FILTER_DROP_SHADOW;
        filter->sigma = parameters->sigma;
        filter->offset = cf_v2(parameters->offset.x, parameters->offset.y);
        filter->color = premultiplied(parameters->color);
        return true;
    }
    return compile_color_matrix(name, parameters->value, filter);
}

bool rml_gradient_compile(const char *name, const rml_effect_parameters *parameters, rml_gradient *gradient)
{
    memset(gradient, 0, sizeof *gradient);
    /* Each repeating function follows its plain one by three. */
    int repeating = parameters->repeating ? RML_GRADIENT_REPEATING_LINEAR : 0;
    if (strcmp(name, "linear-gradient") == 0) {
        gradient->function = RML_GRADIENT_LINEAR + repeating;
        gradient->p = cf_v2(parameters->p0.x, parameters->p0.y);
        gradient->v = cf_v2(
            parameters->p1.x - parameters->p0.x,
            parameters->p1.y - parameters->p0.y
        );
    } else if (strcmp(name, "radial-gradient") == 0) {
        gradient->function = RML_GRADIENT_RADIAL + repeating;
        gradient->p = cf_v2(parameters->center.x, parameters->center.y);
        gradient->v = cf_v2(
            1.0f / fmaxf(parameters->radius.x, FLT_MIN),
            1.0f / fmaxf(parameters->radius.y, FLT_MIN)
        );
    } else if (strcmp(name, "conic-gradient") == 0) {
        gradient->function = RML_GRADIENT_CONIC + repeating;
        gradient->p = cf_v2(parameters->center.x, parameters->center.y);
        gradient->v = cf_v2(cosf(parameters->angle), sinf(parameters->angle));
    } else {
        return false;
    }
    assert(parameters->stop_count > 0);
    gradient->stop_count = parameters->stop_count < RML_GRADIENT_STOPS
                             ? (int)parameters->stop_count
                             : RML_GRADIENT_STOPS;
    for (int i = 0; i < gradient->stop_count; i++) {
        rml_color c = parameters->stops[i].color;
        gradient->positions[i] = parameters->stops[i].position;
        gradient->colors[i] = (CF_Color){ c.r / 255.0f,
                                          c.g / 255.0f,
                                          c.b / 255.0f,
                                          c.a / 255.0f };
    }
    return true;
}

/* ---------- Programs and targets ---------- */

typedef struct quad_vertex
{
    float x, y, u, v;
} quad_vertex;

static CF_Shader program(const char *vertex, const char *fragment)
{
    CF_Shader shader = cf_make_shader_from_source(vertex, fragment);
    assert(shader.id);
    return shader;
}

static CF_Mesh unit_quad()
{
    const CF_VertexAttribute attributes[2] = {
        { "in_position", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(quad_vertex, x), false },
        { "in_uv", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(quad_vertex, u), false },
    };
    const quad_vertex vertices[4] = {
        { 0, 0, 0, 0 },
        { 1, 0, 1, 0 },
        { 1, 1, 1, 1 },
        { 0, 1, 0, 1 },
    };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    CF_Mesh mesh = cf_make_mesh(sizeof vertices, attributes, 2, sizeof(quad_vertex));
    cf_mesh_set_index_buffer(mesh, sizeof indices, 32);
    cf_mesh_update_vertex_data(mesh, (void *)vertices, 4);
    cf_mesh_update_index_data(mesh, (void *)indices, 6);
    return mesh;
}

static CF_Texture white_texture()
{
    CF_TextureParams params = cf_texture_defaults(1, 1);
    params.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    CF_Texture texture = cf_make_texture(params);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    cf_texture_update(texture, (void *)white, sizeof white);
    return texture;
}

void rml_gpu_create(rml_gpu *gpu, int width, int height)
{
    *gpu = (rml_gpu){
        .ui = program(ui_vertex_shader, ui_fragment_shader),
        .resolve = program(pass_vertex_shader, resolve_fragment_shader),
        .gradient = program(ui_vertex_shader, gradient_fragment_shader),
        .copy = program(pass_vertex_shader, copy_fragment_shader),
        .matrix = program(pass_vertex_shader, matrix_fragment_shader),
        .blur = program(pass_vertex_shader, blur_fragment_shader),
        .shadow = program(pass_vertex_shader, shadow_fragment_shader),
        .mask = program(pass_vertex_shader, mask_fragment_shader),
        .quad = unit_quad(),
        .white = white_texture(),
        .width = width,
        .height = height,
    };
}

static void destroy_canvas(CF_Canvas *canvas)
{
    if (canvas->id) {
        cf_destroy_canvas(*canvas);
    }
    *canvas = (CF_Canvas){ 0 };
}

void rml_gpu_resize(rml_gpu *gpu, int width, int height)
{
    for (int i = 0; i < 3; i++) {
        destroy_canvas(&gpu->post[i]);
    }
    destroy_canvas(&gpu->mask_canvas);
    gpu->width = width;
    gpu->height = height;
}

void rml_gpu_destroy(rml_gpu *gpu)
{
    rml_gpu_resize(gpu, 0, 0);
    const CF_Shader shaders[] = { gpu->ui,     gpu->resolve, gpu->gradient,
                                  gpu->copy,   gpu->matrix,  gpu->blur,
                                  gpu->shadow, gpu->mask };
    for (size_t i = 0; i < sizeof shaders / sizeof *shaders; i++) {
        cf_destroy_shader(shaders[i]);
    }
    cf_destroy_mesh(gpu->quad);
    cf_destroy_texture(gpu->white);
}

CF_Canvas rml_gpu_canvas(int width, int height, const char *name, bool stencil)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.name = name;
    params.target.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.target.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.target.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.target.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.depth_stencil_enable = stencil;
    params.sample_count = CF_SAMPLE_COUNT_1;
    CF_Canvas canvas = cf_make_canvas(params);
    cf_canvas_set_clear_color(canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    return canvas;
}

CF_RenderState rml_gpu_blend_state(bool blend)
{
    CF_RenderState state = cf_render_state_defaults();
    state.cull_mode = CF_CULL_MODE_NONE;
    state.depth_compare = CF_COMPARE_FUNCTION_ALWAYS;
    state.depth_write_enabled = false;
    state.blend.enabled = blend;
    state.blend.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    state.blend.rgb_op = CF_BLEND_OP_ADD;
    state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.blend.alpha_op = CF_BLEND_OP_ADD;
    state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return state;
}

void rml_gpu_push_view(CF_Rect bounds)
{
    cf_draw_push_viewport((CF_Rect){ 0, 0, bounds.w, bounds.h });
    cf_draw3d_push_view(cf_m4_identity());
    cf_draw3d_push_projection(cf_ortho(
        (float)bounds.x,
        (float)(bounds.x + bounds.w),
        (float)(bounds.y + bounds.h),
        (float)bounds.y,
        -1,
        1
    ));
    /* A world pass can leave a transform behind. */
    cf_draw3d_push();
    cf_draw3d_transform(cf_m4_invert(cf_draw3d_peek_transform()));
}

void rml_gpu_pop_view()
{
    cf_draw3d_pop();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_view();
    cf_draw_pop_viewport();
}

void rml_gpu_flush(rml_gpu *gpu, CF_Canvas canvas, bool clear)
{
    cf_render_layers_to(canvas, RML_DRAW_LAYER, RML_DRAW_LAYER, clear);
    gpu->passes++;
}

CF_V4 rml_surface_uv(rml_surface surface, CF_Rect region)
{
    CF_Rect b = surface.bounds;
    return cf_v4(
        (float)(region.x - b.x) / b.w,
        (float)(region.y - b.y) / b.h,
        (float)(region.x + region.w - b.x) / b.w,
        (float)(region.y + region.h - b.y) / b.h
    );
}

static void queue_quad(const rml_gpu *gpu, CF_Rect rect, CF_Texture texture, CF_V4 uv)
{
    cf_draw3d_set_texture("u_texture", texture);
    cf_draw3d_push_mesh_attributes(uv);
    cf_draw3d_push();
    cf_draw3d_translate(cf_v3((float)rect.x, (float)rect.y, 0));
    cf_draw3d_scale(cf_v3((float)rect.w, (float)rect.h, 1));
    cf_draw3d_mesh(gpu->quad);
    cf_draw3d_pop();
    cf_draw3d_pop_mesh_attributes();
}

static void queue_program(const rml_gpu *gpu, CF_Shader shader, CF_Rect rect, CF_Texture texture, CF_V4 uv)
{
    cf_draw3d_push_shader(shader);
    queue_quad(gpu, rect, texture, uv);
    cf_draw3d_pop_shader();
}

void rml_gpu_quad(rml_gpu *gpu, CF_Rect rect, CF_Texture texture, CF_V4 uv, CF_Color tint)
{
    cf_draw3d_set_uniform("rml_tint", &tint, CF_UNIFORM_TYPE_FLOAT4, 1);
    queue_program(gpu, gpu->copy, rect, texture, uv);
}

/* ---------- Filters ---------- */

static CF_Canvas post_canvas(rml_gpu *gpu, int index)
{
    if (!gpu->post[index].id) {
        gpu->post[index] = rml_gpu_canvas(gpu->width, gpu->height, "RmlUi postprocess", false);
    }
    return gpu->post[index];
}

static rml_surface whole(const rml_gpu *gpu, CF_Canvas canvas)
{
    return (rml_surface){ canvas, { 0, 0, gpu->width, gpu->height } };
}

static CF_V4 uv_of(const rml_gpu *gpu, CF_Rect region)
{
    return rml_surface_uv((rml_surface){ .bounds = { 0, 0, gpu->width, gpu->height } }, region);
}

/* Queued quads render into a postprocess canvas under one scissor, which
 * stays inside the canvas as Metal requires, and one blend mode. */
static void begin_pass(const rml_gpu *gpu, CF_Rect scissor, bool blend)
{
    int left = math_max(scissor.x, 0), top = math_max(scissor.y, 0);
    int right = math_min(scissor.x + scissor.w, gpu->width);
    int bottom = math_min(scissor.y + scissor.h, gpu->height);
    cf_draw_push_scissor(
        (CF_Rect){ left, top, math_max(right - left, 0), math_max(bottom - top, 0) }
    );
    cf_draw3d_push_render_state(rml_gpu_blend_state(blend));
}

static void end_pass(rml_gpu *gpu, CF_Canvas canvas)
{
    rml_gpu_flush(gpu, canvas, false);
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
}

static void copy_pass(rml_gpu *gpu, rml_surface from, CF_Canvas to, CF_Rect region, CF_Color tint)
{
    begin_pass(gpu, region, false);
    rml_gpu_quad(gpu, region, cf_canvas_get_target(from.canvas), rml_surface_uv(from, region), tint);
    end_pass(gpu, to);
}

static void swap_primary(rml_gpu *gpu)
{
    CF_Canvas primary = gpu->post[0];
    gpu->post[0] = gpu->post[1];
    gpu->post[1] = primary;
}

/* GL3's split of a sigma into halvings and the sigma left for one pass. */
static int blur_levels(float sigma)
{
    enum
    {
        MAX_LEVELS = 10
    };
    int scaled = (int)(sigma * (2.0f / 3.0f)), levels = 0;
    while (scaled > 1 && levels < MAX_LEVELS) {
        scaled >>= 1;
        levels++;
    }
    return levels;
}

static CF_V4 blur_weights(float sigma)
{
    float weights[4], sum = 0;
    for (int i = 0; i < 4; i++) {
        weights[i] = sigma < 0.1f ? (float)(i == 0)
                                  : expf(-(float)(i * i) / (2 * sigma * sigma)) /
                                        (sqrtf(2 * CF_PI) * sigma);
        sum += (i ? 2 : 1) * weights[i];
    }
    return cf_v4(weights[0] / sum, weights[1] / sum, weights[2] / sum, weights[3] / sum);
}

/* One direction of the separable blur, from the region of one canvas into the
 * same region of another. The border around the region clears first. */
static void blur_pass(rml_gpu *gpu, CF_Canvas from, CF_Canvas to, CF_Rect region, CF_V2 step)
{
    CF_Rect border = { region.x - 1, region.y - 1, region.w + 2, region.h + 2 };
    CF_V4 offset = cf_v4(step.x, step.y, 0, 0);
    begin_pass(gpu, border, false);
    rml_gpu_quad(gpu, border, gpu->white, cf_v4(0, 0, 1, 1), (CF_Color){ 0 });
    cf_draw3d_set_uniform("rml_step", &offset, CF_UNIFORM_TYPE_FLOAT4, 1);
    queue_program(gpu, gpu->blur, region, cf_canvas_get_target(from), uv_of(gpu, region));
    end_pass(gpu, to);
}

/* Blurs the window of `image` in place, using `temp`, as GL3 does: a large
 * sigma first halves the window, blurs it at the reduced size and stretches
 * it back. Each halving rounds inward, so it samples only the window. The
 * blurred region keeps a transparent border, which the stretch samples. */
static void blur(rml_gpu *gpu, CF_Canvas image, CF_Canvas temp, CF_Rect window, float sigma)
{
    const float w = (float)gpu->width, h = (float)gpu->height;
    int x0 = window.x, y0 = window.y, x1 = window.x + window.w,
        y1 = window.y + window.h;
    int levels = 0, wanted = blur_levels(sigma);
    for (; levels < wanted; levels++) {
        int nx0 = (x0 + 1) / 2, ny0 = (y0 + 1) / 2;
        int nx1 = math_max(x1 / 2, nx0), ny1 = math_max(y1 / 2, ny0);
        if (nx1 == nx0 || ny1 == ny0) {
            break;
        }
        CF_Rect rect = { nx0, ny0, nx1 - nx0, ny1 - ny0 };
        begin_pass(gpu, rect, false);
        rml_gpu_quad(
            gpu,
            rect,
            cf_canvas_get_target(levels % 2 ? temp : image),
            cf_v4(2 * nx0 / w, 2 * ny0 / h, 2 * nx1 / w, 2 * ny1 / h),
            cf_color_white()
        );
        end_pass(gpu, levels % 2 ? image : temp);
        x0 = nx0, y0 = ny0, x1 = nx1, y1 = ny1;
    }
    CF_Rect region = { x0, y0, x1 - x0, y1 - y0 };
    if (levels % 2 == 0) {
        copy_pass(gpu, whole(gpu, image), temp, region, cf_color_white());
    }
    CF_V4 weights = blur_weights(math_clamp(sigma / (float)(1 << levels), 0.0f, 3.0f));
    CF_V4 limits = cf_v4((x0 + 0.5f) / w, (y0 + 0.5f) / h, (x1 - 0.5f) / w, (y1 - 0.5f) / h);
    cf_draw3d_set_uniform("rml_weights", &weights, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("rml_limits", &limits, CF_UNIFORM_TYPE_FLOAT4, 1);
    blur_pass(gpu, temp, image, region, cf_v2(0, 1 / h));
    blur_pass(gpu, image, temp, region, cf_v2(1 / w, 0));
    /* Stretch over the whole window, then redraw the aligned power-of-two
     * scale, which stays steady as the window moves. */
    CF_V4 source = uv_of(gpu, region);
    begin_pass(gpu, window, false);
    rml_gpu_quad(gpu, window, cf_canvas_get_target(temp), source, cf_color_white());
    if (levels) {
        CF_Rect aligned = { x0 << levels, y0 << levels, region.w << levels, region.h << levels };
        rml_gpu_quad(gpu, aligned, cf_canvas_get_target(temp), source, cf_color_white());
    }
    end_pass(gpu, image);
}

static void drop_shadow(rml_gpu *gpu, const rml_filter *filter, CF_Rect region)
{
    const float w = (float)gpu->width, h = (float)gpu->height;
    CF_Canvas image = post_canvas(gpu, 0), shadow = post_canvas(gpu, 1);
    CF_V4 limits = cf_v4(
        (region.x + 0.5f) / w,
        (region.y + 0.5f) / h,
        (region.x + region.w - 0.5f) / w,
        (region.y + region.h - 0.5f) / h
    );
    CF_V4 uv = uv_of(gpu, region);
    CF_V4 shifted = cf_v4(
        uv.x - filter->offset.x / w,
        uv.y - filter->offset.y / h,
        uv.z - filter->offset.x / w,
        uv.w - filter->offset.y / h
    );
    cf_draw3d_set_uniform("rml_shadow", (void *)&filter->color, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("rml_limits", &limits, CF_UNIFORM_TYPE_FLOAT4, 1);
    begin_pass(gpu, region, false);
    queue_program(gpu, gpu->shadow, region, cf_canvas_get_target(image), shifted);
    end_pass(gpu, shadow);
    if (filter->sigma >= 0.5f) {
        blur(gpu, shadow, post_canvas(gpu, 2), region, filter->sigma);
    }
    begin_pass(gpu, region, true);
    rml_gpu_quad(gpu, region, cf_canvas_get_target(image), uv, cf_color_white());
    end_pass(gpu, shadow);
    swap_primary(gpu);
}

static void apply_filter(rml_gpu *gpu, const rml_filter *filter, CF_Rect region)
{
    CF_Canvas image = post_canvas(gpu, 0), output = post_canvas(gpu, 1);
    CF_V4 uv = uv_of(gpu, region);
    switch (filter->kind) {
        case RML_FILTER_OPACITY: {
            float v = filter->value;
            copy_pass(gpu, whole(gpu, image), output, region, (CF_Color){ v, v, v, v });
            swap_primary(gpu);
            return;
        }
        case RML_FILTER_BLUR:
            blur(gpu, image, output, region, filter->sigma);
            return;
        case RML_FILTER_DROP_SHADOW: drop_shadow(gpu, filter, region); return;
        case RML_FILTER_COLOR_MATRIX:
            cf_draw3d_set_uniform("rml_rows", (void *)filter->rows, CF_UNIFORM_TYPE_FLOAT4, 3);
            begin_pass(gpu, region, false);
            queue_program(gpu, gpu->matrix, region, cf_canvas_get_target(image), uv);
            end_pass(gpu, output);
            swap_primary(gpu);
            return;
        case RML_FILTER_MASK_IMAGE:
            assert(gpu->mask_canvas.id && "a mask image filter follows its saved mask");
            cf_draw3d_set_texture("u_mask", cf_canvas_get_target(gpu->mask_canvas));
            begin_pass(gpu, region, false);
            queue_program(gpu, gpu->mask, region, cf_canvas_get_target(image), uv);
            end_pass(gpu, output);
            swap_primary(gpu);
            return;
    }
}

/* Passes render whole postprocess canvases on the RmlUi draw layer. */
static void begin_passes(rml_gpu *gpu)
{
    rml_gpu_push_view((CF_Rect){ 0, 0, gpu->width, gpu->height });
    cf_draw_push_layer(RML_DRAW_LAYER);
}

/* Bindings outlive the draw, and a canvas may be destroyed before they rebind. */
static void end_passes()
{
    cf_draw3d_set_texture("u_texture", (CF_Texture){ 0 });
    cf_draw3d_set_texture("u_mask", (CF_Texture){ 0 });
    cf_draw_pop_layer();
    rml_gpu_pop_view();
}

CF_Canvas rml_gpu_filter(rml_gpu *gpu, rml_surface source, CF_Rect region, const rml_filter *filters, int count)
{
    begin_passes(gpu);
    copy_pass(gpu, source, post_canvas(gpu, 0), region, cf_color_white());
    for (int i = 0; i < count; i++) {
        apply_filter(gpu, &filters[i], region);
    }
    end_passes();
    return gpu->post[0];
}

void rml_gpu_save_mask(rml_gpu *gpu, rml_surface source, CF_Rect region)
{
    if (!gpu->mask_canvas.id) {
        gpu->mask_canvas = rml_gpu_canvas(gpu->width, gpu->height, "RmlUi mask image", false);
    }
    begin_passes(gpu);
    copy_pass(gpu, source, gpu->mask_canvas, region, cf_color_white());
    end_passes();
}

void rml_gpu_resolve(rml_gpu *gpu, CF_Canvas premultiplied, CF_Canvas straight)
{
    CF_Rect window = { 0, 0, gpu->width, gpu->height };
    begin_passes(gpu);
    begin_pass(gpu, window, false);
    queue_program(gpu, gpu->resolve, window, cf_canvas_get_target(premultiplied), cf_v4(0, 0, 1, 1));
    end_pass(gpu, straight);
    end_passes();
}

void rml_gpu_push_gradient(const rml_gpu *gpu, const rml_gradient *gradient)
{
    CF_V4 header = { (float)gradient->function, (float)gradient->stop_count, 0, 0 };
    CF_V4 vectors = { gradient->p.x, gradient->p.y, gradient->v.x, gradient->v.y };
    cf_draw3d_set_uniform("rml_gradient", &header, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform("rml_gradient_vectors", &vectors, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw3d_set_uniform(
        "rml_stop_positions",
        (void *)gradient->positions,
        CF_UNIFORM_TYPE_FLOAT4,
        RML_GRADIENT_STOPS / 4
    );
    cf_draw3d_set_uniform("rml_stop_colors", (void *)gradient->colors, CF_UNIFORM_TYPE_FLOAT4, RML_GRADIENT_STOPS);
    cf_draw3d_push_shader(gpu->gradient);
}
