/* The RmlUi C binding (rml.h): type conversions and forwarding only. */
#include "ui/rml.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/TextShapingContext.h>
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

Rml::ElementFormControlInput *input(rml_element *handle)
{
    auto *input = dynamic_cast<Rml::ElementFormControlInput *>(element(handle));
    assert(input);
    return input;
}

rml_vec2 vec2(Rml::Vector2f v)
{
    return { v.x, v.y };
}

rml_color color(Rml::Colourb c)
{
    return { c.red, c.green, c.blue, c.alpha };
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

private:
    const rml_render_interface *fn;
    void *user;
};

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
        static const struct
        {
            const char *name;
            unsigned bit;
        } modifiers[] = {
            { "ctrl_key", RML_KM_CTRL },
            { "shift_key", RML_KM_SHIFT },
            { "alt_key", RML_KM_ALT },
            { "meta_key", RML_KM_META },
            { "caps_lock_key", RML_KM_CAPSLOCK },
            { "num_lock_key", RML_KM_NUMLOCK },
            { "scroll_lock_key", RML_KM_SCROLLLOCK },
        };
        rml_event e = {};
        e.id = (rml_event_id)event.GetId();
        e.target = wrap(event.GetTargetElement());
        e.current = wrap(event.GetCurrentElement());
        e.drag_element = wrap(
            static_cast<Rml::Element *>(event.GetParameter<void *>("drag_element", nullptr))
        );
        e.mouse_x = event.GetParameter<float>("mouse_x", 0);
        e.mouse_y = event.GetParameter<float>("mouse_y", 0);
        e.button = event.GetParameter<int>("button", 0);
        e.key = (rml_key)event.GetParameter<int>("key_identifier", 0);
        for (const auto &modifier : modifiers) {
            if (event.GetParameter<int>(modifier.name, 0)) {
                e.modifiers |= modifier.bit;
            }
        }
        e.native = &event;
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
    void OnRender() override { hooks->render(wrap(this), user); }

    void OnResize() override
    {
        if (hooks->resize) {
            hooks->resize(wrap(this), user);
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

void rml_release_render_managers()
{
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

bool rml_context_update(rml_context *c)
{
    return context(c)->Update();
}

bool rml_context_render(rml_context *c)
{
    return context(c)->Render();
}

rml_element *rml_context_load_document(rml_context *c, const char *path)
{
    return wrap(context(c)->LoadDocument(path));
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

void rml_document_reload_style_sheet(rml_element *d)
{
    document(d)->ReloadStyleSheet();
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
    Rml::Element *c = element(child);
    Rml::ElementPtr moved = c->GetParentNode()->RemoveChild(c);
    element(parent)->InsertBefore(std::move(moved), element(before));
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
    input(e)->Select();
}

void rml_element_set_selection_range(rml_element *e, int start, int end)
{
    input(e)->SetSelectionRange(start, end);
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
