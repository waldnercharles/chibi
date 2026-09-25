/* Popups, menus and tooltips after Base UI. Popups stack in open order, close on
 * an outside press, Escape or Tab, and sit below their anchor or beside their
 * parent menu. Menus highlight with the arrow keys, Home, End and typeahead;
 * submenus open after a 100 ms hover; an open menubar moves between its triggers.
 * A tooltip shows after a 500 ms hover, switches instantly within 400 ms of the
 * last one, and hides while a button is held or a popup or the palette is open. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace kit_impl {

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

static std::string top_popup(const kit *k)
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

static void highlight_item(kit *k, Popup &p, int index)
{
    p.highlight = index;
    for (int i = 0; i < (int)p.items.size(); ++i) {
        if (p.items[i]) {
            flag_element(p.items[i]->el, "data-highlighted", i == index);
        }
    }
    (void)k;
}

static bool item_enabled(const Node *n)
{
    return n && !n->el->HasAttribute("data-disabled");
}

static void move_highlight(kit *k, Popup &p, int dir, bool absolute)
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

static void activate_item(kit *k, const std::string &popup_key, Node *item)
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

/* Submenus open after a 100 ms hover. */
void open_hovered_submenus(kit *k)
{
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
}

/* Any activated menu item closes the whole menu chain. */
void close_activated_menus(kit *k)
{
    bool activated = false;
    for (auto &[key, p] : k->popups) {
        (void)key;
        activated |= p.open && p.activated;
    }
    if (activated) {
        close_all_popups(k);
    }
}

/* Outside presses dismiss popups (Base UI outside-press). */
void popup_press(kit *k, Node *n, Rml::Element *target)
{
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
}

void trigger_press(kit *k, Node *n, Rml::Event &e)
{
    std::string key = "menu:" + n->path;
    bool open = k->popups[key].open;
    close_all_popups(k);
    if (!open) {
        open_popup(k, key, element_rect(k, n->el), false, 0, 0, "");
        k->popups[key].menubar = true;
    }
    e.StopPropagation();
}

void menu_click(kit *k, Node *n)
{
    for (const std::string &key : k->popup_stack) {
        Popup &p = k->popups[key];
        if (std::find(p.items.begin(), p.items.end(), n) != p.items.end()) {
            activate_item(k, key, n);
            return;
        }
    }
}

void menu_hover(kit *k, Node *n)
{
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
    }
}

bool popup_key(kit *k, Rml::Event &e, int key)
{
    std::string top = top_popup(k);
    if (top.empty()) {
        return false;
    }
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
        return true;
    }
    e.StopPropagation();
    return true;
}

} // namespace kit_impl

using namespace kit_impl;

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
    p.content = 0;
    k->emitting.push_back(key);
    push_node(k, n);
    return true;
}

static float dp_ratio(const kit *k)
{
    return k->context->GetDensityIndependentPixelRatio();
}

/* RmlUi sizes an absolutely positioned box from its block content and not from
   flex rows, so a menu is widened here to its widest measured row. */
static void fit_row(kit *k, float width)
{
    if (!k->emitting.empty()) {
        Popup &p = k->popups[k->emitting.back()];
        p.content = std::max(p.content, width);
    }
}

extern "C" void kit_popup_end(kit *k)
{
    assert(!k->emitting.empty());
    Popup &p = k->popups[k->emitting.back()];
        if (p.keyboard && p.highlight < 0 && !p.items.empty()) {
        move_highlight(k, p, 1, true);
    }
    constexpr float kPopupChrome = 10, kMenuMin = 180, kMenuMax = 480;
    float width = std::clamp(p.content + kPopupChrome, kMenuMin, kMenuMax);
    if (p.content > 0 && p.node && width != p.content_shown) {
        char value[32];
        std::snprintf(value, sizeof value, "%.0fdp", width);
        p.node->el->SetProperty("min-width", value);
        p.content_shown = width;
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
    /* Padding, lead icon and gaps, then the label and the shortcut or chevron. */
    fit_row(k, 6 + 16 + 8 + kit_text_width(k, l.c_str(), 12, 400, dp_ratio(k)) + 8 +
                   (kind == Kind::Submenu ? 20 : shortcut && shortcut[0] ? 24 + kit_text_width(k, shortcut, 11, 400, dp_ratio(k)) : 0));
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
    fit_row(k, 12 + kit_text_width(k, label, 11, 400, dp_ratio(k)));
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

/* ---------- Tooltips ---------- */

namespace kit_impl {

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

void update_tooltip(kit *k)
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

} // namespace kit_impl

extern "C" void kit_tip(kit *k, const char *label, const char *shortcut)
{
    assert(k->last);
    set_tip(k->last, label, shortcut);
}
