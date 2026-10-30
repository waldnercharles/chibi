/* The RmlUi C binding (rml.h): type conversions and forwarding only. */
#include "ui/rml.h"

#include "ui/json_items.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/TextShapingContext.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

#define SAME(c, rml) static_assert((int)(c) == (int)(rml))
#define SAME_KEY(name) SAME(RML_KEY_##name, Rml::Input::KI_##name)
#define SAME_EVENT(c, rml) SAME(RML_EVENT_##c, Rml::EventId::rml)

SAME_KEY(UNKNOWN);
SAME_KEY(SPACE);
SAME_KEY(0);
SAME_KEY(1);
SAME_KEY(2);
SAME_KEY(3);
SAME_KEY(4);
SAME_KEY(5);
SAME_KEY(6);
SAME_KEY(7);
SAME_KEY(8);
SAME_KEY(9);
SAME_KEY(A);
SAME_KEY(B);
SAME_KEY(C);
SAME_KEY(D);
SAME_KEY(E);
SAME_KEY(F);
SAME_KEY(G);
SAME_KEY(H);
SAME_KEY(I);
SAME_KEY(J);
SAME_KEY(K);
SAME_KEY(L);
SAME_KEY(M);
SAME_KEY(N);
SAME_KEY(O);
SAME_KEY(P);
SAME_KEY(Q);
SAME_KEY(R);
SAME_KEY(S);
SAME_KEY(T);
SAME_KEY(U);
SAME_KEY(V);
SAME_KEY(W);
SAME_KEY(X);
SAME_KEY(Y);
SAME_KEY(Z);
SAME_KEY(OEM_PLUS);
SAME_KEY(OEM_MINUS);
SAME_KEY(OEM_PERIOD);
SAME_KEY(NUMPADENTER);
SAME_KEY(SUBTRACT);
SAME_KEY(BACK);
SAME_KEY(TAB);
SAME_KEY(RETURN);
SAME_KEY(ESCAPE);
SAME_KEY(PRIOR);
SAME_KEY(NEXT);
SAME_KEY(END);
SAME_KEY(HOME);
SAME_KEY(LEFT);
SAME_KEY(UP);
SAME_KEY(RIGHT);
SAME_KEY(DOWN);
SAME_KEY(INSERT);
SAME_KEY(DELETE);
SAME_KEY(F1);
SAME_KEY(F2);
SAME_KEY(F3);
SAME_KEY(F4);
SAME_KEY(F5);
SAME_KEY(F6);
SAME_KEY(F7);
SAME_KEY(F8);
SAME_KEY(F9);
SAME_KEY(F10);
SAME_KEY(F11);
SAME_KEY(F12);
SAME_KEY(LSHIFT);
SAME_KEY(RSHIFT);
SAME_KEY(LCONTROL);
SAME_KEY(RCONTROL);
SAME_KEY(LMENU);
SAME_KEY(RMENU);
SAME_KEY(LMETA);
SAME_KEY(RMETA);

SAME(RML_KM_CTRL, Rml::Input::KM_CTRL);
SAME(RML_KM_SHIFT, Rml::Input::KM_SHIFT);
SAME(RML_KM_ALT, Rml::Input::KM_ALT);
SAME(RML_KM_META, Rml::Input::KM_META);
SAME(RML_KM_CAPSLOCK, Rml::Input::KM_CAPSLOCK);
SAME(RML_KM_NUMLOCK, Rml::Input::KM_NUMLOCK);
SAME(RML_KM_SCROLLLOCK, Rml::Input::KM_SCROLLLOCK);

SAME_EVENT(INVALID, Invalid);
SAME_EVENT(MOUSEDOWN, Mousedown);
SAME_EVENT(MOUSESCROLL, Mousescroll);
SAME_EVENT(MOUSEOVER, Mouseover);
SAME_EVENT(MOUSEOUT, Mouseout);
SAME_EVENT(FOCUS, Focus);
SAME_EVENT(BLUR, Blur);
SAME_EVENT(KEYDOWN, Keydown);
SAME_EVENT(KEYUP, Keyup);
SAME_EVENT(TEXTINPUT, Textinput);
SAME_EVENT(MOUSEUP, Mouseup);
SAME_EVENT(CLICK, Click);
SAME_EVENT(DBLCLICK, Dblclick);
SAME_EVENT(LOAD, Load);
SAME_EVENT(UNLOAD, Unload);
SAME_EVENT(SHOW, Show);
SAME_EVENT(HIDE, Hide);
SAME_EVENT(MOUSEMOVE, Mousemove);
SAME_EVENT(DRAGMOVE, Dragmove);
SAME_EVENT(DRAG, Drag);
SAME_EVENT(DRAGSTART, Dragstart);
SAME_EVENT(DRAGOVER, Dragover);
SAME_EVENT(DRAGDROP, Dragdrop);
SAME_EVENT(DRAGOUT, Dragout);
SAME_EVENT(DRAGEND, Dragend);
SAME_EVENT(HANDLEDRAG, Handledrag);
SAME_EVENT(RESIZE, Resize);
SAME_EVENT(SCROLL, Scroll);
SAME_EVENT(ANIMATIONEND, Animationend);
SAME_EVENT(TRANSITIONEND, Transitionend);
SAME_EVENT(CHANGE, Change);
SAME_EVENT(SUBMIT, Submit);
SAME_EVENT(TABCHANGE, Tabchange);
SAME_EVENT(INPUT, Input);

SAME(RML_BOX_MARGIN, Rml::BoxArea::Margin);
SAME(RML_BOX_BORDER, Rml::BoxArea::Border);
SAME(RML_BOX_PADDING, Rml::BoxArea::Padding);
SAME(RML_BOX_CONTENT, Rml::BoxArea::Content);
SAME(RML_BOX_AUTO, Rml::BoxArea::Auto);
SAME(RML_SCROLL_START, Rml::ScrollAlignment::Start);
SAME(RML_SCROLL_CENTER, Rml::ScrollAlignment::Center);
SAME(RML_SCROLL_END, Rml::ScrollAlignment::End);
SAME(RML_SCROLL_NEAREST, Rml::ScrollAlignment::Nearest);
SAME(RML_SCROLL_ADAPTIVE, Rml::ScrollAlignment::Adaptive);
SAME(RML_MODAL_NONE, Rml::ModalFlag::None);
SAME(RML_MODAL_MODAL, Rml::ModalFlag::Modal);
SAME(RML_MODAL_KEEP, Rml::ModalFlag::Keep);
SAME(RML_FOCUS_NONE, Rml::FocusFlag::None);
SAME(RML_FOCUS_DOCUMENT, Rml::FocusFlag::Document);
SAME(RML_FOCUS_KEEP, Rml::FocusFlag::Keep);
SAME(RML_FOCUS_AUTO, Rml::FocusFlag::Auto);
SAME(RML_SCROLL_FLAG_NONE, Rml::ScrollFlag::None);
SAME(RML_SCROLL_FLAG_AUTO, Rml::ScrollFlag::Auto);
SAME(RML_FONT_NORMAL, Rml::Style::FontStyle::Normal);
SAME(RML_FONT_ITALIC, Rml::Style::FontStyle::Italic);
SAME(RML_LOG_ALWAYS, Rml::Log::LT_ALWAYS);
SAME(RML_LOG_ERROR, Rml::Log::LT_ERROR);
SAME(RML_LOG_ASSERT, Rml::Log::LT_ASSERT);
SAME(RML_LOG_WARNING, Rml::Log::LT_WARNING);
SAME(RML_LOG_INFO, Rml::Log::LT_INFO);
SAME(RML_LOG_DEBUG, Rml::Log::LT_DEBUG);

static_assert(std::is_same_v<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>);
static_assert(std::is_same_v<Rml::byte, uint8_t>);
static_assert(sizeof(rml_vertex) == sizeof(Rml::Vertex));
static_assert(offsetof(rml_vertex, position) == offsetof(Rml::Vertex, position));
static_assert(offsetof(rml_vertex, color) == offsetof(Rml::Vertex, colour));
static_assert(offsetof(rml_vertex, tex_coord) == offsetof(Rml::Vertex, tex_coord));

namespace {

/* Handles are the RmlUi objects; these convert between the two. */
template<typename T, typename Handle>
T *unwrap(const Handle *handle)
{
    return reinterpret_cast<T *>(const_cast<Handle *>(handle));
}

Rml::Element *element(const rml_element *handle)
{
    return unwrap<Rml::Element>(handle);
}

Rml::Context *context(const rml_context *handle)
{
    return unwrap<Rml::Context>(handle);
}

rml_element *wrap(Rml::Element *element)
{
    return reinterpret_cast<rml_element *>(element);
}

rml_context *wrap(Rml::Context *context)
{
    return reinterpret_cast<rml_context *>(context);
}

Rml::ElementDocument *document(rml_element *handle)
{
    auto *document = dynamic_cast<Rml::ElementDocument *>(element(handle));
    assert(document);
    return document;
}

Rml::ElementFormControl *form_control(const rml_element *handle)
{
    auto *control = dynamic_cast<Rml::ElementFormControl *>(element(handle));
    assert(control);
    return control;
}

/* An input or a textarea, which select text alike. */
template<typename F>
void text_control(rml_element *handle, F forward)
{
    if (auto *area = dynamic_cast<Rml::ElementFormControlTextArea *>(element(handle))) {
        forward(area);
        return;
    }
    auto *input = dynamic_cast<Rml::ElementFormControlInput *>(element(handle));
    assert(input);
    forward(input);
}

rml_vec2 vec2(Rml::Vector2f v)
{
    return { v.x, v.y };
}

rml_color color(Rml::Colourb c)
{
    return { c.red, c.green, c.blue, c.alpha };
}

rml_color color(Rml::ColourbPremultiplied c)
{
    return { c.red, c.green, c.blue, c.alpha };
}

/* The stops live in `stops`, which must outlive the parameters. */
rml_effect_parameters effect_parameters(
    const Rml::Dictionary &parameters,
    Rml::Vector<rml_color_stop> &stops
)
{
    rml_effect_parameters p = {};
    p.value = Rml::Get(parameters, "value", 0.0f);
    p.sigma = Rml::Get(parameters, "sigma", 0.0f);
    p.angle = Rml::Get(parameters, "angle", 0.0f);
    p.offset = vec2(Rml::Get(parameters, "offset", Rml::Vector2f()));
    p.color = color(Rml::Get(parameters, "color", Rml::Colourb(0, 0, 0, 0)));
    p.p0 = vec2(Rml::Get(parameters, "p0", Rml::Vector2f()));
    p.p1 = vec2(Rml::Get(parameters, "p1", Rml::Vector2f()));
    p.center = vec2(Rml::Get(parameters, "center", Rml::Vector2f()));
    p.radius = vec2(Rml::Get(parameters, "radius", Rml::Vector2f()));
    p.repeating = Rml::Get(parameters, "repeating", false);
    if (const Rml::Variant *list = Rml::GetIf(parameters, "color_stop_list")) {
        for (const Rml::ColorStop &stop : list->GetReference<Rml::ColorStopList>()) {
            stops.push_back({ stop.position.number, color(stop.color) });
        }
    }
    p.stops = stops.data();
    p.stop_count = stops.size();
    return p;
}

size_t copy(const Rml::String &string, char *buffer, size_t size)
{
    if (size) {
        std::snprintf(buffer, size, "%s", string.c_str());
    }
    return string.size();
}

class System final : public Rml::SystemInterface
{
public:
    const rml_system_interface *fn = nullptr;

    double GetElapsedTime() override { return fn->elapsed_time(); }

    void SetMouseCursor(const Rml::String &name) override
    {
        fn->set_mouse_cursor(name.c_str());
    }

    void SetClipboardText(const Rml::String &text) override
    {
        fn->set_clipboard_text(text.c_str());
    }

    void GetClipboardText(Rml::String &text) override
    {
        fn->get_clipboard_text(reinterpret_cast<rml_string *>(&text));
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        return fn->log_message((rml_log_type)type, message.c_str());
    }
};

System system_interface;

class Renderer final : public Rml::RenderInterface
{
public:
    Renderer(const rml_render_interface *fn, void *user)
      : fn(fn)
      , user(user)
    {
    }

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices
    ) override
    {
        const rml_mesh mesh = {
            reinterpret_cast<const rml_vertex *>(vertices.data()),
            vertices.size(),
            indices.data(),
            indices.size(),
        };
        return fn->compile_geometry(user, mesh);
    }

    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture
    ) override
    {
        fn->render_geometry(user, geometry, vec2(translation), texture);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        fn->release_geometry(user, geometry);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override
    {
        return fn->load_texture(user, source.c_str(), &dimensions.x, &dimensions.y);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size) override
    {
        return fn->generate_texture(user, pixels.data(), size.x, size.y);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        fn->release_texture(user, texture);
    }

    void EnableScissorRegion(bool enable) override
    {
        fn->enable_scissor_region(user, enable);
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        const rml_rect rect = {
            region.Left(),
            region.Top(),
            region.Width(),
            region.Height(),
        };
        fn->set_scissor_region(user, rect);
    }

    void SetTransform(const Rml::Matrix4f *transform) override
    {
        fn->set_transform(user, transform ? transform->data() : nullptr);
    }

    static_assert((int)Rml::ClipMaskOperation::Set == RML_CLIP_MASK_SET);
    static_assert((int)Rml::ClipMaskOperation::SetInverse == RML_CLIP_MASK_SET_INVERSE);
    static_assert((int)Rml::ClipMaskOperation::Intersect == RML_CLIP_MASK_INTERSECT);
    static_assert((int)Rml::BlendMode::Blend == RML_BLEND_MODE_BLEND);
    static_assert((int)Rml::BlendMode::Replace == RML_BLEND_MODE_REPLACE);

    void EnableClipMask(bool enable) override
    {
        if (fn->enable_clip_mask) {
            fn->enable_clip_mask(user, enable);
        }
    }

    void RenderToClipMask(
        Rml::ClipMaskOperation operation,
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation
    ) override
    {
        if (fn->render_to_clip_mask) {
            fn->render_to_clip_mask(user, (rml_clip_mask_operation)operation, geometry, vec2(translation));
        }
    }

    Rml::LayerHandle PushLayer() override
    {
        return fn->push_layer ? fn->push_layer(user) : RenderInterface::PushLayer();
    }

    void CompositeLayers(
        Rml::LayerHandle source,
        Rml::LayerHandle destination,
        Rml::BlendMode blend_mode,
        Rml::Span<const Rml::CompiledFilterHandle> filters
    ) override
    {
        if (!fn->composite_layers) {
            return;
        }
        fn->composite_layers(
            user,
            source,
            destination,
            (rml_blend_mode)blend_mode,
            filters.data(),
            filters.size()
        );
    }

    void PopLayer() override
    {
        if (fn->pop_layer) {
            fn->pop_layer(user);
        }
    }

    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return fn->save_layer_as_texture ? fn->save_layer_as_texture(user) : 0;
    }

    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        return fn->save_layer_as_mask_image ? fn->save_layer_as_mask_image(user) : 0;
    }

    Rml::CompiledFilterHandle CompileFilter(const Rml::String &name, const Rml::Dictionary &parameters) override
    {
        if (!fn->compile_filter) {
            return 0;
        }
        Rml::Vector<rml_color_stop> stops;
        const rml_effect_parameters p = effect_parameters(parameters, stops);
        return fn->compile_filter(user, name.c_str(), &p);
    }

    void ReleaseFilter(Rml::CompiledFilterHandle filter) override
    {
        if (fn->release_filter) {
            fn->release_filter(user, filter);
        }
    }

    Rml::CompiledShaderHandle CompileShader(const Rml::String &name, const Rml::Dictionary &parameters) override
    {
        if (!fn->compile_shader) {
            return 0;
        }
        Rml::Vector<rml_color_stop> stops;
        const rml_effect_parameters p = effect_parameters(parameters, stops);
        return fn->compile_shader(user, name.c_str(), &p);
    }

    void RenderShader(
        Rml::CompiledShaderHandle shader,
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture
    ) override
    {
        if (fn->render_shader) {
            fn->render_shader(user, shader, geometry, vec2(translation), texture);
        }
    }

    void ReleaseShader(Rml::CompiledShaderHandle shader) override
    {
        if (fn->release_shader) {
            fn->release_shader(user, shader);
        }
    }

private:
    const rml_render_interface *fn;
    void *user;
};

/* The parameters RmlUi's events carry the modifier keys in. */
static const struct
{
    const char *name;
    unsigned bit;
} MODIFIER_PARAMETERS[] = {
    { "ctrl_key", RML_KM_CTRL },
    { "shift_key", RML_KM_SHIFT },
    { "alt_key", RML_KM_ALT },
    { "meta_key", RML_KM_META },
    { "caps_lock_key", RML_KM_CAPSLOCK },
    { "num_lock_key", RML_KM_NUMLOCK },
    { "scroll_lock_key", RML_KM_SCROLLLOCK },
};

rml_event convert(Rml::Event &event)
{
    rml_event e = {};
    /* A custom event, such as contextmenu, has no rml_event_id. */
    e.id = event.GetId() < Rml::EventId::NumDefinedIds ? (rml_event_id)event.GetId()
                                                       : RML_EVENT_INVALID;
    e.target = wrap(event.GetTargetElement());
    e.current = wrap(event.GetCurrentElement());
    e.drag_element = wrap(
        static_cast<Rml::Element *>(event.GetParameter<void *>("drag_element", nullptr))
    );
    e.mouse_x = event.GetParameter<float>("mouse_x", 0);
    e.mouse_y = event.GetParameter<float>("mouse_y", 0);
    e.button = event.GetParameter<int>("button", 0);
    e.key = (rml_key)event.GetParameter<int>("key_identifier", 0);
    for (const auto &modifier : MODIFIER_PARAMETERS) {
        if (event.GetParameter<int>(modifier.name, 0)) {
            e.modifiers |= modifier.bit;
        }
    }
    e.native = &event;
    return e;
}

class Listener final : public Rml::EventListener
{
public:
    Listener(rml_event_fn fn, void *user)
      : fn(fn)
      , user(user)
    {
    }

    void ProcessEvent(Rml::Event &event) override
    {
        rml_event e = convert(event);
        fn(user, &e);
    }

private:
    rml_event_fn fn;
    void *user;
};

class CustomElement final : public Rml::Element
{
public:
    CustomElement(const Rml::String &tag, const rml_element_hooks *hooks)
      : Rml::Element(tag)
      , hooks(hooks)
    {
    }

    ~CustomElement() override
    {
        if (hooks->release) {
            hooks->release(user);
        }
    }

    void *user = nullptr;

protected:
    void OnRender() override
    {
        if (hooks->render) {
            hooks->render(wrap(this), user);
        }
    }

    void OnResize() override
    {
        if (hooks->resize) {
            hooks->resize(wrap(this), user);
        }
    }

    void OnUpdate() override
    {
        if (hooks->update) {
            hooks->update(wrap(this), user);
        }
    }

    void OnLayout() override
    {
        if (hooks->layout) {
            hooks->layout(wrap(this), user);
        }
    }

    void OnAttributeChange(const Rml::ElementAttributes &changed) override
    {
        Rml::Element::OnAttributeChange(changed);
        if (!hooks->attribute) {
            return;
        }
        for (const auto &attribute : changed) {
            hooks->attribute(wrap(this), user, attribute.first.c_str());
        }
    }

private:
    const rml_element_hooks *hooks;
};

class Instancer final : public Rml::ElementInstancer
{
public:
    explicit Instancer(const rml_element_hooks *hooks)
      : hooks(hooks)
    {
    }

    Rml::ElementPtr InstanceElement(Rml::Element *, const Rml::String &tag, const Rml::XMLAttributes &) override
    {
        return Rml::ElementPtr(new CustomElement(tag, hooks));
    }

    void ReleaseElement(Rml::Element *element) override { delete element; }

private:
    const rml_element_hooks *hooks;
};

CustomElement *custom(const rml_element *handle)
{
    auto *custom = dynamic_cast<CustomElement *>(element(handle));
    assert(custom);
    return custom;
}

yyjson_mut_val *json(void *ptr)
{
    return static_cast<yyjson_mut_val *>(ptr);
}

/* A data variable over a yyjson value: object members by name, array items
 * by index, and other values as scalars. Strings a binding writes are copied
 * into the document, as are the members it names that are missing. */
class JsonDefinition final : public Rml::VariableDefinition
{
public:
    JsonDefinition(yyjson_mut_doc *doc, json_items *items)
      : Rml::VariableDefinition(Rml::DataVariableType::Scalar)
      , doc(doc)
      , items(items)
    {
    }

    bool Get(void *ptr, Rml::Variant &variant) override
    {
        yyjson_mut_val *v = json(ptr);
        reads.values++;
        switch (yyjson_mut_get_type(v)) {
            case YYJSON_TYPE_NULL: variant.Clear(); return true;
            case YYJSON_TYPE_BOOL:
                variant = yyjson_mut_get_bool(v);
                return true;
            case YYJSON_TYPE_NUM: variant = yyjson_mut_get_num(v); return true;
            case YYJSON_TYPE_STR:
                variant = Rml::String(yyjson_mut_get_str(v), yyjson_mut_get_len(v));
                return true;
            default: return false;
        }
    }

    bool Set(void *ptr, const Rml::Variant &variant) override
    {
        yyjson_mut_val *v = json(ptr);
        bool set = false;
        switch (variant.GetType()) {
            case Rml::Variant::BOOL:
                set = yyjson_mut_set_bool(v, variant.Get<bool>());
                break;
            case Rml::Variant::STRING: {
                const Rml::String &s = variant.GetReference<Rml::String>();
                if (yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == s.size() &&
                    !memcmp(yyjson_mut_get_str(v), s.data(), s.size())) {
                    return true;
                }
                yyjson_mut_val *copy = yyjson_mut_strncpy(doc, s.data(), s.size());
                set = yyjson_mut_set_strn(v, yyjson_mut_get_str(copy), s.size());
                break;
            }
            case Rml::Variant::BYTE:
            case Rml::Variant::INT:
            case Rml::Variant::INT64:
            case Rml::Variant::UINT:
            case Rml::Variant::UINT64:
            case Rml::Variant::FLOAT:
            case Rml::Variant::DOUBLE:
                set = yyjson_mut_set_real(v, variant.Get<double>());
                break;
            default: break;
        }
        writes += set;
        if (set && std::find(written.begin(), written.end(), v) == written.end()) {
            written.push_back(v);
        }
        return set;
    }

    int Size(void *ptr) override { return (int)yyjson_mut_arr_size(json(ptr)); }

    Rml::DataVariable Child(void *ptr, const Rml::DataAddressEntry &address) override
    {
        yyjson_mut_val *v = json(ptr);
        reads.lookups++;
        if (yyjson_mut_is_arr(v) && address.name == "size") {
            return Rml::MakeLiteralIntVariable(Size(ptr));
        }
        if (yyjson_mut_is_arr(v)) {
            yyjson_mut_val *item = json_items_get(items, v, (size_t)address.index);
            return item ? Rml::DataVariable(this, item) : Rml::DataVariable();
        }
        if (!yyjson_mut_is_obj(v)) {
            return Rml::DataVariable();
        }
        /* A member nothing set reads as null, as JavaScript's undefined does,
           and a binding may set it. */
        yyjson_mut_val *member = yyjson_mut_obj_get(v, address.name.c_str());
        if (!member) {
            member = yyjson_mut_null(doc);
            yyjson_mut_obj_add(
                v,
                yyjson_mut_strncpy(doc, address.name.data(), address.name.size()),
                member
            );
        }
        return Rml::DataVariable(this, member);
    }

    uint64_t writes = 0;
    rml_data_reads reads = {};
    Rml::Vector<yyjson_mut_val *> written; /* each once, until taken */

private:
    yyjson_mut_doc *doc;
    json_items *items;
};

} // namespace

/* ---------- Core ---------- */

void rml_set_system_interface(const rml_system_interface *system)
{
    system_interface.fn = system;
    Rml::SetSystemInterface(&system_interface);
}

bool rml_initialize()
{
    return Rml::Initialise();
}

void rml_shutdown()
{
    Rml::Shutdown();
}

bool rml_load_font_face(
    const uint8_t *data,
    size_t size,
    const char *family,
    rml_font_style style,
    int weight,
    bool fallback
)
{
    return Rml::LoadFontFace(
        Rml::Span<const Rml::byte>(data, size),
        family,
        (Rml::Style::FontStyle)style,
        (Rml::Style::FontWeight)weight,
        fallback
    );
}

void rml_string_set(rml_string *string, const char *text)
{
    *unwrap<Rml::String>(string) = text;
}

rml_renderer *rml_renderer_create(const rml_render_interface *render, void *user)
{
    return reinterpret_cast<rml_renderer *>(new Renderer(render, user));
}

void rml_renderer_destroy(rml_renderer *renderer)
{
    delete unwrap<Renderer>(renderer);
}

/* Workstream I: rml_font_generation's count. */
static unsigned font_generation;

void rml_release_render_managers()
{
    font_generation++;
    Rml::ReleaseRenderManagers();
}

/* ---------- Contexts ---------- */

rml_context *rml_create_context(const char *name, int width, int height, rml_renderer *renderer)
{
    return wrap(Rml::CreateContext(name, { width, height }, unwrap<Renderer>(renderer)));
}

bool rml_remove_context(const char *name)
{
    return Rml::RemoveContext(name);
}

void rml_context_set_dimensions(rml_context *c, int width, int height)
{
    context(c)->SetDimensions({ width, height });
}

void rml_context_set_dp_ratio(rml_context *c, float dp_ratio)
{
    context(c)->SetDensityIndependentPixelRatio(dp_ratio);
}

float rml_context_dp_ratio(const rml_context *c)
{
    return context(c)->GetDensityIndependentPixelRatio();
}

double rml_context_next_update_delay(const rml_context *c)
{
    return context(c)->GetNextUpdateDelay();
}

bool rml_context_update(rml_context *c)
{
    return context(c)->Update();
}

bool rml_context_update_profiled(rml_context *c, double seconds[RML_UPDATE_PARTS])
{
    return context(c)->Update(seconds);
}

bool rml_context_render(rml_context *c)
{
    return context(c)->Render();
}

rml_element *rml_context_load_document(rml_context *c, const char *path)
{
    return wrap(context(c)->LoadDocument(path));
}

rml_element *rml_context_create_document(rml_context *c, const char *source_url)
{
    return wrap(context(c)->CreateDocument("body", source_url));
}

rml_element *rml_context_load_document_from_memory(rml_context *c, const char *rml, const char *source_url)
{
    return wrap(context(c)->LoadDocumentFromMemory(rml, source_url));
}

rml_element *rml_context_hover_element(rml_context *c)
{
    return wrap(context(c)->GetHoverElement());
}

rml_element *rml_context_focus_element(rml_context *c)
{
    return wrap(context(c)->GetFocusElement());
}

void rml_context_activate_theme(rml_context *c, const char *name, bool active)
{
    context(c)->ActivateTheme(name, active);
}

bool rml_context_is_mouse_interacting(const rml_context *c)
{
    return context(c)->IsMouseInteracting();
}

bool rml_context_process_mouse_move(rml_context *c, int x, int y, unsigned modifiers)
{
    return context(c)->ProcessMouseMove(x, y, (int)modifiers);
}

bool rml_context_process_mouse_leave(rml_context *c)
{
    return context(c)->ProcessMouseLeave();
}

bool rml_context_process_mouse_button_down(rml_context *c, int button, unsigned modifiers)
{
    return context(c)->ProcessMouseButtonDown(button, (int)modifiers);
}

bool rml_context_process_mouse_button_up(rml_context *c, int button, unsigned modifiers)
{
    return context(c)->ProcessMouseButtonUp(button, (int)modifiers);
}

bool rml_context_process_mouse_wheel(rml_context *c, rml_vec2 delta, unsigned modifiers)
{
    return context(c)->ProcessMouseWheel(Rml::Vector2f(delta.x, delta.y), (int)modifiers);
}

bool rml_context_process_key_down(rml_context *c, rml_key key, unsigned modifiers)
{
    return context(c)->ProcessKeyDown((Rml::Input::KeyIdentifier)key, (int)modifiers);
}

bool rml_context_process_key_up(rml_context *c, rml_key key, unsigned modifiers)
{
    return context(c)->ProcessKeyUp((Rml::Input::KeyIdentifier)key, (int)modifiers);
}

bool rml_context_process_text_input(rml_context *c, const char *text, size_t size)
{
    return context(c)->ProcessTextInput(Rml::String(text, size));
}

/* ---------- Documents ---------- */

void rml_document_show(rml_element *d, rml_modal_flag modal, rml_focus_flag focus, rml_scroll_flag scroll)
{
    document(d)->Show((Rml::ModalFlag)modal, (Rml::FocusFlag)focus, (Rml::ScrollFlag)scroll);
}

void rml_document_close(rml_element *d)
{
    document(d)->Close();
}

void rml_document_update(rml_element *d)
{
    document(d)->UpdateDocument();
}

void rml_document_refresh_layout(rml_element *d)
{
    document(d)->UpdateDocument(false);
}

/* ---------- Elements ---------- */

rml_element *rml_element_create_child(rml_element *parent, const char *tag, rml_element *before)
{
    Rml::Element *p = element(parent);
    Rml::ElementPtr child = p->GetOwnerDocument()->CreateElement(tag);
    return wrap(p->InsertBefore(std::move(child), element(before)));
}

void rml_element_move_child(rml_element *parent, rml_element *child, rml_element *before)
{
    Rml::Element *c = element(child), *p = element(parent);
    Rml::Element *from = c->GetParentNode();
    bool same_model = c->GetDataModel() && c->GetDataModel() == p->GetDataModel();
    Rml::ElementPtr moved = same_model ? from->RemoveChildToMove(c)
                                       : from->RemoveChild(c);
    p->InsertBefore(std::move(moved), element(before));
}

void rml_element_remove_child(rml_element *parent, rml_element *child)
{
    element(parent)->RemoveChild(element(child));
}

rml_element *rml_element_parent(const rml_element *e)
{
    return wrap(element(e)->GetParentNode());
}

int rml_element_child_count(const rml_element *e)
{
    return element(e)->GetNumChildren();
}

rml_element *rml_element_child(const rml_element *e, int index)
{
    return wrap(element(e)->GetChild(index));
}

rml_element *rml_element_first_child(const rml_element *e)
{
    return wrap(element(e)->GetFirstChild());
}

rml_context *rml_element_context(const rml_element *e)
{
    return wrap(element(e)->GetContext());
}

rml_element *rml_element_by_id(rml_element *e, const char *id)
{
    return wrap(element(e)->GetElementById(id));
}

rml_element *rml_element_owner_document(const rml_element *e)
{
    return wrap(element(e)->GetOwnerDocument());
}

rml_element *rml_element_query_selector(rml_element *e, const char *selector)
{
    return wrap(element(e)->QuerySelector(selector));
}

int rml_element_query_selector_all(rml_element *e, const char *selectors, rml_element **matches, int max)
{
    Rml::ElementList list;
    element(e)->QuerySelectorAll(list, selectors);
    int count = (int)list.size();
    for (int i = 0; i < count && i < max; ++i) {
        matches[i] = wrap(list[(size_t)i]);
    }
    return count;
}

const char *rml_element_id(const rml_element *e)
{
    return element(e)->GetId().c_str();
}

const char *rml_element_tag_name(const rml_element *e)
{
    return element(e)->GetTagName().c_str();
}

void rml_element_set_attribute(rml_element *e, const char *name, const char *value)
{
    element(e)->SetAttribute(name, Rml::String(value));
}

void rml_element_remove_attribute(rml_element *e, const char *name)
{
    element(e)->RemoveAttribute(name);
}

bool rml_element_has_attribute(const rml_element *e, const char *name)
{
    return element(e)->HasAttribute(name);
}

size_t rml_element_attribute(const rml_element *e, const char *name, char *buffer, size_t size)
{
    return copy(element(e)->GetAttribute<Rml::String>(name, Rml::String()), buffer, size);
}

void rml_element_set_class(rml_element *e, const char *name, bool on)
{
    element(e)->SetClass(name, on);
}

bool rml_element_is_class_set(const rml_element *e, const char *name)
{
    return element(e)->IsClassSet(name);
}

void rml_element_set_class_names(rml_element *e, const char *names)
{
    element(e)->SetClassNames(names);
}

bool rml_element_is_pseudo_class_set(const rml_element *e, const char *name)
{
    return element(e)->IsPseudoClassSet(name);
}

bool rml_element_set_property(rml_element *e, const char *name, const char *value)
{
    return element(e)->SetProperty(name, value);
}

void rml_element_remove_property(rml_element *e, const char *name)
{
    element(e)->RemoveProperty(name);
}

rml_color rml_element_color(const rml_element *e)
{
    return color(element(e)->GetComputedValues().color());
}

rml_color rml_element_border_top_color(const rml_element *e)
{
    return color(element(e)->GetComputedValues().border_top_color());
}

int rml_element_transitions(const rml_element *e, rml_transition *transitions, int max)
{
    const Rml::TransitionList *list = element(e)->GetComputedValues().transition();
    if (!list || list->none) {
        return 0;
    }
    int count = (int)list->transitions.size();
    for (int i = 0; i < count && i < max; ++i) {
        transitions[i] = { list->transitions[(size_t)i].duration,
                           list->transitions[(size_t)i].delay };
    }
    return count;
}

void rml_element_set_inner_rml(rml_element *e, const char *rml)
{
    element(e)->SetInnerRML(rml);
}

size_t rml_element_inner_rml(const rml_element *e, char *buffer, size_t size)
{
    return copy(element(e)->GetInnerRML(), buffer, size);
}

size_t rml_element_value(const rml_element *e, char *buffer, size_t size)
{
    return copy(form_control(e)->GetValue(), buffer, size);
}

void rml_element_set_value(rml_element *e, const char *value)
{
    form_control(e)->SetValue(value);
}

void rml_element_select(rml_element *e)
{
    text_control(e, [](auto *control) { control->Select(); });
}

void rml_element_set_selection_range(rml_element *e, int start, int end)
{
    text_control(e, [=](auto *control) {
        control->SetSelectionRange(start, end);
    });
}

void rml_element_selection_range(rml_element *e, int *start, int *end)
{
    text_control(e, [=](auto *control) {
        control->GetSelection(start, end, nullptr);
    });
}

void rml_element_click(rml_element *e)
{
    element(e)->Click();
}

bool rml_element_focus(rml_element *e, bool focus_visible)
{
    return element(e)->Focus(focus_visible);
}

void rml_element_blur(rml_element *e)
{
    element(e)->Blur();
}

void rml_element_scroll_into_view(rml_element *e, rml_scroll_alignment vertical, rml_scroll_alignment horizontal)
{
    element(e)->ScrollIntoView(
        Rml::ScrollIntoViewOptions((Rml::ScrollAlignment)vertical, (Rml::ScrollAlignment)horizontal)
    );
}

float rml_element_scroll_top(rml_element *e)
{
    return element(e)->GetScrollTop();
}

void rml_element_set_scroll_top(rml_element *e, float scroll_top)
{
    element(e)->SetScrollTop(scroll_top);
}

float rml_element_client_height(rml_element *e)
{
    return element(e)->GetClientHeight();
}

float rml_element_scroll_height(rml_element *e)
{
    return element(e)->GetScrollHeight();
}

float rml_element_scroll_left(rml_element *e)
{
    return element(e)->GetScrollLeft();
}

void rml_element_set_scroll_left(rml_element *e, float scroll_left)
{
    element(e)->SetScrollLeft(scroll_left);
}

float rml_element_client_width(rml_element *e)
{
    return element(e)->GetClientWidth();
}

float rml_element_scroll_width(rml_element *e)
{
    return element(e)->GetScrollWidth();
}

rml_vec2 rml_element_absolute_offset(rml_element *e, rml_box_area area)
{
    return vec2(element(e)->GetAbsoluteOffset((Rml::BoxArea)area));
}

rml_vec2 rml_element_relative_offset(rml_element *e, rml_box_area area)
{
    return vec2(element(e)->GetRelativeOffset((Rml::BoxArea)area));
}

rml_vec2 rml_element_box_size(rml_element *e, rml_box_area area)
{
    return vec2(element(e)->GetBox().GetSize((Rml::BoxArea)area));
}

bool rml_element_is_visible(const rml_element *e, bool include_ancestors)
{
    return element(e)->IsVisible(include_ancestors);
}

/* ---------- Events ---------- */

rml_listener *rml_listener_create(rml_event_fn fn, void *user)
{
    return reinterpret_cast<rml_listener *>(new Listener(fn, user));
}

void rml_listener_destroy(rml_listener *listener)
{
    delete unwrap<Listener>(listener);
}

void rml_element_add_event_listener(rml_element *e, rml_event_id id, rml_listener *listener, bool capture)
{
    element(e)->AddEventListener((Rml::EventId)id, unwrap<Listener>(listener), capture);
}

void rml_element_remove_event_listener(rml_element *e, rml_event_id id, rml_listener *listener, bool capture)
{
    element(e)->RemoveEventListener((Rml::EventId)id, unwrap<Listener>(listener), capture);
}

void rml_event_stop_propagation(const rml_event *event)
{
    static_cast<Rml::Event *>(event->native)->StopPropagation();
}

bool rml_element_dispatch_event(rml_element *e, const char *type, const rml_event *event)
{
    Rml::Dictionary parameters;
    parameters["mouse_x"] = event->mouse_x;
    parameters["mouse_y"] = event->mouse_y;
    parameters["button"] = event->button;
    for (const auto &modifier : MODIFIER_PARAMETERS) {
        parameters[modifier.name] = (event->modifiers & modifier.bit) ? 1 : 0;
    }
    return element(e)->DispatchEvent(type, parameters);
}

/* ---------- Data models ---------- */

struct rml_data_model
{
    rml_data_model(Rml::Context *context, const char *name, yyjson_mut_doc *doc)
      : context(context)
      , name(name)
      , items(json_items_new())
      , definition(doc, items)
    {
    }

    ~rml_data_model() { json_items_free(items); }

    Rml::Context *context;
    Rml::String name;
    Rml::DataModelHandle handle;
    json_items *items;
    JsonDefinition definition;
};

rml_data_model *rml_data_model_create(
    rml_context *c,
    const char *name,
    yyjson_mut_doc *doc,
    rml_emit_fn emit,
    void *user
)
{
    Rml::DataModelConstructor constructor = context(c)->CreateDataModel(name, nullptr, true);
    assert(constructor);
    auto *model = new rml_data_model(context(c), name, doc);
    model->handle = constructor.GetModelHandle();
    yyjson_mut_val *key, *value;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(yyjson_mut_doc_get_root(doc));
    while ((key = yyjson_mut_obj_iter_next(&iter))) {
        value = yyjson_mut_obj_iter_get_val(key);
        constructor.BindCustomDataVariable(
            yyjson_mut_get_str(key),
            Rml::DataVariable(&model->definition, value)
        );
    }
    constructor.BindEventCallback(
        "emit",
        [emit, user](Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &arguments) {
            rml_event e = convert(event);
            Rml::Vector<Rml::String> texts;
            for (const Rml::Variant &argument : arguments) {
                texts.push_back(argument.Get<Rml::String>());
            }
            Rml::Vector<const char *> args;
            for (size_t i = 1; i < texts.size(); i++) {
                args.push_back(texts[i].c_str());
            }
            emit(user, &e, texts.empty() ? "" : texts[0].c_str(), (int)args.size(), args.data());
        }
    );
    /* stop(), as Vue's .stop modifier: the event goes no further, so a
       button in a row keeps its click from the row's handler. */
    constructor.BindEventCallback(
        "stop",
        [](Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &) {
            event.StopPropagation();
        }
    );
    return model;
}

void rml_data_model_destroy(rml_data_model *model)
{
    model->context->RemoveDataModel(model->name);
    delete model;
}

void rml_data_model_bind(rml_data_model *model, const char *name, yyjson_mut_val *value)
{
    Rml::DataModelConstructor constructor = model->context->GetDataModel(model->name);
    bool bound = constructor && constructor.BindCustomDataVariable(
                                    name,
                                    Rml::DataVariable(&model->definition, value)
                                );
    assert(bound);
    (void)bound;
}

void rml_data_model_dirty(rml_data_model *model, const char *variable)
{
    json_items_clear(model->items);
    model->handle.DirtyVariable(variable);
}

void rml_data_model_dirty_all(rml_data_model *model)
{
    json_items_clear(model->items);
    model->handle.DirtyAllVariables();
}

uint64_t rml_data_model_writes(const rml_data_model *model)
{
    return model->definition.writes;
}

rml_data_reads rml_data_model_reads(const rml_data_model *model)
{
    return model->definition.reads;
}

void rml_element_set_data_model(rml_element *e, const char *name)
{
    Rml::Element *self = element(e), *parent = self->GetParentNode();
    assert(parent);
    Rml::Element *next = self->GetNextSibling();
    Rml::ElementPtr moved = parent->RemoveChild(self);
    moved->SetAttribute("data-model", Rml::String(name));
    parent->InsertBefore(std::move(moved), next);
}

/* ---------- Custom elements and geometry ---------- */

rml_instancer *rml_instancer_create(const rml_element_hooks *hooks)
{
    return reinterpret_cast<rml_instancer *>(new Instancer(hooks));
}

void rml_instancer_destroy(rml_instancer *instancer)
{
    delete unwrap<Instancer>(instancer);
}

void rml_register_element_instancer(const char *tag, rml_instancer *instancer)
{
    Rml::Factory::RegisterElementInstancer(tag, unwrap<Instancer>(instancer));
}

void rml_element_set_user(rml_element *e, void *user)
{
    custom(e)->user = user;
}

void *rml_element_user(const rml_element *e)
{
    return custom(e)->user;
}

rml_render_manager *rml_element_render_manager(const rml_element *e)
{
    return reinterpret_cast<rml_render_manager *>(element(e)->GetRenderManager());
}

rml_geometry *rml_geometry_create(rml_render_manager *manager, rml_mesh mesh)
{
    const auto *vertices = reinterpret_cast<const Rml::Vertex *>(mesh.vertices);
    Rml::Mesh m;
    m.vertices.assign(vertices, vertices + mesh.vertex_count);
    m.indices.assign(mesh.indices, mesh.indices + mesh.index_count);
    Rml::RenderManager *render = unwrap<Rml::RenderManager>(manager);
    return reinterpret_cast<rml_geometry *>(
        new Rml::Geometry(render->MakeGeometry(std::move(m)))
    );
}

void rml_geometry_render(const rml_geometry *geometry, rml_vec2 translation)
{
    unwrap<Rml::Geometry>(geometry)->Render({ translation.x, translation.y });
}

void rml_geometry_destroy(rml_geometry *geometry)
{
    delete unwrap<Rml::Geometry>(geometry);
}

/* ---------- Fonts ---------- */

uintptr_t rml_font_face(const char *family, rml_font_style style, int weight, int size)
{
    Rml::FontEngineInterface *fonts = Rml::GetFontEngineInterface();
    assert(fonts);
    return fonts->GetFontFaceHandle(family, (Rml::Style::FontStyle)style, (Rml::Style::FontWeight)weight, size);
}

int rml_font_string_width(uintptr_t face, const char *text)
{
    static const Rml::String language;
    Rml::TextShapingContext shaping{ language };
    Rml::StringView string(text, text + std::strlen(text));
    return Rml::GetFontEngineInterface()->GetStringWidth(face, string, shaping);
}

/* ---------- UI source maps (workstream A) ---------- */

#include <RmlUi/Core/StreamMemory.h>

namespace {

Rml::SharedPtr<Rml::StyleSheetContainer> named_sheet(const char *name, const char *rcss)
{
    Rml::StreamMemory stream(reinterpret_cast<const Rml::byte *>(rcss), std::strlen(rcss));
    stream.SetSourceURL(name);
    return Rml::Factory::InstanceStyleSheetStream(&stream);
}

} // namespace

bool rml_document_set_named_style_sheet(rml_element *d, const char *name, const char *rcss)
{
    Rml::SharedPtr<Rml::StyleSheetContainer> sheet = named_sheet(name, rcss);
    if (!sheet) {
        return false;
    }
    document(d)->SetStyleSheetContainer(sheet);
    return true;
}

bool rml_document_add_named_style_sheet(rml_element *d, const char *name, const char *rcss)
{
    Rml::SharedPtr<Rml::StyleSheetContainer> added = named_sheet(name, rcss);
    if (!added) {
        return false;
    }
    const Rml::StyleSheetContainer *current = document(d)->GetStyleSheetContainer();
    document(d)->SetStyleSheetContainer(
        current ? current->CombineStyleSheetContainer(*added) : added
    );
    return true;
}

bool rml_element_property_source(rml_element *e, const char *property, char *sheet, size_t size, int *line)
{
    const Rml::Property *p = element(e)->GetProperty(property);
    if (!p || !p->source) {
        return false;
    }
    copy(p->source->path, sheet, size);
    *line = p->source->line_number;
    return true;
}

/* ---------- UI language service (workstream B) ---------- */

const char *rml_property_name(uint8_t id)
{
    Rml::PropertyId property = (Rml::PropertyId)id;
    return Rml::StyleSheetSpecification::GetProperty(property)
             ? Rml::StyleSheetSpecification::GetPropertyName(property).c_str()
             : nullptr;
}

const char *rml_shorthand_name(uint8_t id)
{
    Rml::ShorthandId shorthand = (Rml::ShorthandId)id;
    return Rml::StyleSheetSpecification::GetShorthand(shorthand)
             ? Rml::StyleSheetSpecification::GetShorthandName(shorthand).c_str()
             : nullptr;
}

/* ---------- Inspection (the UI Inspector) ---------- */

int rml_context_document_count(const rml_context *c)
{
    return context(c)->GetNumDocuments();
}

rml_element *rml_context_document(rml_context *c, int index)
{
    return wrap(context(c)->GetDocument(index));
}

rml_element *rml_context_element_at_point(rml_context *c, rml_vec2 point, const rml_element *ignore)
{
    return wrap(context(c)->GetElementAtPoint({ point.x, point.y }, element(ignore)));
}

void rml_element_attributes(const rml_element *e, rml_attribute_fn fn, void *user)
{
    for (const auto &[name, value] : element(e)->GetAttributes()) {
        fn(user, name.c_str(), value.Get<Rml::String>().c_str());
    }
}

size_t rml_element_class_names(const rml_element *e, char *buffer, size_t size)
{
    return copy(element(e)->GetClassNames(), buffer, size);
}

size_t rml_element_pseudo_classes(const rml_element *e, char *buffer, size_t size)
{
    Rml::String names;
    for (const Rml::String &name : element(e)->GetActivePseudoClasses()) {
        names += (names.empty() ? "" : " ") + name;
    }
    return copy(names, buffer, size);
}

void rml_element_set_pseudo_class(rml_element *e, const char *name, bool on)
{
    element(e)->SetPseudoClass(name, on);
}

void rml_element_local_properties(
    const rml_element *e,
    rml_element *inherited_by,
    rml_property_fn fn,
    void *user
)
{
    for (auto it = element(e)->IterateLocalProperties(element(inherited_by));
         !it.AtEnd();
         ++it) {
        const auto &[name, property] = *it;
        const Rml::String value = property.ToString();
        const Rml::PropertySource *source = property.source.get();
        const rml_property_info info = {
            name.c_str(),
            value.c_str(),
            source ? source->path.c_str() : "",
            source ? source->line_number : 0,
            source ? source->rule_name.c_str() : "",
            property.specificity,
        };
        fn(user, &info);
    }
}

size_t rml_element_property(rml_element *e, const char *name, char *buffer, size_t size)
{
    const Rml::Property *property = element(e)->GetProperty(name);
    return copy(property ? property->ToString() : Rml::String(), buffer, size);
}

size_t rml_element_text(const rml_element *e, char *buffer, size_t size)
{
    auto *text = dynamic_cast<Rml::ElementText *>(element(e));
    assert(text);
    return copy(text->GetText(), buffer, size);
}

/* ---------- Text drawn by a custom element (workstream C, kit_code) ---------- */

struct rml_text
{
    Rml::Vector<std::pair<Rml::Geometry, Rml::Texture>> meshes;
};

rml_font_metrics rml_font_face_metrics(uintptr_t face)
{
    const Rml::FontMetrics &m = Rml::GetFontEngineInterface()->GetFontMetrics(face);
    return { m.ascent, m.descent, m.line_spacing };
}

int rml_font_version(uintptr_t face)
{
    return Rml::GetFontEngineInterface()->GetVersion(face);
}

int rml_font_string_width_n(uintptr_t face, const char *text, size_t size)
{
    static const Rml::String language;
    Rml::TextShapingContext shaping{ language };
    return Rml::GetFontEngineInterface()
        ->GetStringWidth(face, Rml::StringView(text, text + size), shaping);
}

rml_text *rml_text_create(rml_render_manager *manager, uintptr_t face, const char *text, size_t size, rml_color color)
{
    static const Rml::String language;
    Rml::TextShapingContext shaping{ language };
    Rml::FontEngineInterface *fonts = Rml::GetFontEngineInterface();
    Rml::RenderManager *render = unwrap<Rml::RenderManager>(manager);
    Rml::TexturedMeshList list;
    fonts->GenerateString(
        *render,
        face,
        fonts->PrepareFontEffects(face, {}),
        Rml::StringView(text, text + size),
        { 0, 0 },
        Rml::ColourbPremultiplied(color.r, color.g, color.b, color.a),
        1,
        shaping,
        list
    );
    auto *out = new rml_text;
    for (Rml::TexturedMesh &mesh : list) {
        out->meshes.emplace_back(render->MakeGeometry(std::move(mesh.mesh)), mesh.texture);
    }
    return out;
}

void rml_text_render(const rml_text *text, rml_vec2 translation)
{
    for (const auto &[geometry, texture] : text->meshes) {
        geometry.Render({ translation.x, translation.y }, texture);
    }
}

void rml_text_destroy(rml_text *text)
{
    delete text;
}

bool rml_render_scissor(const rml_render_manager *manager, rml_rect *region)
{
    Rml::Rectanglei r = unwrap<Rml::RenderManager>(manager)->GetScissorRegion();
    *region = { r.Left(), r.Top(), r.Width(), r.Height() };
    return r.Valid();
}

void rml_render_set_scissor(rml_render_manager *manager, const rml_rect *region)
{
    Rml::RenderManager *render = unwrap<Rml::RenderManager>(manager);
    if (!region) {
        render->DisableScissorRegion();
        return;
    }
    render->SetScissorRegion(
        Rml::Rectanglei::FromPositionSize(
            { region->x, region->y },
            { region->width, region->height }
        )
    );
}

size_t rml_event_text(const rml_event *event, char *buffer, size_t size)
{
    auto *e = static_cast<Rml::Event *>(event->native);
    return copy(e->GetParameter<Rml::String>("text", Rml::String()), buffer, size);
}

rml_vec2 rml_event_wheel(const rml_event *event)
{
    auto *e = static_cast<Rml::Event *>(event->native);
    return { e->GetParameter<float>("wheel_delta_x", 0),
             e->GetParameter<float>("wheel_delta_y", 0) };
}

/* ---------- Code intelligence (workstream I) ---------- */

SAME_KEY(OEM_1);
SAME_KEY(OEM_COMMA);
SAME_KEY(OEM_2);
SAME_KEY(OEM_4);
SAME_KEY(OEM_6);

bool rml_property_describe(const char *name, char *initial, size_t size, bool *inherited, bool *shorthand)
{
    const Rml::PropertyDefinition *property = Rml::StyleSheetSpecification::GetProperty(name);
    *shorthand = !property && Rml::StyleSheetSpecification::GetShorthand(name);
    *inherited = property && property->IsInherited();
    const Rml::Property *value = property ? property->GetDefaultValue() : nullptr;
    copy(value ? value->ToString() : Rml::String(), initial, size);
    return property || *shorthand;
}

unsigned rml_font_generation()
{
    return font_generation;
}

/* ---------- Wave 2 follow-ups (workstream P) ---------- */

bool rml_data_model_take_written(rml_data_model *model, yyjson_mut_val **value)
{
    Rml::Vector<yyjson_mut_val *> &written = model->definition.written;
    if (written.empty()) {
        return false;
    }
    *value = written.front();
    written.erase(written.begin());
    return true;
}

/* ---------- Native elements for authored UI (workstream N) ---------- */

double rml_elapsed_time()
{
    return Rml::GetSystemInterface()->GetElapsedTime();
}

void rml_context_request_next_update(rml_context *c, double delay)
{
    context(c)->RequestNextUpdate(delay);
}

bool rml_context_theme_active(const rml_context *c, const char *name)
{
    return context(c)->IsThemeActive(name);
}

void rml_element_set_text(rml_element *e, const char *text)
{
    auto *node = dynamic_cast<Rml::ElementText *>(element(e));
    assert(node);
    node->SetText(text);
}

void rml_element_suppress_text_layout(rml_element *e)
{
    auto *node = dynamic_cast<Rml::ElementText *>(element(e));
    assert(node);
    node->SuppressAutoLayout();
}

void rml_element_clear_text_lines(rml_element *e)
{
    auto *node = dynamic_cast<Rml::ElementText *>(element(e));
    assert(node);
    node->ClearLines();
}

void rml_element_add_text_line(rml_element *e, rml_vec2 offset, const char *text)
{
    auto *node = dynamic_cast<Rml::ElementText *>(element(e));
    assert(node);
    node->AddLine({ offset.x, offset.y }, text);
}

int rml_element_string_width(rml_element *e, const char *text)
{
    return Rml::ElementUtilities::GetStringWidth(
        element(e),
        Rml::StringView(text, text + std::strlen(text))
    );
}

struct rml_element_ref
{
    Rml::ObserverPtr<Rml::Element> element;
};

rml_element_ref *rml_element_ref_create(rml_element *e)
{
    return new rml_element_ref{ element(e)->GetObserverPtr() };
}

rml_element *rml_element_ref_get(const rml_element_ref *ref)
{
    return wrap(ref->element.get());
}

void rml_element_ref_destroy(rml_element_ref *ref)
{
    delete ref;
}

static rml_data_if_fn data_if_fn;

static bool data_if(Rml::Element *e, bool show, bool first)
{
    return data_if_fn(wrap(e), show, first);
}

void rml_set_data_if_handler(rml_data_if_fn fn)
{
    data_if_fn = fn;
    Rml::SetDataIfHandler(fn ? data_if : nullptr);
}

/* ---------- One document per area (workstream F5) ---------- */

void rml_document_copy_style_sheet(rml_element *d, rml_element *source)
{
    const Rml::StyleSheetContainer *sheet = document(source)->GetStyleSheetContainer();
    if (sheet) {
        document(d)->SetStyleSheetContainer(
            sheet->CombineStyleSheetContainer(Rml::StyleSheetContainer())
        );
    }
}

/* ---------- Native controls (workstream F2) ---------- */

void rml_element_dispatch_value(rml_element *e, const char *type, const char *value, bool canceled)
{
    Rml::Dictionary parameters;
    parameters["value"] = Rml::String(value);
    parameters["canceled"] = canceled;
    parameters["name"] = element(e)->GetAttribute<Rml::String>("name", "");
    element(e)->DispatchEvent(type, parameters, true, true);
}

void rml_element_add_named_listener(rml_element *e, const char *type, rml_listener *l, bool capture)
{
    element(e)->AddEventListener(type, unwrap<Listener>(l), capture);
}

/* ---------- Lists and drag (workstream F4) ---------- */

namespace {

/* Forwards a context's creation and destruction; RmlUi unregisters it at
 * shutdown, which frees it. */
class ContextHooks final : public Rml::Plugin
{
public:
    explicit ContextHooks(const rml_context_hooks *hooks)
      : hooks(hooks)
    {
    }

    int GetEventClasses() override { return EVT_BASIC; }

    void OnContextCreate(Rml::Context *c) override
    {
        if (hooks->create) {
            hooks->create(wrap(c));
        }
    }

    void OnContextDestroy(Rml::Context *c) override
    {
        if (hooks->destroy) {
            hooks->destroy(wrap(c));
        }
    }

    void OnShutdown() override { delete this; }

private:
    const rml_context_hooks *hooks;
};

} // namespace

void rml_add_context_hooks(const rml_context_hooks *hooks)
{
    Rml::RegisterPlugin(new ContextHooks(hooks));
}

/* ---------- Members (workstream P5) ---------- */

namespace {

/* Forwards each element's creation; RmlUi unregisters it at shutdown,
 * which frees it. */
class ElementCreated final : public Rml::Plugin
{
public:
    explicit ElementCreated(void (*created)(rml_element *))
      : created(created)
    {
    }

    int GetEventClasses() override { return EVT_BASIC | EVT_ELEMENT; }

    void OnElementCreate(Rml::Element *e) override { created(wrap(e)); }

    void OnShutdown() override { delete this; }

private:
    void (*created)(rml_element *);
};

} // namespace

void rml_add_element_created(void (*created)(rml_element *element))
{
    Rml::RegisterPlugin(new ElementCreated(created));
}

rml_element *rml_context_root(rml_context *c)
{
    return wrap(context(c)->GetRootElement());
}

bool rml_element_dispatch_texts(
    rml_element *e,
    const char *type,
    int count,
    const char *const *names,
    const char *const *values
)
{
    Rml::Dictionary parameters;
    for (int i = 0; i < count; i++) {
        parameters[names[i]] = Rml::String(values[i]);
    }
    return element(e)->DispatchEvent(type, parameters, true, true);
}
