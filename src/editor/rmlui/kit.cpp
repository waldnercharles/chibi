/* Studio kit: immediate-mode calls reconciled onto a retained RmlUi document.
 * Controllers here implement the Base UI contracts recorded in
 * docs/prototypes/studio/kit.html; the C editor owns every model value. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

double evaluate(const std::string &text, double current)
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

/* command-score (cmdk): continuous 1, word start 0.9, separator 0.8, jump 0.17. */
double command_score(const std::string &string, const std::string &query)
{
    std::string lower, abbr;
    for (char c : string) {
        lower += (c == '-' ? ' ' : (char)std::tolower((unsigned char)c));
    }
    for (char c : query) {
        abbr += (c == '-' ? ' ' : (char)std::tolower((unsigned char)c));
    }
    std::map<std::pair<size_t, size_t>, double> memo;
    auto gap = [](char c) { return std::strchr("\\/_+.#\"@[({&", c) != nullptr; };
    std::function<double(size_t, size_t)> score = [&](size_t si, size_t ai) -> double {
        auto key = std::make_pair(si, ai);
        auto found = memo.find(key);
        if (found != memo.end()) {
            return found->second;
        }
        if (ai == abbr.size()) {
            return si == lower.size() ? 1.0 : 0.99;
        }
        double best = 0;
        size_t index = lower.find(abbr[ai], si);
        while (index != std::string::npos) {
            double s = score(index + 1, ai + 1);
            if (s > best) {
                if (index == si) {
                } else if (lower[index - 1] == ' ') {
                    s *= 0.9;
                } else if (gap(lower[index - 1])) {
                    s *= 0.8;
                } else {
                    s *= 0.17;
                    if (si > 0) {
                        s *= std::pow(0.999, (double)(index - si));
                    }
                }
                if (string[index] != query[ai]) {
                    s *= 0.9999;
                }
            }
            best = std::max(best, s);
            index = lower.find(abbr[ai], index + 1);
        }
        memo[key] = best;
        return best;
    };
    return score(0, 0);
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
Node *owner(kit *k, Node *n)
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

void set_tip(Node *n, const char *label, const char *shortcut)
{
    if (label && label[0]) {
        set_attr(n, "data-tip", label);
    } else {
        set_flag(n, "data-tip", false);
    }
    if (shortcut && shortcut[0]) {
        set_attr(n, "data-kbd", shortcut);
    } else {
        set_flag(n, "data-kbd", false);
    }
}

/* ---------- Popups ---------- */

void close_popup(kit *k, const std::string &key)
{
    auto found = k->popups.find(key);
    if (found == k->popups.end() || !found->second.open) {
        return;
    }
    for (auto &[other, popup] : k->popups) {
        if (popup.open && popup.parent == key) {
            close_popup(k, other);
        }
    }
    found->second.open = false;
    found->second.placed = false;
    found->second.highlight = -1;
    k->popup_stack.erase(std::remove(k->popup_stack.begin(), k->popup_stack.end(), key), k->popup_stack.end());
}

void close_all_popups(kit *k)
{
    std::vector<std::string> open = k->popup_stack;
    for (const std::string &key : open) {
        close_popup(k, key);
    }
}

void open_popup(kit *k, const std::string &key, kit_rect anchor, bool at_point, float x, float y, const std::string &parent)
{
    Popup &p = k->popups[key];
    if (p.open) {
        close_popup(k, key);
    }
    p = Popup{};
    p.open = true;
    p.anchor = anchor;
    p.at_point = at_point;
    p.x = x;
    p.y = y;
    p.parent = parent;
    p.keyboard = k->keyboard_edit;
    k->popup_stack.push_back(key);
}

std::string top_popup(const kit *k)
{
    return k->popup_stack.empty() ? std::string() : k->popup_stack.back();
}

void place_popups(kit *k)
{
    for (auto &[key, p] : k->popups) {
        (void)key;
        if (!p.open || !p.node) {
            continue;
        }
        kit_rect size = element_rect(k, p.node->el);
        if (size.w <= 0 || size.h <= 0) {
            continue;
        }
        float x, y;
        float W = k->in.width, H = k->in.height;
        if (p.at_point) {
            x = p.x;
            y = p.y;
        } else if (!p.parent.empty()) {
            x = p.anchor.x + p.anchor.w + 2;
            y = p.anchor.y - 4;
            if (x + size.w > W - 4) {
                x = p.anchor.x - size.w - 2;
            }
        } else {
            x = p.anchor.x;
            y = p.anchor.y + p.anchor.h + 2;
            if (y + size.h > H - 4) {
                y = p.anchor.y - size.h - 2;
            }
        }
        x = std::clamp(x, 4.0f, std::max(4.0f, W - size.w - 4));
        y = std::clamp(y, 4.0f, std::max(4.0f, H - size.h - 4));
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.1fdp", x);
        p.node->el->SetProperty("left", buffer);
        std::snprintf(buffer, sizeof buffer, "%.1fdp", y);
        p.node->el->SetProperty("top", buffer);
        if (!p.placed) {
            p.placed = true;
            p.node->el->RemoveAttribute("data-unplaced");
        }
    }
}

void highlight_item(kit *k, Popup &p, int index)
{
    p.highlight = index;
    for (int i = 0; i < (int)p.items.size(); ++i) {
        if (p.items[i]) {
            flag_element(p.items[i]->el, "data-highlighted", i == index);
        }
    }
    (void)k;
}

bool item_enabled(const Node *n)
{
    return n && !n->el->HasAttribute("data-disabled");
}

void move_highlight(kit *k, Popup &p, int dir, bool absolute)
{
    int count = (int)p.items.size();
    if (!count) {
        return;
    }
    int start = absolute ? (dir > 0 ? -1 : count) : p.highlight;
    for (int step = 1; step <= count; ++step) {
        int i = absolute ? start + dir * step : ((start + dir * step) % count + count) % count;
        if (i < 0 || i >= count) {
            break;
        }
        if (item_enabled(p.items[i])) {
            highlight_item(k, p, i);
            p.items[i]->el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
            return;
        }
    }
}

void activate_item(kit *k, const std::string &popup_key, Node *item)
{
    if (!item_enabled(item)) {
        return;
    }
    if (item->kind == Kind::Submenu) {
        std::string sub = "sub:" + item->path;
        open_popup(k, sub, element_rect(k, item->el), false, 0, 0, popup_key);
        return;
    }
    item->w.pending = true;
    k->popups[popup_key].activated = true;
}

/* ---------- Number field ---------- */

double clamp_number(const Widget &w, double v)
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

double step_size(const Widget &w)
{
    return w.desc.step > 0 ? w.desc.step : 1;
}

void number_display(kit *k, Node *n)
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

void number_change(Node *n, double v)
{
    Widget &w = n->w;
    v = clamp_number(w, v);
    w.value = v;
    w.pending = true;
    w.pending_value = v;
}

bool number_locked(const Node *n)
{
    return n->w.desc.flags & (KIT_READONLY | KIT_DISABLED);
}

void number_begin_edit(kit *k, Node *n, const char *initial)
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

void number_end_edit(kit *k, Node *n, bool accept, bool refocus)
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

void number_step(Node *n, int dir, unsigned modifiers)
{
    double m = (modifiers & KIT_MOD_ALT) ? 0.1 : (modifiers & KIT_MOD_SHIFT) ? 10 : 1;
    double v = n->w.value + dir * step_size(n->w) * m;
    number_change(n, std::round(v * 1e6) / 1e6);
}

unsigned event_modifiers(Rml::Event &e)
{
    return (e.GetParameter<int>("ctrl_key", 0) ? KIT_MOD_CTRL : 0u) |
           (e.GetParameter<int>("shift_key", 0) ? KIT_MOD_SHIFT : 0u) |
           (e.GetParameter<int>("alt_key", 0) ? KIT_MOD_ALT : 0u) |
           (e.GetParameter<int>("meta_key", 0) ? KIT_MOD_META : 0u);
}

Node *next_number(kit *k, Node *from, int dir)
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

/* ---------- Color ---------- */

void rgb_to_hsv(uint32_t c, float hsv[3])
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

uint32_t hsv_to_rgb(const float hsv[3], uint32_t alpha)
{
    auto f = [&](float n) {
        float k = std::fmod(n + hsv[0] / 60, 6.f);
        return hsv[2] - hsv[2] * hsv[1] * std::max(0.f, std::min({ k, 4 - k, 1.f }));
    };
    auto byte = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 255); };
    return (byte(f(5)) << 24) | (byte(f(3)) << 16) | (byte(f(1)) << 8) | (alpha & 255);
}

std::string hex_color(uint32_t c)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255);
    return buffer;
}

void color_drag(kit *k, Node *n, float mx, float my)
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
    /* Outside presses dismiss popups (Base UI outside-press). */
    bool inside_popup = false;
    for (const std::string &key : k->popup_stack) {
        Popup &p = k->popups[key];
        if (p.node && within(target, p.node->el)) {
            inside_popup = true;
        }
    }
    Node *trigger = n;
    while (trigger && trigger->kind != Kind::Trigger && trigger->kind != Kind::Select && trigger->kind != Kind::Color) {
        trigger = trigger->parent;
    }
    if (!inside_popup && !trigger) {
        close_all_popups(k);
    }
    if (k->command.open && k->command_backdrop && target == k->command_backdrop) {
        k->command.open = false;
    }
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
        case Kind::Number: {
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
            break;
        }
        case Kind::Trigger: {
            std::string key = "menu:" + n->path;
            bool open = k->popups[key].open;
            close_all_popups(k);
            if (!open) {
                open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
                k->popups[key].menubar = true;
            }
            e.StopPropagation();
            break;
        }
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

void KitListener::click(Rml::Event &e, Node *n)
{
    kit *k = k_;
    if (k->drag.active) {
        return;
    }
    Rml::Element *target = e.GetTargetElement();
    switch (n->kind) {
        case Kind::Button:
            if (!n->el->HasAttribute("data-disabled")) {
                n->events |= EV_CLICK;
            }
            return;
        case Kind::Number:
            if (n->w.suppress_click) {
                n->w.suppress_click = false;
                return;
            }
            number_begin_edit(k, n, nullptr);
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
        case Kind::MenuItem:
        case Kind::Submenu:
            for (const std::string &key : k->popup_stack) {
                Popup &p = k->popups[key];
                if (std::find(p.items.begin(), p.items.end(), n) != p.items.end()) {
                    activate_item(k, key, n);
                    return;
                }
            }
            return;
        case Kind::CommandItem:
            k->command.chosen = n->w.pending_index;
            return;
        default: n->events |= EV_CLICK; return;
    }
}

void KitListener::drag(Rml::Event &e, Node *n, Rml::EventId id)
{
    kit *k = k_;
    float mx = e.GetParameter<float>("mouse_x", 0), my = e.GetParameter<float>("mouse_y", 0);
    unsigned mods = event_modifiers(e);
    if (n->kind == Kind::Number) {
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
        return;
    }
    if (n->kind == Kind::Splitter) {
        Widget &w = n->w;
        float along = n->w.desc.integer ? my : mx; /* desc.integer marks the y axis */
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
        double delta = (along - w.press_x) / dp(k) * (w.desc.slider ? -1 : 1);
        w.value = std::clamp(w.start + delta, w.desc.min, w.desc.max);
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

void KitListener::keydown(Rml::Event &e, Node *n)
{
    kit *k = k_;
    int key = e.GetParameter<int>("key_identifier", 0);
    unsigned mods = event_modifiers(e);
    k->keyboard_edit = true;
    if (k->drag.active && key == Rml::Input::KI_ESCAPE) {
        drag_cancel(k);
        e.StopPropagation();
        return;
    }
    if (k->command.open) {
        CommandState &c = k->command;
        int count = (int)c.shown.size();
        bool ctrl = mods & KIT_MOD_CTRL;
        if (key == Rml::Input::KI_ESCAPE) {
            c.open = false;
        } else if (key == Rml::Input::KI_DOWN || (ctrl && (key == Rml::Input::KI_N || key == Rml::Input::KI_J))) {
            c.selected = std::min(c.selected + 1, count - 1);
            c.reveal = true;
        } else if (key == Rml::Input::KI_UP || (ctrl && (key == Rml::Input::KI_P || key == Rml::Input::KI_K))) {
            c.selected = std::max(c.selected - 1, 0);
            c.reveal = true;
        } else if (key == Rml::Input::KI_HOME || key == Rml::Input::KI_END) {
            c.selected = key == Rml::Input::KI_HOME ? 0 : std::max(count - 1, 0);
            c.reveal = true;
        } else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
            if (c.selected >= 0 && c.selected < count) {
                c.chosen = c.shown[(size_t)c.selected];
            }
        } else {
            return;
        }
        e.StopPropagation();
        return;
    }
    std::string top = top_popup(k);
    if (!top.empty()) {
        Popup &p = k->popups[top];
        if (key == Rml::Input::KI_ESCAPE) {
            close_popup(k, top);
        } else if (key == Rml::Input::KI_TAB) {
            close_all_popups(k);
        } else if (key == Rml::Input::KI_DOWN) {
            move_highlight(k, p, 1, false);
        } else if (key == Rml::Input::KI_UP) {
            move_highlight(k, p, -1, false);
        } else if (key == Rml::Input::KI_HOME) {
            move_highlight(k, p, 1, true);
        } else if (key == Rml::Input::KI_END) {
            move_highlight(k, p, -1, true);
        } else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_SPACE) {
            if (p.highlight >= 0 && p.highlight < (int)p.items.size()) {
                activate_item(k, top, p.items[(size_t)p.highlight]);
            }
        } else if (key == Rml::Input::KI_RIGHT && p.highlight >= 0 && p.highlight < (int)p.items.size() &&
                   p.items[(size_t)p.highlight]->kind == Kind::Submenu) {
            activate_item(k, top, p.items[(size_t)p.highlight]);
            k->popups["sub:" + p.items[(size_t)p.highlight]->path].keyboard = true;
        } else if (key == Rml::Input::KI_LEFT && !p.parent.empty()) {
            close_popup(k, top);
        } else if ((key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) && p.menubar) {
            std::vector<Node *> triggers;
            for (Node *t : k->triggers) {
                if (t) {
                    triggers.push_back(t);
                }
            }
            for (size_t i = 0; i < triggers.size(); ++i) {
                if ("menu:" + triggers[i]->path == top) {
                    size_t next = (i + (key == Rml::Input::KI_RIGHT ? 1 : triggers.size() - 1)) % triggers.size();
                    close_all_popups(k);
                    std::string next_key = "menu:" + triggers[next]->path;
                    open_popup(k, next_key, element_rect(k, triggers[next]->el), false, 0, 0, "");
                    k->popups[next_key].menubar = true;
                    k->popups[next_key].keyboard = true;
                    break;
                }
            }
        } else if (key >= Rml::Input::KI_A && key <= Rml::Input::KI_Z) {
            char c = (char)('a' + (key - Rml::Input::KI_A));
            p.typed = k->in.time - p.typed_at < kTypeaheadReset ? p.typed + c : std::string(1, c);
            p.typed_at = k->in.time;
            for (int i = 0; i < (int)p.items.size(); ++i) {
                Node *item = p.items[(size_t)i];
                std::string label = item->w.label;
                std::transform(label.begin(), label.end(), label.begin(), ::tolower);
                if (item_enabled(item) && label.rfind(p.typed, 0) == 0) {
                    highlight_item(k, p, i);
                    break;
                }
            }
        } else {
            return;
        }
        e.StopPropagation();
        return;
    }
    if (!n) {
        return;
    }
    if (n->kind == Kind::Number) {
        number_key(k, n, e, key);
        return;
    }
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

void KitListener::ProcessEvent(Rml::Event &e)
{
    kit *k = k_;
    Rml::Element *target = e.GetTargetElement();
    Node *n = owner(k, node_for(k, target));
    switch (e.GetId()) {
        case Rml::EventId::Mousedown: mousedown(e, n, e.GetParameter<int>("button", 0)); break;
        case Rml::EventId::Mouseup:
            if (n && n->kind == Kind::Number && n->w.pressing && !n->w.scrubbing) {
                n->w.pressing = false;
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
            if (k->command.open && target == k->command.input) {
                k->command.query = static_cast<Rml::ElementFormControlInput *>(target)->GetValue();
                k->command.selected = 0;
                k->command.reveal = true;
            } else if (n && (n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
                n->w.draft = static_cast<Rml::ElementFormControlInput *>(target)->GetValue();
                if (n->kind == Kind::Search) {
                    n->w.pending = true;
                }
            } else if (n && n->kind == Kind::Color && target->IsClassSet("hex")) {
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
            break;
        case Rml::EventId::Focus:
            if (n && (n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
                n->w.focused = true;
                set_flag(n, "data-editing", true);
            }
            break;
        case Rml::EventId::Blur:
            if (n && n->kind == Kind::Number && n->w.editing && target == n->w.input_el) {
                number_end_edit(k, n, true, false);
                if (n->w.editing) {
                    number_end_edit(k, n, false, false);
                }
            } else if (n && (n->kind == Kind::Text || n->kind == Kind::Search) && target == n->w.input_el) {
                n->w.focused = false;
                set_flag(n, "data-editing", false);
                if (n->kind == Kind::Text && n->w.draft != n->w.applied) {
                    n->w.pending = true;
                }
            }
            break;
        case Rml::EventId::Mouseover:
            if (!n) {
                break;
            }
            if (n->kind == Kind::MenuItem || n->kind == Kind::Submenu) {
                for (const std::string &key : k->popup_stack) {
                    Popup &p = k->popups[key];
                    auto at = std::find(p.items.begin(), p.items.end(), n);
                    if (at == p.items.end()) {
                        continue;
                    }
                    highlight_item(k, p, (int)(at - p.items.begin()));
                    p.sub_hover = n->kind == Kind::Submenu ? n->path : std::string();
                    p.sub_hover_since = k->in.time;
                }
            } else if (n->kind == Kind::Trigger) {
                std::string key = "menu:" + n->path;
                bool menubar_open = false;
                for (const std::string &open : k->popup_stack) {
                    menubar_open |= k->popups[open].menubar;
                }
                if (menubar_open && !k->popups[key].open) {
                    close_all_popups(k);
                    open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
                    k->popups[key].menubar = true;
                }
            } else if (n->kind == Kind::CommandItem) {
                auto at = std::find(k->command.shown.begin(), k->command.shown.end(), n->w.pending_index);
                if (at != k->command.shown.end()) {
                    k->command.selected = (int)(at - k->command.shown.begin());
                }
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
    /* Held steppers repeat after 400 ms, every 60 ms (Base UI NumberField). */
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
    /* Submenus open after a 100 ms hover. */
    std::vector<std::pair<std::string, std::string>> to_open;
    for (const std::string &key : k->popup_stack) {
        Popup &p = k->popups[key];
        if (!p.sub_hover.empty() && k->in.time - p.sub_hover_since >= kSubmenuDelay) {
            std::string sub = "sub:" + p.sub_hover;
            if (!k->popups[sub].open) {
                to_open.emplace_back(key, p.sub_hover);
            }
        }
    }
    for (auto &[parent, path] : to_open) {
        auto found = k->by_path.find(path);
        if (found == k->by_path.end()) {
            continue;
        }
        for (auto &[other, q] : k->popups) {
            if (q.open && q.parent == parent) {
                close_popup(k, other);
            }
        }
        open_popup(k, "sub:" + path, element_rect(k, found->second->el), false, 0, 0, parent);
    }
    k->layer->cursor = 0;
    k->layer->frame = k->frame;
}

static void update_tooltip(kit *k)
{
    Rml::Element *hover = k->context->GetHoverElement();
    Rml::Element *target = nullptr;
    for (Rml::Element *el = hover; el; el = el->GetParentNode()) {
        if (el->HasAttribute("data-tip")) {
            target = el;
            break;
        }
    }
    bool blocked = k->in.down[0] || k->in.down[1] || !k->popup_stack.empty() || k->command.open;
    if (blocked) {
        target = nullptr;
    }
    if (target != k->tip_target) {
        if (k->tip_visible) {
            k->tip_warm_until = k->in.time + kTooltipGroup;
        }
        k->tip_visible = false;
        k->tip_target = target;
        k->tip_since = k->in.time;
    }
    if (k->tip_visible && k->frame > k->tip_shown_frame) {
        flag_element(k->tooltip, "data-starting-style", false);
    }
    bool show = target && (k->tip_visible || k->in.time - k->tip_since >= kTooltipDelay ||
                           k->in.time < k->tip_warm_until);
    if (!show) {
        k->tooltip->SetProperty("display", "none");
        return;
    }
    if (!k->tip_visible) {
        std::string html = "<span>" + escape(target->GetAttribute<Rml::String>("data-tip", "").c_str()) + "</span>";
        Rml::String shortcut = target->GetAttribute<Rml::String>("data-kbd", "");
        if (!shortcut.empty()) {
            html += "<span class=\"kbd\">" + escape(shortcut.c_str()) + "</span>";
        }
        k->tooltip->SetInnerRML(html);
        k->tooltip->SetProperty("display", "flex");
        /* Warm tooltips in a group switch instantly (Base UI); a cold one fades in. */
        flag_element(k->tooltip, "data-instant", k->in.time < k->tip_warm_until);
        flag_element(k->tooltip, "data-starting-style", !k->reduced_motion);
        k->tip_shown_frame = k->frame;
        k->tip_visible = true;
    }
    kit_rect r = element_rect(k, target);
    kit_rect size = element_rect(k, k->tooltip);
    bool right = target->GetAttribute<Rml::String>("data-tip-side", "") == "right";
    float x = right ? r.x + r.w + 6 : r.x + r.w / 2 - size.w / 2;
    float y = right ? r.y + r.h / 2 - size.h / 2 : r.y + r.h + 6;
    if (!right && y + size.h > k->in.height - 4) {
        y = r.y - size.h - 6;
    }
    x = std::clamp(x, 4.0f, std::max(4.0f, k->in.width - size.w - 4));
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1fdp", x);
    k->tooltip->SetProperty("left", buffer);
    std::snprintf(buffer, sizeof buffer, "%.1fdp", y);
    k->tooltip->SetProperty("top", buffer);
}

extern "C" void kit_end(kit *k)
{
    close_root(k);
    /* Any activated menu item closes the whole menu chain. */
    bool activated = false;
    for (auto &[key, p] : k->popups) {
        (void)key;
        activated |= p.open && p.activated;
    }
    if (activated) {
        close_all_popups(k);
    }
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

extern "C" void kit_tip(kit *k, const char *label, const char *shortcut)
{
    assert(k->last);
    set_tip(k->last, label, shortcut);
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

/* ---------- Controls ---------- */

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

/* ---------- Popups and menus ---------- */

extern "C" void kit_popup_open(kit *k, const char *key)
{
    close_all_popups(k);
    open_popup(k, key, k->last ? element_rect(k, k->last->el) : kit_rect{}, false, 0, 0, "");
}

extern "C" void kit_popup_open_at(kit *k, const char *key, float x, float y)
{
    close_all_popups(k);
    open_popup(k, key, {}, true, x, y, "");
}

extern "C" void kit_popup_close(kit *k, const char *key)
{
    close_popup(k, key);
}

extern "C" bool kit_popup_is_open(const kit *k, const char *key)
{
    auto found = k->popups.find(key);
    return found != k->popups.end() && found->second.open;
}

extern "C" bool kit_popup_begin(kit *k, const char *key, const char *cls)
{
    auto found = k->popups.find(key);
    if (found == k->popups.end() || !found->second.open) {
        return false;
    }
    Popup &p = found->second;
    std::string c = std::string("popup ") + (cls ? cls : "");
    std::string node_key = std::string("@") + key;
    Node *n = emit_in(k, k->layer, node_key.c_str(), Kind::Popup, "div", c.c_str());
    presence(k, n);
    flag_element(n->el, "data-unplaced", !p.placed);
    p.node = n;
    p.items.clear();
    k->emitting.push_back(key);
    push_node(k, n);
    return true;
}

extern "C" void kit_popup_end(kit *k)
{
    assert(!k->emitting.empty());
    Popup &p = k->popups[k->emitting.back()];
    if (p.keyboard && p.highlight < 0 && !p.items.empty()) {
        move_highlight(k, p, 1, true);
    }
    k->emitting.pop_back();
    pop_node(k);
}

static Node *menu_row(kit *k, const char *key, Kind kind, const char *label, uint32_t icon, const char *shortcut, int checked, bool disabled)
{
    Node *n = emit(k, key, kind, "div", kind == Kind::Submenu ? "menu-item sub" : "menu-item");
    Widget &w = n->w;
    if (!w.label_el) {
        w.icon_el = raw_icon(k, n->el, "lead", 0);
        w.label_el = raw(k, n->el, "span", "label");
        w.kbd_el = raw(k, n->el, "span", "kbd");
    }
    uint32_t lead = checked >= 0 ? (checked ? KIT_ICON_CHECK : 0) : icon;
    if (w.icon != lead) {
        w.icon = lead;
        w.icon_el->SetInnerRML(lead ? utf8(lead) : "");
    }
    std::string l = label ? label : "";
    if (w.label != l) {
        w.label = l;
        w.label_el->SetInnerRML(escape(l.c_str()));
    }
    std::string s = kind == Kind::Submenu ? utf8(KIT_ICON_RIGHT) : shortcut ? escape(shortcut) : "";
    if (w.meta != s) {
        w.meta = s;
        w.kbd_el->SetInnerRML(s);
        w.kbd_el->SetClassNames(kind == Kind::Submenu ? "icon chev" : "kbd");
    }
    set_flag(n, "data-disabled", disabled);
    set_flag(n, "data-checked", checked > 0);
    if (!k->emitting.empty()) {
        Popup &p = k->popups[k->emitting.back()];
        p.items.push_back(n);
        flag_element(n->el, "data-highlighted", p.highlight == (int)p.items.size() - 1);
    }
    return n;
}

extern "C" bool kit_menu_item(kit *k, const char *key, const kit_menu_item_desc *d)
{
    Node *n = menu_row(k, key, Kind::MenuItem, d->label, d->icon, d->shortcut, d->checked, d->disabled);
    set_flag(n, "data-current", d->current);
    bool activated = n->w.pending;
    n->w.pending = false;
    return activated && !d->disabled;
}

extern "C" void kit_menu_separator(kit *k, const char *key)
{
    emit(k, key, Kind::Box, "div", "menu-sep");
}

extern "C" void kit_menu_group(kit *k, const char *key, const char *label)
{
    Node *n = emit(k, key, Kind::Box, "div", "menu-group");
    set_text(n, label);
}

extern "C" bool kit_submenu_begin(kit *k, const char *key, const char *label, uint32_t icon)
{
    Node *n = menu_row(k, key, Kind::Submenu, label, icon, nullptr, -1, false);
    std::string sub = "sub:" + n->path;
    auto found = k->popups.find(sub);
    if (found != k->popups.end() && found->second.open) {
        found->second.anchor = element_rect(k, n->el);
    }
    return kit_popup_begin(k, sub.c_str(), "menu");
}

extern "C" void kit_submenu_end(kit *k)
{
    kit_popup_end(k);
}

extern "C" bool kit_menubar_trigger(kit *k, const char *key, const char *label)
{
    Node *n = emit(k, key, Kind::Trigger, "button", "btn trigger");
    if (!n->w.label_el) {
        n->w.label_el = raw(k, n->el, "span", "label");
    }
    set_element_text(n->w.label_el, label ? label : "");
    k->triggers.push_back(n);
    std::string popup = "menu:" + n->path;
    bool open = k->popups[popup].open;
    set_flag(n, "data-pressed", open);
    return open && kit_popup_begin(k, popup.c_str(), "menu");
}

/* ---------- Command palette ---------- */

extern "C" void kit_command_open(kit *k, const char *placeholder)
{
    close_all_popups(k);
    k->command = CommandState{};
    k->command.open = true;
    k->command.focus = true;
    k->command.placeholder = placeholder ? placeholder : "";
}

extern "C" bool kit_command_is_open(const kit *k)
{
    return k->command.open;
}

extern "C" const char *kit_command_query(const kit *k)
{
    return k->command.query.c_str();
}

extern "C" int kit_command(kit *k, const kit_command_item *items, int count)
{
    CommandState &c = k->command;
    if (!c.open) {
        return -1;
    }
    Node *backdrop = emit_in(k, k->layer, "@command-backdrop", Kind::Box, "div", "command-backdrop");
    presence(k, backdrop);
    k->command_backdrop = backdrop->el;
    Node *n = emit_in(k, k->layer, "@command", Kind::Command, "div", "command");
    presence(k, n);
    if (!n->w.input_el) {
        Rml::Element *row = raw(k, n->el, "div", "cmd-input");
        raw_icon(k, row, "", KIT_ICON_SEARCH);
        Rml::ElementPtr ptr = k->doc->CreateElement("input");
        ptr->SetAttribute("type", "text");
        n->w.input_el = row->AppendChild(std::move(ptr));
        Rml::Element *list = raw(k, n->el, "div", "cmd-list");
        Rml::Element *footer = raw(k, n->el, "div", "cmd-footer");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\x91");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\x93");
        raw(k, footer, "span", "hint", "Navigate");
        raw(k, footer, "span", "kbd-key", "\xe2\x86\xb5");
        raw(k, footer, "span", "hint", "Open");
        raw(k, footer, "span", "spacer");
        raw(k, footer, "span", "kbd-key", "esc");
        raw(k, footer, "span", "hint", "Close");
        n->container = list;
    }
    c.input = n->w.input_el;
    c.input->SetAttribute("placeholder", c.placeholder);
    if (c.focus) {
        c.focus = false;
        static_cast<Rml::ElementFormControlInput *>(c.input)->SetValue("");
        c.input->Focus(true);
    }
    /* Rank with command-score; groups order by their best match (cmdk). */
    struct Scored
    {
        int index;
        double score;
    };
    std::vector<Scored> scored;
    for (int i = 0; i < count; ++i) {
        double s = 1;
        if (!c.query.empty()) {
            std::string text = std::string(items[i].label) + (items[i].keywords ? std::string(" ") + items[i].keywords : "");
            s = command_score(text, c.query);
            if (s <= (c.query.size() > 1 ? 0.02 : 0)) {
                continue;
            }
        }
        scored.push_back({ i, s });
    }
    std::vector<std::string> groups;
    std::map<std::string, std::vector<Scored>> by_group;
    for (const Scored &s : scored) {
        std::string g = items[s.index].group ? items[s.index].group : "";
        if (!by_group.count(g)) {
            groups.push_back(g);
        }
        by_group[g].push_back(s);
    }
    if (!c.query.empty()) {
        for (auto &[g, list] : by_group) {
            (void)g;
            std::stable_sort(list.begin(), list.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
            if (list.size() > 8) {
                list.resize(8);
            }
        }
        std::stable_sort(groups.begin(), groups.end(), [&](const std::string &a, const std::string &b) {
            return by_group[a].front().score > by_group[b].front().score;
        });
    }
    c.shown.clear();
    for (const std::string &g : groups) {
        for (const Scored &s : by_group[g]) {
            c.shown.push_back(s.index);
        }
    }
    c.selected = std::clamp(c.selected, 0, std::max(0, (int)c.shown.size() - 1));
    push_node(k, n);
    int row = 0;
    for (const std::string &g : groups) {
        if (!g.empty()) {
            std::string key = "g:" + g;
            Node *h = emit(k, key.c_str(), Kind::Box, "div", "cmd-group");
            set_text(h, g.c_str());
        }
        for (const Scored &s : by_group[g]) {
            const kit_command_item &it = items[s.index];
            char key[32];
            std::snprintf(key, sizeof key, "i%d", s.index);
            Node *item = emit(k, key, Kind::CommandItem, "div", "cmd-item");
            Widget &w = item->w;
            if (!w.label_el) {
                w.icon_el = raw_icon(k, item->el, "", 0);
                w.label_el = raw(k, item->el, "span", "label");
                w.meta_el = raw(k, item->el, "span", "meta");
                w.kbd_el = raw(k, item->el, "span", "kbd");
            }
            w.pending_index = s.index;
            if (w.icon != it.icon) {
                w.icon = it.icon;
                w.icon_el->SetInnerRML(it.icon ? utf8(it.icon) : "");
            }
            /* Bold the matched characters when they appear in order. */
            std::string label = it.label ? it.label : "", html;
            size_t qi = 0;
            for (char ch : label) {
                if (qi < c.query.size() && std::tolower((unsigned char)ch) == std::tolower((unsigned char)c.query[qi])) {
                    html += "<b>" + escape(std::string(1, ch).c_str()) + "</b>";
                    ++qi;
                } else {
                    html += escape(std::string(1, ch).c_str());
                }
            }
            if (qi != c.query.size()) {
                html = escape(label.c_str());
            }
            if (w.label != html) {
                w.label = html;
                w.label_el->SetInnerRML(html);
            }
            set_element_text(w.meta_el, it.meta ? it.meta : "");
            set_element_text(w.kbd_el, it.shortcut ? it.shortcut : "");
            bool selected = row == c.selected;
            /* Keyboard moves reveal the selection with the least scrolling; hovering
               selects without scrolling (cmdk). */
            if (selected && c.reveal) {
                item->el->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
                c.reveal = false;
            }
            set_flag(item, "data-selected", selected);
            ++row;
        }
    }
    if (c.shown.empty()) {
        Node *empty = emit(k, "@empty", Kind::Box, "div", "cmd-empty");
        std::string text = "No results for \"" + c.query + "\"";
        set_text(empty, text.c_str());
    }
    pop_node(k);
    int chosen = c.chosen;
    if (chosen >= 0) {
        c.open = false;
        c.input->Blur();
    }
    c.chosen = -1;
    return chosen;
}

/* ---------- Splitter and surfaces ---------- */

extern "C" bool kit_splitter(kit *k, const char *key, int axis, float *value, float minimum, float maximum, bool invert)
{
    Node *n = emit(k, key, Kind::Splitter, "div", axis ? "split-h" : "split-v");
    Widget &w = n->w;
    w.desc.integer = axis == 1;
    w.desc.slider = invert;
    w.desc.min = minimum;
    w.desc.max = maximum;
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
