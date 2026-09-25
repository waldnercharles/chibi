/* Controls after Base UI: buttons, checkboxes, toggle groups and selects, text
 * and search fields, color pickers, panels, property and tree rows, splitters and
 * surfaces. Number fields are in kit_number.cpp. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kit_impl {

/* ---------- Color ---------- */

static void rgb_to_hsv(uint32_t c, float hsv[3])
{
    float r = ((c >> 24) & 255) / 255.f, g = ((c >> 16) & 255) / 255.f, b = ((c >> 8) & 255) / 255.f;
    float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
    float h = 0;
    if (d > 0) {
        h = mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    }
    hsv[0] = std::fmod(h * 60 + 360, 360.f);
    hsv[1] = mx > 0 ? d / mx : 0;
    hsv[2] = mx;
}

static uint32_t hsv_to_rgb(const float hsv[3], uint32_t alpha)
{
    auto f = [&](float n) {
        float k = std::fmod(n + hsv[0] / 60, 6.f);
        return hsv[2] - hsv[2] * hsv[1] * std::max(0.f, std::min({ k, 4 - k, 1.f }));
    };
    auto byte = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 255); };
    return (byte(f(5)) << 24) | (byte(f(3)) << 16) | (byte(f(1)) << 8) | (alpha & 255);
}

static std::string hex_color(uint32_t c)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255);
    return buffer;
}

static void color_drag(kit *k, Node *n, float mx, float my)
{
    Node *popup = k->popups["color:" + n->path].node;
    if (!popup) {
        return;
    }
    Rml::Element *part = popup->el->QuerySelector(n->w.drag_part == 1 ? ".sv" : ".hue");
    if (!part) {
        return;
    }
    kit_rect r = element_rect(k, part);
    float x = std::clamp((mx / dp(k) - r.x) / std::max(r.w, 1.f), 0.f, 1.f);
    float y = std::clamp((my / dp(k) - r.y) / std::max(r.h, 1.f), 0.f, 1.f);
    if (n->w.drag_part == 1) {
        n->w.hsv[1] = x;
        n->w.hsv[2] = 1 - y;
    } else {
        n->w.hsv[0] = x * 360;
    }
    n->w.color = hsv_to_rgb(n->w.hsv, n->w.color & 255);
    n->w.pending = true;
}

/* ---------- Events ---------- */

void control_press(kit *k, Node *n, Rml::Event &e)
{
    Rml::Element *target = e.GetTargetElement();
    switch (n->kind) {
        case Kind::Splitter:
            n->w.pressing = true;
            break;
        case Kind::Color:
            for (Rml::Element *el = target; el && el != n->el; el = el->GetParentNode()) {
                if (el->HasAttribute("data-part")) {
                    n->w.drag_part = el->GetAttribute<int>("data-part", 0);
                    color_drag(k, n, e.GetParameter<float>("mouse_x", 0), e.GetParameter<float>("mouse_y", 0));
                    break;
                }
            }
            break;
        default: break;
    }
}

void control_click(kit *k, Node *n, Rml::Element *target)
{
    switch (n->kind) {
        case Kind::Button:
            if (!n->el->HasAttribute("data-disabled")) {
                n->events |= EV_CLICK;
            }
            return;
        case Kind::Check:
            if (!(n->w.desc.flags & (KIT_READONLY | KIT_DISABLED))) {
                n->w.pending = true;
            }
            return;
        case Kind::Toggle: {
            if (n->w.desc.flags & (KIT_READONLY | KIT_DISABLED)) {
                return;
            }
            for (Rml::Element *el = target; el && el != n->el; el = el->GetParentNode()) {
                if (el->HasAttribute("data-index")) {
                    n->w.pending_index = el->GetAttribute<int>("data-index", -1);
                    n->w.pending = true;
                    return;
                }
            }
            return;
        }
        case Kind::Select: {
            if (n->w.desc.flags & (KIT_READONLY | KIT_DISABLED)) {
                return;
            }
            std::string key = "select:" + n->path;
            if (k->popups[key].open) {
                close_popup(k, key);
            } else {
                close_all_popups(k);
                open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
            }
            return;
        }
        case Kind::Color: {
            if ((n->w.desc.flags & (KIT_READONLY | KIT_DISABLED)) || !within(target, n->el)) {
                return;
            }
            std::string key = "color:" + n->path;
            if (k->popups[key].open) {
                close_popup(k, key);
            } else {
                close_all_popups(k);
                rgb_to_hsv(n->w.color, n->w.hsv);
                open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
            }
            return;
        }
        case Kind::Panel:
            if (n->w.head_el && within(target, n->w.head_el)) {
                n->w.open = !n->w.open;
            }
            return;
        case Kind::Row:
            if (n->w.twisty_el && within(target, n->w.twisty_el)) {
                n->events |= EV_TWISTY;
                return;
            }
            n->events |= EV_CLICK;
            return;
        default: return;
    }
}

void control_drag(kit *k, Node *n, Rml::Event &e, Rml::EventId id)
{
    float mx = e.GetParameter<float>("mouse_x", 0), my = e.GetParameter<float>("mouse_y", 0);
    if (n->kind == Kind::Splitter) {
        Widget &w = n->w;
        float along = w.split.axis == 1 ? my : mx;
        if (id == Rml::EventId::Dragstart) {
            w.press_x = along;
            w.start = w.value;
            w.scrubbing = true;
            return;
        }
        if (id == Rml::EventId::Dragend) {
            w.scrubbing = w.pressing = false;
            w.committed = true;
            return;
        }
        double delta = (along - w.press_x) / dp(k) * (w.split.invert ? -1 : 1);
        w.value = std::clamp(w.start + delta, w.split.min, w.split.max);
        w.pending = true;
        w.pending_value = w.value;
        return;
    }
    if (n->kind == Kind::Color && n->w.drag_part) {
        if (id == Rml::EventId::Dragend) {
            n->w.drag_part = 0;
            return;
        }
        color_drag(k, n, mx, my);
    }
}

void control_key(Node *n, Rml::Event &e, int key)
{
    if (n->kind == Kind::Text || n->kind == Kind::Search) {
        auto *input = static_cast<Rml::ElementFormControlInput *>(n->w.input_el);
        if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
            if (n->kind == Kind::Text) {
                n->w.draft = input->GetValue();
                n->w.pending = true;
                n->w.input_el->Blur();
            }
            e.StopPropagation();
        } else if (key == Rml::Input::KI_ESCAPE) {
            if (n->kind == Kind::Search) {
                input->SetValue("");
                n->w.draft.clear();
                n->w.pending = true;
            } else {
                input->SetValue(n->w.applied);
                n->w.draft = n->w.applied;
            }
            n->w.input_el->Blur();
            e.StopPropagation();
        }
        return;
    }
    if ((n->kind == Kind::Check || n->kind == Kind::Button) && (key == Rml::Input::KI_SPACE || key == Rml::Input::KI_RETURN)) {
        if (n->kind == Kind::Check) {
            n->w.pending = true;
        } else {
            n->events |= EV_CLICK;
        }
        e.StopPropagation();
    }
}

void control_change(Node *n, Rml::Element *target)
{
    if ((n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
        n->w.draft = static_cast<Rml::ElementFormControlInput *>(target)->GetValue();
        if (n->kind == Kind::Search) {
            n->w.pending = true;
        }
    } else if (n->kind == Kind::Color && target->IsClassSet("hex")) {
        std::string v = static_cast<Rml::ElementFormControlInput *>(target)->GetValue();
        if (!v.empty() && v[0] == '#') {
            v.erase(v.begin());
        }
        if (v.size() == 6) {
            unsigned rgb = 0;
            if (std::sscanf(v.c_str(), "%6x", &rgb) == 1) {
                n->w.color = (rgb << 8) | (n->w.color & 255);
                rgb_to_hsv(n->w.color, n->w.hsv);
                n->w.pending = true;
            }
        }
    }
}

void control_focus(Node *n, Rml::Element *target)
{
    if ((n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
        n->w.focused = true;
        set_flag(n, "data-editing", true);
    }
}

void control_blur(Node *n, Rml::Element *target)
{
    if ((n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
        n->w.focused = false;
        set_flag(n, "data-editing", false);
        if (n->kind == Kind::Text && n->w.draft != n->w.applied) {
            n->w.pending = true;
        }
    }
}

} // namespace kit_impl

using namespace kit_impl;

/* ---------- Controls ---------- */

/* Button variants and sizes (cva): the only place their classes are named. */
static const char *const BUTTON_VARIANTS[] = { "", "subtle", "primary", "outline", "danger" };
static const char *const BUTTON_SIZES[] = { "", "sm" };

extern "C" bool kit_button(kit *k, const char *key, const kit_button_desc *d)
{
    bool square = d->icon && !(d->label && d->label[0]);
    const kit_class list[] = {
        { BUTTON_VARIANTS[d->variant], true },
        { BUTTON_SIZES[d->size], true },
        { "icon-only", square },
        { d->cls, d->cls != nullptr },
    };
    const char *cls = kit_classes(k, "btn", list, 4);
    Node *n = emit(k, key, Kind::Button, "button", cls);
    Widget &w = n->w;
    if (!w.label_el) {
        w.icon_el = raw_icon(k, n->el, "", 0);
        w.label_el = raw(k, n->el, "span", "label");
    }
    if (w.icon != d->icon) {
        w.icon = d->icon;
        w.icon_el->SetInnerRML(d->icon ? utf8(d->icon) : "");
        w.icon_el->SetProperty("display", d->icon ? "inline-block" : "none");
    }
    std::string label = d->label ? d->label : "";
    if (w.label != label) {
        w.label = label;
        if (d->morph) {
            morph_label(k, n, w.label_el, label.c_str());
        } else {
            w.label_el->SetInnerRML(escape(label.c_str()));
        }
        w.label_el->SetProperty("display", label.empty() ? "none" : "inline-block");
    }
    set_flag(n, "data-disabled", d->flags & KIT_DISABLED);
    if (d->flags & KIT_ON) {
        set_attr(n, "data-state", "on");
    } else {
        set_flag(n, "data-state", false);
    }
    set_tip(n, d->tip, d->shortcut);
    return (n->events & EV_CLICK) && !(d->flags & KIT_DISABLED);
}

extern "C" bool kit_checkbox(kit *k, const char *key, bool *value, const char *label, unsigned flags)
{
    Node *n = emit(k, key, Kind::Check, "div", "check");
    Widget &w = n->w;
    if (!w.label_el) {
        Rml::Element *box = raw(k, n->el, "span", "box");
        raw_icon(k, box, "", KIT_ICON_CHECK);
        w.label_el = raw(k, n->el, "span", "label");
    }
    w.desc.flags = flags;
    std::string l = label ? label : "";
    if (w.label != l) {
        w.label = l;
        w.label_el->SetInnerRML(escape(l.c_str()));
        w.label_el->SetProperty("display", l.empty() ? "none" : "inline");
    }
    bool changed = false;
    if (w.pending) {
        w.pending = false;
        *value = !*value;
        changed = true;
    }
    set_flag(n, "data-checked", *value);
    set_flag(n, "data-disabled", flags & KIT_DISABLED);
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    return changed;
}

extern "C" bool kit_choice(kit *k, const char *key, int *value, const char *const *labels, int count, unsigned flags)
{
    std::vector<std::string> list(labels, labels + count);
    if (count <= 3) {
        Node *n = emit(k, key, Kind::Toggle, "div", "segmented");
        Widget &w = n->w;
        w.desc.flags = flags;
        if (w.labels != list) {
            w.labels = list;
            while (n->el->GetNumChildren()) {
                n->el->RemoveChild(n->el->GetChild(0));
            }
            for (int i = 0; i < count; ++i) {
                Rml::Element *seg = raw(k, n->el, "button", "seg", labels[i]);
                seg->SetAttribute("data-index", i);
            }
        }
        bool changed = false;
        if (w.pending && w.pending_index >= 0 && w.pending_index < count) {
            changed = *value != w.pending_index;
            *value = w.pending_index;
        }
        w.pending = false;
        for (int i = 0; i < count; ++i) {
            flag_element(n->el->GetChild(i), "data-pressed", i == *value);
        }
        set_flag(n, "data-readonly", flags & KIT_READONLY);
        return changed;
    }
    Node *n = emit(k, key, Kind::Select, "div", "field select");
    Widget &w = n->w;
    w.desc.flags = flags;
    if (!w.value_el) {
        w.value_el = raw(k, n->el, "span", "part value");
        raw_icon(k, n->el, "caret", KIT_ICON_DOWN);
    }
    set_element_text(w.value_el, *value >= 0 && *value < count ? labels[*value] : "-");
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    set_flag(n, "data-open", k->popups["select:" + n->path].open);
    bool changed = false;
    if (w.pending) {
        w.pending = false;
        changed = *value != w.pending_index;
        *value = w.pending_index;
    }
    std::string popup = "select:" + n->path;
    Node *self = n;
    if (kit_popup_begin(k, popup.c_str(), "menu")) {
        kit_rect trigger = element_rect(k, self->el);
        char width[32];
        std::snprintf(width, sizeof width, "%.1fdp", trigger.w);
        k->popups[popup].node->el->SetProperty("min-width", width);
        for (int i = 0; i < count; ++i) {
            char item_key[16];
            std::snprintf(item_key, sizeof item_key, "o%d", i);
            kit_menu_item_desc item{};
            item.label = labels[i];
            item.checked = i == *value ? 1 : 0;
            if (kit_menu_item(k, item_key, &item)) {
                self->w.pending = true;
                self->w.pending_index = i;
            }
        }
        kit_popup_end(k);
    }
    k->last = self;
    return changed;
}

static Node *emit_input(kit *k, const char *key, Kind kind, const char *cls, const char *placeholder)
{
    Node *n = emit(k, key, kind, "div", cls);
    Widget &w = n->w;
    if (!w.input_el) {
        if (kind == Kind::Search) {
            Rml::Element *addon = raw(k, n->el, "span", "part addon");
            raw_icon(k, addon, "sm", KIT_ICON_SEARCH);
        }
        Rml::ElementPtr ptr = k->doc->CreateElement("input");
        ptr->SetAttribute("type", "text");
        ptr->SetClassNames("part value");
        w.input_el = n->el->AppendChild(std::move(ptr));
    }
    std::string ph = placeholder ? placeholder : "";
    if (w.placeholder != ph) {
        w.placeholder = ph;
        w.input_el->SetAttribute("placeholder", ph);
    }
    return n;
}

extern "C" bool kit_text_field(kit *k, const char *key, char *buffer, size_t size, const char *placeholder, unsigned flags)
{
    Node *n = emit_input(k, key, Kind::Text, "field text", placeholder);
    Widget &w = n->w;
    auto *input = static_cast<Rml::ElementFormControlInput *>(w.input_el);
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    if (flags & KIT_READONLY) {
        w.input_el->SetAttribute("readonly", true);
    } else {
        w.input_el->RemoveAttribute("readonly");
    }
    bool committed = false;
    if (w.pending) {
        w.pending = false;
        if (w.draft != buffer) {
            std::snprintf(buffer, size, "%s", w.draft.c_str());
            committed = true;
            w.committed = true;
        }
    }
    if (!w.focused && w.applied != buffer) {
        w.applied = buffer;
        w.draft = buffer;
        input->SetValue(buffer);
    }
    return committed;
}

extern "C" bool kit_search(kit *k, const char *key, char *buffer, size_t size, const char *placeholder)
{
    Node *n = emit_input(k, key, Kind::Search, "field search", placeholder);
    Widget &w = n->w;
    auto *input = static_cast<Rml::ElementFormControlInput *>(w.input_el);
    bool changed = false;
    if (w.pending) {
        w.pending = false;
        if (w.draft != buffer) {
            std::snprintf(buffer, size, "%s", w.draft.c_str());
            changed = true;
        }
    }
    if (!w.focused && input->GetValue() != buffer) {
        input->SetValue(buffer);
        w.draft = buffer;
    }
    return changed;
}

extern "C" bool kit_color(kit *k, const char *key, uint32_t *rgba, unsigned flags)
{
    Node *n = emit(k, key, Kind::Color, "div", "field color");
    Widget &w = n->w;
    w.desc.flags = flags;
    if (!w.swatch_el) {
        raw(k, n->el, "div", "mark");
        w.swatch_el = raw(k, n->el, "div", "swatch");
    }
    std::string popup = "color:" + n->path;
    bool open = k->popups[popup].open;
    bool changed = false;
    if (w.pending && open) {
        w.pending = false;
        changed = w.color != *rgba;
        *rgba = w.color;
    }
    if (!open) {
        w.color = *rgba;
        if (w.open) {
            w.committed = true;
        }
    }
    w.open = open;
    bool none = (*rgba & 255) == 0;
    w.swatch_el->SetClass("none", none);
    w.swatch_el->SetProperty("background-color", none ? "transparent" : hex_color(*rgba));
    set_flag(n, "data-overridden", flags & KIT_OVERRIDDEN);
    set_flag(n, "data-readonly", flags & KIT_READONLY);
    Node *self = n;
    if (kit_popup_begin(k, popup.c_str(), "picker")) {
        Node *p = k->popups[popup].node;
        if (!p->w.head_el) {
            Rml::Element *sv = raw(k, p->el, "div", "sv");
            sv->SetAttribute("data-part", 1);
            raw(k, sv, "div", "sv-white");
            raw(k, sv, "div", "sv-black");
            raw(k, sv, "div", "puck");
            Rml::Element *hue = raw(k, p->el, "div", "hue");
            hue->SetAttribute("data-part", 2);
            for (int i = 0; i < 6; ++i) {
                Rml::Element *seg = raw(k, hue, "div", "hue-seg");
                static const char *stops[7] = { "#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000" };
                seg->SetProperty("decorator", std::string("horizontal-gradient(") + stops[i] + " " + stops[i + 1] + ")");
            }
            raw(k, hue, "div", "puck");
            Rml::Element *row = raw(k, p->el, "div", "field text left hexrow");
            raw(k, row, "span", "part addon", "Hex");
            Rml::ElementPtr ptr = k->doc->CreateElement("input");
            ptr->SetAttribute("type", "text");
            ptr->SetClassNames("part value hex");
            row->AppendChild(std::move(ptr));
            p->w.head_el = sv;
        }
        Rml::Element *sv = p->el->QuerySelector(".sv");
        const float pure[3] = { self->w.hsv[0], 1, 1 };
        sv->SetProperty("background-color", hex_color(hsv_to_rgb(pure, 255)));
        char buffer[32];
        Rml::Element *puck = sv->QuerySelector(".puck");
        std::snprintf(buffer, sizeof buffer, "%.2f%%", self->w.hsv[1] * 100);
        puck->SetProperty("left", buffer);
        std::snprintf(buffer, sizeof buffer, "%.2f%%", (1 - self->w.hsv[2]) * 100);
        puck->SetProperty("top", buffer);
        Rml::Element *hue_puck = p->el->QuerySelector(".hue .puck");
        std::snprintf(buffer, sizeof buffer, "%.2f%%", self->w.hsv[0] / 360 * 100);
        hue_puck->SetProperty("left", buffer);
        auto *hex = static_cast<Rml::ElementFormControlInput *>(p->el->QuerySelector(".hex"));
        if (hex != k->context->GetFocusElement()) {
            hex->SetValue(hex_color(self->w.color).substr(1));
        }
        kit_popup_end(k);
    }
    k->last = self;
    return changed;
}

extern "C" bool kit_panel_begin(kit *k, const char *key, const char *title, const char *badge, bool default_open)
{
    Node *n = emit(k, key, Kind::Panel, "section", "panel");
    Widget &w = n->w;
    if (!w.head_el) {
        w.head_el = raw(k, n->el, "div", "panel-head");
        raw_icon(k, w.head_el, "chev", KIT_ICON_DOWN);
        w.label_el = raw(k, w.head_el, "span", "title");
        w.badge_el = raw(k, w.head_el, "span", "badge override");
        raw(k, w.head_el, "span", "spacer");
        raw_icon(k, w.head_el, "grip", KIT_ICON_GRIP);
        w.open = default_open;
    }
    std::string t = title ? title : "";
    if (w.label != t) {
        w.label = t;
        w.label_el->SetInnerRML(escape(t.c_str()));
    }
    std::string b = badge ? badge : "";
    if (w.badge != b) {
        w.badge = b;
        w.badge_el->SetInnerRML(escape(b.c_str()));
        w.badge_el->SetProperty("display", b.empty() ? "none" : "inline-block");
    }
    set_flag(n, "data-open", w.open);
    push_node(k, n);
    if (!w.open) {
        return false;
    }
    push_node(k, emit(k, "@body", Kind::Body, "div", "panel-body"));
    return true;
}

extern "C" void kit_panel_end(kit *k)
{
    if (k->stack.back()->kind == Kind::Body) {
        pop_node(k);
    }
    assert(k->stack.back()->kind == Kind::Panel);
    pop_node(k);
}

extern "C" void kit_prop_begin(kit *k, const char *key, const char *label)
{
    Node *n = emit(k, key, Kind::Prop, "div", "prop");
    if (!n->w.label_el) {
        Rml::Element *l = raw(k, n->el, "span", "label");
        n->w.label_el = raw(k, l, "span", "");
    }
    std::string text = label ? label : "";
    if (n->w.label != text) {
        n->w.label = text;
        n->w.label_el->SetInnerRML(escape(text.c_str()));
    }
    push_node(k, n);
    push_node(k, emit(k, "@control", Kind::Body, "div", "control"));
}

extern "C" void kit_prop_end(kit *k)
{
    pop_node(k);
    pop_node(k);
}

extern "C" unsigned kit_row_begin(kit *k, const char *key, const kit_row_desc *d)
{
    Node *n = emit(k, key, Kind::Row, "div", "tree-row");
    Widget &w = n->w;
    if (!w.label_el) {
        w.twisty_el = raw_icon(k, n->el, "twisty sm", 0);
        w.icon_el = raw_icon(k, n->el, "", 0);
        w.label_el = raw(k, n->el, "span", "name");
        w.meta_el = raw(k, n->el, "span", "meta");
    }
    uint32_t twisty = d->twisty < 0 ? 0 : d->twisty ? KIT_ICON_DOWN : KIT_ICON_RIGHT;
    std::string twisty_text = twisty ? utf8(twisty) : "";
    if (w.twisty_el->GetInnerRML() != twisty_text) {
        w.twisty_el->SetInnerRML(twisty_text);
    }
    if (w.icon != d->icon) {
        w.icon = d->icon;
        w.icon_el->SetInnerRML(d->icon ? utf8(d->icon) : "");
    }
    std::string label = d->label ? d->label : "";
    if (w.label != label) {
        w.label = label;
        w.label_el->SetInnerRML(escape(label.c_str()));
    }
    std::string meta = d->meta ? d->meta : "";
    if (w.meta != meta) {
        w.meta = meta;
        w.meta_el->SetInnerRML(escape(meta.c_str()));
    }
    char pad[32];
    std::snprintf(pad, sizeof pad, "%ddp", 6 + d->depth * 14);
    n->el->SetProperty("padding-left", pad);
    set_flag(n, "data-selected", d->flags & KIT_ON);
    set_flag(n, "data-active", d->flags & KIT_ACTIVE);
    unsigned result = ((n->events & EV_CLICK) ? KIT_ROW_CLICK : 0u) |
                      ((n->events & EV_DOUBLE) ? KIT_ROW_DOUBLE_CLICK : 0u) |
                      ((n->events & EV_CONTEXT) ? KIT_ROW_CONTEXT : 0u) |
                      ((n->events & EV_TWISTY) ? KIT_ROW_TWISTY : 0u);
    push_node(k, n);
    push_node(k, emit(k, "@trail", Kind::Body, "span", "trail"));
    return result;
}

extern "C" void kit_row_end(kit *k)
{
    pop_node(k);
    pop_node(k);
}

/* ---------- Splitter and surfaces ---------- */

extern "C" bool kit_splitter(kit *k, const char *key, int axis, float *value, float minimum, float maximum, bool invert)
{
    Node *n = emit(k, key, Kind::Splitter, "div", axis ? "split-h" : "split-v");
    Widget &w = n->w;
    w.split = { axis, invert, minimum, maximum };
    set_flag(n, "data-dragging", w.scrubbing);
    if (w.pending) {
        w.pending = false;
        *value = (float)w.pending_value;
        return true;
    }
    if (!w.scrubbing) {
        w.value = *value;
    }
    return false;
}

extern "C" void kit_surface(kit *k, const char *key, const char *cls, const char *texture, kit_surface_input *input)
{
    Node *n = emit(k, key, Kind::Surface, "img", cls);
    std::string src = std::string("kit:") + texture;
    set_attr(n, "src", src);
    kit_surface_input out{};
    out.rect = element_rect(k, n->el);
    out.x = k->in.mouse_x - out.rect.x;
    out.y = k->in.mouse_y - out.rect.y;
    out.hovered = k->context->GetHoverElement() == n->el && rect_contains(out.rect, k->in.mouse_x, k->in.mouse_y);
    bool captured = k->surface_capture == n->path;
    for (int b = 0; b < 3; ++b) {
        if (out.hovered && k->in.pressed[b] && !captured) {
            k->surface_capture = n->path;
            k->surface_button = b;
            captured = true;
            out.pressed[b] = true;
        }
    }
    if (captured) {
        int b = k->surface_button;
        out.down[b] = k->in.down[b];
        out.released[b] = k->in.released[b];
        if (out.released[b] || !k->in.down[b]) {
            k->surface_capture.clear();
            k->surface_button = -1;
        }
    }
    out.wheel = out.hovered ? k->in.wheel : 0;
    *input = out;
}
