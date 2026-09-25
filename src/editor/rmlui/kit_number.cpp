/* Number fields after Base UI NumberField: drag to scrub, click to type a value
 * or an expression, step with the arrow keys or the held steppers, and Tab to the
 * next field. Bounded sliders scrub across their width and draw a fill. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace kit_impl {

/* Numeric entry accepts arithmetic: "=2*pi", "120*1.5", or "*2" relative to the current value. */
struct Expression
{
    const char *p;
    bool ok = true;
    void space()
    {
        while (*p == ' ') {
            ++p;
        }
    }
    double primary()
    {
        space();
        if (*p == '(') {
            ++p;
            double v = sum();
            space();
            ok &= *p == ')';
            if (*p) {
                ++p;
            }
            return v;
        }
        if (*p == '-') {
            ++p;
            return -primary();
        }
        if (*p == '+') {
            ++p;
            return primary();
        }
        if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'i' || p[1] == 'I')) {
            p += 2;
            return M_PI;
        }
        char *end = nullptr;
        double v = std::strtod(p, &end);
        ok &= end != p;
        p = end;
        return v;
    }
    double product()
    {
        double v = primary();
        for (;;) {
            space();
            if (*p == '*') {
                ++p;
                v *= primary();
            } else if (*p == '/') {
                ++p;
                v /= primary();
            } else {
                return v;
            }
        }
    }
    double sum()
    {
        double v = product();
        for (;;) {
            space();
            if (*p == '+') {
                ++p;
                v += product();
            } else if (*p == '-') {
                ++p;
                v -= product();
            } else {
                return v;
            }
        }
    }
};

static double evaluate(const std::string &text, double current)
{
    std::string s = text;
    while (!s.empty() && s.front() == ' ') {
        s.erase(s.begin());
    }
    if (!s.empty() && s.front() == '=') {
        s.erase(s.begin());
    }
    if (!s.empty() && (s.front() == '*' || s.front() == '/' || s.front() == '+')) {
        s = format_number(current, 12, false) + s;
    }
    if (s.empty()) {
        return NAN;
    }
    Expression e{ s.c_str() };
    double v = e.sum();
    e.space();
    return e.ok && !*e.p && std::isfinite(v) ? v : NAN;
}

static double clamp_number(const Widget &w, double v)
{
    bool bounded = w.desc.min != 0 || w.desc.max != 0;
    if (bounded) {
        v = std::clamp(v, w.desc.min, w.desc.max);
    }
    if (w.desc.integer) {
        v = std::round(v);
    }
    return v;
}

static double step_size(const Widget &w)
{
    return w.desc.step > 0 ? w.desc.step : 1;
}

static void number_display(kit *k, Node *n)
{
    Widget &w = n->w;
    if (!w.editing) {
        set_element_text(w.value_el, format_number(w.value, w.desc.precision, w.desc.integer));
    }
    if (w.fill_el) {
        double t = w.desc.max > w.desc.min ? (w.value - w.desc.min) / (w.desc.max - w.desc.min) : 0;
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.2f%%", std::clamp(t, 0.0, 1.0) * 100);
        w.fill_el->SetProperty("width", buffer);
    }
    (void)k;
}

static void number_change(Node *n, double v)
{
    Widget &w = n->w;
    v = clamp_number(w, v);
    w.value = v;
    w.pending = true;
    w.pending_value = v;
}

static bool number_locked(const Node *n)
{
    return n->w.desc.flags & (KIT_READONLY | KIT_DISABLED);
}

static void number_begin_edit(kit *k, Node *n, const char *initial)
{
    Widget &w = n->w;
    if (w.editing || number_locked(n)) {
        return;
    }
    w.editing = true;
    w.invalid = false;
    set_flag(n, "data-editing", true);
    w.value_el->SetProperty("display", "none");
    Rml::ElementPtr ptr = k->doc->CreateElement("input");
    ptr->SetAttribute("type", "text");
    ptr->SetClassNames("part value edit");
    w.input_el = n->el->InsertBefore(std::move(ptr), w.value_el);
    auto *input = static_cast<Rml::ElementFormControlInput *>(w.input_el);
    input->SetValue(initial ? initial : format_number(w.value, std::max(w.desc.precision, 3), w.desc.integer));
    w.input_el->Focus(true);
    if (!initial) {
        input->Select();
    } else {
        input->SetSelectionRange((int)std::strlen(initial), (int)std::strlen(initial));
    }
    w.start = w.value;
}

static void number_end_edit(kit *k, Node *n, bool accept, bool refocus)
{
    Widget &w = n->w;
    if (!w.editing) {
        return;
    }
    if (accept) {
        auto *input = static_cast<Rml::ElementFormControlInput *>(w.input_el);
        double v = evaluate(input->GetValue(), w.value);
        if (!std::isfinite(v)) {
            w.invalid = true;
            set_flag(n, "data-invalid", true);
            input->Select();
            return;
        }
        if (clamp_number(w, v) != w.value) {
            number_change(n, v);
        }
        w.committed = true;
    }
    w.editing = false;
    w.invalid = false;
    set_flag(n, "data-invalid", false);
    set_flag(n, "data-editing", false);
    Rml::Element *input = w.input_el;
    w.input_el = nullptr;
    w.value_el->SetProperty("display", "block");
    if (refocus) {
        n->el->Focus(true);
    }
    n->el->RemoveChild(input);
    number_display(k, n);
}

static void number_step(Node *n, int dir, unsigned modifiers)
{
    double m = (modifiers & KIT_MOD_ALT) ? 0.1 : (modifiers & KIT_MOD_SHIFT) ? 10 : 1;
    double v = n->w.value + dir * step_size(n->w) * m;
    number_change(n, std::round(v * 1e6) / 1e6);
}

static Node *next_number(kit *k, Node *from, int dir)
{
    std::vector<Node *> fields;
    std::function<void(Node *)> walk = [&](Node *n) {
        if (n->kind == Kind::Number && !number_locked(n)) {
            fields.push_back(n);
        }
        for (auto &c : n->children) {
            walk(c.get());
        }
    };
    for (auto &[id, root] : k->roots) {
        (void)id;
        walk(root.get());
    }
    auto at = std::find(fields.begin(), fields.end(), from);
    if (at == fields.end() || fields.size() < 2) {
        return nullptr;
    }
    long i = (long)(at - fields.begin()) + dir;
    i = (i % (long)fields.size() + (long)fields.size()) % (long)fields.size();
    return fields[(size_t)i];
}

void number_key(kit *k, Node *n, Rml::Event &e, int key)
{
    Widget &w = n->w;
    unsigned mods = event_modifiers(e);
    if (w.editing) {
        if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
            number_end_edit(k, n, true, true);
            e.StopPropagation();
        } else if (key == Rml::Input::KI_ESCAPE) {
            number_end_edit(k, n, false, true);
            e.StopPropagation();
        } else if (key == Rml::Input::KI_TAB) {
            number_end_edit(k, n, true, false);
            if (!w.editing) {
                Node *next = next_number(k, n, (mods & KIT_MOD_SHIFT) ? -1 : 1);
                if (next) {
                    number_begin_edit(k, next, nullptr);
                }
            }
            e.StopPropagation();
        }
        return;
    }
    if (number_locked(n)) {
        return;
    }
    bool bounded = w.desc.min != 0 || w.desc.max != 0;
    if (key == Rml::Input::KI_UP || key == Rml::Input::KI_RIGHT) {
        number_step(n, 1, mods);
        w.committed = true;
    } else if (key == Rml::Input::KI_DOWN || key == Rml::Input::KI_LEFT) {
        number_step(n, -1, mods);
        w.committed = true;
    } else if (key == Rml::Input::KI_HOME && bounded) {
        number_change(n, w.desc.min);
        w.committed = true;
    } else if (key == Rml::Input::KI_END && bounded) {
        number_change(n, w.desc.max);
        w.committed = true;
    } else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
        number_begin_edit(k, n, nullptr);
    } else if ((key >= Rml::Input::KI_0 && key <= Rml::Input::KI_9) || key == Rml::Input::KI_OEM_MINUS ||
               key == Rml::Input::KI_OEM_PERIOD || key == Rml::Input::KI_SUBTRACT) {
        if (!(mods & (KIT_MOD_CTRL | KIT_MOD_META))) {
            number_begin_edit(k, n, "");
        }
        return;
    } else {
        return;
    }
    e.StopPropagation();
}

/* Held steppers repeat after 400 ms, every 60 ms (Base UI NumberField). */
void repeat_steppers(kit *k)
{
    for (auto &[path, n] : k->by_path) {
        (void)path;
        if (n->kind != Kind::Number || !n->w.step_dir) {
            continue;
        }
        if (!k->in.down[0]) {
            n->w.step_dir = 0;
            n->w.committed = true;
            continue;
        }
        if (k->in.time >= n->w.step_next) {
            number_step(n, n->w.step_dir, k->in.modifiers);
            n->w.step_next = k->in.time + kStepRepeatInterval;
        }
    }
}

void number_press(kit *k, Node *n, Rml::Event &e)
{
    Rml::Element *target = e.GetTargetElement();
    Widget &w = n->w;
    if (w.editing || number_locked(n)) {
        return;
    }
    Rml::Element *step = target;
    while (step && step != n->el && !step->IsClassSet("step")) {
        step = step->GetParentNode();
    }
    w.start = w.value;
    w.suppress_click = false;
    if (step && step != n->el) {
        w.step_dir = step->IsClassSet("inc") ? 1 : -1;
        number_step(n, w.step_dir, event_modifiers(e));
        w.step_next = k->in.time + kStepRepeatDelay;
        w.suppress_click = true;
        return;
    }
    w.pressing = true;
    w.scrubbing = false;
    w.press_x = e.GetParameter<float>("mouse_x", 0);
}

void number_release(Node *n)
{
    if (n->w.pressing && !n->w.scrubbing) {
        n->w.pressing = false;
    }
}

void number_click(kit *k, Node *n)
{
    if (n->w.suppress_click) {
        n->w.suppress_click = false;
        return;
    }
    number_begin_edit(k, n, nullptr);
}

void number_drag(kit *k, Node *n, Rml::Event &e, Rml::EventId id)
{
    float mx = e.GetParameter<float>("mouse_x", 0);
    unsigned mods = event_modifiers(e);
    Widget &w = n->w;
    if (!w.pressing) {
        return;
    }
    if (id == Rml::EventId::Dragend) {
        if (w.scrubbing) {
            w.committed = true;
            w.suppress_click = true;
        }
        w.pressing = w.scrubbing = false;
        k->surface_capture.clear();
        return;
    }
    float dx = (mx - w.press_x) / dp(k);
    if (!w.scrubbing && std::fabs(dx) >= kScrubThreshold) {
        /* RmlUi dispatches click before dragend; the release must not start editing. */
        w.scrubbing = true;
        w.suppress_click = true;
        w.start = w.value;
    }
    if (!w.scrubbing) {
        return;
    }
    bool slider = w.desc.slider && (w.desc.min != 0 || w.desc.max != 0);
    kit_rect r = element_rect(k, n->el);
    double per_px = slider ? (w.desc.max - w.desc.min) / std::max(r.w, 1.f) : step_size(w) / 2;
    double m = (mods & KIT_MOD_ALT) ? 0.1 : ((mods & KIT_MOD_SHIFT) && !slider) ? 10 : 1;
    double v = w.start + dx * per_px * m;
    if (mods & KIT_MOD_CTRL) {
        double snap = slider ? (w.desc.max - w.desc.min) / 10 : step_size(w) * 10;
        v = std::round(v / snap) * snap;
    }
    number_change(n, std::round(v * 1e6) / 1e6);
}

void number_blur(kit *k, Node *n, Rml::Element *target)
{
    if (!n->w.editing || target != n->w.input_el) {
        return;
    }
    number_end_edit(k, n, true, false);
    if (n->w.editing) {
        number_end_edit(k, n, false, false);
    }
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" bool kit_number(kit *k, const char *key, double *value, const kit_number_desc *d)
{
    Node *n = emit(k, key, Kind::Number, "div", "field number");
    Widget &w = n->w;
    bool bounded = d->min != 0 || d->max != 0;
    bool slider = d->slider && bounded;
    if (!w.value_el) {
        w.fill_el = raw(k, n->el, "div", "fill");
        raw(k, n->el, "div", "mark");
        w.addon_el = raw(k, n->el, "span", "part addon");
        w.value_el = raw(k, n->el, "span", "part value");
        w.unit_el = raw(k, n->el, "span", "part unit");
        Rml::Element *dec = raw(k, n->el, "span", "step dec");
        raw_icon(k, dec, "", KIT_ICON_LEFT);
        Rml::Element *inc = raw(k, n->el, "span", "step inc");
        raw_icon(k, inc, "", KIT_ICON_RIGHT);
        w.value = *value;
    }
    w.desc = *d;
    std::string addon = d->addon ? d->addon : "";
    std::string addon_cls = d->addon_cls ? d->addon_cls : "";
    if (w.addon != addon || w.addon_cls != addon_cls) {
        w.addon = addon;
        w.addon_cls = addon_cls;
        w.addon_el->SetInnerRML(escape(addon.c_str()));
        w.addon_el->SetClassNames("part addon " + addon_cls);
        w.addon_el->SetProperty("display", addon.empty() ? "none" : "block");
    }
    std::string unit = d->unit ? d->unit : "";
    if (w.unit != unit) {
        w.unit = unit;
        w.unit_el->SetInnerRML(escape(unit.c_str()));
        w.unit_el->SetProperty("display", unit.empty() ? "none" : "block");
    }
    w.fill_el->SetProperty("display", slider ? "block" : "none");
    set_class(n, slider ? "field number slider" : "field number");
    set_flag(n, "data-overridden", d->flags & KIT_OVERRIDDEN);
    set_flag(n, "data-readonly", d->flags & KIT_READONLY);
    set_flag(n, "data-disabled", d->flags & KIT_DISABLED);
    set_flag(n, "data-scrubbing", w.scrubbing);
    bool changed = false;
    if (w.pending) {
        w.pending = false;
        *value = w.pending_value;
        changed = true;
    } else if (!w.scrubbing && !w.editing && !w.step_dir) {
        w.value = *value;
    }
    number_display(k, n);
    return changed;
}
