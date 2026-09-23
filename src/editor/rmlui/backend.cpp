#include "backend.h"

#include "platform/cute.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/StyleTypes.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Vertex.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
    extern const unsigned char editor_ui_font[];
    extern const unsigned char editor_ui_bold_font[];
    extern const size_t editor_ui_font_size;
    extern const size_t editor_ui_bold_font_size;
}

namespace {

constexpr char k_vertex_shader
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

constexpr char k_fragment_shader
    [] = "layout (location = 0) in vec4 v_color;\n"
         "layout (location = 1) in vec2 v_uv;\n"
         "layout (location = 0) out vec4 result;\n"
         "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"
         "void main() { result = texture(u_texture, v_uv) * v_color; }\n";

constexpr char k_resolve_vertex_shader
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

constexpr char k_resolve_fragment_shader
    [] = "layout (location = 0) in vec2 v_uv;\n"
         "layout (location = 0) out vec4 result;\n"
         "layout (set = 2, binding = 0) uniform sampler2D u_texture;\n"
         "void main() {\n"
         "    vec4 c = texture(u_texture, v_uv);\n"
         "    result = vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a);\n"
         "}\n";

/* Kept separate from application layers so the Rml passes can flush without
 * consuming queued scene/UI geometry. */
constexpr int k_rml_draw_layer = INT_MAX / 2;

struct ResolveVertex
{
    float x;
    float y;
    float u;
    float v;
};

CF_Texture texture_from_pixels(int width, int height, const void *pixels)
{
    CF_TextureParams params = cf_texture_defaults(width, height);
    params.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM;
    params.usage |= CF_TEXTURE_USAGE_SAMPLER_BIT;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
    params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    CF_Texture texture = cf_make_texture(params);
    cf_texture_update(texture, const_cast<void *>(pixels), width * height * 4);
    return texture;
}

CF_Canvas create_canvas(int width, int height, const char *name)
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

CF_RenderState premultiplied_blend_state()
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

CF_RenderState replace_state()
{
    CF_RenderState state = cf_render_state_defaults();
    state.cull_mode = CF_CULL_MODE_NONE;
    state.depth_compare = CF_COMPARE_FUNCTION_ALWAYS;
    state.depth_write_enabled = false;
    state.blend.enabled = false;
    return state;
}

struct Geometry
{
    CF_Mesh mesh = {};
};

class BackendSystem final : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override { return elapsed_seconds; }

    void advance(double delta_seconds) { elapsed_seconds += delta_seconds; }

    void SetClipboardText(const Rml::String &text) override
    {
        cf_clipboard_set(text.c_str());
    }

    void GetClipboardText(Rml::String &text) override
    {
        char *clipboard = cf_clipboard_get();
        if (!clipboard) {
            text.clear();
            return;
        }
        text = clipboard;
        cf_free(clipboard);
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        const char *prefix = type == Rml::Log::LT_ERROR   ? "error"
                           : type == Rml::Log::LT_WARNING ? "warning"
                                                          : "info";
        std::fprintf(stderr, "RmlUi %s: %s\n", prefix, message.c_str());
        return true;
    }

private:
    double elapsed_seconds = 0.0;
};

class BackendRenderer final : public Rml::RenderInterface
{
public:
    explicit BackendRenderer(editor_rml_backend *owner)
      : backend(owner)
    {
        transform = cf_m4_identity();
    }

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices
    ) override;
    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture
    ) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override;
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i source_dimensions
    ) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void SetTransform(const Rml::Matrix4f *transform) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    editor_rml_backend *backend;
    bool scissor_enabled = false;
    CF_Rect scissor = {};
    CF_M4x4 transform = {};
};

} // namespace

struct editor_rml_backend
{
    BackendSystem system;
    BackendRenderer renderer{ this };
    Rml::Context *context = nullptr;
    std::string context_name;
    int logical_width = 1;
    int logical_height = 1;
    float scale = 1.0f;
    int physical_width = 1;
    int physical_height = 1;
    bool initialized = false;
    bool gpu_ready = false;
    uint64_t next_geometry = 1;
    uint64_t next_texture = 1;
    std::unordered_map<uintptr_t, Geometry> geometries;
    std::unordered_map<uintptr_t, CF_Texture> textures;
    CF_Texture white_texture = {};
    CF_Shader ui_shader = {};
    CF_Shader resolve_shader = {};
    CF_Mesh resolve_mesh = {};
    CF_Canvas premultiplied_canvas = {};
    CF_Canvas straight_canvas = {};
};

namespace {

void set_dimensions(editor_rml_backend *backend)
{
    backend->logical_width = std::max(backend->logical_width, 1);
    backend->logical_height = std::max(backend->logical_height, 1);
    backend->scale = std::max(backend->scale, 0.01f);
    backend->physical_width = std::max(
        1,
        (int)std::lround(backend->logical_width * backend->scale)
    );
    backend->physical_height = std::max(
        1,
        (int)std::lround(backend->logical_height * backend->scale)
    );
    if (backend->context) {
        backend->context->SetDimensions({ backend->physical_width, backend->physical_height });
        backend->context->SetDensityIndependentPixelRatio(backend->scale);
    }
}

void destroy_gpu(editor_rml_backend *backend)
{
    if (!backend->gpu_ready) {
        return;
    }
    for (auto &[handle, geometry] : backend->geometries) {
        (void)handle;
        cf_destroy_mesh(geometry.mesh);
    }
    backend->geometries.clear();
    for (auto &[handle, texture] : backend->textures) {
        (void)handle;
        cf_destroy_texture(texture);
    }
    backend->textures.clear();
    cf_destroy_texture(backend->white_texture);
    cf_destroy_mesh(backend->resolve_mesh);
    cf_destroy_shader(backend->ui_shader);
    cf_destroy_shader(backend->resolve_shader);
    cf_destroy_canvas(backend->premultiplied_canvas);
    cf_destroy_canvas(backend->straight_canvas);
    backend->white_texture = {};
    backend->resolve_mesh = {};
    backend->ui_shader = {};
    backend->resolve_shader = {};
    backend->premultiplied_canvas = {};
    backend->straight_canvas = {};
    backend->gpu_ready = false;
}

void create_canvas_resources(editor_rml_backend *backend)
{
    backend->premultiplied_canvas = create_canvas(
        backend->physical_width,
        backend->physical_height,
        "RmlUi premultiplied"
    );
    backend->straight_canvas = create_canvas(backend->physical_width, backend->physical_height, "RmlUi output");

    const CF_VertexAttribute resolve_attributes[2] = {
        { "in_position", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(ResolveVertex, x), false },
        { "in_uv", CF_VERTEX_FORMAT_FLOAT2, (int)offsetof(ResolveVertex, u), false },
    };
    backend->resolve_mesh = cf_make_mesh(
        sizeof(ResolveVertex) * 4,
        resolve_attributes,
        2,
        sizeof(ResolveVertex)
    );
    const ResolveVertex vertices[4] = {
        { 0, 0, 0, 0 },
        { (float)backend->physical_width, 0, 1, 0 },
        { (float)backend->physical_width, (float)backend->physical_height, 1, 1 },
        { 0, (float)backend->physical_height, 0, 1 },
    };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    cf_mesh_set_index_buffer(backend->resolve_mesh, sizeof(indices), 32);
    cf_mesh_update_vertex_data(backend->resolve_mesh, (void *)vertices, 4);
    cf_mesh_update_index_data(backend->resolve_mesh, (void *)indices, 6);
}

void ensure_gpu(editor_rml_backend *backend)
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

    backend->ui_shader = cf_make_shader_from_source(k_vertex_shader, k_fragment_shader);
    backend->resolve_shader = cf_make_shader_from_source(k_resolve_vertex_shader, k_resolve_fragment_shader);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    backend->white_texture = texture_from_pixels(1, 1, white);
    backend->gpu_ready = true;
    create_canvas_resources(backend);
}

unsigned modifiers_to_rml(unsigned modifiers)
{
    int result = 0;
    if (modifiers & EDITOR_RML_MOD_CONTROL) {
        result |= Rml::Input::KM_CTRL;
    }
    if (modifiers & EDITOR_RML_MOD_SHIFT) {
        result |= Rml::Input::KM_SHIFT;
    }
    if (modifiers & EDITOR_RML_MOD_ALT) {
        result |= Rml::Input::KM_ALT;
    }
    if (modifiers & EDITOR_RML_MOD_META) {
        result |= Rml::Input::KM_META;
    }
    if (modifiers & EDITOR_RML_MOD_CAPS_LOCK) {
        result |= Rml::Input::KM_CAPSLOCK;
    }
    if (modifiers & EDITOR_RML_MOD_NUM_LOCK) {
        result |= Rml::Input::KM_NUMLOCK;
    }
    if (modifiers & EDITOR_RML_MOD_SCROLL_LOCK) {
        result |= Rml::Input::KM_SCROLLLOCK;
    }
    return (unsigned)result;
}

} // namespace

Rml::CompiledGeometryHandle BackendRenderer::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices
)
{
    assert(vertices.size() <= (size_t)INT32_MAX && indices.size() <= (size_t)INT32_MAX);
    CF_VertexAttribute attributes[3] = {};
    attributes[0] = { "in_position",
                      CF_VERTEX_FORMAT_FLOAT2,
                      (int)offsetof(Rml::Vertex, position),
                      false };
    attributes[1] = { "in_color",
                      CF_VERTEX_FORMAT_UBYTE4_NORM,
                      (int)offsetof(Rml::Vertex, colour),
                      false };
    attributes[2] = { "in_uv",
                      CF_VERTEX_FORMAT_FLOAT2,
                      (int)offsetof(Rml::Vertex, tex_coord),
                      false };
    CF_Mesh mesh = cf_make_mesh(
        (int)(vertices.size() * sizeof(Rml::Vertex)),
        attributes,
        3,
        sizeof(Rml::Vertex)
    );
    cf_mesh_set_index_buffer(mesh, (int)(indices.size() * sizeof(int)), 32);
    cf_mesh_update_vertex_data(mesh, (void *)vertices.data(), (int)vertices.size());
    cf_mesh_update_index_data(mesh, (void *)indices.data(), (int)indices.size());
    const uintptr_t handle = (uintptr_t)backend->next_geometry++;
    backend->geometries.emplace(handle, Geometry{ mesh });
    return handle;
}

void BackendRenderer::RenderGeometry(
    Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle texture
)
{
    auto found = backend->geometries.find(geometry);
    assert(found != backend->geometries.end());
    auto image = backend->textures.find(texture);
    if (texture) {
        assert(image != backend->textures.end());
    }
    CF_Texture gpu_texture = texture ? image->second : backend->white_texture;
    cf_draw_push_layer(k_rml_draw_layer);
    cf_draw3d_set_texture("u_texture", gpu_texture);
    cf_draw3d_push();
    cf_draw3d_transform(transform);
    cf_draw3d_translate(cf_v3(translation.x, translation.y, 0));
    cf_draw3d_mesh(found->second.mesh);
    cf_draw3d_pop();
    cf_draw_pop_layer();
}

void BackendRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    auto found = backend->geometries.find(geometry);
    if (found == backend->geometries.end()) {
        return;
    }
    cf_destroy_mesh(found->second.mesh);
    backend->geometries.erase(found);
}

Rml::TextureHandle BackendRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
    assert(backend->gpu_ready);
    CF_Texture texture = texture_from_pixels(dimensions.x, dimensions.y, source.data());
    uintptr_t handle = (uintptr_t)backend->next_texture++;
    backend->textures.emplace(handle, texture);
    return handle;
}

Rml::TextureHandle BackendRenderer::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
{
    assert(backend->gpu_ready);
    CF_Image image = {};
    std::string lower = source;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    CF_Result result = {};
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") {
        result = cf_image_load_png(source.c_str(), &image);
    } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg") {
        result = cf_image_load_jpg(source.c_str(), &image);
    } else if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg") {
        result = cf_image_load_jpg(source.c_str(), &image);
    } else {
        std::fprintf(stderr, "RmlUi: unsupported texture format: %s\n", source.c_str());
        return 0;
    }
    if (cf_is_error(result)) {
        std::fprintf(
            stderr,
            "RmlUi: failed to load texture %s: %s\n",
            source.c_str(),
            result.details
        );
        return 0;
    }
    cf_image_premultiply(&image);
    dimensions = { image.w, image.h };
    CF_Texture texture = texture_from_pixels(image.w, image.h, image.pix);
    cf_image_free(&image);
    uintptr_t handle = (uintptr_t)backend->next_texture++;
    backend->textures.emplace(handle, texture);
    return handle;
}

void BackendRenderer::ReleaseTexture(Rml::TextureHandle handle)
{
    auto found = backend->textures.find(handle);
    if (found == backend->textures.end()) {
        return;
    }
    cf_destroy_texture(found->second);
    backend->textures.erase(found);
}

void BackendRenderer::SetTransform(const Rml::Matrix4f *new_transform)
{
    transform = cf_m4_identity();
    if (!new_transform) {
        return;
    }
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            transform.elements[column * 4 + row] = (*new_transform)[column][row];
        }
    }
}

void BackendRenderer::EnableScissorRegion(bool enable)
{
    if (enable == scissor_enabled) {
        return;
    }
    scissor_enabled = enable;
    if (enable) {
        cf_draw_push_scissor(scissor);
    } else {
        cf_draw_pop_scissor();
    }
}

void BackendRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    scissor = { region.Left(), region.Top(), region.Width(), region.Height() };
    if (!scissor_enabled) {
        return;
    }
    cf_draw_pop_scissor();
    cf_draw_push_scissor(scissor);
}

extern "C" editor_rml_backend *editor_rml_backend_create(int logical_width, int logical_height, float framebuffer_scale)
{
    static uint64_t context_serial = 0;
    auto *backend = new editor_rml_backend();
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
    backend->context_name = "Chibi Editor " + std::to_string(++context_serial);

    Rml::SetSystemInterface(&backend->system);
    Rml::SetRenderInterface(&backend->renderer);
    if (!Rml::Initialise()) {
        delete backend;
        return nullptr;
    }
    backend->initialized = true;
    Rml::LoadFontFace(
        Rml::Span<const Rml::byte>(editor_ui_font, editor_ui_font_size),
        "Inter",
        Rml::Style::FontStyle::Normal,
        Rml::Style::FontWeight::Normal,
        true
    );
    Rml::LoadFontFace(
        Rml::Span<const Rml::byte>(editor_ui_bold_font, editor_ui_bold_font_size),
        "Inter",
        Rml::Style::FontStyle::Normal,
        Rml::Style::FontWeight::Bold,
        false
    );
    backend->context = Rml::CreateContext(
        backend->context_name,
        { backend->physical_width, backend->physical_height }
    );
    if (!backend->context) {
        editor_rml_backend_destroy(backend);
        return nullptr;
    }
    backend->context->SetDensityIndependentPixelRatio(backend->scale);
    return backend;
}

extern "C" void editor_rml_backend_destroy(editor_rml_backend *backend)
{
    if (!backend) {
        return;
    }
    if (backend->initialized) {
        if (backend->context) {
            Rml::RemoveContext(backend->context_name);
            backend->context = nullptr;
        }
        Rml::ReleaseRenderManagers();
        Rml::Shutdown();
        backend->initialized = false;
    }
    destroy_gpu(backend);
    delete backend;
}

extern "C" void editor_rml_backend_resize(
    editor_rml_backend *backend,
    int logical_width,
    int logical_height,
    float framebuffer_scale
)
{
    assert(backend);
    backend->logical_width = logical_width;
    backend->logical_height = logical_height;
    backend->scale = framebuffer_scale;
    set_dimensions(backend);
}

extern "C" void editor_rml_backend_update(editor_rml_backend *backend, double delta_seconds)
{
    assert(backend && backend->context);
    backend->system.advance(std::max(0.0, delta_seconds));
    backend->context->Update();
}

extern "C" void editor_rml_backend_render(editor_rml_backend *backend)
{
    assert(backend && backend->context);
    ensure_gpu(backend);

    const CF_Rect viewport = { 0, 0, backend->physical_width, backend->physical_height };
    const CF_RenderState ui_state = premultiplied_blend_state();
    cf_draw_push_viewport(viewport);
    cf_draw3d_push_view(cf_m4_identity());
    cf_draw3d_push_projection(
        cf_ortho(0, (float)backend->physical_width, (float)backend->physical_height, 0, -1, 1)
    );
    cf_draw3d_push();
    cf_draw3d_transform(cf_m4_invert(cf_draw3d_peek_transform()));
    cf_draw3d_push_shader(backend->ui_shader);
    cf_draw3d_push_render_state(ui_state);
    backend->renderer.EnableScissorRegion(false);
    backend->context->Render();
    backend->renderer.EnableScissorRegion(false);
    cf_render_layers_to(backend->premultiplied_canvas, k_rml_draw_layer, k_rml_draw_layer, true);
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    cf_draw3d_pop();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_view();
    cf_draw_pop_viewport();

    cf_draw_push_viewport(viewport);
    cf_draw3d_push_view(cf_m4_identity());
    cf_draw3d_push_projection(
        cf_ortho(0, (float)backend->physical_width, (float)backend->physical_height, 0, -1, 1)
    );
    cf_draw3d_push();
    cf_draw3d_transform(cf_m4_invert(cf_draw3d_peek_transform()));
    cf_draw3d_push_shader(backend->resolve_shader);
    cf_draw_push_layer(k_rml_draw_layer);
    cf_draw3d_set_texture("u_texture", cf_canvas_get_target(backend->premultiplied_canvas));
    cf_draw3d_push_render_state(replace_state());
    cf_draw3d_mesh(backend->resolve_mesh);
    cf_render_layers_to(backend->straight_canvas, k_rml_draw_layer, k_rml_draw_layer, true);
    cf_draw_pop_layer();
    cf_draw3d_pop_render_state();
    cf_draw3d_pop_shader();
    cf_draw3d_pop();
    cf_draw3d_pop_projection();
    cf_draw3d_pop_view();
    cf_draw_pop_viewport();
}

extern "C" uint64_t editor_rml_backend_texture(const editor_rml_backend *backend)
{
    assert(backend);
    if (!backend->gpu_ready) {
        return 0;
    }
    return cf_canvas_get_target(backend->straight_canvas).id;
}

extern "C" void *editor_rml_backend_context(editor_rml_backend *backend)
{
    assert(backend);
    return backend->context;
}

extern "C" unsigned editor_rml_backend_process_input(editor_rml_backend *backend, const editor_rml_input *input)
{
    assert(backend && backend->context && input);
    Rml::Context &context = *backend->context;
    const int modifiers = (int)modifiers_to_rml(input->modifiers);
    const int x = (int)std::lround(input->x * backend->scale);
    const int y = (int)std::lround(input->y * backend->scale);
    bool unconsumed = true;
    unsigned capture = 0;
    switch (input->type) {
        case EDITOR_RML_POINTER_MOVE:
            unconsumed = context.ProcessMouseMove(x, y, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_POINTER;
            }
            break;
        case EDITOR_RML_POINTER_LEAVE:
            unconsumed = context.ProcessMouseLeave();
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_POINTER;
            }
            break;
        case EDITOR_RML_POINTER_DOWN:
            unconsumed = context.ProcessMouseButtonDown(input->button, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_POINTER;
            }
            break;
        case EDITOR_RML_POINTER_UP:
            unconsumed = context.ProcessMouseButtonUp(input->button, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_POINTER;
            }
            break;
        case EDITOR_RML_POINTER_WHEEL:
            unconsumed = context.ProcessMouseWheel({ input->wheel_x, input->wheel_y }, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_POINTER;
            }
            break;
        case EDITOR_RML_KEY_PRESS:
            unconsumed = context.ProcessKeyDown((Rml::Input::KeyIdentifier)input->key, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_KEYBOARD;
            }
            break;
        case EDITOR_RML_KEY_RELEASE:
            unconsumed = context.ProcessKeyUp((Rml::Input::KeyIdentifier)input->key, modifiers);
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_KEYBOARD;
            }
            break;
        case EDITOR_RML_TEXT_INPUT:
            unconsumed = context.ProcessTextInput(
                Rml::String(input->text ? input->text : "", input->text ? input->text_size : 0)
            );
            if (!unconsumed) {
                capture |= EDITOR_RML_CAPTURE_KEYBOARD | EDITOR_RML_CAPTURE_TEXT;
            }
            break;
    }
    if (context.IsMouseInteracting()) {
        capture |= EDITOR_RML_CAPTURE_POINTER;
    }
    return capture;
}
