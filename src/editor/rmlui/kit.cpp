/* Studio kit: immediate-mode calls reconciled onto a retained RmlUi document.
 * This file keys, updates and retires nodes and routes RmlUi events to the
 * controllers in kit_number.cpp, kit_controls.cpp, kit_popup.cpp and
 * kit_command.cpp. They implement the Base UI contracts recorded in
 * docs/prototypes/studio/kit.html; the C editor owns every model value. */
#include "editor/rmlui/kit_internal.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/TextShapingContext.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace kit_impl {

std::string escape(const char *text)
{
    std::string out;
    for (const char *c = text ? text : ""; *c; ++c) {
        switch (*c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += *c;
        }
    }
    return out;
}

std::string utf8(uint32_t cp)
{
    std::string out;
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xc0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += (char)(0xe0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    } else {
        out += (char)(0xf0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3f));
        out += (char)(0x80 | ((cp >> 6) & 0x3f));
        out += (char)(0x80 | (cp & 0x3f));
    }
    return out;
}

std::string format_number(double v, int precision, bool integer)
{
    if (!std::isfinite(v)) {
        return "-";
    }
    char buffer[64];
    if (integer) {
        std::snprintf(buffer, sizeof buffer, "%.0f", v);
        return buffer;
    }
    std::snprintf(buffer, sizeof buffer, "%.*f", std::max(precision, 0), v);
    std::string s = buffer;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    if (s == "-0") {
        s = "0";
    }
    return s;
}

bool rect_contains(kit_rect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

} // namespace


namespace kit_impl {

float dp(const kit *k)
{
    return k->in.dp_ratio > 0 ? k->in.dp_ratio : 1;
}

kit_rect element_rect(const kit *k, Rml::Element *el)
{
    if (!el) {
        return {};
    }
    Rml::Vector2f offset = el->GetAbsoluteOffset(Rml::BoxArea::Border);
    Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Border);
    float s = dp(k);
    return { offset.x / s, offset.y / s, size.x / s, size.y / s };
}

void set_class(Node *n, const std::string &cls)
{
    if (n->cls != cls) {
        n->cls = cls;
        n->el->SetClassNames(cls);
    }
}

void set_attr(Node *n, const std::string &name, const std::string &value)
{
    auto found = n->attrs.find(name);
    if (found != n->attrs.end() && found->second == value) {
        return;
    }
    n->attrs[name] = value;
    n->el->SetAttribute(name, value);
}

void set_flag(Node *n, const std::string &name, bool on)
{
    auto found = n->attrs.find(name);
    bool has = found != n->attrs.end();
    if (has == on) {
        return;
    }
    if (on) {
        n->attrs[name] = "";
        n->el->SetAttribute(name, Rml::String());
    } else {
        n->attrs.erase(found);
        n->el->RemoveAttribute(name);
    }
}

void flag_element(Rml::Element *el, const char *name, bool on)
{
    if (on == el->HasAttribute(name)) {
        return;
    }
    if (on) {
        el->SetAttribute(name, Rml::String());
    } else {
        el->RemoveAttribute(name);
    }
}

void set_text(Node *n, const char *text)
{
    std::string t = text ? text : "";
    if (n->text != t) {
        n->text = t;
        n->el->SetInnerRML(escape(t.c_str()));
    }
}

void set_element_text(Rml::Element *el, const std::string &text)
{
    if (el->GetInnerRML() != escape(text.c_str())) {
        el->SetInnerRML(escape(text.c_str()));
    }
}

Rml::Element *raw(kit *k, Rml::Element *parent, const char *tag, const char *cls, const char *text)
{
    Rml::ElementPtr ptr = k->doc->CreateElement(tag);
    ptr->SetClassNames(cls);
    if (text) {
        ptr->SetInnerRML(escape(text));
    }
    return parent->AppendChild(std::move(ptr));
}

Rml::Element *raw_icon(kit *k, Rml::Element *parent, const char *cls, uint32_t cp)
{
    std::string c = std::string("icon ") + (cls ? cls : "");
    Rml::Element *el = raw(k, parent, "span", c.c_str());
    if (cp) {
        el->SetInnerRML(utf8(cp));
    }
    return el;
}

void unmap(kit *k, Node *n)
{
    for (auto &child : n->children) {
        unmap(k, child.get());
    }
    forget_motion(k, n);
    k->by_element.erase(n->el);
    k->by_path.erase(n->path);
    if (k->last == n) {
        k->last = nullptr;
    }
    for (Node *&t : k->triggers) {
        if (t == n) {
            t = nullptr;
        }
    }
    for (auto &[key, popup] : k->popups) {
        (void)key;
        if (popup.node == n) {
            popup.node = nullptr;
        }
        popup.items.erase(std::remove(popup.items.begin(), popup.items.end(), n), popup.items.end());
    }
}

void prune(kit *k, Node *n)
{
    while (n->children.size() > n->cursor) {
        std::unique_ptr<Node> child = std::move(n->children.back());
        n->children.pop_back();
        retire(k, std::move(child));
    }
}

Node *emit_in(kit *k, Node *parent, const char *key, Kind kind, const char *tag, const char *cls)
{
    assert(parent && key && key[0]);
    const char *element_tag = tag && tag[0] ? tag : "div";
    size_t i = parent->cursor;
    Node *n = nullptr;
    for (size_t j = i; j < parent->children.size(); ++j) {
        Node *candidate = parent->children[j].get();
        if (candidate->key != key) {
            continue;
        }
        if (candidate->kind != kind || candidate->tag != element_tag) {
            std::unique_ptr<Node> replaced = std::move(parent->children[j]);
            parent->children.erase(parent->children.begin() + (long)j);
            replaced->presence = false;
            retire(k, std::move(replaced));
            break;
        }
        if (j != i) {
            Rml::ElementPtr moved = parent->container->RemoveChild(candidate->el);
            parent->container->InsertBefore(std::move(moved), parent->children[i]->el);
            std::rotate(
                parent->children.begin() + (long)i,
                parent->children.begin() + (long)j,
                parent->children.begin() + (long)j + 1
            );
        }
        n = parent->children[i].get();
        break;
    }
    if (!n) {
        auto created = std::make_unique<Node>();
        created->key = key;
        created->kind = kind;
        created->tag = element_tag;
        created->parent = parent;
        created->path = parent->path.empty() ? key : parent->path + "/" + key;
        created->created = k->frame;
        Rml::ElementPtr ptr = k->doc->CreateElement(element_tag);
        Rml::Element *ref = i < parent->children.size() ? parent->children[i]->el : nullptr;
        created->el = ref ? parent->container->InsertBefore(std::move(ptr), ref)
                          : parent->container->AppendChild(std::move(ptr));
        created->container = created->el;
        n = created.get();
        k->by_element[n->el] = n;
        k->by_path[n->path] = n;
        parent->children.insert(parent->children.begin() + (long)i, std::move(created));
    }
    parent->cursor = i + 1;
    n->frame = k->frame;
    n->drop_type.clear();
    set_class(n, cls ? cls : "");
    k->last = n;
    return n;
}

Node *emit(kit *k, const char *key, Kind kind, const char *tag, const char *cls)
{
    assert(!k->stack.empty());
    return emit_in(k, k->stack.back(), key, kind, tag, cls);
}

void push_node(kit *k, Node *n)
{
    n->cursor = 0;
    k->stack.push_back(n);
}

void pop_node(kit *k)
{
    assert(k->stack.size() > 1);
    Node *n = k->stack.back();
    prune(k, n);
    k->stack.pop_back();
    k->last = n;
}

Node *node_for(kit *k, Rml::Element *el)
{
    for (; el; el = el->GetParentNode()) {
        auto found = k->by_element.find(el);
        if (found != k->by_element.end()) {
            return found->second;
        }
    }
    return nullptr;
}

/* Picker content lives in a layer popup; route its events to the owning field. */
static Node *owner(kit *k, Node *n)
{
    if (n && n->kind == Kind::Popup && n->key.rfind("@color:", 0) == 0) {
        auto found = k->by_path.find(n->key.substr(7));
        if (found != k->by_path.end()) {
            return found->second;
        }
    }
    return n;
}

bool within(Rml::Element *el, Rml::Element *ancestor)
{
    for (; el; el = el->GetParentNode()) {
        if (el == ancestor) {
            return true;
        }
    }
    return false;
}

unsigned event_modifiers(Rml::Event &e)
{
    return (e.GetParameter<int>("ctrl_key", 0) ? KIT_MOD_CTRL : 0u) |
           (e.GetParameter<int>("shift_key", 0) ? KIT_MOD_SHIFT : 0u) |
           (e.GetParameter<int>("alt_key", 0) ? KIT_MOD_ALT : 0u) |
           (e.GetParameter<int>("meta_key", 0) ? KIT_MOD_META : 0u);
}

} // namespace

using namespace kit_impl;

/* ---------- Event routing ---------- */

class KitListener final : public Rml::EventListener
{
public:
    explicit KitListener(kit *k)
      : k_(k)
    {
    }
    void ProcessEvent(Rml::Event &e) override;

private:
    void mousedown(Rml::Event &e, Node *n, int button);
    void click(Rml::Event &e, Node *n);
    void keydown(Rml::Event &e, Node *n);
    void drag(Rml::Event &e, Node *n, Rml::EventId id);
    kit *k_;
};

void KitListener::mousedown(Rml::Event &e, Node *n, int button)
{
    kit *k = k_;
    Rml::Element *target = e.GetTargetElement();
    popup_press(k, n, target);
    command_press(k, target);
    if (!n) {
        return;
    }
    if (button == 1) {
        n->events |= EV_CONTEXT;
        return;
    }
    if (button != 0) {
        return;
    }
    n->events |= EV_PRESS;
    k->keyboard_edit = false;
    switch (n->kind) {
        case Kind::Number: number_press(k, n, e); break;
        case Kind::Trigger: trigger_press(k, n, e); break;
        default: control_press(k, n, e); break;
    }
}

void KitListener::click(Rml::Event &e, Node *n)
{
    kit *k = k_;
    if (k->drag.active) {
        return;
    }
    switch (n->kind) {
        case Kind::Number: number_click(k, n); return;
        case Kind::MenuItem:
        case Kind::Submenu: menu_click(k, n); return;
        case Kind::CommandItem: command_click(k, n); return;
        case Kind::Box:
            /* A click on a label or icon is also a click on the boxes around it,
               as DOM clicks bubble, so a whole row or tab answers. */
            for (Node *box = n; box && box->kind == Kind::Box; box = box->parent) {
                box->events |= EV_CLICK;
            }
            return;
        default: control_click(k, n, e.GetTargetElement()); return;
    }
}

void KitListener::drag(Rml::Event &e, Node *n, Rml::EventId id)
{
    if (n->kind == Kind::Number) {
        number_drag(k_, n, e, id);
        return;
    }
    control_drag(k_, n, e, id);
}

void KitListener::keydown(Rml::Event &e, Node *n)
{
    kit *k = k_;
    int key = e.GetParameter<int>("key_identifier", 0);
    k->keyboard_edit = true;
    if (k->drag.active && key == Rml::Input::KI_ESCAPE) {
        drag_cancel(k);
        e.StopPropagation();
        return;
    }
    if (command_key(k, e, key) || popup_key(k, e, key) || !n) {
        return;
    }
    if (n->kind == Kind::Number) {
        number_key(k, n, e, key);
        return;
    }
    control_key(n, e, key);
}

void KitListener::ProcessEvent(Rml::Event &e)
{
    kit *k = k_;
    Rml::Element *target = e.GetTargetElement();
    Node *n = owner(k, node_for(k, target));
    switch (e.GetId()) {
        case Rml::EventId::Mousedown: mousedown(e, n, e.GetParameter<int>("button", 0)); break;
        case Rml::EventId::Mouseup:
            if (n && n->kind == Kind::Number) {
                number_release(n);
            }
            break;
        case Rml::EventId::Click:
            if (n) {
                click(e, n);
            }
            break;
        case Rml::EventId::Dblclick:
            if (n) {
                n->events |= EV_DOUBLE;
            }
            break;
        case Rml::EventId::Dragstart:
        case Rml::EventId::Drag:
        case Rml::EventId::Dragend: {
            Rml::Element *dragged = static_cast<Rml::Element *>(e.GetParameter<void *>("drag_element", nullptr));
            Node *d = owner(k, node_for(k, dragged ? dragged : target));
            if (d) {
                drag(e, d, e.GetId());
            }
            break;
        }
        case Rml::EventId::Keydown: keydown(e, n); break;
        case Rml::EventId::Change:
            if (!command_change(k, target) && n) {
                control_change(n, target);
            }
            break;
        case Rml::EventId::Focus:
            if (n) {
                control_focus(n, target);
            }
            break;
        case Rml::EventId::Blur:
            if (n && n->kind == Kind::Number) {
                number_blur(k, n, target);
            } else if (n) {
                control_blur(n, target);
            }
            break;
        case Rml::EventId::Mouseover:
            if (n && n->kind == Kind::CommandItem) {
                command_hover(k, n);
            } else if (n) {
                menu_hover(k, n);
            }
            break;
        default: break;
    }
}

/* ---------- API ---------- */

#ifndef CHIBI_RMLUI_RESOURCE_DIR
#define CHIBI_RMLUI_RESOURCE_DIR "src/editor/rmlui/resources"
#endif

extern "C" kit *kit_create(void *context, const char *document_path)
{
    auto *k = new kit();
    k->context = static_cast<Rml::Context *>(context);
    k->doc = k->context->LoadDocument(std::string(CHIBI_RMLUI_RESOURCE_DIR) + "/" + document_path);
    assert(k->doc);
    k->doc->Show();
    register_chart_element();
    register_wires_element();
    register_dot_element();
    k->listener = std::make_unique<KitListener>(k);
    const Rml::EventId events[] = {
        Rml::EventId::Mousedown, Rml::EventId::Mouseup,   Rml::EventId::Click,   Rml::EventId::Dblclick,
        Rml::EventId::Dragstart, Rml::EventId::Drag,      Rml::EventId::Dragend, Rml::EventId::Keydown,
        Rml::EventId::Change,    Rml::EventId::Mouseover, Rml::EventId::Focus,   Rml::EventId::Blur,
    };
    for (Rml::EventId id : events) {
        k->doc->AddEventListener(id, k->listener.get(), true);
    }
    Rml::Element *layer = k->doc->GetElementById("layer");
    assert(layer);
    auto layer_node = std::make_unique<Node>();
    layer_node->kind = Kind::Root;
    layer_node->el = layer_node->container = layer;
    layer_node->path = "layer";
    k->layer = layer_node.get();
    k->by_element[layer] = k->layer;
    k->roots["layer"] = std::move(layer_node);
    k->tooltip = raw(k, layer, "div", "tooltip");
    k->tooltip->SetProperty("display", "none");
    return k;
}

extern "C" void kit_destroy(kit *k)
{
    if (!k) {
        return;
    }
    const Rml::EventId events[] = {
        Rml::EventId::Mousedown, Rml::EventId::Mouseup,   Rml::EventId::Click,   Rml::EventId::Dblclick,
        Rml::EventId::Dragstart, Rml::EventId::Drag,      Rml::EventId::Dragend, Rml::EventId::Keydown,
        Rml::EventId::Change,    Rml::EventId::Mouseover, Rml::EventId::Focus,   Rml::EventId::Blur,
    };
    for (Rml::EventId id : events) {
        k->doc->RemoveEventListener(id, k->listener.get(), true);
    }
    k->doc->Close();
    delete k;
}

static void close_root(kit *k)
{
    if (!k->root) {
        return;
    }
    while (k->stack.size() > 1) {
        pop_node(k);
    }
    prune(k, k->root);
    k->stack.clear();
    k->root = nullptr;
}

extern "C" void kit_begin(kit *k, const kit_frame_input *input)
{
    k->in = *input;
    k->triggers.clear();
    place_popups(k);
    drag_begin_frame(k);
    repeat_steppers(k);
    open_hovered_submenus(k);
    k->layer->cursor = 0;
    k->layer->frame = k->frame;
}

extern "C" void kit_end(kit *k)
{
    close_root(k);
    close_activated_menus(k);
    toasts_frame(k);
    drag_end_frame(k);
    prune(k, k->layer);
    if (!k->command.open) {
        k->command_backdrop = nullptr;
    }
    update_tooltip(k);
    motion_frame(k);
    for (auto &[path, n] : k->by_path) {
        (void)path;
        n->events = 0;
        n->w.committed = false;
    }
    k->strings.clear();
    k->frame++;
}

extern "C" void kit_root(kit *k, const char *element_id)
{
    close_root(k);
    auto found = k->roots.find(element_id);
    if (found == k->roots.end()) {
        Rml::Element *el = k->doc->GetElementById(element_id);
        assert(el && "kit_root element must exist in the document");
        auto root = std::make_unique<Node>();
        root->kind = Kind::Root;
        root->el = root->container = el;
        root->path = element_id;
        k->by_element[el] = root.get();
        k->by_path[root->path] = root.get();
        found = k->roots.emplace(element_id, std::move(root)).first;
    }
    k->root = found->second.get();
    k->root->frame = k->frame;
    push_node(k, k->root);
}

extern "C" void kit_push(kit *k, const char *key, const char *tag, const char *cls)
{
    push_node(k, emit(k, key, Kind::Box, tag, cls));
}

extern "C" void kit_pop(kit *k)
{
    pop_node(k);
}

extern "C" void kit_leaf(kit *k, const char *key, const char *tag, const char *cls)
{
    emit(k, key, Kind::Box, tag, cls);
}

extern "C" void kit_label(kit *k, const char *key, const char *cls, const char *text)
{
    Node *n = emit(k, key, Kind::Box, "span", cls);
    set_text(n, text);
}

extern "C" void kit_icon(kit *k, const char *key, const char *cls, uint32_t codepoint)
{
    std::string c = std::string("icon ") + (cls ? cls : "");
    Node *n = emit(k, key, Kind::Box, "span", c.c_str());
    set_text(n, utf8(codepoint).c_str());
}

extern "C" void kit_markup(kit *k, const char *key, const char *cls, const char *rml)
{
    Node *n = emit(k, key, Kind::Box, "span", cls);
    std::string markup = rml ? rml : "";
    if (n->text != markup) {
        n->text = markup;
        n->el->SetInnerRML(markup);
    }
}

extern "C" void kit_text(kit *k, const char *text)
{
    assert(k->last);
    set_text(k->last, text);
}

extern "C" void kit_attr(kit *k, const char *name, const char *value)
{
    assert(k->last);
    set_attr(k->last, name, value ? value : "");
}

extern "C" void kit_flag(kit *k, const char *name, bool on)
{
    assert(k->last);
    set_flag(k->last, name, on);
}

extern "C" void kit_style(kit *k, const char *property, const char *value)
{
    Node *n = k->last;
    assert(n);
    auto found = n->styles.find(property);
    if (found != n->styles.end() && found->second == value) {
        return;
    }
    n->styles[property] = value;
    n->el->SetProperty(property, value);
}

extern "C" bool kit_item_clicked(kit *k)
{
    return k->last && (k->last->events & EV_CLICK);
}

extern "C" bool kit_item_double_clicked(kit *k)
{
    return k->last && (k->last->events & EV_DOUBLE);
}

extern "C" bool kit_item_context_clicked(kit *k)
{
    return k->last && (k->last->events & EV_CONTEXT);
}

extern "C" bool kit_item_pressed(kit *k)
{
    return k->last && (k->last->events & EV_PRESS);
}

extern "C" bool kit_item_hovered(kit *k)
{
    return k->last && k->last->el->IsPseudoClassSet("hover");
}

extern "C" bool kit_item_active(kit *k)
{
    if (!k->last) {
        return false;
    }
    const Widget &w = k->last->w;
    bool picker = k->last->kind == Kind::Color && k->popups["color:" + k->last->path].open;
    return w.scrubbing || w.editing || w.step_dir || w.focused || picker ||
           (w.pressing && k->last->kind != Kind::Number);
}

extern "C" bool kit_item_committed(kit *k)
{
    return k->last && k->last->w.committed;
}

extern "C" kit_rect kit_item_rect(kit *k)
{
    return k->last ? element_rect(k, k->last->el) : kit_rect{};
}

extern "C" unsigned kit_modifiers(const kit *k)
{
    return k->in.modifiers;
}

extern "C" const char *kit_string(kit *k, const char *text)
{
    k->strings.emplace_back(text ? text : "");
    return k->strings.back().c_str();
}

extern "C" const char *kit_classes(kit *k, const char *base, const kit_class *list, int count)
{
    std::string cls = base ? base : "";
    for (int i = 0; i < count; ++i) {
        if (list[i].on && list[i].name && list[i].name[0]) {
            cls += cls.empty() ? "" : " ";
            cls += list[i].name;
        }
    }
    k->strings.push_back(std::move(cls));
    return k->strings.back().c_str();
}

/* ---------- Host queries ---------- */

extern "C" bool kit_wants_keyboard(const kit *k)
{
    if (k->command.open || !k->popup_stack.empty()) {
        return true;
    }
    Rml::Element *focus = k->context->GetFocusElement();
    if (!focus) {
        return false;
    }
    /* A focused number field handles its own keys during RmlUi dispatch; global
       shortcuts still apply unless text is being edited. */
    return focus->GetTagName() == "input";
}

extern "C" bool kit_accepts_text(const kit *k)
{
    Rml::Element *focus = k->context->GetFocusElement();
    if (!focus) {
        return false;
    }
    auto found = k->by_element.find(focus);
    return focus->GetTagName() == "input" || (found != k->by_element.end() && found->second->kind == Kind::Number);
}

extern "C" bool kit_wants_text(const kit *k, kit_rect *caret)
{
    Rml::Element *focus = k->context->GetFocusElement();
    if (!focus || focus->GetTagName() != "input") {
        return false;
    }
    if (caret) {
        *caret = element_rect(k, focus);
    }
    return true;
}

extern "C" bool kit_wants_pointer(const kit *k)
{
    if (k->command.open || !k->popup_stack.empty()) {
        return true;
    }
    if (!k->surface_capture.empty()) {
        return false;
    }
    Rml::Element *hover = k->context->GetHoverElement();
    auto found = k->by_element.find(hover);
    return hover && hover != k->doc && (found == k->by_element.end() || found->second->kind != Kind::Surface);
}

/* Measured with the face RmlUi draws at this ratio: it truncates font sizes to
   whole pixels (ElementStyle) and hints glyph advances. */
extern "C" float kit_text_width(const kit *, const char *text, float size, int weight, float ratio)
{
    assert(ratio > 0);
    Rml::FontEngineInterface *fonts = Rml::GetFontEngineInterface();
    int pixels = (int)(size * ratio);
    if (!fonts || !text || !text[0] || pixels < 1) {
        return 0;
    }
    Rml::FontFaceHandle face = fonts->GetFontFaceHandle(
        "inter", /* the font provider keys families in lower case */
        Rml::Style::FontStyle::Normal,
        (Rml::Style::FontWeight)(weight > 0 ? weight : 400),
        pixels
    );
    if (!face) {
        return 0;
    }
    static const Rml::String language;
    Rml::TextShapingContext shaping{ language };
    const Rml::String string = text;
    return (float)fonts->GetStringWidth(face, Rml::StringView(string), shaping) / ratio;
}

extern "C" void kit_focus(kit *k)
{
    assert(k->last);
    Rml::Element *el = k->last->w.input_el ? k->last->w.input_el : k->last->el;
    el->Focus(true);
    if (auto *input = dynamic_cast<Rml::ElementFormControlInput *>(el)) {
        input->Select();
    }
}

extern "C" void kit_blur(kit *k)
{
    if (Rml::Element *focus = k->context->GetFocusElement(); focus && focus != k->doc) {
        focus->Blur();
    }
}

extern "C" const char *kit_hovered_path(const kit *k)
{
    Node *n = node_for(const_cast<kit *>(k), k->context->GetHoverElement());
    return n && n->kind != Kind::Root ? n->path.c_str() : nullptr;
}

extern "C" bool kit_pointer_on_control(const kit *k)
{
    for (Node *n = node_for(const_cast<kit *>(k), k->context->GetHoverElement()); n; n = n->parent) {
        switch (n->kind) {
            case Kind::Button:
            case Kind::Number:
            case Kind::Check:
            case Kind::Toggle:
            case Kind::Select:
            case Kind::Text:
            case Kind::Search:
            case Kind::Color: return true;
            default: break;
        }
    }
    return false;
}

extern "C" const char *kit_item_path(const kit *k)
{
    return k->last ? k->last->path.c_str() : nullptr;
}

extern "C" bool kit_find(const kit *k, const char *path, kit_rect *out)
{
    auto found = k->by_path.find(path);
    if (found == k->by_path.end()) {
        return false;
    }
    *out = element_rect(k, found->second->el);
    return out->w > 0 && out->h > 0;
}

extern "C" void kit_set_theme(kit *k, const char *name, bool active)
{
    k->context->ActivateTheme(name, active);
    if (std::strcmp(name, "reduced-motion") == 0) {
        k->reduced_motion = active;
    }
}

extern "C" void kit_set_body_class(kit *k, const char *name, bool on)
{
    k->doc->SetClass(name, on);
}
