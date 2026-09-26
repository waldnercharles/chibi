/* The RmlUi backend: a context per backend, drawn by Cute through the render
 * interface below into a premultiplied canvas, then resolved to a
 * straight-alpha canvas the host samples or presents. */
#include "ui/rml_backend.h"

#include "platform/cute.h"
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/rml.h"

#include <assert.h>
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
extern const size_t editor_ui_font_size;
extern const size_t editor_ui_medium_font_size;
extern const size_t editor_ui_semibold_font_size;
extern const size_t editor_ui_numeric_font_size;
extern const size_t editor_ui_bold_font_size;
extern const size_t editor_nerd_symbols_font_size;

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

/* A host-owned texture named by "kit:<name>" sources. Handles to it stay valid
 * as the host rebinds it. */
typedef struct external_texture
{
    char *name;
    CF_Texture texture;
} external_texture;

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
    bool scissor_enabled;
    CF_Rect scissor;
    CF_M4x4 transform;
    uint64_t next_geometry;
    uint64_t next_texture;
    CF_MAP(CF_Mesh) geometries;
    CF_MAP(held_texture) textures;
    CF_Texture *released; /* destroyed once the frame that drew them renders */
    external_texture *externals;
    CF_Texture white_texture;
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

static bool log_message(rml_log_type type, const char *message)
{
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

static CF_Texture texture_from_pixels(int width, int height, const void *pixels)
{
    CF_TextureParams params = cf_texture_defaults(width, height);
    params.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
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
        cf_destroy_mesh(backend->resolve_mesh);
        cf_destroy_canvas(backend->premultiplied_canvas);
        cf_destroy_canvas(backend->straight_canvas);
        create_canvas_resources(backend);
        return;
    }
    backend->ui_shader = cf_make_shader_from_source(vertex_shader, fragment_shader);
    backend->resolve_shader = cf_make_shader_from_source(resolve_vertex_shader, resolve_fragment_shader);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    backend->white_texture = texture_from_pixels(1, 1, white);
    backend->gpu_ready = true;
    create_canvas_resources(backend);
}

static void destroy_gpu(rml_backend *backend)
{
    if (!backend->gpu_ready) {
        return;
    }
    for (int i = 0; i < cf_map_size(backend->geometries); i++) {
        cf_destroy_mesh(backend->geometries[i]);
    }
    for (int i = 0; i < cf_map_size(backend->textures); i++) {
        if (backend->textures[i].external < 0) {
            cf_destroy_texture(backend->textures[i].texture);
        }
    }
    for (int i = 0; i < acount(backend->released); i++) {
        cf_destroy_texture(backend->released[i]);
    }
    afree(backend->released);
    cf_destroy_texture(backend->white_texture);
    cf_destroy_mesh(backend->resolve_mesh);
    cf_destroy_shader(backend->ui_shader);
    cf_destroy_shader(backend->resolve_shader);
    cf_destroy_canvas(backend->premultiplied_canvas);
    cf_destroy_canvas(backend->straight_canvas);
    backend->gpu_ready = false;
}

/* ---------- Render interface ---------- */

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
    uintptr_t handle = (uintptr_t)backend->next_geometry++;
    cf_map_set(backend->geometries, handle, gpu_mesh);
    return handle;
}

static void render_geometry(void *user, uintptr_t geometry, rml_vec2 translation, uintptr_t texture)
{
    rml_backend *backend = user;
    const CF_Mesh *mesh = cf_map_get_ptr(backend->geometries, geometry);
    assert(mesh);
    CF_Texture gpu_texture = backend->white_texture;
    if (texture) {
        const held_texture *held = cf_map_get_ptr(backend->textures, texture);
        assert(held);
        bool external = held->external >= 0;
        gpu_texture = external ? backend->externals[held->external].texture
                               : held->texture;
        if (external && !gpu_texture.id) {
            return;
        }
    }
    cf_draw_push_layer(RML_DRAW_LAYER);
    cf_draw3d_set_texture("u_texture", gpu_texture);
    cf_draw3d_push();
    cf_draw3d_transform(backend->transform);
    cf_draw3d_translate(cf_v3(translation.x, translation.y, 0));
    cf_draw3d_mesh(*mesh);
    cf_draw3d_pop();
    cf_draw_pop_layer();
}

static void release_geometry(void *user, uintptr_t geometry)
{
    rml_backend *backend = user;
    const CF_Mesh *mesh = cf_map_get_ptr(backend->geometries, geometry);
    if (!mesh) {
        return;
    }
    cf_destroy_mesh(*mesh);
    cf_map_del(backend->geometries, geometry);
}

static uintptr_t hold_texture(rml_backend *backend, held_texture held)
{
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

static uintptr_t load_texture(void *user, const char *source, int *width, int *height)
{
    rml_backend *backend = user;
    ensure_gpu(backend);
    if (strncmp(source, "kit:", 4) == 0) {
        *width = *height = 1;
        held_texture held = { .external = external_index(backend, source + 4) };
        return hold_texture(backend, held);
    }
    CF_Image image = { 0 };
    if (!load_image(source, &image)) {
        return 0;
    }
    cf_image_premultiply(&image);
    *width = image.w;
    *height = image.h;
    held_texture held = { texture_from_pixels(image.w, image.h, image.pix), -1 };
    cf_image_free(&image);
    return hold_texture(backend, held);
}

static uintptr_t generate_texture(void *user, const uint8_t *pixels, int width, int height)
{
    rml_backend *backend = user;
    ensure_gpu(backend);
    held_texture held = { texture_from_pixels(width, height, pixels), -1 };
    return hold_texture(backend, held);
}

static void release_texture(void *user, uintptr_t texture)
{
    rml_backend *backend = user;
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
}

static void enable_scissor_region(void *user, bool enable)
{
    rml_backend *backend = user;
    if (enable == backend->scissor_enabled) {
        return;
    }
    backend->scissor_enabled = enable;
    if (enable) {
        cf_draw_push_scissor(backend->scissor);
        return;
    }
    cf_draw_pop_scissor();
}

static void set_scissor_region(void *user, rml_rect region)
{
    rml_backend *backend = user;
    backend->scissor = (CF_Rect){ region.x, region.y, region.width, region.height };
    if (!backend->scissor_enabled) {
        return;
    }
    cf_draw_pop_scissor();
    cf_draw_push_scissor(backend->scissor);
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
        { editor_ui_numeric_font, editor_ui_numeric_font_size, "InterNumeric", 400 },
        { editor_nerd_symbols_font, editor_nerd_symbols_font_size, "Symbols", 400 },
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
    return true;
}

static void free_backend(rml_backend *backend)
{
    destroy_gpu(backend);
    for (int i = 0; i < acount(backend->externals); i++) {
        free(backend->externals[i].name);
    }
    afree(backend->externals);
    cf_map_free(backend->geometries);
    cf_map_free(backend->textures);
    rml_renderer_destroy(backend->renderer);
    free(backend);
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
    backend->context = rml_create_context(
        backend->context_name,
        backend->physical_width,
        backend->physical_height,
        backend->renderer
    );
    if (!backend->context) {
        if (--backend_count == 0) {
            rml_shutdown();
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
            rml_shutdown();
        }
    }
    free_backend(backend);
}

void rml_backend_resize(rml_backend *backend, int logical_width, int logical_height, float framebuffer_scale)
{
    assert(backend);
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
}

void rml_backend_resize_pixels(rml_backend *backend, int physical_width, int physical_height, float dp_ratio)
{
    assert(backend);
    backend->scale = math_max(dp_ratio, 0.01f);
    backend->physical_width = math_max(physical_width, 1);
    backend->physical_height = math_max(physical_height, 1);
    backend->logical_width = math_max(
        1,
        (int)lroundf(backend->physical_width / backend->scale)
    );
    backend->logical_height = math_max(
        1,
        (int)lroundf(backend->physical_height / backend->scale)
    );
    apply_dimensions(backend);
}

void rml_backend_update(rml_backend *backend, double delta_seconds)
{
    assert(backend && backend->context);
    backend->elapsed += delta_seconds > 0.0 ? delta_seconds : 0.0;
    if (elapsed_seconds < backend->elapsed) {
        elapsed_seconds = backend->elapsed;
    }
    driven = backend;
    rml_context_update(backend->context);
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

void rml_backend_render(rml_backend *backend)
{
    assert(backend && backend->context);
    ensure_gpu(backend);

    const CF_Rect viewport = { 0, 0, backend->physical_width, backend->physical_height };
    push_target_view(backend, viewport);
    cf_draw3d_push_shader(backend->ui_shader);
    cf_draw3d_push_render_state(premultiplied_blend_state());
    /* SDL_GPU keeps a pass's last scissor for the draws after it, so geometry
       RmlUi leaves unclipped is drawn under a scissor of the whole target. */
    cf_draw_push_scissor(viewport);
    enable_scissor_region(backend, false);
    rml_context_render(backend->context);
    enable_scissor_region(backend, false);
    cf_render_layers_to(backend->premultiplied_canvas, RML_DRAW_LAYER, RML_DRAW_LAYER, true);
    destroy_released(backend);
    cf_draw_pop_scissor();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    pop_target_view();

    push_target_view(backend, viewport);
    cf_draw3d_push_shader(backend->resolve_shader);
    cf_draw_push_layer(RML_DRAW_LAYER);
    cf_draw3d_set_texture("u_texture", cf_canvas_get_target(backend->premultiplied_canvas));
    cf_draw3d_push_render_state(replace_state());
    cf_draw3d_mesh(backend->resolve_mesh);
    /* Later meshes would otherwise keep the canvas bound past its resize or destroy. */
    cf_draw3d_set_texture("u_texture", (CF_Texture){ 0 });
    cf_render_layers_to(backend->straight_canvas, RML_DRAW_LAYER, RML_DRAW_LAYER, true);
    cf_draw_pop_layer();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    pop_target_view();
}

uint64_t rml_backend_texture(const rml_backend *backend)
{
    assert(backend);
    if (!backend->gpu_ready) {
        return 0;
    }
    return cf_canvas_get_target(backend->straight_canvas).id;
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
    driven = backend;
    unsigned capture = consumed(backend, input) ? capture_of(input->type) : 0;
    driven = NULL;
    if (rml_context_is_mouse_interacting(backend->context)) {
        capture |= RML_INPUT_CAPTURE_POINTER;
    }
    return capture;
}

void rml_backend_set_texture(rml_backend *backend, const char *name, uint64_t texture)
{
    assert(backend && name);
    /* The lookup may grow externals, so it runs before the array is read. */
    int i = external_index(backend, name);
    backend->externals[i].texture = (CF_Texture){ texture };
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
