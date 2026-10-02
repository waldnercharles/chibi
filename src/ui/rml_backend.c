/* The RmlUi backend: a context per backend, drawn by Cute through the render
 * interface below into a premultiplied canvas, then resolved to a
 * straight-alpha canvas the host samples or presents. */
#include "ui/rml_backend.h"

#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/motion.h"
#include "ui/rml.h"
#include "ui/rml_shadow.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern const unsigned char editor_ui_font[];
extern const unsigned char editor_ui_medium_font[];
extern const unsigned char editor_ui_semibold_font[];
extern const unsigned char editor_ui_numeric_font[];
extern const unsigned char editor_ui_bold_font[];
extern const unsigned char editor_nerd_symbols_font[];
extern const unsigned char editor_noto_sans_font[];
extern const unsigned char editor_noto_sans_semibold_font[];
extern const unsigned char editor_noto_sans_bold_font[];
extern const unsigned char editor_inconsolata_font[];
extern const size_t editor_ui_font_size;
extern const size_t editor_ui_medium_font_size;
extern const size_t editor_ui_semibold_font_size;
extern const size_t editor_ui_numeric_font_size;
extern const size_t editor_ui_bold_font_size;
extern const size_t editor_nerd_symbols_font_size;
extern const unsigned char studio_regular_font[];
extern const size_t studio_regular_font_size;
extern const unsigned char studio_medium_font[];
extern const size_t studio_medium_font_size;
extern const unsigned char studio_semibold_font[];
extern const size_t studio_semibold_font_size;
extern const unsigned char studio_bold_font[];
extern const size_t studio_bold_font_size;
extern const unsigned char studio_numeric_font[];
extern const size_t studio_numeric_font_size;
extern const size_t editor_noto_sans_font_size;
extern const size_t editor_noto_sans_semibold_font_size;
extern const size_t editor_noto_sans_bold_font_size;
extern const size_t editor_inconsolata_font_size;
/* Workstream C: the code editor's face. */
extern const unsigned char editor_code_font[];
extern const size_t editor_code_font_size;

static const char vertex_shader
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

static const char fragment_shader
    [] = "layout (location = 0) in vec4 v_color;\n"
         "layout (location = 1) in vec2 v_uv;\n"
         "layout (location = 0) out vec4 result;\n"
         "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"
         "void main() { result = texture(u_texture, v_uv) * v_color; }\n";

static const char resolve_vertex_shader
    [] = "layout (location = 0) in vec2 in_position;\n"
         "layout (location = 1) in vec2 in_uv;\n"
         "layout (location = 8) in vec4 in_model0;\n"
         "layout (location = 9) in vec4 in_model1;\n"
         "layout (location = 10) in vec4 in_model2;\n"
         "layout (location = 0) out vec2 v_uv;\n"
         "layout (set = 1, binding = 0) uniform uniform_block { mat4 "
         "u_view_projection; };\n"
         "void main() {\n"
         "    vec4 p = vec4(in_position, 0.0, 1.0);\n"
         "    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), "
         "dot(in_model2, p));\n"
         "    v_uv = in_uv;\n"
         "    gl_Position = u_view_projection * vec4(world, 1.0);\n"
         "}\n";

static const char resolve_fragment_shader
    [] = "layout (location = 0) in vec2 v_uv;\n"
         "layout (location = 0) out vec4 result;\n"
         "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"
         "void main() {\n"
         "    vec4 c = texture(u_texture, v_uv);\n"
         "    result = vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a);\n"
         "}\n";

/* Kept separate from application layers so the Rml passes can flush without
 * consuming queued scene/UI geometry. */
#define RML_DRAW_LAYER (INT_MAX / 2)

typedef struct resolve_vertex
{
    float x;
    float y;
    float u;
    float v;
} resolve_vertex;

/* A texture RmlUi holds: one of ours, or the host texture bound to the
 * "kit:<name>" source at `external`, when not negative. */
typedef struct held_texture
{
    CF_Texture texture;
    int external;
} held_texture;

typedef struct compiled_geometry
{
    CF_Mesh mesh;
    rml_mesh cpu;
    CF_Aabb bounds;
} compiled_geometry;

typedef rml_shadow_layer shadow_layer;

/* A host-owned texture named by "kit:<name>" sources. Handles to it stay valid
 * as the host rebinds it. */
typedef struct external_texture
{
    char *name;
    CF_Texture texture;
    uint64_t revision;
} external_texture;

typedef struct draw_command
{
    uintptr_t geometry, texture;
    CF_Mesh mesh;
    CF_Texture gpu_texture;
    int external;
    uint64_t revision;
    rml_vec2 translation;
    CF_M4x4 transform;
    CF_Rect scissor;
    CF_Aabb bounds;
    bool has_geometry;
} draw_command;

/* Static paint runs stay on either side of each live host surface. Keeping
 * their original order also preserves translucent surfaces and foreground UI. */
typedef struct static_layer
{
    CF_Canvas canvas;
    CF_Mesh mesh;
    CF_Rect bounds;
    int first, count;
} static_layer;

struct rml_backend
{
    rml_renderer *renderer;
    rml_context *context;
    char context_name[32];
    char cursor[64];
    double elapsed;
    int logical_width;
    int logical_height;
    float scale;
    int physical_width;
    int physical_height;
    bool gpu_ready;
    bool changed; /* the last render drew what the one before it did not */
    bool output_invalid;
    bool frame_dirty;
    bool captured;
    bool retained_started;
    bool continuous_render;
    double next_update_at;
    int captured_documents;
    uint64_t output_revision;
    rml_backend_stats stats;
    draw_command *commands, *previous;
    static_layer *layers;
    bool scissor_enabled;
    CF_Rect scissor;
    CF_M4x4 transform;
    uint64_t next_geometry;
    uint64_t next_texture;
    CF_MAP(compiled_geometry) geometries;
    CF_MAP(held_texture) textures;
    CF_MAP(float) shadow_filters;
    CF_Texture *released; /* destroyed once the frame that drew them renders */
    CF_Mesh *released_meshes; /* same lifetime as released textures */
    shadow_layer *shadow_layers;
    uintptr_t next_layer;
    uintptr_t next_filter;
    bool clip_mask_enabled;
    external_texture *externals;
    CF_Texture white_texture;
    CF_Texture transparent_texture;
    CF_Shader ui_shader;
    CF_Shader resolve_shader;
    CF_Mesh resolve_mesh;
    CF_Canvas premultiplied_canvas;
    CF_Canvas straight_canvas;
};

/* RmlUi has one system interface for every context. Contexts share its clock,
 * which follows the backend that has advanced furthest, and each backend
 * collects the cursor its own context asks for while it is being driven. */
static double elapsed_seconds;
static rml_backend *driven;
static int backend_count;
static char *pending_paste_text;

/* ---------- System interface ---------- */

static double elapsed_time()
{
    return elapsed_seconds;
}

static void set_mouse_cursor(const char *name)
{
    if (driven) {
        snprintf(driven->cursor, sizeof driven->cursor, "%s", name);
    }
}

static void set_clipboard_text(const char *text)
{
    cf_clipboard_set(text);
}

static void get_clipboard_text(rml_string *text)
{
    if (pending_paste_text) {
        char *paste = pending_paste_text;
        pending_paste_text = NULL;
        rml_string_set(text, paste);
        free(paste);
        return;
    }
    char *clipboard = cf_clipboard_get();
    if (!clipboard) {
        rml_string_set(text, "");
        return;
    }
    rml_string_set(text, clipboard);
    cf_free(clipboard);
}

/* The first warning since rml_backend_take_warning; RmlUi logs globally. */
static char first_warning[256], taken_warning[256];

static void keep_warning(rml_log_type type, const char *message); /* workstream B */

static bool log_message(rml_log_type type, const char *message)
{
    keep_warning(type, message);
    const char *prefix = type == RML_LOG_ERROR   ? "error"
                       : type == RML_LOG_WARNING ? "warning"
                                                 : "info";
    fprintf(stderr, "RmlUi %s: %s\n", prefix, message);
    if (type != RML_LOG_INFO && !first_warning[0]) {
        snprintf(first_warning, sizeof first_warning, "%s", message);
    }
    return true;
}

const char *rml_backend_take_warning()
{
    memcpy(taken_warning, first_warning, sizeof taken_warning);
    first_warning[0] = '\0';
    return taken_warning;
}

static const rml_system_interface system_interface = {
    .elapsed_time = elapsed_time,
    .set_mouse_cursor = set_mouse_cursor,
    .set_clipboard_text = set_clipboard_text,
    .get_clipboard_text = get_clipboard_text,
    .log_message = log_message,
};

/* ---------- GPU resources ---------- */

static CF_Texture texture_from_pixels(int width, int height, const void *pixels, CF_Filter filter)
{
    CF_TextureParams params = cf_texture_defaults(width, height);
    params.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = filter;
    params.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(params);
    cf_texture_update(texture, (void *)pixels, width * height * 4);
    return texture;
}

static CF_Canvas create_canvas(int width, int height, const char *name)
{
    CF_CanvasParams params = cf_canvas_defaults(width, height);
    params.name = name;
    params.target.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.target.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.depth_stencil_enable = false;
    params.sample_count = CF_SAMPLE_COUNT_1;
    CF_Canvas canvas = cf_make_canvas(params);
    cf_canvas_set_clear_color(canvas, cf_make_color_rgba_f(0, 0, 0, 0));
    return canvas;
}

static CF_RenderState premultiplied_blend_state()
{
    CF_RenderState state = cf_render_state_defaults();
    state.cull_mode = CF_CULL_MODE_NONE;
    state.depth_compare = CF_COMPARE_FUNCTION_ALWAYS;
    state.depth_write_enabled = false;
    state.blend.enabled = true;
    state.blend.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    state.blend.write_R_enabled = true;
    state.blend.write_G_enabled = true;
    state.blend.write_B_enabled = true;
    state.blend.write_A_enabled = true;
    state.blend.rgb_op = CF_BLEND_OP_ADD;
    state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.blend.alpha_op = CF_BLEND_OP_ADD;
    state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
    state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return state;
}

static CF_RenderState replace_state()
{
    CF_RenderState state = cf_render_state_defaults();
    state.cull_mode = CF_CULL_MODE_NONE;
    state.depth_compare = CF_COMPARE_FUNCTION_ALWAYS;
    state.depth_write_enabled = false;
    state.blend.enabled = false;
    return state;
}

static void create_canvas_resources(rml_backend *backend)
{
    int w = backend->physical_width, h = backend->physical_height;
    backend->premultiplied_canvas = create_canvas(w, h, "RmlUi premultiplied");
    backend->straight_canvas = create_canvas(w, h, "RmlUi output");

    const CF_VertexAttribute attributes[2] = {
        { "in_position", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(resolve_vertex, x), false },
        { "in_uv", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(resolve_vertex, u), false },
    };
    backend->resolve_mesh = cf_make_mesh(sizeof(resolve_vertex) * 4, attributes, 2, sizeof(resolve_vertex));
    const resolve_vertex vertices[4] = {
        { 0, 0, 0, 0 },
        { (float)w, 0, 1, 0 },
        { (float)w, (float)h, 1, 1 },
        { 0, (float)h, 0, 1 },
    };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    cf_mesh_set_index_buffer(backend->resolve_mesh, sizeof(indices), 32);
    cf_mesh_update_vertex_data(backend->resolve_mesh, (void *)vertices, 4);
    cf_mesh_update_index_data(backend->resolve_mesh, (void *)indices, 6);

    backend->output_invalid = true;
}

static void release_static_layer(rml_backend *backend, static_layer *layer)
{
    if (layer->canvas.id) {
        backend->stats.cached_texture_bytes -= (uint64_t)layer->bounds.w *
                                               layer->bounds.h * 4;
        cf_destroy_canvas(layer->canvas);
    }
    if (layer->mesh.id) {
        cf_destroy_mesh(layer->mesh);
    }
    layer->canvas = (CF_Canvas){ 0 };
    layer->mesh = (CF_Mesh){ 0 };
}

static void release_static_layers(rml_backend *backend)
{
    for (int i = 0; i < acount(backend->layers); i++) {
        release_static_layer(backend, &backend->layers[i]);
    }
    asetlen(backend->layers, 0);
}

/* Layout can load textures before the first render, as a headless run does,
 * so texture loading creates these too. */
static void ensure_gpu(rml_backend *backend)
{
    if (backend->gpu_ready) {
        int width = 0, height = 0;
        cf_canvas_get_size(backend->premultiplied_canvas, &width, &height);
        if (width == backend->physical_width && height == backend->physical_height) {
            return;
        }
        assert(acount(backend->shadow_layers) == 0);
        release_static_layers(backend);
        cf_destroy_mesh(backend->resolve_mesh);
        cf_destroy_canvas(backend->premultiplied_canvas);
        cf_destroy_canvas(backend->straight_canvas);
        create_canvas_resources(backend);
        return;
    }
    backend->ui_shader = cf_make_shader_from_source(vertex_shader, fragment_shader);
    backend->resolve_shader = cf_make_shader_from_source(resolve_vertex_shader, resolve_fragment_shader);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    backend->white_texture = texture_from_pixels(1, 1, white, CF_FILTER_LINEAR);
    const uint8_t transparent[4] = { 0 };
    backend->transparent_texture = texture_from_pixels(1, 1, transparent, CF_FILTER_LINEAR);
    backend->gpu_ready = true;
    create_canvas_resources(backend);
}

static void destroy_gpu(rml_backend *backend)
{
    if (!backend->gpu_ready) {
        return;
    }
    for (int i = 0; i < cf_map_size(backend->geometries); i++) {
        cf_destroy_mesh(backend->geometries[i].mesh);
        free((void *)backend->geometries[i].cpu.vertices);
        free((void *)backend->geometries[i].cpu.indices);
    }
    for (int i = 0; i < cf_map_size(backend->textures); i++) {
        if (backend->textures[i].external < 0) {
            cf_destroy_texture(backend->textures[i].texture);
        }
    }
    for (int i = 0; i < acount(backend->released); i++) {
        cf_destroy_texture(backend->released[i]);
    }
    for (int i = 0; i < acount(backend->released_meshes); i++) {
        cf_destroy_mesh(backend->released_meshes[i]);
    }
    afree(backend->released);
    afree(backend->released_meshes);
    for (int i = 0; i < acount(backend->shadow_layers); i++) {
        free(backend->shadow_layers[i].pixels);
        free(backend->shadow_layers[i].mask);
    }
    afree(backend->shadow_layers);
    cf_map_free(backend->shadow_filters);
    cf_destroy_texture(backend->white_texture);
    cf_destroy_texture(backend->transparent_texture);
    cf_destroy_mesh(backend->resolve_mesh);
    release_static_layers(backend);
    cf_destroy_shader(backend->ui_shader);
    cf_destroy_shader(backend->resolve_shader);
    cf_destroy_canvas(backend->premultiplied_canvas);
    cf_destroy_canvas(backend->straight_canvas);
    backend->gpu_ready = false;
}

/* ---------- Render interface ---------- */

static rml_shadow_target shadow_target(const rml_backend *backend)
{
    return (rml_shadow_target){
        backend->physical_width, backend->physical_height,
        backend->transform,      backend->scissor_enabled,
        backend->scissor,        backend->clip_mask_enabled
    };
}

static void raster_shadow_geometry(rml_backend *backend, shadow_layer *layer, rml_mesh mesh, rml_vec2 translation)
{
    rml_shadow_geometry(shadow_target(backend), layer, mesh, translation);
}

static uintptr_t compile_geometry(void *user, rml_mesh mesh)
{
    rml_backend *backend = user;
    assert(mesh.vertex_count <= INT32_MAX && mesh.index_count <= INT32_MAX);
    const CF_VertexAttribute attributes[3] = {
        { "in_position", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(rml_vertex, position), false },
        { "in_color", CF_VERTEX_FORMAT_UBYTE4_NORM, (int)offsetof(rml_vertex, color), false },
        { "in_uv", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(rml_vertex, tex_coord), false },
    };
    CF_Mesh gpu_mesh = cf_make_mesh(
        (int)(mesh.vertex_count * sizeof(rml_vertex)),
        attributes,
        3,
        sizeof(rml_vertex)
    );
    cf_mesh_set_index_buffer(gpu_mesh, (int)(mesh.index_count * sizeof(int)), 32);
    cf_mesh_update_vertex_data(gpu_mesh, (void *)mesh.vertices, (int)mesh.vertex_count);
    cf_mesh_update_index_data(gpu_mesh, (void *)mesh.indices, (int)mesh.index_count);
    backend->frame_dirty = true;
    backend->stats.geometry_compiles++;
    backend->stats.uploaded_bytes += mesh.vertex_count * sizeof *mesh.vertices +
                                     mesh.index_count * sizeof *mesh.indices;
    uintptr_t handle = (uintptr_t)backend->next_geometry++;
    rml_mesh cpu = mesh;
    cpu.vertices = mesh.vertex_count
                     ? xmalloc(mesh.vertex_count * sizeof *mesh.vertices)
                     : NULL;
    cpu.indices = mesh.index_count ? xmalloc(mesh.index_count * sizeof *mesh.indices)
                                   : NULL;
    if (mesh.vertex_count) {
        memcpy(
            (void *)cpu.vertices,
            mesh.vertices,
            mesh.vertex_count * sizeof *mesh.vertices
        );
    }
    if (mesh.index_count) {
        memcpy(
            (void *)cpu.indices,
            mesh.indices,
            mesh.index_count * sizeof *mesh.indices
        );
    }
    CF_Aabb bounds = { 0 };
    if (mesh.vertex_count) {
        bounds.min = bounds.max = cf_v2(
            mesh.vertices[0].position.x,
            mesh.vertices[0].position.y
        );
        for (size_t i = 1; i < mesh.vertex_count; i++) {
            bounds.min.x = fminf(bounds.min.x, mesh.vertices[i].position.x);
            bounds.min.y = fminf(bounds.min.y, mesh.vertices[i].position.y);
            bounds.max.x = fmaxf(bounds.max.x, mesh.vertices[i].position.x);
            bounds.max.y = fmaxf(bounds.max.y, mesh.vertices[i].position.y);
        }
    }
    cf_map_set(
        backend->geometries,
        handle,
        ((compiled_geometry){ .mesh = gpu_mesh, .cpu = cpu, .bounds = bounds })
    );
    return handle;
}

/* Rectangles are intersected before converting transformed coordinates back
 * to integers. A distant/large authored transform must not overflow a crop. */
static CF_Rect intersect_rect(CF_Rect a, CF_Rect b)
{
    int64_t left = math_max(a.x, b.x), top = math_max(a.y, b.y);
    int64_t right = math_min((int64_t)a.x + a.w, (int64_t)b.x + b.w);
    int64_t bottom = math_min((int64_t)a.y + a.h, (int64_t)b.y + b.h);
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0 || right <= left || bottom <= top) {
        return (CF_Rect){ 0 };
    }
    return (CF_Rect){ (int)left, (int)top, (int)(right - left), (int)(bottom - top) };
}

static CF_Rect command_bounds(const rml_backend *backend, const draw_command *command)
{
    CF_Rect clip = intersect_rect(
        command->scissor,
        (CF_Rect){ 0, 0, backend->physical_width, backend->physical_height }
    );
    if (!clip.w || !clip.h || !command->has_geometry) {
        return (CF_Rect){ 0 };
    }
    CF_M4x4 model = cf_mul_m4(
        command->transform,
        cf_m4_translate(cf_v3(command->translation.x, command->translation.y, 0))
    );
    const float *m = model.elements;
    double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
    for (int i = 0; i < 4; i++) {
        double x = i & 1 ? command->bounds.max.x : command->bounds.min.x;
        double y = i & 2 ? command->bounds.max.y : command->bounds.min.y;
        /* Cute multiplies transform * translation, and the UI shader uses the
         * first two model rows without a perspective divide. */
        double tx = m[0] * x + m[4] * y + m[12];
        double ty = m[1] * x + m[5] * y + m[13];
        if (!isfinite(tx) || !isfinite(ty)) {
            return clip;
        }
        /* Bound float dot-product rounding as well as raster edges. This
         * remains conservative when large translated terms cancel on screen. */
        double error_x = 8 * FLT_EPSILON *
                         (fabs(m[0] * x) + fabs(m[4] * y) + fabs(m[12]) + 1);
        double error_y = 8 * FLT_EPSILON *
                         (fabs(m[1] * x) + fabs(m[5] * y) + fabs(m[13]) + 1);
        left = fmin(left, tx - error_x);
        top = fmin(top, ty - error_y);
        right = fmax(right, tx + error_x);
        bottom = fmax(bottom, ty + error_y);
    }
    /* One pixel protects subpixel edges and model/projection rounding. */
    left = floor(fmax(left - 1, clip.x));
    top = floor(fmax(top - 1, clip.y));
    right = ceil(fmin(right + 1, (double)clip.x + clip.w));
    bottom = ceil(fmin(bottom + 1, (double)clip.y + clip.h));
    if (right <= left || bottom <= top) {
        return (CF_Rect){ 0 };
    }
    return (CF_Rect){ (int)left, (int)top, (int)(right - left), (int)(bottom - top) };
}

static void render_geometry(void *user, uintptr_t geometry, rml_vec2 translation, uintptr_t texture)
{
    rml_backend *backend = user;
    const compiled_geometry *compiled = cf_map_get_ptr(backend->geometries, geometry);
    assert(compiled);
    if (acount(backend->shadow_layers)) {
        assert(texture == 0 && "shadow callback layers currently render untextured geometry");
        raster_shadow_geometry(
            backend,
            &backend->shadow_layers[acount(backend->shadow_layers) - 1],
            compiled->cpu,
            translation
        );
        return;
    }
    draw_command command = {
        .geometry = geometry,
        .texture = texture,
        .mesh = compiled->mesh,
        .bounds = compiled->bounds,
        .has_geometry = compiled->cpu.vertex_count && compiled->cpu.index_count,
        .gpu_texture = backend->white_texture,
        .external = -1,
        .translation = translation,
        .transform = backend->transform,
        .scissor = backend->scissor_enabled
                     ? backend->scissor
                     : (CF_Rect){ 0, 0, backend->physical_width, backend->physical_height },
    };
    if (texture) {
        const held_texture *held = cf_map_get_ptr(backend->textures, texture);
        assert(held);
        command.external = held->external;
        command.gpu_texture = held->external >= 0
                                ? backend->externals[held->external].texture
                                : held->texture;
        if (held->external >= 0) {
            command.revision = backend->externals[held->external].revision;
        }
    }
    apush(backend->commands, command);
}

static void release_geometry(void *user, uintptr_t geometry)
{
    rml_backend *backend = user;
    backend->frame_dirty = true;
    const compiled_geometry *mesh = cf_map_get_ptr(backend->geometries, geometry);
    if (!mesh) {
        return;
    }
    /* Cute's draw queue still refers to compiled meshes until its layer flush. */
    apush(backend->released_meshes, mesh->mesh);
    free((void *)mesh->cpu.vertices);
    free((void *)mesh->cpu.indices);
    cf_map_del(backend->geometries, geometry);
}

static uintptr_t hold_texture(rml_backend *backend, held_texture held)
{
    backend->frame_dirty = true;
    uintptr_t handle = (uintptr_t)backend->next_texture++;
    cf_map_set(backend->textures, handle, held);
    return handle;
}

static int external_index(rml_backend *backend, const char *name)
{
    for (int i = 0; i < acount(backend->externals); i++) {
        if (strcmp(backend->externals[i].name, name) == 0) {
            return i;
        }
    }
    apush(backend->externals, (external_texture){ xstrdup(name), { 0 } });
    return acount(backend->externals) - 1;
}

static bool has_extension(const char *source, const char *extension)
{
    size_t length = strlen(source), suffix = strlen(extension);
    return length >= suffix && strcasecmp(source + length - suffix, extension) == 0;
}

static bool load_image(const char *source, CF_Image *image)
{
    CF_Result result;
    if (has_extension(source, ".png")) {
        result = cf_image_load_png(source, image);
    } else if (has_extension(source, ".jpg") || has_extension(source, ".jpeg")) {
        result = cf_image_load_jpg(source, image);
    } else {
        fprintf(stderr, "RmlUi: unsupported texture format: %s\n", source);
        return false;
    }
    if (cf_is_error(result)) {
        fprintf(stderr, "RmlUi: failed to load texture %s: %s\n", source, result.details);
        return false;
    }
    return true;
}

/* Use content/art/sprite.png#nearest in RML to keep authored pixel art crisp. */
static char *image_path(const char *source, CF_Filter *filter)
{
    static const char nearest_suffix[] = "#nearest";
    size_t length = strlen(source);
    size_t suffix_length = sizeof(nearest_suffix) - 1;
    bool nearest = length >= suffix_length &&
                   strcmp(source + length - suffix_length, nearest_suffix) == 0;
    size_t path_length = length - (nearest ? suffix_length : 0);
    char *path = xmalloc(path_length + 1);
    memcpy(path, source, path_length);
    path[path_length] = '\0';
    *filter = nearest ? CF_FILTER_NEAREST : CF_FILTER_LINEAR;
    return path;
}

static uintptr_t load_texture(void *user, const char *source, int *width, int *height)
{
    rml_backend *backend = user;
    ensure_gpu(backend);
    if (strncmp(source, "kit:", 4) == 0) {
        *width = *height = 1;
        held_texture held = { .external = external_index(backend, source + 4) };
        return hold_texture(backend, held);
    }
    CF_Filter filter;
    char *path = image_path(source, &filter);
    CF_Image image = { 0 };
    if (!load_image(path, &image)) {
        free(path);
        return 0;
    }
    free(path);
    cf_image_premultiply(&image);
    *width = image.w;
    *height = image.h;
    held_texture held = { texture_from_pixels(image.w, image.h, image.pix, filter), -1 };
    cf_image_free(&image);
    return hold_texture(backend, held);
}

static uintptr_t generate_texture(void *user, const uint8_t *pixels, int width, int height)
{
    rml_backend *backend = user;
    ensure_gpu(backend);
    held_texture held = { texture_from_pixels(width, height, pixels, CF_FILTER_LINEAR),
                          -1 };
    return hold_texture(backend, held);
}

static void release_texture(void *user, uintptr_t texture)
{
    rml_backend *backend = user;
    backend->frame_dirty = true;
    const held_texture *held = cf_map_get_ptr(backend->textures, texture);
    if (!held) {
        return;
    }
    /* Draws already queued this frame may still sample it. */
    if (held->external < 0) {
        apush(backend->released, held->texture);
    }
    cf_map_del(backend->textures, texture);
}

static void destroy_released(rml_backend *backend)
{
    for (int i = 0; i < acount(backend->released); i++) {
        cf_destroy_texture(backend->released[i]);
    }
    asetlen(backend->released, 0);
    for (int i = 0; i < acount(backend->released_meshes); i++) {
        cf_destroy_mesh(backend->released_meshes[i]);
    }
    asetlen(backend->released_meshes, 0);
}

static shadow_layer *shadow_layer_by_handle(rml_backend *backend, uintptr_t handle)
{
    for (int i = 0; i < acount(backend->shadow_layers); i++) {
        if (backend->shadow_layers[i].handle == handle) {
            return &backend->shadow_layers[i];
        }
    }
    return NULL;
}

static uintptr_t push_shadow_layer(void *user)
{
    rml_backend *backend = user;
    const size_t pixels = (size_t)backend->physical_width * backend->physical_height;
    shadow_layer layer = {
        .handle = backend->next_layer++,
        .pixels = xcalloc(pixels * 4, 1),
        .mask = xmalloc(pixels),
    };
    memset(layer.mask, 255, pixels);
    apush(backend->shadow_layers, layer);
    return layer.handle;
}

static void composite_shadow_layers(
    void *user,
    uintptr_t source_handle,
    uintptr_t destination_handle,
    rml_blend_mode blend_mode,
    const uintptr_t *filters,
    size_t filter_count
)
{
    rml_backend *backend = user;
    shadow_layer *source = shadow_layer_by_handle(backend, source_handle);
    shadow_layer *destination = shadow_layer_by_handle(backend, destination_handle);
    assert(source && destination && "shadow callbacks composite only live layers");
    assert(destination_handle != 0 && "the shadow callback composites into its parent layer");
    assert(blend_mode == RML_BLEND_MODE_BLEND || blend_mode == RML_BLEND_MODE_REPLACE);
    assert(filter_count <= 1 && "box-shadow callbacks use at most one blur filter");

    uint8_t *blurred = NULL;
    const uint8_t *source_pixels = source->pixels;
    if (filter_count) {
        const float *sigma = cf_map_get_ptr(backend->shadow_filters, filters[0]);
        assert(sigma && isfinite(*sigma) && *sigma > 0);
        blurred = rml_shadow_blur(shadow_target(backend), source->pixels, *sigma);
        source_pixels = blurred;
    }

    int left = backend->scissor_enabled ? backend->scissor.x : 0;
    int top = backend->scissor_enabled ? backend->scissor.y : 0;
    int right = backend->scissor_enabled ? backend->scissor.x + backend->scissor.w
                                         : backend->physical_width;
    int bottom = backend->scissor_enabled
                   ? backend->scissor.y + backend->scissor.h
                   : backend->physical_height;
    left = math_max(left, 0);
    top = math_max(top, 0);
    right = math_min(right, backend->physical_width);
    bottom = math_min(bottom, backend->physical_height);
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            size_t pixel = (size_t)y * backend->physical_width + x;
            const uint8_t *src = source_pixels + pixel * 4;
            uint8_t *dst = destination->pixels + pixel * 4;
            float mask = backend->clip_mask_enabled && destination->mask_set
                           ? (float)destination->mask[pixel] / 255.0f
                           : 1.0f;
            if (blend_mode == RML_BLEND_MODE_REPLACE) {
                for (int channel = 0; channel < 4; channel++) {
                    dst[channel] = (uint8_t)lroundf(
                        dst[channel] * (1 - mask) + src[channel] * mask
                    );
                }
                continue;
            }
            float alpha = src[3] / 255.0f * mask;
            float inverse_alpha = 1.0f - alpha;
            for (int channel = 0; channel < 3; channel++) {
                dst[channel] = (uint8_t)lroundf(
                    fminf(255, src[channel] * mask + dst[channel] * inverse_alpha)
                );
            }
            dst[3] = (uint8_t)lroundf(fminf(255, alpha * 255 + dst[3] * inverse_alpha));
        }
    }
    free(blurred);
}

static void pop_shadow_layer(void *user)
{
    rml_backend *backend = user;
    assert(acount(backend->shadow_layers));
    shadow_layer layer = backend->shadow_layers[acount(backend->shadow_layers) - 1];
    free(layer.pixels);
    free(layer.mask);
    asetlen(backend->shadow_layers, acount(backend->shadow_layers) - 1);
}

static uintptr_t save_shadow_layer_as_texture(void *user)
{
    rml_backend *backend = user;
    assert(acount(backend->shadow_layers));
    int x = backend->scissor_enabled ? backend->scissor.x : 0;
    int y = backend->scissor_enabled ? backend->scissor.y : 0;
    int width = backend->scissor_enabled ? backend->scissor.w : backend->physical_width;
    int height = backend->scissor_enabled ? backend->scissor.h : backend->physical_height;
    x = math_max(x, 0);
    y = math_max(y, 0);
    width = math_min(width, backend->physical_width - x);
    height = math_min(height, backend->physical_height - y);
    if (width <= 0 || height <= 0) {
        return 0;
    }
    shadow_layer *layer = &backend->shadow_layers[acount(backend->shadow_layers) - 1];
    uint8_t *crop = xmalloc((size_t)width * height * 4);
    for (int row = 0; row < height; row++) {
        memcpy(
            crop + (size_t)row * width * 4,
            layer->pixels + ((size_t)(y + row) * backend->physical_width + x) * 4,
            (size_t)width * 4
        );
    }
    CF_Texture texture = texture_from_pixels(width, height, crop, CF_FILTER_LINEAR);
    free(crop);
    return hold_texture(backend, (held_texture){ .texture = texture, .external = -1 });
}

static uintptr_t compile_shadow_filter(void *user, const char *name, float sigma)
{
    rml_backend *backend = user;
    /* Rml's box-shadow generator requests only a Gaussian blur. Zero is the
       documented unsupported-filter handle; no substitute effect is applied. */
    if (strcmp(name, "blur") != 0 || !isfinite(sigma) || sigma <= 0) {
        fprintf(stderr, "RmlUi: unsupported box-shadow filter '%s'\n", name);
        return 0;
    }
    uintptr_t handle = backend->next_filter++;
    cf_map_set(backend->shadow_filters, handle, sigma);
    return handle;
}

static void release_shadow_filter(void *user, uintptr_t filter)
{
    rml_backend *backend = user;
    backend->frame_dirty = true;
    cf_map_del(backend->shadow_filters, filter);
}

static void enable_shadow_clip_mask(void *user, bool enable)
{
    rml_backend *backend = user;
    backend->clip_mask_enabled = enable;
}

static void render_shadow_clip_mask(
    void *user,
    rml_clip_mask_operation operation,
    uintptr_t geometry,
    rml_vec2 translation
)
{
    rml_backend *backend = user;
    /* Basic GPU rendering keeps rectangular scissor clipping. Offscreen
       shadow generation also consumes the shape's rounded clip mask. */
    if (!acount(backend->shadow_layers)) {
        return;
    }
    assert(
        operation == RML_CLIP_MASK_SET || operation == RML_CLIP_MASK_SET_INVERSE ||
        operation == RML_CLIP_MASK_INTERSECT
    );
    shadow_layer *layer = &backend->shadow_layers[acount(backend->shadow_layers) - 1];
    const compiled_geometry *compiled = cf_map_get_ptr(backend->geometries, geometry);
    assert(compiled);
    rml_shadow_mask(shadow_target(backend), layer, compiled->cpu, translation, operation);
}

static void enable_scissor_region(void *user, bool enable)
{
    rml_backend *backend = user;
    backend->scissor_enabled = enable;
}

static void set_scissor_region(void *user, rml_rect region)
{
    rml_backend *backend = user;
    backend->scissor = (CF_Rect){ region.x, region.y, region.width, region.height };
}

static void set_transform(void *user, const float *transform)
{
    rml_backend *backend = user;
    backend->transform = cf_m4_identity();
    if (!transform) {
        return;
    }
    memcpy(backend->transform.elements, transform, sizeof backend->transform.elements);
}

static const rml_render_interface render_interface = {
    .compile_geometry = compile_geometry,
    .render_geometry = render_geometry,
    .release_geometry = release_geometry,
    .load_texture = load_texture,
    .generate_texture = generate_texture,
    .release_texture = release_texture,
    .enable_scissor_region = enable_scissor_region,
    .set_scissor_region = set_scissor_region,
    .set_transform = set_transform,
    .enable_clip_mask = enable_shadow_clip_mask,
    .render_to_clip_mask = render_shadow_clip_mask,
    .push_layer = push_shadow_layer,
    .composite_layers = composite_shadow_layers,
    .pop_layer = pop_shadow_layer,
    .save_layer_as_texture = save_shadow_layer_as_texture,
    .compile_filter = compile_shadow_filter,
    .release_filter = release_shadow_filter,
};

/* ---------- Backend ---------- */

static void apply_dimensions(rml_backend *backend)
{
    if (!backend->context) {
        return;
    }
    rml_context_set_dimensions(
        backend->context,
        backend->physical_width,
        backend->physical_height
    );
    rml_context_set_dp_ratio(backend->context, backend->scale);
}

static void set_dimensions(rml_backend *backend)
{
    backend->logical_width = math_max(backend->logical_width, 1);
    backend->logical_height = math_max(backend->logical_height, 1);
    backend->scale = math_max(backend->scale, 0.01f);
    backend->physical_width = math_max(
        1,
        (int)lroundf(backend->logical_width * backend->scale)
    );
    backend->physical_height = math_max(
        1,
        (int)lroundf(backend->logical_height * backend->scale)
    );
    apply_dimensions(backend);
}

/* The first backend initializes RmlUi and its fonts. */
static bool initialize_rml()
{
    rml_set_system_interface(&system_interface);
    if (!rml_initialize()) {
        return false;
    }
    const struct
    {
        const unsigned char *data;
        size_t size;
        const char *family;
        int weight;
        bool fallback;
    } faces[] = {
        { editor_ui_font, editor_ui_font_size, "Inter", 400, true },
        { editor_ui_medium_font, editor_ui_medium_font_size, "Inter", 500 },
        { editor_ui_semibold_font, editor_ui_semibold_font_size, "Inter", 600 },
        { editor_ui_bold_font, editor_ui_bold_font_size, "Inter", 700 },
        { editor_noto_sans_font, editor_noto_sans_font_size, "Noto Sans", 400 },
        { editor_noto_sans_semibold_font, editor_noto_sans_semibold_font_size, "Noto Sans", 600 },
        { editor_noto_sans_bold_font, editor_noto_sans_bold_font_size, "Noto Sans", 700 },
        { editor_ui_numeric_font, editor_ui_numeric_font_size, "InterNumeric", 400 },
        { studio_regular_font, studio_regular_font_size, "Geist", 400 },
        { studio_medium_font, studio_medium_font_size, "Geist", 500 },
        { studio_semibold_font, studio_semibold_font_size, "Geist", 600 },
        { studio_bold_font, studio_bold_font_size, "Geist", 700 },
        { studio_numeric_font, studio_numeric_font_size, "GeistNumeric", 400 },
        { editor_nerd_symbols_font, editor_nerd_symbols_font_size, "Symbols", 400 },
        { editor_inconsolata_font, editor_inconsolata_font_size, "Inconsolata", 400 },
        /* Workstream C: kit_code's face. */
        { editor_code_font, editor_code_font_size, "JetBrains Mono", 400 },
    };
    for (size_t i = 0; i < sizeof faces / sizeof *faces; i++) {
        rml_load_font_face(
            faces[i].data,
            faces[i].size,
            faces[i].family,
            RML_FONT_NORMAL,
            faces[i].weight,
            faces[i].fallback
        );
    }
    /* Workstream N: the native elements every document may use. */
    motion_register();
    return true;
}

static void free_backend(rml_backend *backend)
{
    destroy_gpu(backend);
    afree(backend->commands);
    afree(backend->previous);
    afree(backend->layers);
    for (int i = 0; i < acount(backend->externals); i++) {
        free(backend->externals[i].name);
    }
    afree(backend->externals);
    cf_map_free(backend->geometries);
    cf_map_free(backend->textures);
    rml_renderer_destroy(backend->renderer);
    free(backend);
}

/* RmlUi starts over when its last context goes, as opening another project
   does, and so does its clock: a clock left at the last session's time would
   hold every transition of the next session until it caught up. */
static void shut_down()
{
    rml_shutdown();
    elapsed_seconds = 0;
}

rml_backend *rml_backend_create(int logical_width, int logical_height, float framebuffer_scale)
{
    static int context_serial;
    rml_backend *backend = xcalloc(1, sizeof *backend);
    backend->renderer = rml_renderer_create(&render_interface, backend);
    snprintf(backend->cursor, sizeof backend->cursor, "arrow");
    backend->transform = cf_m4_identity();
    backend->next_geometry = 1;
    backend->next_texture = 1;
    backend->next_layer = 1;
    backend->next_filter = 1;
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
    snprintf(backend->context_name, sizeof backend->context_name, "Chibi Editor %d", ++context_serial);

    /* Later backends add a context with their own renderer and canvas, such
       as the graph editor's. */
    if (backend_count == 0 && !initialize_rml()) {
        free_backend(backend);
        return NULL;
    }
    backend_count++;
    /* A later context joins the shared clock where it stands. */
    backend->elapsed = elapsed_seconds;
    backend->context = rml_create_context(
        backend->context_name,
        backend->physical_width,
        backend->physical_height,
        backend->renderer
    );
    if (!backend->context) {
        if (--backend_count == 0) {
            shut_down();
        }
        free_backend(backend);
        return NULL;
    }
    rml_context_set_dp_ratio(backend->context, backend->scale);
    return backend;
}

void rml_backend_destroy(rml_backend *backend)
{
    if (!backend) {
        return;
    }
    if (backend->context) {
        rml_remove_context(backend->context_name);
        backend->context = NULL;
        /* Drops this renderer's manager while the renderer is still alive. */
        rml_release_render_managers();
        if (--backend_count == 0) {
            shut_down();
        }
    }
    free_backend(backend);
}

void rml_backend_resize(rml_backend *backend, int logical_width, int logical_height, float framebuffer_scale)
{
    assert(backend);
    logical_width = math_max(logical_width, 1);
    logical_height = math_max(logical_height, 1);
    framebuffer_scale = math_max(framebuffer_scale, 0.01f);
    if (backend->logical_width == logical_width &&
        backend->logical_height == logical_height && backend->scale == framebuffer_scale &&
        backend->physical_width ==
            math_max(1, (int)lroundf(logical_width * framebuffer_scale)) &&
        backend->physical_height ==
            math_max(1, (int)lroundf(logical_height * framebuffer_scale))) {
        return;
    }
    backend->frame_dirty = true;
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
}

void rml_backend_resize_pixels(rml_backend *backend, int physical_width, int physical_height, float dp_ratio)
{
    assert(backend);
    dp_ratio = math_max(dp_ratio, 0.01f);
    physical_width = math_max(physical_width, 1);
    physical_height = math_max(physical_height, 1);
    if (backend->physical_width == physical_width &&
        backend->physical_height == physical_height && backend->scale == dp_ratio) {
        return;
    }
    backend->frame_dirty = true;
    backend->scale = dp_ratio;
    backend->physical_width = physical_width;
    backend->physical_height = physical_height;
    backend->logical_width = math_max(1, (int)lroundf(physical_width / dp_ratio));
    backend->logical_height = math_max(1, (int)lroundf(physical_height / dp_ratio));
    apply_dimensions(backend);
}

static void advance_time(rml_backend *backend, double delta_seconds)
{
    backend->elapsed += delta_seconds > 0.0 ? delta_seconds : 0.0;
    if (elapsed_seconds < backend->elapsed) {
        elapsed_seconds = backend->elapsed;
    }
}

void rml_backend_update(rml_backend *backend, double delta_seconds)
{
    assert(backend && backend->context);
    advance_time(backend, delta_seconds);
    backend->frame_dirty = true;
    backend->stats.context_updates++;
    driven = backend;
    rml_context_update(backend->context);
    presence_settle(backend->context);
    driven = NULL;
}

static void push_target_view(const rml_backend *backend, CF_Rect viewport)
{
    cf_draw_push_viewport(viewport);
    cf_draw3d_push_view(cf_m4_identity());
    cf_draw3d_push_projection(
        cf_ortho(0, (float)backend->physical_width, (float)backend->physical_height, 0, -1, 1)
    );
    cf_draw3d_push();
    cf_draw3d_transform(cf_m4_invert(cf_draw3d_peek_transform()));
}

static void pop_target_view()
{
    cf_draw3d_pop();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_view();
    cf_draw_pop_viewport();
}

static bool same_command(const draw_command *a, const draw_command *b)
{
    return a->geometry == b->geometry && a->texture == b->texture &&
           a->gpu_texture.id == b->gpu_texture.id && a->external == b->external &&
           a->revision == b->revision && a->translation.x == b->translation.x &&
           a->translation.y == b->translation.y &&
           memcmp(&a->transform, &b->transform, sizeof a->transform) == 0 &&
           memcmp(&a->scissor, &b->scissor, sizeof a->scissor) == 0;
}

static bool same_commands(const draw_command *a, int first_a, const draw_command *b, int first_b, int count)
{
    for (int i = 0; i < count; i++) {
        if (!same_command(&a[first_a + i], &b[first_b + i])) {
            return false;
        }
    }
    return true;
}

static bool queue_clipped_command(const draw_command *command, CF_Rect target, CF_Rect clip)
{
    if (command->external >= 0 && !command->gpu_texture.id) {
        return false;
    }
    CF_Rect scissor = intersect_rect(command->scissor, intersect_rect(target, clip));
    if (!scissor.w || !scissor.h) {
        return false;
    }
    scissor.x -= target.x;
    scissor.y -= target.y;
    cf_draw_push_scissor(scissor);
    cf_draw3d_set_texture("u_texture", command->gpu_texture);
    cf_draw3d_push();
    cf_draw3d_transform(command->transform);
    cf_draw3d_translate(cf_v3(command->translation.x, command->translation.y, 0));
    cf_draw3d_mesh(command->mesh);
    cf_draw3d_pop();
    cf_draw_pop_scissor();
    return true;
}

static void queue_command(const draw_command *command, CF_Rect target)
{
    queue_clipped_command(command, target, target);
}

static void queue_commands(rml_backend *backend, const draw_command *commands, int first, int count, CF_Rect target)
{
    for (int i = first; i < first + count; i++) {
        backend->stats.static_commands_replayed += queue_clipped_command(&commands[i], target, target);
    }
}

static void render_pass(rml_backend *backend, CF_Canvas canvas, bool clear)
{
    cf_render_layers_to(canvas, RML_DRAW_LAYER, RML_DRAW_LAYER, clear);
    backend->stats.render_passes++;
}

static CF_Rect union_rect(CF_Rect a, CF_Rect b)
{
    if (!a.w || !a.h) {
        return b;
    }
    if (!b.w || !b.h) {
        return a;
    }
    int left = math_min(a.x, b.x), top = math_min(a.y, b.y);
    int right = math_max(a.x + a.w, b.x + b.w);
    int bottom = math_max(a.y + a.h, b.y + b.h);
    return (CF_Rect){ left, top, right - left, bottom - top };
}

static CF_Rect static_bounds(const rml_backend *backend, int first, int count)
{
    CF_Rect bounds = { 0 };
    for (int i = first; i < first + count; i++) {
        bounds = union_rect(bounds, command_bounds(backend, &backend->commands[i]));
    }
    return bounds;
}

static void push_layer_view(CF_Rect bounds)
{
    cf_draw_push_viewport((CF_Rect){ 0, 0, bounds.w, bounds.h });
    cf_draw3d_push_projection(cf_ortho(
        (float)bounds.x,
        (float)(bounds.x + bounds.w),
        (float)(bounds.y + bounds.h),
        (float)bounds.y,
        -1,
        1
    ));
}

static void pop_layer_view()
{
    cf_draw3d_pop_projection();
    cf_draw_pop_viewport();
}

/* Keep the previous run when its structure and crop are unchanged. Old
 * commands contribute copied bounds only; their resources may already retire
 * during Rml's capture. Merging all changes into one rectangle avoids drawing
 * translucent commands twice where dirty regions overlap. */
static bool update_static_region(rml_backend *backend, static_layer *layer, int first, int count, CF_Rect bounds)
{
    if (backend->output_invalid || !layer->canvas.id || layer->first != first ||
        layer->count != count ||
        acount(backend->commands) != acount(backend->previous) ||
        memcmp(&layer->bounds, &bounds, sizeof bounds) != 0) {
        return false;
    }
    /* A changed live-surface partition can move commands between paint runs. */
    for (int i = 0; i < acount(backend->commands); i++) {
        if (backend->commands[i].external != backend->previous[i].external) {
            return false;
        }
    }
    CF_Rect dirty = { 0 };
    for (int i = first; i < first + count; i++) {
        const draw_command *command = &backend->commands[i];
        const draw_command *old = &backend->previous[i];
        if (same_command(command, old)) {
            continue;
        }
        /* A changed handle whose old resource is still held can mean commands
         * were reordered or rebound. Keep the full-refresh fallback for it. */
        if (!cf_map_get_ptr(backend->geometries, command->geometry) ||
            (command->geometry != old->geometry &&
             cf_map_get_ptr(backend->geometries, old->geometry)) ||
            (command->texture && !cf_map_get_ptr(backend->textures, command->texture)) ||
            (command->texture != old->texture && old->texture &&
             cf_map_get_ptr(backend->textures, old->texture))) {
            return false;
        }
        dirty = union_rect(dirty, command_bounds(backend, old));
        dirty = union_rect(dirty, command_bounds(backend, command));
    }
    dirty = intersect_rect(dirty, bounds);
    if (!dirty.w || !dirty.h) {
        return true;
    }
    /* A full dirty crop has no useful incremental work to retain. */
    if (memcmp(&dirty, &bounds, sizeof bounds) == 0) {
        return false;
    }
    push_layer_view(bounds);
    CF_Rect scissor = { dirty.x - bounds.x, dirty.y - bounds.y, dirty.w, dirty.h };
    cf_draw_push_scissor(scissor);
    cf_draw3d_push_render_state(replace_state());
    cf_draw3d_set_texture("u_texture", backend->transparent_texture);
    cf_draw3d_mesh(layer->mesh);
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
    for (int i = first; i < first + count; i++) {
        if (intersect_rect(command_bounds(backend, &backend->commands[i]), dirty)
                .w) {
            backend->stats.static_commands_replayed += queue_clipped_command(
                &backend->commands[i],
                bounds,
                dirty
            );
        }
    }
    /* The replace quad clears only the dirty scissor. Preserve the rest. */
    render_pass(backend, layer->canvas, false);
    pop_layer_view();
    backend->stats.static_layer_updates++;
    backend->stats.static_partial_updates++;
    return true;
}

static void update_layer_mesh(rml_backend *backend, static_layer *layer, CF_Rect bounds)
{
    const CF_VertexAttribute attributes[3] = {
        { "in_position", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(rml_vertex, position), false },
        { "in_color", CF_VERTEX_FORMAT_UBYTE4_NORM, (int)offsetof(rml_vertex, color), false },
        { "in_uv", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(rml_vertex, tex_coord), false },
    };
    const rml_color white = { 255, 255, 255, 255 };
    float left = (float)bounds.x, top = (float)bounds.y;
    float right = (float)(bounds.x + bounds.w),
          bottom = (float)(bounds.y + bounds.h);
    const rml_vertex vertices[4] = {
        { { left, top }, white, { 0, 0 } },
        { { right, top }, white, { 1, 0 } },
        { { right, bottom }, white, { 1, 1 } },
        { { left, bottom }, white, { 0, 1 } },
    };
    if (!layer->mesh.id) {
        const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        layer->mesh = cf_make_mesh(sizeof vertices, attributes, 3, sizeof(rml_vertex));
        cf_mesh_set_index_buffer(layer->mesh, sizeof indices, 32);
        cf_mesh_update_index_data(layer->mesh, (void *)indices, 6);
        backend->stats.uploaded_bytes += sizeof indices;
    }
    cf_mesh_update_vertex_data(layer->mesh, (void *)vertices, 4);
    backend->stats.uploaded_bytes += sizeof vertices;
}

static void update_static_layer(rml_backend *backend, int index, int first, int count)
{
    if (index == acount(backend->layers)) {
        apush(backend->layers, ((static_layer){ 0 }));
    }
    static_layer *layer = &backend->layers[index];
    bool same = layer->count == count &&
                same_commands(backend->commands, first, backend->previous, layer->first, count);
    if (same && !backend->output_invalid) {
        layer->first = first;
        layer->count = count;
        return;
    }
    CF_Rect bounds = static_bounds(backend, first, count);
    bool partial = update_static_region(backend, layer, first, count, bounds);
    layer->first = first;
    layer->count = count;
    if (partial) {
        return;
    }
    if (!bounds.w || !bounds.h) {
        release_static_layer(backend, layer);
        layer->bounds = bounds;
        return;
    }
    if (layer->canvas.id &&
        (bounds.w != layer->bounds.w || bounds.h != layer->bounds.h)) {
        backend->stats.cached_texture_bytes -= (uint64_t)layer->bounds.w *
                                               layer->bounds.h * 4;
        cf_destroy_canvas(layer->canvas);
        layer->canvas = (CF_Canvas){ 0 };
    }
    if (!layer->mesh.id || memcmp(&bounds, &layer->bounds, sizeof bounds) != 0) {
        update_layer_mesh(backend, layer, bounds);
    }
    layer->bounds = bounds;
    if (!layer->canvas.id) {
        layer->canvas = create_canvas(bounds.w, bounds.h, "RmlUi static layer");
        backend->stats.cached_texture_bytes += (uint64_t)bounds.w * bounds.h * 4;
    }
    /* UI commands keep global coordinates. Only the target projection and
     * integer scissors move into the cropped canvas's coordinate space. */
    push_layer_view(bounds);
    queue_commands(backend, backend->commands, first, count, bounds);
    render_pass(backend, layer->canvas, true);
    pop_layer_view();
    backend->stats.static_layer_updates++;
}

static void update_static_layers(rml_backend *backend)
{
    int first = 0, index = 0;
    for (int i = 0; i <= acount(backend->commands); i++) {
        if (i < acount(backend->commands) && backend->commands[i].external < 0) {
            continue;
        }
        update_static_layer(backend, index++, first, i - first);
        first = i + 1;
    }
    for (int i = index; i < acount(backend->layers); i++) {
        release_static_layer(backend, &backend->layers[i]);
    }
    asetlen(backend->layers, index);
}

static void compose_layers(rml_backend *backend, const draw_command *commands)
{
    /* All static refresh passes finish before queuing the composite, because
     * every pass consumes the same dedicated Cute layer. */
    for (int i = 0; i < acount(backend->layers); i++) {
        static_layer *layer = &backend->layers[i];
        if (layer->canvas.id) {
            cf_draw3d_set_texture("u_texture", cf_canvas_get_target(layer->canvas));
            cf_draw3d_mesh(layer->mesh);
        }
        int next = layer->first + layer->count;
        if (next < acount(commands)) {
            queue_command(
                &commands[next],
                (CF_Rect){ 0, 0, backend->physical_width, backend->physical_height }
            );
            backend->stats.texture_composites++;
        }
    }
    render_pass(backend, backend->premultiplied_canvas, true);
}

static void resolve_output(rml_backend *backend, CF_Rect viewport)
{

    push_target_view(backend, viewport);
    cf_draw3d_push_shader(backend->resolve_shader);
    cf_draw_push_layer(RML_DRAW_LAYER);
    cf_draw3d_set_texture("u_texture", cf_canvas_get_target(backend->premultiplied_canvas));
    cf_draw3d_push_render_state(replace_state());
    cf_draw3d_mesh(backend->resolve_mesh);
    /* Later meshes would otherwise keep the canvas bound past its resize or
     * destroy. */
    cf_draw3d_set_texture("u_texture", (CF_Texture){ 0 });
    render_pass(backend, backend->straight_canvas, true);
    cf_draw_pop_layer();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    pop_target_view();
}

static void draw_output(rml_backend *backend, const draw_command *commands, bool capture)
{
    const CF_Rect viewport = { 0, 0, backend->physical_width, backend->physical_height };
    push_target_view(backend, viewport);
    cf_draw3d_push_shader(backend->ui_shader);
    cf_draw3d_push_render_state(premultiplied_blend_state());
    cf_draw_push_layer(RML_DRAW_LAYER);
    /* Explicit scissor also resets the last SDL_GPU pass's clipping. */
    cf_draw_push_scissor(viewport);
    bool external = false;
    for (int i = 0; i < acount(commands); i++) {
        external |= commands[i].external >= 0;
    }
    if (external) {
        if (capture) {
            update_static_layers(backend);
        }
        compose_layers(backend, commands);
    } else {
        release_static_layers(backend);
        queue_commands(backend, commands, 0, acount(commands), viewport);
        render_pass(backend, backend->premultiplied_canvas, true);
        backend->stats.static_layer_updates += acount(commands) > 0;
    }
    cf_draw3d_set_texture("u_texture", (CF_Texture){ 0 });
    cf_draw_pop_scissor();
    cf_draw_pop_layer();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    pop_target_view();
    resolve_output(backend, viewport);
    backend->output_invalid = false;
    backend->output_revision++;
    backend->stats.output_updates++;
    backend->changed = true;
}

/* Zep's custom render hook owns timers which do not request Rml updates yet. */
static bool visible_code(rml_element *element)
{
    if (!rml_element_is_visible(element, true)) {
        return false;
    }
    if (strcmp(rml_element_tag_name(element), "kit-code") == 0) {
        return true;
    }
    for (int i = 0; i < rml_element_child_count(element); i++) {
        if (visible_code(rml_element_child(element, i))) {
            return true;
        }
    }
    return false;
}

void rml_backend_render(rml_backend *backend)
{
    assert(backend && backend->context);
    ensure_gpu(backend);
    asetlen(backend->commands, 0);
    backend->scissor_enabled = false;
    backend->transform = cf_m4_identity();
    backend->stats.render_captures++;
    if (backend->retained_started) {
        CHIBI_TRACE_ZONE_BEGIN(capture_zone, "Rml retained capture");
        rml_context_render(backend->context);
        CHIBI_TRACE_ZONE_END(capture_zone);
    } else {
        rml_context_render(backend->context);
    }

    int count = acount(backend->commands);
    bool changed = backend->output_invalid || count != acount(backend->previous) ||
                   !same_commands(backend->commands, 0, backend->previous, 0, count);
    if (changed) {
        if (backend->retained_started) {
            CHIBI_TRACE_ZONE_BEGIN(output_zone, "Rml retained output");
            draw_output(backend, backend->commands, true);
            CHIBI_TRACE_ZONE_END(output_zone);
        } else {
            draw_output(backend, backend->commands, true);
        }
    }
    destroy_released(backend);
    draw_command *old = backend->previous;
    backend->previous = backend->commands;
    backend->commands = old;
    backend->captured = true;
    backend->frame_dirty = false;
    /* A custom hook may release a resource after queuing its draw. That draw
     * completed above, but its command must not be replayed next frame. */
    for (int i = 0; i < count; i++) {
        const draw_command *command = &backend->previous[i];
        if (!cf_map_get_ptr(backend->geometries, command->geometry) ||
            (command->texture && !cf_map_get_ptr(backend->textures, command->texture))) {
            backend->frame_dirty = true;
        }
    }
    backend->captured_documents = rml_context_document_count(backend->context);
    backend->next_update_at = elapsed_seconds +
                              fmax(0.0, rml_context_next_update_delay(backend->context));
    if (backend->retained_started) {
        backend->continuous_render = false;
        for (int i = 0; i < backend->captured_documents; i++) {
            backend->continuous_render |= visible_code(
                rml_context_document(backend->context, i)
            );
        }
    }
}

static bool refresh_external_commands(rml_backend *backend)
{
    bool changed = false;
    for (int i = 0; i < acount(backend->previous); i++) {
        draw_command *command = &backend->previous[i];
        if (command->external < 0) {
            continue;
        }
        const external_texture *external = &backend->externals[command->external];
        changed |= command->gpu_texture.id != external->texture.id ||
                   command->revision != external->revision;
        command->gpu_texture = external->texture;
        command->revision = external->revision;
    }
    return changed;
}

void rml_backend_frame_retained(rml_backend *backend, double delta_seconds, bool dirty)
{
    assert(backend && backend->context);
    dirty |= !backend->retained_started;
    backend->retained_started = true;
    advance_time(backend, delta_seconds);
    ensure_gpu(backend);
    /* GetNextUpdateDelay is relative to the last update, not a countdown.
     * Live requests may shorten the deadline, but must never postpone it. */
    backend->next_update_at = fmin(
        backend->next_update_at,
        elapsed_seconds + fmax(0.0, rml_context_next_update_delay(backend->context))
    );
    if (dirty || backend->frame_dirty || !backend->captured || backend->output_invalid ||
        backend->continuous_render || elapsed_seconds >= backend->next_update_at ||
        backend->captured_documents != rml_context_document_count(backend->context)) {
        /* The clock already advanced. Ordinary update semantics remain intact. */
        CHIBI_TRACE_ZONE_BEGIN(update_zone, "Rml retained update");
        rml_backend_update(backend, 0.0);
        CHIBI_TRACE_ZONE_END(update_zone);
        rml_backend_render(backend);
        return;
    }
    backend->stats.retained_frames++;
    CHIBI_TRACE_ZONE_BEGIN(composite_zone, "Rml retained composite");
    if (refresh_external_commands(backend)) {
        /* Static runs and draw order were fixed by the last capture. */
        draw_output(backend, backend->previous, false);
    }
    CHIBI_TRACE_ZONE_END(composite_zone);
}

bool rml_backend_take_changed(rml_backend *backend)
{
    bool changed = backend->changed;
    backend->changed = false;
    return changed;
}

uint64_t rml_backend_texture(const rml_backend *backend)
{
    assert(backend);
    if (!backend->gpu_ready) {
        return 0;
    }
    return cf_canvas_get_target(backend->straight_canvas).id;
}

uint64_t rml_backend_texture_revision(const rml_backend *backend)
{
    return backend->output_revision;
}

rml_backend_stats rml_backend_get_stats(const rml_backend *backend)
{
    return backend->stats;
}

void *rml_backend_context(rml_backend *backend)
{
    assert(backend);
    return backend->context;
}

static unsigned rml_modifiers(unsigned modifiers)
{
    static const unsigned pairs[][2] = {
        { RML_INPUT_MOD_CONTROL, RML_KM_CTRL },
        { RML_INPUT_MOD_SHIFT, RML_KM_SHIFT },
        { RML_INPUT_MOD_ALT, RML_KM_ALT },
        { RML_INPUT_MOD_META, RML_KM_META },
        { RML_INPUT_MOD_CAPS_LOCK, RML_KM_CAPSLOCK },
        { RML_INPUT_MOD_NUM_LOCK, RML_KM_NUMLOCK },
        { RML_INPUT_MOD_SCROLL_LOCK, RML_KM_SCROLLLOCK },
    };
    unsigned result = 0;
    for (size_t i = 0; i < sizeof pairs / sizeof *pairs; i++) {
        if (modifiers & pairs[i][0]) {
            result |= pairs[i][1];
        }
    }
    return result;
}

/* True when RmlUi consumed the input. */
static bool consumed(rml_backend *backend, const rml_input *input)
{
    rml_context *context = backend->context;
    unsigned modifiers = rml_modifiers(input->modifiers);
    int x = (int)lroundf(input->x * backend->scale);
    int y = (int)lroundf(input->y * backend->scale);
    switch (input->type) {
        case RML_INPUT_POINTER_MOVE:
            return !rml_context_process_mouse_move(context, x, y, modifiers);
        case RML_INPUT_POINTER_LEAVE:
            return !rml_context_process_mouse_leave(context);
        case RML_INPUT_POINTER_DOWN:
            return !rml_context_process_mouse_button_down(context, input->button, modifiers);
        case RML_INPUT_POINTER_UP:
            return !rml_context_process_mouse_button_up(context, input->button, modifiers);
        case RML_INPUT_POINTER_WHEEL:
            return !rml_context_process_mouse_wheel(
                context,
                (rml_vec2){ input->wheel_x, input->wheel_y },
                modifiers
            );
        case RML_INPUT_KEY_PRESS:
            return !rml_context_process_key_down(context, (rml_key)input->key, modifiers);
        case RML_INPUT_KEY_RELEASE:
            return !rml_context_process_key_up(context, (rml_key)input->key, modifiers);
        case RML_INPUT_TEXT_INPUT:
            return !rml_context_process_text_input(
                context,
                input->text ? input->text : "",
                input->text ? input->text_size : 0
            );
    }
    return false;
}

static unsigned capture_of(rml_input_type type)
{
    switch (type) {
        case RML_INPUT_KEY_PRESS:
        case RML_INPUT_KEY_RELEASE: return RML_INPUT_CAPTURE_KEYBOARD;
        case RML_INPUT_TEXT_INPUT:
            return RML_INPUT_CAPTURE_KEYBOARD | RML_INPUT_CAPTURE_TEXT;
        default: return RML_INPUT_CAPTURE_POINTER;
    }
}

unsigned rml_backend_process_input(rml_backend *backend, const rml_input *input)
{
    assert(backend && backend->context && input);
    backend->frame_dirty = true;
    driven = backend;
    unsigned capture = consumed(backend, input) ? capture_of(input->type) : 0;
    driven = NULL;
    if (rml_context_is_mouse_interacting(backend->context)) {
        capture |= RML_INPUT_CAPTURE_POINTER;
    }
    return capture;
}

void rml_backend_set_pending_paste_text(const char *text)
{
    free(pending_paste_text);
    pending_paste_text = xstrdup(text);
}

void rml_backend_clear_pending_paste_text()
{
    free(pending_paste_text);
    pending_paste_text = NULL;
}

void rml_backend_set_texture(rml_backend *backend, const char *name, uint64_t texture)
{
    assert(backend && name);
    /* The lookup may grow externals, so it runs before the array is read. */
    int i = external_index(backend, name);
    if (texture || backend->externals[i].texture.id != texture) {
        backend->externals[i].revision++;
    }
    backend->externals[i].texture = (CF_Texture){ texture };
}

void rml_backend_set_texture_revision(rml_backend *backend, const char *name, uint64_t texture, uint64_t revision)
{
    assert(backend && name);
    int i = external_index(backend, name);
    backend->externals[i].texture = (CF_Texture){ texture };
    backend->externals[i].revision = revision;
}

const char *rml_backend_cursor(const rml_backend *backend)
{
    return backend->cursor;
}

void rml_backend_present(rml_backend *backend)
{
    assert(backend);
    if (!backend->gpu_ready) {
        return;
    }
    float w = (float)backend->logical_width;
    float h = (float)backend->logical_height;
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, w, h));
    cf_draw_canvas(backend->straight_canvas, cf_v2(0, 0), cf_v2(w, h));
    cf_draw_pop();
}

/* ---------- UI language service (workstream B) ---------- */

/* Every warning since rml_backend_keep_warnings, while kept. */
static char **kept_warnings;
static bool keeping_warnings;

static void keep_warning(rml_log_type type, const char *message)
{
    if (keeping_warnings && type != RML_LOG_INFO && type != RML_LOG_DEBUG) {
        apush(kept_warnings, xstrdup(message));
    }
}

void rml_backend_keep_warnings()
{
    assert(!keeping_warnings);
    keeping_warnings = true;
}

char **rml_backend_take_warnings()
{
    assert(keeping_warnings);
    char **warnings = kept_warnings;
    kept_warnings = NULL;
    keeping_warnings = false;
    return warnings;
}
