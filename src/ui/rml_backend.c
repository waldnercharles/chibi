/* The RmlUi backend: a context per backend, drawn by Cute through the render
 * interface below into a premultiplied canvas, then resolved to a
 * straight-alpha canvas the host samples. Direct presentation uses the
 * premultiplied canvas to match Cute's source-over blend. Rendering records
 * RmlUi's calls as commands, and replays them on the GPU after RmlUi's
 * reference GL3 renderer: layers are window-sized canvases, clip masks are
 * stencil, and filters and gradients are rml_gpu programs. */
#include "ui/rml_backend.h"

#include "diagnostics/tracy.h"
#include "platform/cute.h"
#include "runtime/ui/builtin_fonts.h"
#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/autofocus.h"
#include "ui/controls/controls.h"
#include "ui/drag.h"
#include "ui/focus_within.h"
#include "ui/iframe.h"
#include "ui/iframe_profile.h"
#include "ui/motion.h"
#include "ui/popover.h"
#include "ui/rml.h"
#include "ui/rml_gpu.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* A texture RmlUi holds: one of ours, a saved layer's canvas, or the host
 * texture bound to the "kit:<name>" source at `external`, when not negative. */
typedef struct held_texture
{
    CF_Texture texture;
    int external;
    CF_Canvas canvas;
} held_texture;

typedef struct compiled_geometry
{
    CF_Mesh mesh;
    CF_Aabb bounds;
    bool drawable; /* has vertices and indices */
} compiled_geometry;

/* A host-owned texture named by "kit:<name>" sources. Handles to it stay valid
 * as the host rebinds it. A still one (rml_backend_set_image) draws in the
 * retained paint; the others draw live between its static runs. */
typedef struct external_texture
{
    char *name;
    CF_Texture texture;
    uint64_t revision;
    bool still;
} external_texture;

/* RmlUi's render calls, in order. Replay needs only the commands: each draw
 * names the layer it paints and the clip commands whose mask it tests. */
typedef enum command_kind
{
    COMMAND_DRAW,
    COMMAND_GRADIENT,
    COMMAND_CLIP,
    COMMAND_PUSH_LAYER,
    COMMAND_COMPOSITE,
    COMMAND_POP_LAYER,
    COMMAND_SAVE_TEXTURE,
    COMMAND_SAVE_MASK,
} command_kind;

typedef struct draw_command
{
    command_kind kind;
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
    int layer; /* the layer it paints, or a composite's destination; 0 is the base */
    /* The clip commands whose mask it tests and the stencil value that
     * passes; none when clip_first is negative. */
    int clip_first, clip_last, clip_value;
    int operation; /* clip: rml_clip_mask_operation; composite: rml_blend_mode */
    int source;    /* composite */
    int first_arg, arg_count; /* composite filters, or a gradient, in its capture */
    CF_Canvas canvas;         /* save texture */
} draw_command;

/* One render's commands with the filters and gradients they carry by value,
 * since RmlUi may release a filter as soon as it composites. */
typedef struct capture
{
    draw_command *commands;
    rml_filter *filters;
    rml_gradient *gradients;
} capture;

/* Static paint runs stay on either side of each live host surface. Keeping
 * their original order also preserves translucent surfaces and foreground UI. */
typedef struct static_layer
{
    CF_Canvas canvas;
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
    bool resident_images;
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
    capture current, previous;
    static_layer *layers;
    bool scissor_enabled;
    CF_Rect scissor;
    CF_M4x4 transform;
    /* While capturing: open layers and the clip mask draws test. */
    int depth;
    bool clip_enabled;
    int clip_first, clip_last, clip_value;
    uint64_t next_geometry;
    uint64_t next_texture;
    uintptr_t next_filter;
    uintptr_t next_shader;
    CF_MAP(compiled_geometry) geometries;
    CF_MAP(held_texture) textures;
    CF_MAP(rml_filter) filters;
    CF_MAP(rml_gradient) gradients;
    CF_Texture *released; /* destroyed once the frame that drew them renders */
    CF_Mesh *released_meshes;     /* same lifetime as released textures */
    CF_Canvas *released_canvases; /* same lifetime as released textures */
    CF_Canvas *stack; /* window-sized layer canvases; stack[i] holds layer i + 1 */
    external_texture *externals;
    rml_gpu gpu;
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
static int warning_count;

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
    warning_count += type == RML_LOG_WARNING || type == RML_LOG_ERROR ||
                     type == RML_LOG_ASSERT;
    return true;
}

int rml_backend_warning_count()
{
    return warning_count;
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

static CF_Rect window_rect(const rml_backend *backend)
{
    return (CF_Rect){ 0, 0, backend->physical_width, backend->physical_height };
}

static void create_canvas_resources(rml_backend *backend)
{
    int w = backend->physical_width, h = backend->physical_height;
    /* Clip masks test the stencil of whichever canvas a draw paints. */
    backend->premultiplied_canvas = rml_gpu_canvas(w, h, "RmlUi premultiplied", true);
    backend->straight_canvas = rml_gpu_canvas(w, h, "RmlUi output", false);
    rml_gpu_resize(&backend->gpu, w, h);
    backend->output_invalid = true;
}

static void release_static_layer(rml_backend *backend, static_layer *layer)
{
    if (layer->canvas.id) {
        backend->stats.cached_texture_bytes -= (uint64_t)layer->bounds.w *
                                               layer->bounds.h * 4;
        cf_destroy_canvas(layer->canvas);
    }
    layer->canvas = (CF_Canvas){ 0 };
}

static void release_static_layers(rml_backend *backend)
{
    for (int i = 0; i < acount(backend->layers); i++) {
        release_static_layer(backend, &backend->layers[i]);
    }
    asetlen(backend->layers, 0);
}

static void release_stack(rml_backend *backend)
{
    for (int i = 0; i < acount(backend->stack); i++) {
        cf_destroy_canvas(backend->stack[i]);
    }
    asetlen(backend->stack, 0);
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
        release_static_layers(backend);
        release_stack(backend);
        cf_destroy_canvas(backend->premultiplied_canvas);
        cf_destroy_canvas(backend->straight_canvas);
        create_canvas_resources(backend);
        return;
    }
    rml_gpu_create(&backend->gpu, backend->physical_width, backend->physical_height);
    backend->gpu_ready = true;
    create_canvas_resources(backend);
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
    for (int i = 0; i < acount(backend->released_canvases); i++) {
        cf_destroy_canvas(backend->released_canvases[i]);
    }
    asetlen(backend->released_canvases, 0);
}

static void destroy_gpu(rml_backend *backend)
{
    if (!backend->gpu_ready) {
        return;
    }
    for (int i = 0; i < cf_map_size(backend->geometries); i++) {
        cf_destroy_mesh(backend->geometries[i].mesh);
    }
    for (int i = 0; i < cf_map_size(backend->textures); i++) {
        const held_texture *held = &backend->textures[i];
        if (held->canvas.id) {
            cf_destroy_canvas(held->canvas);
        } else if (held->external < 0) {
            cf_destroy_texture(held->texture);
        }
    }
    destroy_released(backend);
    afree(backend->released);
    afree(backend->released_meshes);
    afree(backend->released_canvases);
    release_static_layers(backend);
    release_stack(backend);
    afree(backend->stack);
    rml_gpu_destroy(&backend->gpu);
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
    backend->frame_dirty = true;
    backend->stats.geometry_compiles++;
    backend->stats.uploaded_bytes += mesh.vertex_count * sizeof *mesh.vertices +
                                     mesh.index_count * sizeof *mesh.indices;
    uintptr_t handle = (uintptr_t)backend->next_geometry++;
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
        ((compiled_geometry){ .mesh = gpu_mesh,
                              .bounds = bounds,
                              .drawable = mesh.vertex_count && mesh.index_count })
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

/* The base pixels a geometry draw can touch. */
static CF_Rect geometry_bounds(const rml_backend *backend, const draw_command *command)
{
    CF_Rect clip = intersect_rect(command->scissor, window_rect(backend));
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

/* The base pixels a command paints: draws and composites into layer 0. */
static CF_Rect command_bounds(const rml_backend *backend, const draw_command *command)
{
    if (command->layer != 0) {
        return (CF_Rect){ 0 };
    }
    switch (command->kind) {
        case COMMAND_DRAW:
        case COMMAND_GRADIENT: return geometry_bounds(backend, command);
        case COMMAND_COMPOSITE:
            return intersect_rect(command->scissor, window_rect(backend));
        default: return (CF_Rect){ 0 };
    }
}

static CF_Rect current_scissor(const rml_backend *backend)
{
    return backend->scissor_enabled ? backend->scissor : window_rect(backend);
}

/* A command with the state RmlUi set for it. */
static draw_command record(const rml_backend *backend, command_kind kind)
{
    bool clipped = backend->clip_enabled;
    return (draw_command){
        .kind = kind,
        .external = -1,
        .transform = backend->transform,
        .scissor = current_scissor(backend),
        .layer = backend->depth,
        .clip_first = clipped ? backend->clip_first : -1,
        .clip_last = clipped ? backend->clip_last : -1,
        .clip_value = clipped ? backend->clip_value : 0,
    };
}

static void set_geometry(rml_backend *backend, draw_command *command, uintptr_t geometry, rml_vec2 translation)
{
    const compiled_geometry *compiled = cf_map_get_ptr(backend->geometries, geometry);
    assert(compiled);
    command->geometry = geometry;
    command->mesh = compiled->mesh;
    command->bounds = compiled->bounds;
    command->has_geometry = compiled->drawable;
    command->translation = translation;
}

static void render_geometry(void *user, uintptr_t geometry, rml_vec2 translation, uintptr_t texture)
{
    rml_backend *backend = user;
    draw_command command = record(backend, COMMAND_DRAW);
    set_geometry(backend, &command, geometry, translation);
    command.texture = texture;
    command.gpu_texture = backend->gpu.white;
    if (texture) {
        const held_texture *held = cf_map_get_ptr(backend->textures, texture);
        assert(held);
        const external_texture *external = held->external >= 0
                                             ? &backend->externals[held->external]
                                             : NULL;
        command.external = external && !external->still ? held->external : -1;
        command.gpu_texture = external ? external->texture : held->texture;
        command.revision = external ? external->revision : 0;
        /* An unbound still image draws nothing, as a live one does. */
        if (external && external->still && !external->texture.id) {
            return;
        }
    }
    apush(backend->current.commands, command);
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
    apush(backend->externals, (external_texture){ .name = xstrdup(name) });
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

static rml_image_provider image_provider;
static void *image_provider_user;
void rml_backend_set_image_provider(rml_image_provider provider, void *user)
{
    image_provider = provider;
    image_provider_user = user;
}
void rml_backend_require_resident_images(rml_backend *backend, bool required)
{
    backend->resident_images = required;
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
    if (backend->resident_images) {
        const unsigned char *pixels = NULL;
        int w = 0, h = 0;
        if (!image_provider ||
            !image_provider(image_provider_user, path, &pixels, &w, &h) ||
            !pixels || w <= 0 || h <= 0 || w > 16384 || h > 16384 ||
            (size_t)w * (size_t)h > 16 * 1024 * 1024) {
            free(path);
            return 0;
        }
        size_t bytes = (size_t)w * (size_t)h * 4;
        unsigned char *copy = xmalloc(bytes);
        memcpy(copy, pixels, bytes);
        for (size_t i = 0; i < bytes; i += 4) {
            for (int channel = 0; channel < 3; channel++) {
                copy[i + channel] = (unsigned)copy[i + channel] * copy[i + 3] / 255;
            }
        }
        *width = w;
        *height = h;
        held_texture held = { .texture = texture_from_pixels(w, h, copy, filter),
                              .external = -1 };
        free(copy);
        free(path);
        return hold_texture(backend, held);
    }
    if (!load_image(path, &image)) {
        free(path);
        return 0;
    }
    free(path);
    cf_image_premultiply(&image);
    *width = image.w;
    *height = image.h;
    held_texture held = { .texture = texture_from_pixels(image.w, image.h, image.pix, filter),
                          .external = -1 };
    cf_image_free(&image);
    return hold_texture(backend, held);
}

static uintptr_t generate_texture(void *user, const uint8_t *pixels, int width, int height)
{
    rml_backend *backend = user;
    ensure_gpu(backend);
    held_texture held = { .texture = texture_from_pixels(width, height, pixels, CF_FILTER_LINEAR),
                          .external = -1 };
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
    if (held->canvas.id) {
        apush(backend->released_canvases, held->canvas);
    } else if (held->external < 0) {
        apush(backend->released, held->texture);
    }
    cf_map_del(backend->textures, texture);
}

static void enable_clip_mask(void *user, bool enable)
{
    rml_backend *backend = user;
    backend->clip_enabled = enable;
}

/* Set and set-inverse start a mask; each intersect narrows it, so the value
 * that passes counts the operations, as GL3's stencil does. */
static void render_to_clip_mask(void *user, rml_clip_mask_operation operation, uintptr_t geometry, rml_vec2 translation)
{
    rml_backend *backend = user;
    draw_command command = record(backend, COMMAND_CLIP);
    command.clip_first = command.clip_last = -1;
    command.clip_value = 0;
    command.operation = operation;
    set_geometry(backend, &command, geometry, translation);
    int index = acount(backend->current.commands);
    if (operation == RML_CLIP_MASK_INTERSECT) {
        assert(backend->clip_first >= 0 && "an intersect narrows a mask set before it");
        backend->clip_value++;
    } else {
        backend->clip_first = index;
        backend->clip_value = 1;
    }
    backend->clip_last = index;
    apush(backend->current.commands, command);
}

static uintptr_t push_layer(void *user)
{
    rml_backend *backend = user;
    backend->depth++;
    apush(backend->current.commands, record(backend, COMMAND_PUSH_LAYER));
    return (uintptr_t)backend->depth;
}

static void composite_layers(
    void *user,
    uintptr_t source,
    uintptr_t destination,
    rml_blend_mode blend_mode,
    const uintptr_t *filters,
    size_t filter_count
)
{
    rml_backend *backend = user;
    assert((int)source <= backend->depth && (int)destination <= backend->depth);
    draw_command command = record(backend, COMMAND_COMPOSITE);
    command.layer = (int)destination;
    command.source = (int)source;
    command.operation = blend_mode;
    command.first_arg = acount(backend->current.filters);
    command.arg_count = (int)filter_count;
    for (size_t i = 0; i < filter_count; i++) {
        const rml_filter *filter = cf_map_get_ptr(backend->filters, filters[i]);
        assert(filter);
        apush(backend->current.filters, *filter);
    }
    apush(backend->current.commands, command);
}

static void pop_layer(void *user)
{
    rml_backend *backend = user;
    assert(backend->depth > 0);
    apush(backend->current.commands, record(backend, COMMAND_POP_LAYER));
    backend->depth--;
}

/* The top layer's scissored pixels, as a texture RmlUi caches, such as a
 * box-shadow. Its canvas fills in when the commands replay. */
static uintptr_t save_layer_as_texture(void *user)
{
    rml_backend *backend = user;
    CF_Rect region = intersect_rect(current_scissor(backend), window_rect(backend));
    if (!region.w || !region.h) {
        return 0;
    }
    CF_Canvas canvas = rml_gpu_canvas(region.w, region.h, "RmlUi layer texture", false);
    held_texture held = { .texture = cf_canvas_get_target(canvas),
                          .external = -1,
                          .canvas = canvas };
    draw_command command = record(backend, COMMAND_SAVE_TEXTURE);
    command.scissor = region;
    command.texture = hold_texture(backend, held);
    command.canvas = canvas;
    apush(backend->current.commands, command);
    return command.texture;
}

static uintptr_t add_filter(rml_backend *backend, rml_filter filter)
{
    uintptr_t handle = backend->next_filter++;
    cf_map_set(backend->filters, handle, filter);
    return handle;
}

static uintptr_t save_layer_as_mask_image(void *user)
{
    rml_backend *backend = user;
    draw_command command = record(backend, COMMAND_SAVE_MASK);
    command.scissor = intersect_rect(command.scissor, window_rect(backend));
    apush(backend->current.commands, command);
    return add_filter(backend, (rml_filter){ .kind = RML_FILTER_MASK_IMAGE });
}

/* Zero is the documented unsupported-filter handle; RmlUi then skips it. */
static uintptr_t compile_filter(void *user, const char *name, const rml_effect_parameters *parameters)
{
    rml_backend *backend = user;
    rml_filter filter;
    if (!rml_filter_compile(name, parameters, &filter)) {
        fprintf(stderr, "RmlUi: unsupported filter '%s'\n", name);
        return 0;
    }
    return add_filter(backend, filter);
}

/* Composites copied their filters, so a release takes effect at once. */
static void release_filter(void *user, uintptr_t filter)
{
    rml_backend *backend = user;
    backend->frame_dirty = true;
    cf_map_del(backend->filters, filter);
}

static uintptr_t compile_shader(void *user, const char *name, const rml_effect_parameters *parameters)
{
    rml_backend *backend = user;
    rml_gradient gradient;
    if (!rml_gradient_compile(name, parameters, &gradient)) {
        fprintf(stderr, "RmlUi: unsupported shader '%s'\n", name);
        return 0;
    }
    uintptr_t handle = backend->next_shader++;
    cf_map_set(backend->gradients, handle, gradient);
    return handle;
}

static void render_shader(void *user, uintptr_t shader, uintptr_t geometry, rml_vec2 translation, uintptr_t)
{
    rml_backend *backend = user;
    const rml_gradient *gradient = cf_map_get_ptr(backend->gradients, shader);
    assert(gradient);
    draw_command command = record(backend, COMMAND_GRADIENT);
    set_geometry(backend, &command, geometry, translation);
    command.first_arg = acount(backend->current.gradients);
    command.arg_count = 1;
    apush(backend->current.gradients, *gradient);
    apush(backend->current.commands, command);
}

static void release_shader(void *user, uintptr_t shader)
{
    rml_backend *backend = user;
    backend->frame_dirty = true;
    cf_map_del(backend->gradients, shader);
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
    .enable_clip_mask = enable_clip_mask,
    .render_to_clip_mask = render_to_clip_mask,
    .push_layer = push_layer,
    .composite_layers = composite_layers,
    .pop_layer = pop_layer,
    .save_layer_as_texture = save_layer_as_texture,
    .save_layer_as_mask_image = save_layer_as_mask_image,
    .compile_filter = compile_filter,
    .release_filter = release_filter,
    .compile_shader = compile_shader,
    .render_shader = render_shader,
    .release_shader = release_shader,
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

static ui_font *registered_fonts;
static size_t registered_font_bytes;
static bool fonts_ready;
bool rml_backend_install_font_budget(const ui_font *f, size_t *remaining, char *error, size_t size)
{
    assert(remaining);
    const char *failure = NULL;
    if (!fonts_ready || !ui_font_valid(f)) {
        failure = "Font engine unavailable or invalid font record";
    }
    if (!failure) {
        for (int i = 0; i < acount(registered_fonts); i++) {
            const ui_font *old = &registered_fonts[i];
            if (old->style == f->style && old->weight == f->weight &&
                !strcmp(old->family, f->family)) {
                if (old->length == f->length && old->fallback == f->fallback &&
                    !memcmp(old->data, f->data, f->length)) {
                    return true;
                }
                failure = "Resident font face has different bytes or fallback "
                          "policy";
                break;
            }
        }
    }
    if (!failure && (acount(registered_fonts) >= 128 ||
                     f->length > 64 * 1024 * 1024 - registered_font_bytes)) {
        failure = "Resident font lifetime budget exceeded";
    }
    if (failure) {
        if (size) {
            snprintf(error, size, "%s", failure);
        }
        return false;
    }
    int cap = acap(registered_fonts), next = cap;
    if (acount(registered_fonts) == cap) {
        next = cap ? cap * 2 : 16;
    }
    size_t charge = f->length + strlen(f->key) + 1 + strlen(f->family) + 1 +
                    (size_t)(next - cap) * sizeof(ui_font) +
                    (cap ? 0 : sizeof(dyna_hdr));
    if (*remaining < charge) {
        if (size) {
            snprintf(error, size, "Font retained capacity budget exceeded");
        }
        return false;
    }
    ui_font owned = *f;
    owned.key = xstrdup(f->key);
    owned.family = xstrdup(f->family);
    unsigned char *bytes = xmalloc(f->length);
    memcpy(bytes, f->data, f->length);
    owned.data = bytes;
    if (!rml_load_font_face(
            owned.data,
            owned.length,
            owned.family,
            owned.style,
            owned.weight,
            owned.fallback
        )) {
        ui_font_free(&owned);
        if (size) {
            snprintf(error, size, "RmlUi rejected font face");
        }
        return false;
    }
    *remaining -= charge;
    registered_font_bytes += owned.length;
    apush(registered_fonts, owned);
    return true;
}
bool rml_backend_install_font(const ui_font *f, char *error, size_t size)
{
    size_t remaining = SIZE_MAX;
    return rml_backend_install_font_budget(f, &remaining, error, size);
}
static void shut_down();

/* The first backend initializes RmlUi and its fonts. */
static bool initialize_rml()
{
    rml_set_system_interface(&system_interface);
    if (!rml_initialize()) {
        return false;
    }
    fonts_ready = true;
    for (int i = 0; i < ui_builtin_font_count(); i++) {
        ui_font face = ui_builtin_font_at(i);
        if (!rml_backend_install_font(&face, NULL, 0)) {
            shut_down();
            return false;
        }
    }
    /* Workstream N: the native elements every document may use. */
    motion_register();
    /* Workstream F2: the native controls. */
    controls_register();
    /* Workstream F4: drag and drop in every context. */
    ui_drag_register();
    /* :focus-within in every context (workstream P3). */
    ui_focus_within_register();
    /* Workstream P5: autofocus on elements built after their document shows. */
    autofocus_register();
    return true;
}

bool rml_backend_prepare_fonts()
{
    return fonts_ready || initialize_rml();
}
void rml_backend_release_prepared_fonts()
{
    assert(backend_count == 0);
    if (fonts_ready) {
        shut_down();
    }
}

static void free_backend(rml_backend *backend)
{
    destroy_gpu(backend);
    capture *captures[] = { &backend->current, &backend->previous };
    for (int i = 0; i < 2; i++) {
        afree(captures[i]->commands);
        afree(captures[i]->filters);
        afree(captures[i]->gradients);
    }
    afree(backend->layers);
    for (int i = 0; i < acount(backend->externals); i++) {
        free(backend->externals[i].name);
    }
    afree(backend->externals);
    cf_map_free(backend->geometries);
    cf_map_free(backend->textures);
    cf_map_free(backend->filters);
    cf_map_free(backend->gradients);
    rml_renderer_destroy(backend->renderer);
    free(backend);
}

/* RmlUi starts over when its last context goes, as opening another project
   does, and so does its clock: a clock left at the last session's time would
   hold every transition of the next session until it caught up. */
static void shut_down()
{
    rml_shutdown();
    fonts_ready = false;
    for (int i = 0; i < acount(registered_fonts); i++) {
        ui_font_free(&registered_fonts[i]);
    }
    afree(registered_fonts);
    registered_font_bytes = 0;
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
    backend->next_filter = 1;
    backend->next_shader = 1;
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
    snprintf(backend->context_name, sizeof backend->context_name, "Chibi Editor %d", ++context_serial);

    /* Later backends add a context with their own renderer and canvas, such
       as the graph editor's. */
    if (backend_count == 0 && !rml_backend_prepare_fonts()) {
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
    iframe_profile_begin(IFRAME_PROFILE_PRE);
    iframes_settle(backend->context);
    iframe_profile_end();
    iframe_profile_begin(IFRAME_PROFILE_CONTEXT);
    if (iframe_profile_current() == IFRAME_PROFILE_CONTEXT) {
        double seconds[RML_UPDATE_PARTS];
        rml_context_update_profiled(backend->context, seconds);
        iframe_profile_context_add(seconds);
    } else {
        rml_context_update(backend->context);
    }
    iframe_profile_end();
    iframe_profile_begin(IFRAME_PROFILE_POST);
    iframes_settle(backend->context);
    iframe_profile_end();
    presence_settle(backend->context);
    popovers_settle(backend->context);
    autofocus_settle(backend->context);
    driven = NULL;
}

/* ---------- Replay ---------- */

static bool same_fields(const draw_command *a, const draw_command *b)
{
    return a->kind == b->kind && a->geometry == b->geometry &&
           a->texture == b->texture && a->gpu_texture.id == b->gpu_texture.id &&
           a->external == b->external && a->revision == b->revision &&
           a->translation.x == b->translation.x &&
           a->translation.y == b->translation.y && a->layer == b->layer &&
           a->clip_value == b->clip_value && a->operation == b->operation &&
           a->source == b->source && a->canvas.id == b->canvas.id &&
           memcmp(&a->transform, &b->transform, sizeof a->transform) == 0 &&
           memcmp(&a->scissor, &b->scissor, sizeof a->scissor) == 0;
}

static bool same_args(const capture *a, const draw_command *x, const capture *b, const draw_command *y)
{
    if (x->arg_count != y->arg_count || !x->arg_count) {
        return x->arg_count == y->arg_count;
    }
    if (x->kind == COMMAND_GRADIENT) {
        return memcmp(&a->gradients[x->first_arg], &b->gradients[y->first_arg], sizeof(rml_gradient)) ==
               0;
    }
    return memcmp(
               &a->filters[x->first_arg],
               &b->filters[y->first_arg],
               (size_t)x->arg_count * sizeof(rml_filter)
           ) == 0;
}

/* Equal commands paint equal pixels, so they also compare the clip commands
 * whose mask they test. */
static bool same_command(const capture *a, int i, const capture *b, int j)
{
    const draw_command *x = &a->commands[i], *y = &b->commands[j];
    if (!same_fields(x, y) || !same_args(a, x, b, y) ||
        (x->clip_first < 0) != (y->clip_first < 0) ||
        x->clip_last - x->clip_first != y->clip_last - y->clip_first) {
        return false;
    }
    for (int k = 0; x->clip_first >= 0 && k <= x->clip_last - x->clip_first; k++) {
        if (!same_fields(&a->commands[x->clip_first + k], &b->commands[y->clip_first + k])) {
            return false;
        }
    }
    return true;
}

static bool same_commands(const capture *a, int first_a, const capture *b, int first_b, int count)
{
    for (int i = 0; i < count; i++) {
        if (!same_command(a, first_a + i, b, first_b + i)) {
            return false;
        }
    }
    return true;
}

static bool uses_geometry(command_kind kind)
{
    return kind == COMMAND_DRAW || kind == COMMAND_GRADIENT || kind == COMMAND_CLIP;
}

/* Only draws and their clips: a partial update may repaint any subset. */
static bool plain_run(const draw_command *commands, int first, int count)
{
    for (int i = first; i < first + count; i++) {
        if (!uses_geometry(commands[i].kind)) {
            return false;
        }
    }
    return true;
}

/* The clip commands a layer's stencil holds. */
typedef struct stencil_state
{
    int first, last;
} stencil_state;

/* A replay of commands onto a base surface, as GL3 renders them directly:
 * the open layers, the clip mask each one's stencil holds, and whether draws
 * wait for a flush into the top layer. Draws outside `clip` are skipped. */
typedef struct replay
{
    rml_backend *backend;
    const capture *capture;
    rml_surface base;
    CF_Rect clip;
    int depth;
    bool queued, clear;
    stencil_state *stencils;
    bool viewing;
    CF_Rect view;
} replay;

static replay begin_replay(rml_backend *backend, const capture *capture, rml_surface base, bool clear)
{
    cf_draw_push_layer(RML_DRAW_LAYER);
    return (replay){ .backend = backend,
                     .capture = capture,
                     .base = base,
                     .clip = base.bounds,
                     .clear = clear };
}

/* Pixel space for the canvas covering bounds. Queued draws keep the view
 * they were queued under, so it changes only with the target. */
static void view(replay *r, CF_Rect bounds)
{
    if (r->viewing && memcmp(&r->view, &bounds, sizeof bounds) == 0) {
        return;
    }
    if (r->viewing) {
        rml_gpu_pop_view();
    }
    rml_gpu_push_view(bounds);
    r->viewing = true;
    r->view = bounds;
}

/* Layers above the base are window-sized canvases, kept for later frames. */
static rml_surface surface(replay *r, int depth)
{
    rml_backend *backend = r->backend;
    if (depth == 0) {
        return r->base;
    }
    while (acount(backend->stack) < depth) {
        apush(
            backend->stack,
            rml_gpu_canvas(backend->physical_width, backend->physical_height, "RmlUi layer", true)
        );
    }
    return (rml_surface){ backend->stack[depth - 1], window_rect(backend) };
}

/* Where a layer may be painted: a partial update repaints only part of the base. */
static CF_Rect paintable(replay *r, int depth)
{
    rml_surface target = surface(r, depth);
    return depth ? target.bounds : intersect_rect(target.bounds, r->clip);
}

static stencil_state *stencil_of(replay *r, int depth)
{
    while (acount(r->stencils) <= depth) {
        apush(r->stencils, ((stencil_state){ -1, -1 }));
    }
    return &r->stencils[depth];
}

static void flush_to(replay *r, int depth)
{
    rml_gpu_flush(&r->backend->gpu, surface(r, depth).canvas, depth == 0 && r->clear);
    r->clear &= depth != 0;
    r->queued = false;
}

static void flush(replay *r)
{
    if (r->queued || (r->depth == 0 && r->clear)) {
        flush_to(r, r->depth);
    }
}

/* A scissor in the coordinates of the canvas covering bounds. */
static void push_scissor(CF_Rect scissor, CF_Rect bounds)
{
    cf_draw_push_scissor((
        CF_Rect
    ){ scissor.x - bounds.x, scissor.y - bounds.y, scissor.w, scissor.h });
}

static void queue_mesh(const draw_command *command)
{
    cf_draw3d_push();
    cf_draw3d_transform(command->transform);
    cf_draw3d_translate(cf_v3(command->translation.x, command->translation.y, 0));
    cf_draw3d_mesh(command->mesh);
    cf_draw3d_pop();
}

/* Stencil writes without color, as GL3 renders a clip mask: a set clears
 * the mask first, which a set-inverse fills; an intersect counts up. */
static void queue_clip(replay *r, const draw_command *clip, CF_Rect bounds)
{
    rml_gpu *gpu = &r->backend->gpu;
    CF_RenderState state = rml_gpu_blend_state(true);
    state.blend.write_R_enabled = state.blend.write_G_enabled = false;
    state.blend.write_B_enabled = state.blend.write_A_enabled = false;
    state.stencil.enabled = true;
    state.stencil.read_mask = state.stencil.write_mask = 0xff;
    state.stencil.front = (CF_StencilFunction){ CF_COMPARE_FUNCTION_ALWAYS,
                                                CF_STENCIL_OP_KEEP,
                                                CF_STENCIL_OP_KEEP,
                                                CF_STENCIL_OP_REPLACE };
    if (clip->operation == RML_CLIP_MASK_INTERSECT) {
        state.stencil.front.pass_op = CF_STENCIL_OP_INCREMENT_CLAMP;
    } else {
        bool inverse = clip->operation == RML_CLIP_MASK_SET_INVERSE;
        state.stencil.reference = inverse;
        state.stencil.back = state.stencil.front;
        cf_draw3d_push_render_state(state);
        rml_gpu_quad(gpu, bounds, gpu->white, cf_v4(0, 0, 1, 1), cf_color_white());
        cf_draw3d_pop_render_state();
        state.stencil.reference = !inverse;
    }
    state.stencil.back = state.stencil.front;
    if (!clip->has_geometry) {
        return;
    }
    cf_draw3d_push_render_state(state);
    cf_draw3d_push_shader(gpu->ui);
    cf_draw3d_set_texture("u_texture", gpu->white);
    queue_mesh(clip);
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
}

/* Renders the command's clip mask into the stencil of the layer it paints,
 * unless that stencil holds it already. Masks cover the whole canvas, so
 * every later scissor finds them. */
static void apply_clip(replay *r, int depth, const draw_command *command)
{
    stencil_state held = *stencil_of(r, depth);
    if (command->clip_first < 0 ||
        (held.first == command->clip_first && held.last == command->clip_last)) {
        return;
    }
    rml_surface target = surface(r, depth);
    view(r, target.bounds);
    push_scissor(target.bounds, target.bounds);
    for (int i = command->clip_first; i <= command->clip_last; i++) {
        const draw_command *clip = &r->capture->commands[i];
        if (clip->kind == COMMAND_CLIP) {
            queue_clip(r, clip, target.bounds);
        }
    }
    cf_draw_pop_scissor();
    *stencil_of(r, depth) = (stencil_state){ command->clip_first, command->clip_last };
}

/* Draws test the stencil against their mask's value. */
static CF_RenderState clip_test(CF_RenderState state, const draw_command *command)
{
    if (command->clip_first < 0) {
        return state;
    }
    state.stencil.enabled = true;
    state.stencil.read_mask = 0xff;
    state.stencil.write_mask = 0;
    state.stencil.reference = (uint8_t)command->clip_value;
    state.stencil.front = (CF_StencilFunction){ CF_COMPARE_FUNCTION_EQUAL,
                                                CF_STENCIL_OP_KEEP,
                                                CF_STENCIL_OP_KEEP,
                                                CF_STENCIL_OP_KEEP };
    state.stencil.back = state.stencil.front;
    return state;
}

/* Whether the draw queued: it may lie outside the target or the clip. */
static bool queue_draw(replay *r, const draw_command *command)
{
    assert(command->layer == r->depth);
    rml_backend *backend = r->backend;
    rml_surface target = surface(r, r->depth);
    CF_Rect scissor = intersect_rect(command->scissor, paintable(r, r->depth));
    if (!command->has_geometry || !scissor.w || !scissor.h ||
        (command->external >= 0 && !command->gpu_texture.id)) {
        return false;
    }
    apply_clip(r, r->depth, command);
    view(r, target.bounds);
    push_scissor(scissor, target.bounds);
    cf_draw3d_push_render_state(clip_test(rml_gpu_blend_state(true), command));
    if (command->kind == COMMAND_GRADIENT) {
        rml_gpu_push_gradient(&backend->gpu, &r->capture->gradients[command->first_arg]);
    } else {
        cf_draw3d_push_shader(backend->gpu.ui);
        cf_draw3d_set_texture("u_texture", command->gpu_texture);
    }
    queue_mesh(command);
    cf_draw3d_pop_shader();
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
    r->queued = true;
    return true;
}

static void replay_push_layer(replay *r, const draw_command *command)
{
    flush(r);
    r->depth++;
    assert(command->layer == r->depth);
    (void)command;
    /* A cleared canvas holds no mask yet. */
    rml_gpu_flush(&r->backend->gpu, surface(r, r->depth).canvas, true);
    *stencil_of(r, r->depth) = (stencil_state){ -1, -1 };
}

/* GL3's CompositeLayers: filter the source's scissored pixels, then draw
 * them over the destination, tested against the clip mask. */
static void replay_composite(replay *r, const draw_command *command)
{
    rml_backend *backend = r->backend;
    flush(r);
    rml_surface source = surface(r, command->source);
    rml_surface destination = surface(r, command->layer);
    CF_Rect region = intersect_rect(command->scissor, window_rect(backend));
    CF_Rect scissor = intersect_rect(region, paintable(r, command->layer));
    if (!scissor.w || !scissor.h) {
        return;
    }
    CF_Texture texture = cf_canvas_get_target(source.canvas);
    CF_V4 uv = rml_surface_uv(source, region);
    if (command->arg_count) {
        const rml_filter *filters = &r->capture->filters[command->first_arg];
        CF_Canvas filtered = rml_gpu_filter(
            &backend->gpu,
            source,
            region,
            filters,
            command->arg_count
        );
        texture = cf_canvas_get_target(filtered);
        uv = rml_surface_uv((rml_surface){ filtered, window_rect(backend) }, region);
    }
    apply_clip(r, command->layer, command);
    bool blend = command->operation == RML_BLEND_MODE_BLEND;
    view(r, destination.bounds);
    push_scissor(scissor, destination.bounds);
    cf_draw3d_push_render_state(clip_test(rml_gpu_blend_state(blend), command));
    rml_gpu_quad(&backend->gpu, region, texture, uv, cf_color_white());
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
    flush_to(r, command->layer);
}

static void replay_save_texture(replay *r, const draw_command *command)
{
    flush(r);
    CF_Rect region = command->scissor, local = { 0, 0, region.w, region.h };
    rml_surface source = surface(r, r->depth);
    view(r, local);
    cf_draw_push_scissor(local);
    cf_draw3d_push_render_state(rml_gpu_blend_state(false));
    rml_gpu_quad(
        &r->backend->gpu,
        local,
        cf_canvas_get_target(source.canvas),
        rml_surface_uv(source, region),
        cf_color_white()
    );
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
    rml_gpu_flush(&r->backend->gpu, command->canvas, true);
}

static void replay_commands(replay *r, int first, int count)
{
    for (int i = first; i < first + count; i++) {
        const draw_command *command = &r->capture->commands[i];
        switch (command->kind) {
            case COMMAND_DRAW:
            case COMMAND_GRADIENT:
                r->backend->stats.static_commands_replayed += queue_draw(r, command);
                break;
            /* Masks render with the draws that test them. */
            case COMMAND_CLIP: break;
            case COMMAND_PUSH_LAYER: replay_push_layer(r, command); break;
            case COMMAND_COMPOSITE: replay_composite(r, command); break;
            case COMMAND_POP_LAYER:
                flush(r);
                r->depth--;
                break;
            case COMMAND_SAVE_TEXTURE: replay_save_texture(r, command); break;
            case COMMAND_SAVE_MASK:
                flush(r);
                rml_gpu_save_mask(
                    &r->backend->gpu,
                    surface(r, r->depth),
                    command->scissor
                );
                break;
        }
    }
}

/* Bindings outlive the draw, and a canvas may be destroyed before they rebind. */
static void end_replay(replay *r)
{
    assert(r->depth == 0);
    flush(r);
    if (r->viewing) {
        rml_gpu_pop_view();
    }
    cf_draw3d_set_texture("u_texture", (CF_Texture){ 0 });
    cf_draw_pop_layer();
    afree(r->stencils);
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
        bounds = union_rect(bounds, command_bounds(backend, &backend->current.commands[i]));
    }
    return bounds;
}

/* Keep the previous run when its structure and crop are unchanged. Old
 * commands contribute copied bounds only; their resources may already retire
 * during Rml's capture. Merging all changes into one rectangle avoids drawing
 * translucent commands twice where dirty regions overlap. */
static bool update_static_region(rml_backend *backend, static_layer *layer, int first, int count, CF_Rect bounds)
{
    const capture *current = &backend->current, *previous = &backend->previous;
    if (backend->output_invalid || !layer->canvas.id || layer->first != first ||
        layer->count != count ||
        acount(current->commands) != acount(previous->commands) ||
        memcmp(&layer->bounds, &bounds, sizeof bounds) != 0 ||
        !plain_run(current->commands, first, count)) {
        return false;
    }
    /* A changed live-surface partition can move commands between paint runs. */
    for (int i = 0; i < acount(current->commands); i++) {
        if (current->commands[i].external != previous->commands[i].external) {
            return false;
        }
    }
    CF_Rect dirty = { 0 };
    for (int i = first; i < first + count; i++) {
        const draw_command *command = &current->commands[i];
        const draw_command *old = &previous->commands[i];
        if (same_command(current, i, previous, i)) {
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
    /* A replacing quad clears only the dirty region. Preserve the rest. */
    replay r = begin_replay(backend, current, (rml_surface){ layer->canvas, bounds }, false);
    r.clip = dirty;
    view(&r, bounds);
    push_scissor(dirty, bounds);
    cf_draw3d_push_render_state(rml_gpu_blend_state(false));
    rml_gpu_quad(&backend->gpu, dirty, backend->gpu.white, cf_v4(0, 0, 1, 1), (CF_Color){ 0 });
    cf_draw3d_pop_render_state();
    cf_draw_pop_scissor();
    r.queued = true;
    for (int i = first; i < first + count; i++) {
        const draw_command *command = &current->commands[i];
        if (command->kind != COMMAND_CLIP &&
            intersect_rect(command_bounds(backend, command), dirty).w) {
            backend->stats.static_commands_replayed += queue_draw(&r, command);
        }
    }
    end_replay(&r);
    backend->stats.static_layer_updates++;
    backend->stats.static_partial_updates++;
    return true;
}

static void update_static_layer(rml_backend *backend, int index, int first, int count)
{
    if (index == acount(backend->layers)) {
        apush(backend->layers, ((static_layer){ 0 }));
    }
    static_layer *layer = &backend->layers[index];
    bool
        same = layer->count == count &&
               same_commands(&backend->current, first, &backend->previous, layer->first, count);
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
    if ((!bounds.w || !bounds.h) && plain_run(backend->current.commands, first, count)) {
        release_static_layer(backend, layer);
        layer->bounds = bounds;
        return;
    }
    /* A run that only fills layer textures still replays, onto a pixel. */
    if (!bounds.w || !bounds.h) {
        bounds = (CF_Rect){ 0, 0, 1, 1 };
    }
    if (layer->canvas.id &&
        (bounds.w != layer->bounds.w || bounds.h != layer->bounds.h)) {
        release_static_layer(backend, layer);
    }
    layer->bounds = bounds;
    if (!layer->canvas.id) {
        layer->canvas = rml_gpu_canvas(bounds.w, bounds.h, "RmlUi static layer", true);
        backend->stats.cached_texture_bytes += (uint64_t)bounds.w * bounds.h * 4;
    }
    /* UI commands keep global coordinates. Only the target projection and
     * integer scissors move into the cropped canvas's coordinate space. */
    replay r = begin_replay(backend, &backend->current, (rml_surface){ layer->canvas, bounds }, true);
    replay_commands(&r, first, count);
    end_replay(&r);
    backend->stats.static_layer_updates++;
}

/* Only host surfaces on the base split paint runs: one inside a layer
 * renders with that layer's commands. */
static bool splits_runs(const draw_command *command)
{
    return command->external >= 0 && command->layer == 0;
}

static void update_static_layers(rml_backend *backend)
{
    const draw_command *commands = backend->current.commands;
    int first = 0, index = 0;
    for (int i = 0; i <= acount(commands); i++) {
        if (i < acount(commands) && !splits_runs(&commands[i])) {
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

/* All static refresh passes finish before queuing the composite, because
 * every pass consumes the same dedicated Cute layer. */
static void compose_layers(rml_backend *backend, const capture *capture)
{
    CF_Rect window = window_rect(backend);
    replay r = begin_replay(backend, capture, (rml_surface){ backend->premultiplied_canvas, window }, true);
    for (int i = 0; i < acount(backend->layers); i++) {
        static_layer *layer = &backend->layers[i];
        if (layer->canvas.id) {
            view(&r, window);
            cf_draw_push_scissor(window);
            cf_draw3d_push_render_state(rml_gpu_blend_state(true));
            rml_gpu_quad(
                &backend->gpu,
                layer->bounds,
                cf_canvas_get_target(layer->canvas),
                cf_v4(0, 0, 1, 1),
                cf_color_white()
            );
            cf_draw3d_pop_render_state();
            cf_draw_pop_scissor();
            r.queued = true;
        }
        int next = layer->first + layer->count;
        if (next < acount(capture->commands)) {
            queue_draw(&r, &capture->commands[next]);
            backend->stats.texture_composites++;
        }
    }
    end_replay(&r);
}

/* Static runs pay off when every host surface paints the base. */
static bool has_static_runs(const capture *capture)
{
    bool external = false;
    for (int i = 0; i < acount(capture->commands); i++) {
        const draw_command *command = &capture->commands[i];
        if (command->external >= 0 && command->layer > 0) {
            return false;
        }
        external |= command->external >= 0;
    }
    return external;
}

static void draw_output(rml_backend *backend, const capture *capture, bool captured)
{
    CF_Rect window = window_rect(backend);
    if (has_static_runs(capture)) {
        if (captured) {
            update_static_layers(backend);
        }
        compose_layers(backend, capture);
    } else {
        release_static_layers(backend);
        replay r = begin_replay(backend, capture, (rml_surface){ backend->premultiplied_canvas, window }, true);
        replay_commands(&r, 0, acount(capture->commands));
        end_replay(&r);
        backend->stats.static_layer_updates += acount(capture->commands) > 0;
    }
    rml_gpu_resolve(&backend->gpu, backend->premultiplied_canvas, backend->straight_canvas);
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
    capture *current = &backend->current;
    asetlen(current->commands, 0);
    asetlen(current->filters, 0);
    asetlen(current->gradients, 0);
    backend->scissor_enabled = false;
    backend->transform = cf_m4_identity();
    backend->depth = 0;
    backend->clip_enabled = false;
    backend->clip_first = backend->clip_last = -1;
    backend->clip_value = 0;
    backend->stats.render_captures++;
    if (backend->retained_started) {
        CHIBI_TRACE_ZONE_BEGIN(capture_zone, "Rml retained capture");
        rml_context_render(backend->context);
        CHIBI_TRACE_ZONE_END(capture_zone);
    } else {
        rml_context_render(backend->context);
    }
    assert(backend->depth == 0);

    int count = acount(current->commands);
    bool changed = backend->output_invalid ||
                   count != acount(backend->previous.commands) ||
                   !same_commands(current, 0, &backend->previous, 0, count);
    if (changed) {
        if (backend->retained_started) {
            CHIBI_TRACE_ZONE_BEGIN(output_zone, "Rml retained output");
            draw_output(backend, current, true);
            CHIBI_TRACE_ZONE_END(output_zone);
        } else {
            draw_output(backend, current, true);
        }
    }
    destroy_released(backend);
    capture old = backend->previous;
    backend->previous = backend->current;
    backend->current = old;
    backend->captured = true;
    backend->frame_dirty = false;
    /* A custom hook may release a resource after queuing its draw. That draw
     * completed above, but its command must not be replayed next frame. */
    for (int i = 0; i < count; i++) {
        const draw_command *command = &backend->previous.commands[i];
        if ((uses_geometry(command->kind) &&
             !cf_map_get_ptr(backend->geometries, command->geometry)) ||
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
    for (int i = 0; i < acount(backend->previous.commands); i++) {
        draw_command *command = &backend->previous.commands[i];
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
        draw_output(backend, &backend->previous, false);
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
    rml_backend_stats stats = backend->stats;
    stats.render_passes = backend->gpu.passes;
    return stats;
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

/* A right press sends contextmenu to the element under the pointer after
   its mousedown, as browsers do, so markup opens menus with
   data-event-contextmenu. */
static bool pressed(rml_context *context, int button, int x, int y, unsigned modifiers)
{
    bool passed = rml_context_process_mouse_button_down(context, button, modifiers);
    rml_element *target = rml_context_hover_element(context);
    if (button == 1 && target) {
        rml_element_dispatch_event(
            target,
            "contextmenu",
            &(rml_event){ .mouse_x = (float)x,
                          .mouse_y = (float)y,
                          .button = button,
                          .modifiers = modifiers }
        );
    }
    return passed;
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
            return !pressed(context, input->button, x, y, modifiers);
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
    backend->externals[i].still = false;
}

void rml_backend_set_image(rml_backend *backend, const char *name, uint64_t texture)
{
    assert(backend && name);
    int i = external_index(backend, name);
    external_texture *external = &backend->externals[i];
    external->texture = (CF_Texture){ texture };
    external->revision++;
    external->still = true;
    /* Its draws are in the captured paint: capture again. */
    backend->frame_dirty = true;
}

void rml_backend_set_texture_revision(rml_backend *backend, const char *name, uint64_t texture, uint64_t revision)
{
    assert(backend && name);
    int i = external_index(backend, name);
    backend->externals[i].texture = (CF_Texture){ texture };
    backend->externals[i].revision = revision;
    backend->externals[i].still = false;
}

const char *rml_backend_cursor(const rml_backend *backend)
{
    return backend->cursor;
}

void rml_backend_present(rml_backend *backend)
{
    rml_backend_present_region(backend, (const float[4]){ 0, 0, 1, 1 });
}

void rml_backend_present_region(rml_backend *backend, const float region[4])
{
    assert(backend);
    if (!backend->gpu_ready) {
        return;
    }
    float w = (float)backend->logical_width;
    float h = (float)backend->logical_height;
    float center_x = ((region[0] + region[2]) * 0.5f - 0.5f) * w;
    float center_y = (0.5f - (region[1] + region[3]) * 0.5f) * h;
    cf_draw_push();
    cf_draw_projection(
        cf_ortho_2d(center_x, center_y, (region[2] - region[0]) * w, (region[3] - region[1]) * h)
    );
    cf_draw_canvas(backend->premultiplied_canvas, cf_v2(0, 0), cf_v2(w, h));
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
